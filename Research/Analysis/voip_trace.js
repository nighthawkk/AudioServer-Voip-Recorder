// frida -U -f com.target.app -l voip_trace.js --no-pause
// frida -U -n audioserver -l voip_trace.js     (root required for audioserver)

// ================================
// Utilities
// ================================
function log(msg) {
  const tid = Process.getCurrentThreadId();
  const tn = (typeof Process.getCurrentThreadId === 'function') ? tid : '?';
  console.log(`[${new Date().toISOString()}][tid:${tn}] ${msg}`);
}

function demangle(name) {
  try { return DebugSymbol.fromName(name).name || name; } catch (_) { return name; }
}

function waitForModule(name, cb) {
  const m = Process.findModuleByName(name);
  if (m) return cb(m);
  const int = setInterval(() => {
    const mod = Process.findModuleByName(name);
    if (mod) { clearInterval(int); cb(mod); }
  }, 100);
}

function hookDebugSymbol(symbolName, onEnterCb, onLeaveCb) {
    try {
        const sym = DebugSymbol.fromName(symbolName);
        if (!sym || !sym.address || sym.address.isNull()) {
            log(`[-] Symbol not found or invalid: ${symbolName}`);
            return false;
        }

        Interceptor.attach(sym.address, {
            onEnter(args) { try { onEnterCb && onEnterCb.call(this, args); } catch(e){ log(`hook err: ${e}`); } },
            onLeave(retval) { try { onLeaveCb && onLeaveCb.call(this, retval); } catch(e){ log(`hook err: ${e}`); } }
        });

        log(`[+] Hooked ${symbolName}`);
        return true;
    } catch(e) {
        log(`[-] Exception while hooking ${symbolName}: ${e}`);
        return false;
    }
}


function hookExport(symbolName, onEnterCb, onLeaveCb) {
    try {
        const sym = DebugSymbol.fromName(symbolName);
        if (!sym || !sym.address) {
            log(`[-] Symbol not found: ${symbolName}`);
            return false;
        }

        Interceptor.attach(sym.address, {
            onEnter(args) { try { onEnterCb && onEnterCb.call(this, args); } catch(e){ log(`hook err: ${e}`); } },
            onLeave(retval) { try { onLeaveCb && onLeaveCb.call(this, retval); } catch(e){ log(`hook err: ${e}`); } }
        });
        log(`[+] Hooked ${symbolName}`);
        return true;
    } catch(e) {
        log(`[-] Failed to hook ${symbolName}: ${e}`);
        return false;
    }
}

function hookAllFunctionsByRegex(moduleName, regex, maxCount=200) {
  const mod = Process.findModuleByName(moduleName);
  if (!mod) { log(`[-] Module not loaded: ${moduleName}`); return 0; }
  let count = 0;
  mod.enumerateExports().forEach(exp => {
    if (exp.type === 'function' && regex.test(exp.name)) {
      try {
        Interceptor.attach(exp.address, {
          onEnter(args) { log(`[CALL] ${moduleName}:${exp.name}`); }
        });
        count++;
      } catch (_) {}
    }
  });
  log(`[i] Regex hooks on ${moduleName}: ${count}`);
  return count;
}

function whichProcess() {
  try {
    return Process.enumerateModules()[0].name; // main executable name
  } catch (_) {
    return '?';
  }
}

// ================================
// 1) Java hook: detect MODE_IN_COMMUNICATION
// ================================
const MODE = {
  MODE_INVALID: -2,
  MODE_CURRENT: -1,
  MODE_NORMAL: 0,
  MODE_RINGTONE: 1,
  MODE_IN_CALL: 2,
  MODE_IN_COMMUNICATION: 3,
  MODE_CALL_SCREENING: 4 // newer platforms
};

function installJavaModeHooks() {
  if (!Java.available) { log('[i] Java not available in this process (likely audioserver).'); return; }
  Java.perform(() => {
    const AudioManager = Java.use('android.media.AudioManager');

    // setMode(int)
    if (AudioManager.setMode) {
      AudioManager.setMode.overload('int').implementation = function(mode) {
        const ret = this.setMode(mode);
        log(`[*] AudioManager.setMode(${mode}) → ${Object.keys(MODE).find(k => MODE[k]===mode) || 'UNKNOWN'}`);
        if (mode === MODE.MODE_IN_COMMUNICATION) {
          log('[+] Entered MODE_IN_COMMUNICATION (likely VoIP active)');
        }
        return ret;
      };
      log('[+] Hooked AudioManager.setMode(int)');
    }

    // getMode() for passive polling
    if (AudioManager.getMode) {
      AudioManager.getMode.implementation = function() {
        const m = this.getMode();
        // Passive log (don’t spam)
        // log(`[i] AudioManager.getMode() = ${m}`);
        return m;
      };
      log('[+] Hooked AudioManager.getMode()');
    }
  });
}

// ================================
// 2) Client-side native hooks (libaudioclient.so)
//    These run in the APP process
// ================================
function installClientHooks() {
  waitForModule('libaudioclient.so', () => {
    log('[*] libaudioclient.so loaded — installing client hooks');

    // Common mangled names across Android 10-15 (may vary by build)
    const candidates = [
      // AudioRecord
      '_ZN7android11AudioRecord5startEv',
      '_ZN7android11AudioRecord4stopEv',
      '_ZN7android11AudioRecord12obtainBufferEPNS0_6BufferEPK8timespecPS3_Pm', // older
      '_ZN7android11AudioRecord12obtainBufferEPNS0_6BufferEPK8timespecPS3_Pj', // newer size_t/uint32_t variant
      // AudioTrack
      '_ZN7android10AudioTrack5startEv',
      '_ZN7android10AudioTrack4stopEv',
      '_ZN7android10AudioTrack13releaseBufferEPKNS0_6BufferE'
    ];

    candidates.forEach(sym => {
      hookExport('libaudioclient.so', sym,
        function() { log(`[client] ${sym} ENTER`); },
        function(ret) { log(`[client] ${sym} LEAVE → ${ret}`); }
      );
    });

    // Optional: broad scan to see other interesting calls
    hookAllFunctionsByRegex('libaudioclient.so', /(AudioRecord|AudioTrack|open|start|stop|obtain|release)/);
  });
}

