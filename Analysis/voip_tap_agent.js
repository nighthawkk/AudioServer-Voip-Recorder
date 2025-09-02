// voip_tap_agent.js
// Hooks Android libaudioclient symbols and streams audio buffers to host.
// No file I/O happens on device — chunks are sent to the Python host via send().

'use strict';

(function () {
  const is64 = Process.pointerSize === 8;

  // Helper: resolve any of these mangled names (API-level differences)
  function findSymbol(candidates, moduleName) {
    for (const name of candidates) {
    //   const addr = Module.findExportByName(moduleName, name) || DebugSymbol.fromName(name).address;
      const addr = DebugSymbol.fromName(name).address;
      if (!addr.isNull()) {
        send({ log: `Resolved ${name} @ ${addr}` });
        return addr;
      }
    }
    return ptr(0);
  }

  const lib = "libaudioclient.so";

  // C++ methods we want
  const sym_AudioTrack_start = findSymbol([
    "_ZN7android10AudioTrack5startEv"
  ], lib);

  const sym_AudioTrack_stop = findSymbol([
    "_ZN7android10AudioTrack4stopEv"
  ], lib);

  const sym_AudioTrack_releaseBuffer = findSymbol([
    "_ZN7android10AudioTrack13releaseBufferEPKNS0_6BufferE"
  ], lib);

  const sym_AudioRecord_stop = findSymbol([
    "_ZN7android11AudioRecord4stopEv"
  ], lib);

  // Two possible obtainBuffer signatures (size_t* vs unsigned int*)
  const sym_AudioRecord_obtainBuffer = (function () {
    const cands = [
      "_ZN7android11AudioRecord12obtainBufferEPNS0_6BufferEPK8timespecPS3_Pm", // size_t*
      "_ZN7android11AudioRecord12obtainBufferEPNS0_6BufferEPK8timespecPS3_Pj"  // unsigned int*
    ];
    return findSymbol(cands, lib);
  })();

  // State similar to the original
  let ifMic = false;        // becomes true after first mic buffer
  // reset_time is host-managed, but we also gate downlink here like the C code
  // by only sending downlink when ifMic === true.

  // Helpers to read android::AudioTrack/AudioRecord::Buffer
  // struct Buffer { size_t frameCount; size_t size; void* raw; ... }
  function readBufferFields(bufPtr) {
    if (bufPtr.isNull()) return null;
    try {
      const off_frameCount = 0;
      const off_size = Process.pointerSize;
      const off_raw = Process.pointerSize * 2;

      const size = is64 ? bufPtr.add(off_size).readU64() : bufPtr.add(off_size).readU32();
      const raw = bufPtr.add(off_raw).readPointer();
      return { size, raw };
    } catch (e) {
      send({ log: `readBufferFields error: ${e}` });
      return null;
    }
  }

  function idForThis(p) {
    return p.toString(); // stable string key for host map
  }

  // ---- Hooks ----

  if (!sym_AudioTrack_start.isNull()) {
    Interceptor.attach(sym_AudioTrack_start, {
      onEnter(args) {
        this.thisptr = args[0];
        send({ event: "track_start", id: idForThis(this.thisptr) });
      }
    });
  }

  if (!sym_AudioTrack_stop.isNull()) {
    Interceptor.attach(sym_AudioTrack_stop, {
      onEnter(args) {
        this.thisptr = args[0];
        send({ event: "track_stop", id: idForThis(this.thisptr) });
      }
    });
  }

  if (!sym_AudioRecord_stop.isNull()) {
    Interceptor.attach(sym_AudioRecord_stop, {
      onEnter(args) {
        this.thisptr = args[0];
        // On mic stop: host will close & rename .bc; agent also drops downlink gating
        send({ event: "record_stop", id: idForThis(this.thisptr) });
        ifMic = false;
      }
    });
  }

  if (!sym_AudioRecord_obtainBuffer.isNull()) {
    Interceptor.attach(sym_AudioRecord_obtainBuffer, {
      onEnter(args) {
        this.thisptr = args[0];
        this.bufPtr = args[1];
      },
      onLeave(retval) {
        // retval >= 0 indicates success
        let ok = false;
        try {
          ok = (is64 ? retval.toInt64().toNumber() : retval.toInt32()) >= 0;
        } catch (e) { ok = false; }

        if (!ok) return;

        const fields = readBufferFields(this.bufPtr);
        if (!fields || fields.size <= 0) return;

        // mic seen
        ifMic = true;

        // Send uplink chunk; host will open/create file if needed
        const bytes = Memory.readByteArray(fields.raw, Number(fields.size));
        send({ event: "uplink_chunk", id: idForThis(this.thisptr), size: Number(fields.size) }, bytes);
      }
    });
  }

  if (!sym_AudioTrack_releaseBuffer.isNull()) {
    Interceptor.attach(sym_AudioTrack_releaseBuffer, {
      onEnter(args) {
        this.thisptr = args[0];
        const bufPtr = args[1];
        const fields = readBufferFields(bufPtr);
        if (!fields || fields.size <= 0) return;

        // Match original behavior: only write downlink after mic has been detected
        if (!ifMic) return;

        const bytes = Memory.readByteArray(fields.raw, Number(fields.size));
        send({ event: "downlink_chunk", id: idForThis(this.thisptr), size: Number(fields.size) }, bytes);
      }
    });
  }

  send({ log: "Agent loaded. Hooks installed (where available)." });
})();