// ================================
// 3) Server-side native hooks (libaudioflinger.so)
//    Run this when attached to the audioserver process
// ================================
function installServerHooks() {
    waitForModule('libaudioflinger.so', () => {
        log('[*] libaudioflinger.so loaded — installing server hooks');

        const serverCandidates = [
            '_ZN7android12AudioFlinger11instantiateEv',
'_ZN7android12AudioFlingerC1Ev',
'_ZN7android12AudioFlingerC2Ev',
'_ZN7android19MmapStreamInterface14openMmapStreamENS0_18stream_direction_tEPK18audio_attributes_tP17audio_config_baseRKNS_11AudioClientEPiP15audio_session_tRKNS_2spINS_18MmapStreamCallbackEEERNSD_IS0_EESA_',
'_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEED2Ev',
'_ZN7android20PermissionControllerC1Ev',
'_ZN7android20PermissionController17getPackagesForUidEjRNS_6VectorINS_8String16EEE',
'_ZN7android27legacy2aidl_String16_stringERKNS_8String16E',
'_ZN7android32aidl2legacy_string_view_String16ENSt3__117basic_string_viewIcNS0_11char_traitsIcEEEE',
'_ZN7android8String16C1EOS0_',
'_ZN7android8String16D1Ev',
'_ZN7android21defaultServiceManagerEv',
'_ZN7android8String16C1EPKc',
'_ZN7android25AudioFlingerServerAdapterC1ERKNS_2spINS0_8DelegateEEE',
'_ZN7android15BatteryNotifier14noteResetAudioEv',
'_ZN7android26DevicesFactoryHalInterface6createEv',
'_ZN7android10mediautils9TimeCheck15setAudioHalPidsERKNSt3__16vectorIiNS2_9allocatorIiEEEE',
'_ZN7android7RefBaseC2Ev',
'_ZN7android25aidl2legacy_int32_t_pid_tEi',
'_ZN7android14IPCThreadState4selfEv',
'_ZNK7android14IPCThreadState13getCallingUidEv',
'_ZN7android25aidl2legacy_int32_t_uid_tEi',
'_ZN7android25legacy2aidl_uid_t_int32_tEj',
'_ZNK7android14IPCThreadState13getCallingPidEv',
'_ZN7android25legacy2aidl_pid_t_int32_tEi',
'_ZN7android11AudioSystem16getOutputForAttrEP18audio_attributes_tPi15audio_session_tP19audio_stream_type_tRKNS_7content22AttributionSourceStateEP12audio_config20audio_output_flags_tS3_S3_PNSt3__16vectorIiNSE_9allocatorIiEEEEPbSK_',
'_ZN7android11AudioSystem15getInputForAttrEPK18audio_attributes_tPii15audio_session_tRKNS_7content22AttributionSourceStateEP17audio_config_base19audio_input_flags_tS4_S4_',
'_ZN7android11AudioSystem13releaseOutputEi',
'_ZN7android11AudioSystem12releaseInputEi',
'_ZN7android2os17ExternalVibration35externalVibrationScaleToHapticScaleEi',
'_ZN7android7String8C1Ev',
'_ZN7android7String86appendEPKc',
'_ZN7android7String812appendFormatEPKcz',
'_ZN7android10mediautils7UidInfo7getInfoEj',
'_ZN7android7String8D1Ev',
'_ZN7android11dumpAllowedEv',
'_ZN7android7String8C1EPKc',
'_ZN7android19dumpMemoryAddressesEm',
'_ZN7android26GetUnreachableMemoryStringEbm',
'_ZN7android10mediautils29getStatisticsClassesForModuleENSt3__117basic_string_viewIcNS1_11char_traitsIcEEEE',
'_ZN7android10mediautils21getStatisticsForClassENSt3__117basic_string_viewIcNS1_11char_traitsIcEEEE',
'_ZN7android10mediautils9TimeCheck8toStringEv',
'_ZN7android5NBLog8Timeline10sharedSizeEm',
'_ZNK7android7IMemory15unsecurePointerEv',
'_ZN7android5NBLog6WriterC1ERKNS_2spINS_7IMemoryEEEm',
'_ZN7android13IAudioFlinger16CreateTrackInput8fromAidlERKNS_5media18CreateTrackRequestE',
'_ZN7android11AudioSystem15moveEffectsToIoERKNSt3__16vectorIiNS1_9allocatorIiEEEEi',
'_ZNK7android13IAudioFlinger17CreateTrackOutput6toAidlEv',
'_ZN7android15settingsAllowedEv',
'_ZN7android14AudioParameterC1ERKNS_7String8E',
'_ZNK7android14AudioParameter3getERKNS_7String8ERS1_',
'_ZN7android14AudioParameter3addERKNS_7String8ES3_',
'_ZN7android14AudioParameter6removeERKNS_7String8E',
'_ZN7android14AudioParameterD1Ev',
'_ZN7android7String86formatEPKcz',
'_ZN7android7String8C1ERKS0_',
'_ZNK7android14AudioParameter6getIntERKNS_7String8ERi',
'_ZN7android10IInterface8asBinderERKNS_2spIS0_EE',
'_ZN7android54legacy2aidl_audio_io_config_event_t_AudioIoConfigEventENS_23audio_io_config_event_tE',
'_ZN7android47legacy2aidl_AudioIoDescriptor_AudioIoDescriptorERKNS_2spINS_17AudioIoDescriptorEEE',
'_ZN7android37legacy2aidl_audio_io_handle_t_int32_tEi',
'_ZN7android49legacy2aidl_audio_latency_mode_t_AudioLatencyModeE20audio_latency_mode_t',
'_ZN7android6ThreadC2Eb',
'_ZN7android13IAudioFlinger17CreateRecordInput8fromAidlERKNS_5media19CreateRecordRequestE',
'_ZNK7android13IAudioFlinger18CreateRecordOutput6toAidlEv',
'_ZN7android14AudioValidator23validateAudioPortConfigERK17audio_port_configNSt3__117basic_string_viewIcNS4_11char_traitsIcEEEE',
'_ZN7android14AudioParameter6addIntERKNS_7String8Ei',
'_ZN7android41aidl2legacy_int32_t_audio_module_handle_tEi',
'_ZN7android38aidl2legacy_AudioConfig_audio_config_tERKNS_5media5audio6common11AudioConfigEb',
'_ZN7android47aidl2legacy_AudioConfigBase_audio_config_base_tERKNS_5media5audio6common15AudioConfigBaseEb',
'_ZN7android32aidl2legacy_DeviceDescriptorBaseERKNS_5media11AudioPortFwE',
'_ZN7android45aidl2legacy_int32_t_audio_output_flags_t_maskEi',
'_ZNK7android20DeviceDescriptorBase8toStringEb',
'_ZN7android38legacy2aidl_audio_config_t_AudioConfigERK12audio_configb',
'_ZN7android45legacy2aidl_audio_output_flags_t_int32_t_maskE20audio_output_flags_t',
'_ZN7android34aidl2legacy_AudioDeviceTypeAddressERKNS_5media5audio6common11AudioDeviceE',
'_ZN7android37aidl2legacy_int32_t_audio_io_handle_tEi',
'_ZNK7android19AudioDeviceTypeAddr7addressEv',
'_ZN7android38aidl2legacy_AudioSource_audio_source_tENS_5media5audio6common11AudioSourceE',
'_ZN7android44aidl2legacy_int32_t_audio_input_flags_t_maskEi',
'_ZN7android11AudioSystem22calculateMinFrameCountEjjjjf',
'_ZN7android26EffectsFactoryHalInterface10isNullUuidEPK12audio_uuid_s',
'_ZN7android35aidl2legacy_int32_t_audio_session_tEi',
'_ZN7android48aidl2legacy_EffectDescriptor_effect_descriptor_tERKNS_5media16EffectDescriptorE',
'_ZN7android32modifyDefaultAudioEffectsAllowedERKNS_7content22AttributionSourceStateE',
'_ZN7android16recordingAllowedERKNS_7content22AttributionSourceStateE14audio_source_t',
'_ZN7android11AudioSystem18getOutputForEffectEPK19effect_descriptor_s',
'_ZN7android48legacy2aidl_effect_descriptor_t_EffectDescriptorERK19effect_descriptor_s',
'_ZN7android4base12StringPrintfEPKcz',
'_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEC1ERKS5_',
'_ZN7android10mediautils9TimeCheckC1ENSt3__117basic_string_viewIcNS2_11char_traitsIcEEEEONS2_8functionIFvbfEEENS2_6chrono8durationIxNS2_5ratioILl1ELl1000000000EEEEESF_b',
'_ZN7android11AudioSystem24get_audio_policy_serviceEv',
'_ZN7android10mediautils9TimeCheckD1Ev',
'_ZN7android7RefBase10onFirstRefEv',
'_ZN7android7RefBase15onLastStrongRefEPKv',
'_ZN7android7RefBase20onIncStrongAttemptedEjPKv',
'_ZN7android7RefBase13onLastWeakRefEPKv',
'_ZN7android6ThreadD1Ev',
'_ZN7android6ThreadD0Ev',
'_ZN7android6Thread3runEPKcim',
'_ZN7android6Thread11requestExitEv',
'_ZN7android6Thread10readyToRunEv',
'_ZNK7android7RefBase9decStrongEPKv',
'_ZNK7android7RefBase9incStrongEPKv',
'_ZNKSt3__120__vector_base_commonILb1EE20__throw_length_errorEv',
'_ZN7android14statusToStringEi',
'_ZN7android26EffectsFactoryHalInterface6createEv',
'_ZN7android12mediametrics8BaseItem12submitBufferEPKcm',
'_ZNSt3__16thread6detachEv',
'_ZNSt3__16threadD1Ev',
'_ZNSt3__115__thread_structC1Ev',
'_ZNSt3__120__throw_system_errorEiPKc',
'_ZNSt3__119__thread_local_dataEv',
'_ZN7android11AudioSystem26onNewAudioModulesAvailableEv',
'_ZNSt3__115__thread_structD1Ev',
'_ZNSt3__15mutexD1Ev',
'_ZN7android10VectorImpl13finish_vectorEv',
'_ZN7android16SortedVectorImplD2Ev',
'_ZN7android7RefBase12weakref_type7incWeakEPKv',
'_ZN7android14sp_report_raceEv',
'_ZN7android2os24IExternalVibratorService11asInterfaceERKNS_2spINS_7IBinderEEE',
'_ZNK7android7String86lengthEv',
'_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEED1Ev',
'_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEED0Ev',
'_ZNSt3__18ios_base4initEPv',
'_ZNSt3__115basic_streambufIcNS_11char_traitsIcEEEC2Ev',
'_ZNSt3__115basic_streambufIcNS_11char_traitsIcEEE5imbueERKNS_6localeE',
'_ZNSt3__115basic_streambufIcNS_11char_traitsIcEEE6setbufEPcl',
'_ZNSt3__115basic_streambufIcNS_11char_traitsIcEEE4syncEv',
'_ZNSt3__115basic_streambufIcNS_11char_traitsIcEEE9showmanycEv',
'_ZNSt3__115basic_streambufIcNS_11char_traitsIcEEE6xsgetnEPcl',
'_ZNSt3__115basic_streambufIcNS_11char_traitsIcEEE5uflowEv',
'_ZNSt3__115basic_streambufIcNS_11char_traitsIcEEE6xsputnEPKcl',
'_ZNSt3__115basic_streambufIcNS_11char_traitsIcEEED2Ev',
'_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE9push_backEc',
'_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6resizeEmc',
'_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_',
'_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryD1Ev',
'_ZNKSt3__121__basic_string_commonILb1EE20__throw_length_errorEv',
'_ZNKSt3__18ios_base6getlocEv',
'_ZNSt3__16localeD1Ev',
'_ZNKSt3__16locale9use_facetERNS0_2idE',
'_ZNSt3__18ios_base5clearEj',
'_ZNSt3__19to_stringEi',
'_ZNSt3__19to_stringEf',
'_ZNSt3__19basic_iosIcNS_11char_traitsIcEEED2Ev',
'_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEED2Ev',
'_ZNSt3__114basic_iostreamIcNS_11char_traitsIcEEED1Ev',
'_ZNSt3__114basic_iostreamIcNS_11char_traitsIcEEED0Ev',
'_ZNSt3__113basic_istreamIcNS_11char_traitsIcEEED1Ev',
'_ZNSt3__113basic_istreamIcNS_11char_traitsIcEEED0Ev',
'_ZNSt3__15mutex4lockEv',
'_ZNSt3__15mutex6unlockEv',
'_ZNSt3__114basic_iostreamIcNS_11char_traitsIcEEED2Ev',
'_ZNK7android8String164sizeEv',
'_ZN7android16SortedVectorImplC2Emj',
'_ZNK7android14AudioParameter12toStringImplEb',
'_ZN7android7String85setToERKS0_',
'_ZN7android7String86appendERKS0_',
'_ZNKSt3__119__shared_weak_count13__get_deleterERKSt9type_info',
'_ZNSt3__112__next_primeEm',
'_ZN7android7RefBase12weakref_type7decWeakEPKv',
'_ZNSt3__119__shared_weak_countD2Ev',
'_ZNSt3__119__shared_weak_count14__release_weakEv',
'_ZN7android2os20ParcelFileDescriptorD1Ev',
'_ZN7android10VectorImplC2ERKS0_',
'_ZN7android7RefBase10renameRefsEmRKNS_16ReferenceRenamerE',
'_ZN7android7RefBase11renameRefIdEPS0_PKvS3_',
'_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEaSERKS5_',
'_ZNSt3__118condition_variableD1Ev',
'_ZNSt13exception_ptrD1Ev',
'_ZNSt3__114__shared_countD2Ev',
'_ZNSt3__117__assoc_sub_state4waitEv',
'_ZNSt3__118condition_variable10notify_allEv',
'_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE5writeEPKcl',
'_ZNSt3__16chrono12steady_clock3nowEv',
'_ZNSt3__16chrono12system_clock3nowEv',
'_ZNSt3__118condition_variable15__do_timed_waitERNS_11unique_lockINS_5mutexEEENS_6chrono10time_pointINS5_12system_clockENS5_8durationIxNS_5ratioILl1ELl1000000000EEEEEEE',
'_ZNSt3__117__assoc_sub_state10__sub_waitERNS_11unique_lockINS_5mutexEEE',
'_ZNSt13exception_ptrC1ERKS_',
'_ZN7android19getAudioDeviceTypesERKNSt3__16vectorINS_19AudioDeviceTypeAddrENS0_9allocatorIS2_EEEE',
'_ZN7android6ThreadD2Ev',
'_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6assignEPKc',
'_ZN7android7RefBase12weakref_type16attemptIncStrongEPKv',
'_ZN7android10VectorImplC2Emj',
'_ZN7android8String16C1Ev',
'_ZN7android8String16C1ERKS0_',
'_ZN7android10VectorImplD2Ev',
'_ZN7android10VectorImpl13editArrayImplEv',
'_ZN7android7RefBaseD2Ev',
'_ZN7android15BatteryNotifierC1Ev',
'_ZN7android10VectorImpl3popEv',
'_ZNK7android16SortedVectorImpl7indexOfEPKv',
'_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEElsEi',
'_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEElsEm',
'_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEElsEl',
'_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEElsEd',
'_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEElsEf',
'_ZN7android16SortedVectorImpl3addEPKv',
'_ZNK7android7RefBase10createWeakEPKv',
'_ZN7android10VectorImpl13removeItemsAtEmm',
'_ZN7android10VectorImpl4pushEPKv',
'_ZN7android16SortedVectorImpl6removeEPKv',
'_ZN7android13IAudioManager11asInterfaceERKNS_2spINS_7IBinderEEE',
'_ZN7android62legacy2aidl_audio_microphone_characteristic_t_MicrophoneInfoFwERK33audio_microphone_characteristic_t',
'_ZN7android10VectorImpl16editItemLocationEm',
'_ZN7android10VectorImpl3addEPKv',
'_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6appendEPKcm',
'_ZN7android14AudioParameter10keyRoutingE',
'_ZN7android14AudioParameter15keySamplingRateE',
'_ZN7android14AudioParameter9keyFormatE',
'_ZN7android14AudioParameter11keyChannelsE',
'_ZN7android14AudioParameter13keyFrameCountE',
'_ZN7android14AudioParameter14keyInputSourceE',
'_ZN7android14AudioParameter13keyMonoOutputE',
'_ZN7android14AudioParameter16keyDeviceConnectE',
'_ZN7android14AudioParameter19keyDeviceDisconnectE',
'_ZN7android14AudioParameter25keyStreamSupportedFormatsE',
'_ZN7android14AudioParameter26keyStreamSupportedChannelsE',
'_ZN7android14AudioParameter31keyStreamSupportedSamplingRatesE',
'_ZN7android14AudioParameter10keyClosingE',
'_ZN7android14AudioParameter10keyExitingE',
'_ZN7android14AudioParameter9keyBtNrecE',
'_ZN7android14AudioParameter8valueOffE',
'_ZN7android14AudioParameter14keyScreenStateE',
'_ZN7android14AudioParameter17keyStreamHwAvSyncE',
'_ZNSt3__15ctypeIcE2idE',
'_ZN7android9SingletonINS_15BatteryNotifierEE5sLockE',
'_ZN7android9SingletonINS_15BatteryNotifierEE9sInstanceE',
'_ZN7android13TypeConverterINS_13DefaultTraitsI12audio_mode_tEEE6mTableE',
'_ZN7android12SPDIFEncoder17isFormatSupportedE14audio_format_t',
'_ZNK7android19AudioDeviceTypeAddr10getAddressEv',
'_ZNK7android5media15AudioHalVersion13writeToParcelEPNS_6ParcelE',
'_ZN7android5media15AudioHalVersion14readFromParcelEPKNS_6ParcelE',
'_ZNK7android19AudioDeviceTypeAddrltERKS0_',
'_ZN7android11AudioSystem14registerEffectEPK19effect_descriptor_siNS_18product_strategy_tE15audio_session_ti',
'_ZN7android11AudioSystem16unregisterEffectEi',
'_ZN7android11AudioSystem16setEffectEnabledEib',
'_ZN7android11AudioEffect12guidToStringEPK12audio_uuid_sPcm',
'_ZN7android5media8BnEffectC2Ev',
'_ZN7android7BBinder21setMinSchedulerPolicyEii',
'_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6appendEPKc',
'_ZN7android5media8BnEffect10onTransactEjRKNS_6ParcelEPS2_j',
'_ZN7android6binder6Status2okEv',
'_ZN7android5media32convertIMemoryToSharedFileRegionERKNS_2spINS_7IMemoryEEEPNS0_16SharedFileRegionE',
'_ZN7android47legacy2aidl_audio_config_base_t_AudioConfigBaseERK17audio_config_baseb',
'_ZN7android7String85clearEv',
'_ZNK7android7BBinder13isBinderAliveEv',
'_ZN7android7BBinder10pingBinderEv',
'_ZN7android7BBinder4dumpEiRKNS_6VectorINS_8String16EEE',
'_ZN7android7BBinder8transactEjRKNS_6ParcelEPS1_j',
'_ZN7android7BBinder11linkToDeathERKNS_2spINS_7IBinder14DeathRecipientEEEPvj',
'_ZN7android7BBinder13unlinkToDeathERKNS_2wpINS_7IBinder14DeathRecipientEEEPvjPS4_',
'_ZNK7android7IBinder13checkSubclassEPKv',
'_ZN7android7BBinder12attachObjectEPKvPvS3_PFvS2_S3_S3_E',
'_ZNK7android7BBinder10findObjectEPKv',
'_ZN7android7BBinder12detachObjectEPKv',
'_ZN7android7BBinder11localBinderEv',
'_ZN7android7IBinder12remoteBinderEv',
'_ZN7android7BBinder10onTransactEjRKNS_6ParcelEPS1_j',
'_ZN7android5media7IEffectD1Ev',
'_ZN7android5media7IEffectD0Ev',
'_ZNK7android5media7IEffect22getInterfaceDescriptorEv',
'_ZN7android10IInterfaceD1Ev',
'_ZN7android10IInterfaceD0Ev',
'_ZN7android7IBinder19queryLocalInterfaceERKNS_8String16E',
'_ZNK7android7BBinder22getInterfaceDescriptorEv',
'_ZN7android7BBinderD1Ev',
'_ZN7android7BBinderD0Ev',
'_ZN7android7IBinder11localBinderEv',
'_ZN7android7IBinderD1Ev',
'_ZN7android7IBinderD0Ev',
'_ZN7android7String810lockBufferEm',
'_ZN7android7String812unlockBufferEm',
'_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEElsEPKv',
'_ZN7android14MemoryHeapBaseC1EmjPKc',
'_ZN7android10MemoryBaseC1ERKNS_2spINS_11IMemoryHeapEEElm',
'_ZNK7android7IMemory4sizeEv',
'_ZN7android8BnMemoryC2Ev',
'_ZN7android8BnMemoryD1Ev',
'_ZN7android8BnMemoryD0Ev',
'_ZN7android8BnMemory10onTransactEjRKNS_6ParcelEPS1_j',
'_ZN7android7IMemoryD1Ev',
'_ZN7android7IMemoryD0Ev',
'_ZNK7android7IMemory22getInterfaceDescriptorEv',
'_ZN7android14MemoryHeapBase7disposeEv',
'_ZNK7android7RefBase22incStrongRequireStrongEPKv',
'_ZN7android8BnMemoryD2Ev',
'_ZN7android7BBinderD2Ev',
'_ZN7android5media7IEffectD2Ev',
'_ZN7android10VectorImpl8insertAtEPKvmm',
'_ZN7android10VectorImpl5clearEv',
'_ZNKSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE7compareEmmPKcm',
'_ZN7android7IMemory10descriptorE',
'_ZN7android5media7IEffect10descriptorE',
'_ZN7android17Format_sampleRateERKNS_12NBAIO_FormatE',
'_ZN7android14Format_isEqualERKNS_12NBAIO_FormatES2_',
'_ZN7android16Format_frameSizeERKNS_12NBAIO_FormatE',
'_ZN7android14Format_InvalidE',
'_ZN7android11audio_utils7Balance14setChannelMaskE20audio_channel_mask_t',
'_ZN7android14AudioMixerBase7destroyEi',
'_ZN7android14AudioMixerBase6createEi20audio_channel_mask_t14audio_format_ti',
'_ZN7android10AudioMixer17setBufferProviderEiPNS_19AudioBufferProviderE',
'_ZN7android14AudioMixerBase6enableEi',
'_ZN7android19Format_channelCountERKNS_12NBAIO_FormatE',
'_ZN7android14AudioMixerBase7disableEi',
'_ZN7android11audio_utils7Balance10setBalanceEf',
'_ZN7android11audio_utils7Balance7processEPfm',
'_ZN7android10AudioMixer12sInitRoutineEv',
'_ZN7android14AudioMixerBase12process__nopEv',
'_ZN7android5NBLog6Writer3logENS0_5EventEPKvm',
'_ZN7android10AudioMixer12sOnceControlE',
'_ZNK7android16SoundDoseManager20forceUseFrameworkMelEv',
'_ZN4aidl7android8hardware5audio4core9sounddose10ISoundDose10fromBinderERKN3ndk10SpAIBinderE',
'_ZN7android16SoundDoseManager24setHalSoundDoseInterfaceERKNSt3__110shared_ptrIN4aidl7android8hardware5audio4core9sounddose10ISoundDoseEEE',
'_ZN7android16SoundDoseManager12isCsdEnabledEv',
'_ZNK7android16SoundDoseManager27forceComputeCsdOnAllDevicesEv',
'_ZN7android19AudioDeviceTypeAddrC1E15audio_devices_tRKNSt3__112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEE',
'_ZN7android16SoundDoseManager20mapAddressToDeviceIdERKNS_19AudioDeviceTypeAddrEi',
'_ZN7android16SoundDoseManager29getOrCreateProcessorForDeviceEiijm14audio_format_t',
'_ZN7android16SoundDoseManager21getSoundDoseInterfaceERKNS_2spINS_5media18ISoundDoseCallbackEEE',
'_ZN7android16SoundDoseManager23clearMapDeviceIdEntriesEi',
'_ZNK7android16SoundDoseManager4dumpEv',
'_ZNK7android6Thread11exitPendingEv',
'_ZNSt3__118condition_variable4waitERNS_11unique_lockINS_5mutexEEE',
'_ZNSt3__118condition_variable10notify_oneEv',
'_ZN7android6Thread18requestExitAndWaitEv',
'_ZN7android14AudioValidator17validateAudioPortERK13audio_port_v7NSt3__117basic_string_viewIcNS4_11char_traitsIcEEEE',
'_ZN7android14AudioValidator18validateAudioPatchERK11audio_patchNSt3__117basic_string_viewIcNS4_11char_traitsIcEEEE',
'_ZN7android20DeviceDescriptorBaseC1E15audio_devices_t',
'_ZN7android20DeviceDescriptorBase10setAddressERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE',
'_ZN7android12SPDIFEncoder5resetEv',
'_ZN7android12SPDIFEncoder5writeEPKvm',
'_ZN7android12SPDIFEncoderC2E14audio_format_t',
'_ZN7android12SPDIFEncoderD2Ev',
'_ZNK7android6Thread6getTidEv',
'_ZN7android15requestPriorityEiiibb',
'_ZN7android15dumpDeviceTypesERKNSt3__13setI15audio_devices_tNS0_4lessIS2_EENS0_9allocatorIS2_EEEE',
'_ZNK7android10mediautils14ThreadSnapshot8toStringEv',
'_ZN7android7BBinderC1Ev',
'_ZN7android12mediametrics4Item10selfrecordEv',
'_ZN7android11AudioSystem20getStrategyForStreamE19audio_stream_type_t',
'_ZN7android10mediautils14ThreadSnapshot6setTidEi',
'_ZN7android12checkIMemoryERKNS_2spINS_7IMemoryEEE',
'_ZN7android11AudioSystem11startOutputEi',
'_ZN7android11AudioSystem10stopOutputEi',
'_ZN7android5NBLog6Writer14logEventHistTsENS0_5EventEm',
'_ZN7android8MonoPipe12setAvgFramesEm',
'_ZN7android10mediautils14ThreadSnapshot7onBeginEv',
'_ZN7android18AudioStreamOutSink19startMelComputationERKNS_2spINS_11audio_utils12MelProcessorEEE',
'_ZN7android18AudioStreamOutSink18stopMelComputationEv',
'_ZN7android27getAudioDeviceOutAllA2dpSetEv',
'_ZN7android10mediautils14ThreadSnapshot5onEndEv',
'_ZN7android14IPCThreadState13flushCommandsEv',
'_ZN7android18AudioStreamOutSinkC1ENS_2spINS_21StreamOutHalInterfaceEEE',
'_ZN7android16Format_from_SR_CEjj14audio_format_t',
'_ZN7android8MonoPipeC1EmRKNS_12NBAIO_FormatEb',
'_ZN7android14MonoPipeReaderC1EPNS_8MonoPipeE',
'_ZN7android25SourceAudioBufferProviderC1ERKNS_2spINS_12NBAIO_SourceEEE',
'_ZN7android6Thread4joinEv',
'_ZN7android21AudioTrackServerProxy15getPlaybackRateEv',
'_ZNK7android14AudioMixerBase19getUnreleasedFramesEi',
'_ZNK7android14AudioMixerBase10trackNamesEv',
'_ZNK7android11audio_utils7Balance8toStringEv',
'_ZNK7android11audio_utils7Balance20computeStereoBalanceEfPfS2_',
'_ZN7android12audioflinger21MonotonicFrameCounter31updateAndGetMonotonicFrameCountEll',
'_ZN7android12audioflinger21MonotonicFrameCounter7onFlushEv',
'_ZN7android26requestSpatializerPriorityEii',
'_ZN7android12MemoryDealerC1EmPKcj',
'_ZN7android19AudioStreamInSourceC1ENS_2spINS_20StreamInHalInterfaceEEE',
'_ZN7android4PipeC1EmRKNS_12NBAIO_FormatEPv',
'_ZN7android10PipeReaderC1ERNS_4PipeE',
'_ZN7android21RecordBufferConverter7convertEPvPNS_19AudioBufferProviderEm',
'_ZN7android21captureHotwordAllowedERKNS_7content22AttributionSourceStateE',
'_ZN7android11AudioSystem10startInputEi',
'_ZN7android21RecordBufferConverter5resetEv',
'_ZN7android19AudioDeviceTypeAddr10setAddressERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE',
'_ZN7android19AudioDeviceTypeAddr5resetEv',
'_ZN7android30deviceTypeAddrsFromDescriptorsERKNSt3__16vectorINS_2spINS_20DeviceDescriptorBaseEEENS0_9allocatorIS4_EEEE',
'_ZN7android11AudioSystem9stopInputEi',
'_ZN7android11audio_utils12MelProcessor7processEPKvm',
'_ZN7android11audio_utils12MelProcessor6resumeEv',
'_ZN7android11audio_utils12MelProcessor5pauseEv',
'_ZN7android10mediautils14ThreadSnapshot5State5resetEi',
'_ZN7android19deviceTypesToStringERKNSt3__13setI15audio_devices_tNS0_4lessIS2_EENS0_9allocatorIS2_EEEE',
'_ZN7android16SortedVectorImplaSERKS0_',
'_ZN7android2os18isValidHapticScaleENS0_11HapticScaleE',
'_ZN7android16IActivityManager11asInterfaceERKNS_2spINS_7IBinderEEE',
'_ZN7android12mediametrics4ItemD1Ev',
'_ZN7android15BatteryNotifier14noteStartAudioEj',
'_ZN7android15BatteryNotifier13noteStopAudioEj',
'_ZN7android2os13IPowerManager11asInterfaceERKNS_2spINS_7IBinderEEE',
'_ZN7android10VectorImplaSERKS0_',
'_ZNKSt3__121__basic_string_commonILb1EE20__throw_out_of_rangeEv',
'_ZN7android14AudioParameter18valueListSeparatorE',
'_ZN7android18audio_track_cblk_tC1Ev',
'_ZN7android5media12BnAudioTrackC2Ev',
'_ZN7android44legacy2aidl_NullableIMemory_SharedFileRegionERKNS_2spINS_7IMemoryEEE',
'_ZN7android49legacy2aidl_AudioTimestamp_AudioTimestampInternalERKNS_14AudioTimestampE',
'_ZN7android14AudioValidator20validateDualMonoModeE22audio_dual_mono_mode_t',
'_ZN7android52legacy2aidl_audio_dual_mono_mode_t_AudioDualMonoModeE22audio_dual_mono_mode_t',
'_ZN7android52aidl2legacy_AudioDualMonoMode_audio_dual_mono_mode_tENS_5media5audio6common17AudioDualMonoModeE',
'_ZN7android14AudioValidator32validateAudioDescriptionMixLevelEf',
'_ZN7android14AudioValidator20validatePlaybackRateERK19audio_playback_rate',
'_ZN7android51legacy2aidl_audio_playback_rate_t_AudioPlaybackRateERK19audio_playback_rate',
'_ZN7android51aidl2legacy_AudioPlaybackRate_audio_playback_rate_tERKNS_5media5audio6common17AudioPlaybackRateE',
'_ZN7android13AppOpsManagerC1Ev',
'_ZN7android13AppOpsManager16stopWatchingModeERKNS_2spINS_15IAppOpsCallbackEEE',
'_ZN7android13AppOpsManager17startWatchingModeEiRKNS_8String16ERKNS_2spINS_15IAppOpsCallbackEEE',
'_ZN7android13AppOpsManager19checkAudioOpNoThrowEiiiRKNS_8String16E',
'_ZN7android27StaticAudioTrackServerProxyC1EPNS_18audio_track_cblk_tEPvmmj',
'_ZN7android5Proxy25setStartThresholdInFramesEj',
'_ZN7android2os17ExternalVibrationC1EiNSt3__112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEERK18audio_attributes_tNS_2spINS0_28IExternalVibrationControllerEEE',
'_ZNK7android5Proxy25getStartThresholdInFramesEv',
'_ZN7android2os17PersistableBundle10putBooleanERKNS_8String16Eb',
'_ZN7android2os17PersistableBundle6putIntERKNS_8String16Ei',
'_ZN7android21AudioTrackServerProxy16setStreamEndDoneEv',
'_ZN7android11ClientProxy12obtainBufferEPNS_5Proxy6BufferEPK8timespecPS4_',
'_ZN7android11ClientProxy13releaseBufferEPNS_5Proxy6BufferE',
'_ZN7android11ClientProxyC1EPNS_18audio_track_cblk_tEPvmmbb',
'_ZN7android5media13BnAudioRecordC2Ev',
'_ZN7android21RecordBufferConverterC1E20audio_channel_mask_t14audio_format_tjS1_S2_j',
'_ZN7android21RecordBufferConverterD1Ev',
'_ZN7android2os29BnExternalVibrationController10onTransactEjRKNS_6ParcelEPS2_j',
'_ZN7android2os28IExternalVibrationControllerD1Ev',
'_ZN7android2os28IExternalVibrationControllerD0Ev',
'_ZNK7android2os28IExternalVibrationController22getInterfaceDescriptorEv',
'_ZN7android5media12BnAudioTrack10onTransactEjRKNS_6ParcelEPS2_j',
'_ZN7android5media11IAudioTrackD1Ev',
'_ZN7android5media11IAudioTrackD0Ev',
'_ZNK7android5media11IAudioTrack22getInterfaceDescriptorEv',
'_ZN7android16BnAppOpsCallback10onTransactEjRKNS_6ParcelEPS1_j',
'_ZN7android15IAppOpsCallbackD1Ev',
'_ZN7android15IAppOpsCallbackD0Ev',
'_ZNK7android15IAppOpsCallback22getInterfaceDescriptorEv',
'_ZN7android5media13BnAudioRecord10onTransactEjRKNS_6ParcelEPS2_j',
'_ZN7android5media12IAudioRecordD1Ev',
'_ZN7android5media12IAudioRecordD0Ev',
'_ZNK7android5media12IAudioRecord22getInterfaceDescriptorEv',
'_ZN7android5media11IAudioTrackD2Ev',
'_ZNK7android5media25VolumeShaperConfiguration13writeToParcelEPNS_6ParcelE',
'_ZN7android5media25VolumeShaperConfiguration14readFromParcelEPKNS_6ParcelE',
'_ZNK7android5media21VolumeShaperOperation13writeToParcelEPNS_6ParcelE',
'_ZN7android5media21VolumeShaperOperation14readFromParcelEPKNS_6ParcelE',
'_ZN7android6binder6Status17fromExceptionCodeEiPKc',
'_ZN7android6binder6Status24fromServiceSpecificErrorEiPKc',
'_ZN7android15IAppOpsCallbackC2Ev',
'_ZN7android7BBinderC2Ev',
'_ZN7android11ServerProxyC2EPNS_18audio_track_cblk_tEPvmmbb',
'_ZN7android2os29BnExternalVibrationControllerC2Ev',
'_ZNK7android5media17VolumeShaperState13writeToParcelEPNS_6ParcelE',
'_ZN7android5media17VolumeShaperState14readFromParcelEPKNS_6ParcelE',
'_ZN7android11ClientProxyC2EPNS_18audio_track_cblk_tEPvmmbb',
'_ZN7android5media12IAudioRecordD2Ev',
'_ZN7android2os28IExternalVibrationControllerD2Ev',
'_ZN7android15IAppOpsCallbackD2Ev',
'_ZN7android5media11IAudioTrack10descriptorE',
'_ZN7android2os28IExternalVibrationController10descriptorE',
'_ZN7android5media12IAudioRecord10descriptorE',
'_ZN7android15IAppOpsCallback10descriptorE',
'_ZN7android13AppOpsManagerC2Ev',
'_ZN7android13AppOpsManager10getServiceEv',
'_ZN7android13AppOpsManager7checkOpEiiRKNS_8String16E',
'_ZN7android13AppOpsManager6noteOpEiiRKNS_8String16E',
'_ZN7android13AppOpsManager6noteOpEiiRKNS_8String16ERKNSt3__18optionalIS1_EES3_',
'_ZN7android13AppOpsManager18shouldCollectNotesEi',
'_ZN7android13AppOpsManager14startOpNoThrowEiiRKNS_8String16Eb',
'_ZN7android13AppOpsManager14startOpNoThrowEiiRKNS_8String16EbRKNSt3__18optionalIS1_EES3_',
'_ZN7android13AppOpsManager8finishOpEiiRKNS_8String16E',
'_ZN7android13AppOpsManager8finishOpEiiRKNS_8String16ERKNSt3__18optionalIS1_EE',
'_ZN7android13AppOpsManager17startWatchingModeEiRKNS_8String16EiRKNS_2spINS_15IAppOpsCallbackEEE',
'_ZN7android13AppOpsManager18permissionToOpCodeERKNS_8String16E',
'_ZN7android13AppOpsManager25setCameraAudioRestrictionEi',
'_ZN7android12uptimeMillisEv',
'_ZN7android14IAppOpsService11asInterfaceERKNS_2spINS_7IBinderEEE',
'_ZN7android15IAppOpsCallback11asInterfaceERKNS_2spINS_7IBinderEEE',
'_ZN7android2spINS_15IAppOpsCallbackEED2Ev',
'_ZN7android15IAppOpsCallback14setDefaultImplENS_2spIS0_EE',
'_ZN7android15IAppOpsCallback14getDefaultImplEv',
'_ZN7android15IAppOpsCallback12default_implE',
'_ZN7android8String16D2Ev',
'_ZN7android10IInterfaceC2Ev',
'_ZN7android10IInterfaceD2Ev',
'_ZNK7android6Parcel14checkInterfaceEPNS_7IBinderE',
'_ZNK7android6Parcel9readInt32Ev',
'_ZNK7android6Parcel12readString16EPNS_8String16E',
'_ZN7android9BpRefBase10onFirstRefEv',
'_ZN7android9BpRefBase15onLastStrongRefEPKv',
'_ZN7android9BpRefBase20onIncStrongAttemptedEjPKv',
'_ZN7android9BpRefBaseD1Ev',
'_ZN7android9BpRefBaseD0Ev',
'_ZN7android9BpRefBaseC2ERKNS_2spINS_7IBinderEEE',
'_ZN7android9BpRefBaseD2Ev',
'_ZN7android6ParcelC1Ev',
'_ZN7android6Parcel19writeInterfaceTokenERKNS_8String16E',
'_ZN7android6Parcel10writeInt32Ei',
'_ZN7android6Parcel13writeString16ERKNS_8String16E',
'_ZN7android6ParcelD1Ev',
'_ZNK7android14IAppOpsService22getInterfaceDescriptorEv',
'_ZN7android2spINS_14IAppOpsServiceEED2Ev',
'_ZN7android14IAppOpsService14setDefaultImplENS_2spIS0_EE',
'_ZN7android14IAppOpsService14getDefaultImplEv',
'_ZN7android14IAppOpsServiceC2Ev',
'_ZN7android14IAppOpsServiceD2Ev',
'_ZN7android14IAppOpsServiceD1Ev',
'_ZN7android14IAppOpsServiceD0Ev',
'_ZN7android15BnAppOpsService10onTransactEjRKNS_6ParcelEPS1_j',
'_ZN7android11BnInterfaceINS_14IAppOpsServiceEE10onAsBinderEv',
'_ZN7android14IAppOpsService10descriptorE',
'_ZN7android14IAppOpsService12default_implE',
'_ZNK7android6Parcel12readString16Ev',
'_ZN7android6Parcel16writeNoExceptionEv',
'_ZNK7android6Parcel12readString16EPNSt3__18optionalINS_8String16EEE',
'_ZNK7android6Parcel8readBoolEv',
'_ZNK7android6Parcel16readStrongBinderEv',
'_ZN7android6Parcel9writeBoolEb',
'_ZNK7android6Parcel17readExceptionCodeEv',
'_ZN7android6Parcel13writeString16ERKNSt3__18optionalINS_8String16EEE',
'_ZNK7android6Parcel8readByteEv',
'_ZN7android6Parcel17writeStrongBinderERKNS_2spINS_7IBinderEEE',
'_ZN7android10permission17PermissionCheckerC2Ev',
'_ZN7android10permission17PermissionChecker10getServiceEv',
'_ZN7android10permission17PermissionChecker44checkPermissionForDataDeliveryFromDatasourceERKNS_8String16ERKNS_7content22AttributionSourceStateES4_i',
'_ZN7android10permission17PermissionChecker15checkPermissionERKNS_8String16ERKNS_7content22AttributionSourceStateES4_bbbi',
'_ZN7android10permission17PermissionChecker49checkPermissionForStartDataDeliveryFromDatasourceERKNS_8String16ERKNS_7content22AttributionSourceStateES4_i',
'_ZN7android10permission17PermissionChecker27checkPermissionForPreflightERKNS_8String16ERKNS_7content22AttributionSourceStateES4_i',
'_ZN7android10permission17PermissionChecker41checkPermissionForPreflightFromDatasourceERKNS_8String16ERKNS_7content22AttributionSourceStateES4_i',
'_ZN7android10permission17PermissionChecker32finishDataDeliveryFromDatasourceEiRKNS_7content22AttributionSourceStateE',
'_ZN7android10permission17PermissionCheckerC1Ev',
'_ZN7android10permission18IPermissionChecker11asInterfaceERKNS_2spINS_7IBinderEEE',
        ];

        serverCandidates.forEach(sym => {
            const ok = hookDebugSymbol(sym,
                args => log(`[server] ${sym} ENTER args=${args[0]} ...`),
                retval => log(`[server] ${sym} LEAVE → ${retval}`)
            );
            if (!ok) log(`[-] Skipping symbol: ${sym}`);
        });

        // serverCandidates.forEach(sym => {
        //     hookExport(sym,
        //         function(args) { log(`[server] ${sym} ENTER args=${args[0]} ...`); },
        //         function(retval) { log(`[server] ${sym} LEAVE → ${retval}`); }
        //     );
        // });

        // Optional: enumerate likely suspects by regex
        // Module.enumerateSymbols('libaudioflinger.so').forEach(s => {
        //     if (/(RecordThread|PlaybackThread|AudioStreamIn|read|write|threadLoop)/.test(s.name)) {
        //         try {
        //             Interceptor.attach(s.address, {
        //                 onEnter(args) { log(`[CALL] ${s.name}`); },
        //                 onLeave(retval) { log(`[RET] ${s.name} → ${retval}`); }
        //             });
        //         } catch (_) {}
        //     }
        // });
    });
}

function installServerHooksAudioClient() {
    waitForModule('libaudioclient.so', () => {
        log('[*] libaudioclient.so loaded — installing server hooks');

        const serverCandidates = [
      // AudioRecord
      '_ZN7android11AudioRecord5startEv',
      '_ZN7android11AudioRecord4stopEv',
      '_ZN7android11AudioRecord12obtainBufferEPNS0_6BufferEPK8timespecPS3_Pm', // older
      '_ZN7android11AudioRecord12obtainBufferEPNS0_6BufferEPK8timespecPS3_Pj', // newer size_t/uint32_t variant
      // AudioTrack
      '_ZN7android10AudioTrack5startEv',
      '_ZN7android10AudioTrack4stopEv',
      '_ZN7android10AudioTrack13releaseBufferEPKNS0_6BufferE',


      '_ZN7android11AudioSystem10stopOutputEi',
      '_ZN7android11AudioSystem13releaseOutputEi',
      '_ZN7android27StaticAudioTrackServerProxy12obtainBufferEPNS_5Proxy6BufferEb',
      '_ZN7android27StaticAudioTrackServerProxy13releaseBufferEPNS_5Proxy6BufferE',
      '_ZN7android11ServerProxy12obtainBufferEPNS_5Proxy6BufferEb',
      ''
    ];

        serverCandidates.forEach(sym => {
            const ok = hookDebugSymbol(sym,
                args => log(`[server] ${sym} ENTER args=${args[0]} ...`),
                retval => log(`[server] ${sym} LEAVE → ${retval}`)
            );
            if (!ok) log(`[-] Skipping symbol: ${sym}`);
        });

        // serverCandidates.forEach(sym => {
        //     hookExport(sym,
        //         function(args) { log(`[server] ${sym} ENTER args=${args[0]} ...`); },
        //         function(retval) { log(`[server] ${sym} LEAVE → ${retval}`); }
        //     );
        // });

        // Optional: enumerate likely suspects by regex
        // Module.enumerateSymbols('libaudioflinger.so').forEach(s => {
        //     if (/(RecordThread|PlaybackThread|AudioStreamIn|read|write|threadLoop)/.test(s.name)) {
        //         try {
        //             Interceptor.attach(s.address, {
        //                 onEnter(args) { log(`[CALL] ${s.name}`); },
        //                 onLeave(retval) { log(`[RET] ${s.name} → ${retval}`); }
        //             });
        //         } catch (_) {}
        //     }
        // });
    });
}

// ================================
// Bootstrap
// ================================
(function main() {
  const proc = whichProcess();
  log(`[i] Attached to: ${proc}`);

  // 1) Java (only in app processes)
  // installJavaModeHooks();

  // 2) Client hooks (when inside an app that loaded libaudioclient)
  // installClientHooks();

  // 3) Server hooks (when attached to audioserver)
  // if (proc.indexOf('audioserver') !== -1) {
    // installServerHooks();
    installServerHooksAudioClient();
  // } else {
    // If someone accidentally runs this in an app but still wants server info,
    // they should attach a second session to audioserver separately.
    // log('[i] Not audioserver; server hooks skipped.');
  // }
})();
