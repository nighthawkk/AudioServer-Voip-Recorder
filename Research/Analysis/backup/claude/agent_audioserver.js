// agent_audioserver.js
// Frida agent for audioserver / libaudioflinger
// Hooks RecordTrack::getNextBuffer / releaseBuffer (mic uplink)
// and Track::getNextBuffer / releaseBuffer (speaker downlink).
// Fill in OFFSETS with your reversed offsets.

'use strict';


// android::AudioFlinger::ThreadBase::TrackBase::TrackBase(android::AudioFlinger::ThreadBase*,android::sp<android::AudioFlinger::Client> const&,audio_attributes_t const&,uint,audio_format_t,audio_channel_mask_t,ulong,void *,ulong,audio_session_t,int,uint,bool,android::AudioFlinger::ThreadBase::TrackBase::alloc_type,android::AudioFlinger::ThreadBase::TrackBase::track_type,int,std::__1::basic_string<char,std::__1::char_traits<char>,std::__1::allocator<char>>)
const SAMPLERATE_OFFSET = 0x178;
const UID_OFFSET        = 0x234;
const FRAMESIZE_OFFSET = 0x184;  // <- adjust to your build
const ISOUT_OFFSET = 0x1C8;  // <- adjust to your build
const FORMAT_OFFSET = 0x17C;  // <- adjust to your build
const CHANNELMASK_OFFSET = 0x180;

function bytesPerSample(format) {
    // values from AOSP audio_format_t enum
    switch (format) {
        case 0x1: return 2; // PCM_16_BIT
        case 0x2: return 4; // PCM_32_BIT
        case 0x3: return 4; // PCM_FLOAT
        case 0x4: return 1; // PCM_8_BIT
        case 0x5: return 3; // PCM_24_BIT_PACKED
        default:  return 2; // fallback
    }
}

const AudioChannels = {
    AUDIO_CHANNEL_OUT_ALL: 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 |
                           0x40 | 0x80 | 0x100 | 0x200 | 0x400 |
                           0x800 | 0x1000 | 0x2000 | 0x4000 |
                           0x8000 | 0x10000 | 0x20000,

    AUDIO_CHANNEL_IN_ALL:  0x4 | 0x8 | 0x10 | 0x20 |
                           0x40 | 0x80 | 0x100 | 0x200 |
                           0x400 | 0x800 | 0x1000 | 0x2000 |
                           0x4000 | 0x8000
};

// popcount helper (bit count)
function popcount(x) {
  let count = 0;
  while (x) {
    x &= x - 1;
    count++;
  }
  return count;
}



function debugAudioPath(thisPtr, bufPtr, pathType) {
    const fields = readFrameSizeCalc(thisPtr);
    const sampleRate = readSampleRate(thisPtr);
    const uid = readUid(thisPtr);
    
    // Read buffer info
    const raw = bufPtr.add(BufferStruct.raw).readPointer();
    const frameCount = bufPtr.add(BufferStruct.frameCount).readU64();
    
    // Calculate expected vs actual sizes
    const expectedFrameSize = fields.frameSize;
    const actualSize = Number(frameCount) * expectedFrameSize;
    
    // Try alternative frame size calculations
    const altFrameSize1 = fields.channelCount * 2; // Force 16-bit
    const altSize1 = Number(frameCount) * altFrameSize1;
    
    const altFrameSize2 = 2; // Force mono 16-bit
    const altSize2 = Number(frameCount) * altFrameSize2;
    
    const altFrameSize3 = 4; // Force stereo 16-bit
    const altSize3 = Number(frameCount) * altFrameSize3;
    
    log(`\n=== ${pathType} DEBUG ===`);
    log(`UID: ${uid}`);
    log(`Sample Rate: ${sampleRate}`);
    log(`Format: 0x${fields.format.toString(16)}`);
    log(`Channel Mask: 0x${fields.channelMask.toString(16)}`);
    log(`Channel Count: ${fields.channelCount}`);
    log(`isOut flag: ${fields.isOut}`);
    log(`Frame Count: ${frameCount}`);
    log(`Calculated Frame Size: ${expectedFrameSize}`);
    log(`Buffer Size: ${actualSize}`);
    log(`Alternative sizes: mono16=${altSize2}, stereo16=${altSize3}, ch*16bit=${altSize1}`);
    
    // Sample first few bytes to detect patterns
    try {
        const sample = raw.readByteArray(Math.min(32, actualSize));
        const hex = Array.from(new Uint8Array(sample))
            .map(b => b.toString(16).padStart(2, '0'))
            .join(' ');
        log(`First 32 bytes: ${hex}`);
        
        // Check for silence or patterns
        const allZero = Array.from(new Uint8Array(sample)).every(b => b === 0);
        const allFF = Array.from(new Uint8Array(sample)).every(b => b === 0xFF);
        if (allZero) log("WARNING: Buffer appears to be silence!");
        if (allFF) log("WARNING: Buffer appears to be all 0xFF!");
    } catch(e) {
        log(`Could not read sample: ${e}`);
    }
    
    log(`===================\n`);
    
    return fields;
}

// Equivalent to C inline function
function audioChannelCountFromOutMask(channel) {
  return popcount(channel & AudioChannels.AUDIO_CHANNEL_OUT_ALL);
}

function audioChannelCountFromInMask(channel) {
  return popcount(channel & AudioChannels.AUDIO_CHANNEL_IN_ALL);
}

function readFrameSizeCalc(thisPtr) {
    const isOut = thisPtr.add(ISOUT_OFFSET).readU32();
    log("isOut: " + isOut);

    const channelMask = thisPtr.add(CHANNELMASK_OFFSET).readU32();
    log("channelMask: " + channelMask);

    // channel count must be mutable
    let channelCount;
    if (isOut === 1) {
        channelCount = audioChannelCountFromOutMask(channelMask);
        log("channelCount1: " + channelCount);
    } else {
        channelCount = audioChannelCountFromInMask(channelMask);
        log("channelCount0: " + channelCount);
    }

    // read format value from struct (needs FORMAT_OFFSET)
    const format = thisPtr.add(FORMAT_OFFSET).readU32();
    log("format: " + format);

    const frameSize = channelCount * bytesPerSample(format);
    log("frameSize: " + frameSize);

    return {frameSize, format, channelCount, channelMask, isOut};
}

function readSampleRate(thisPtr) {
    try { 
        const SampleRate = thisPtr.add(SAMPLERATE_OFFSET).readU32();
        return SampleRate; 
        }
    catch (_) { return 0; }
}

function readUid(thisPtr) {
    try {
        const uid = thisPtr.add(UID_OFFSET).readU32();
        return uid; 
    }
    catch (_) { return -1; }
}

const OFFSETS = {
    recordtrack_getNextBuffer: DebugSymbol.fromName("_ZN7android12AudioFlinger12RecordThread11RecordTrack13getNextBufferEPNS_19AudioBufferProvider6BufferE").address,   // RecordTrack::getNextBuffer
    track_getNextBuffer: DebugSymbol.fromName("_ZN7android12AudioFlinger14PlaybackThread5Track13getNextBufferEPNS_19AudioBufferProvider6BufferE").address,         // Playback Track::getNextBuffer
    track_releaseBuffer: DebugSymbol.fromName("_ZN7android12AudioFlinger14PlaybackThread5Track13releaseBufferEPNS_19AudioBufferProvider6BufferE").address,         // Playback Track::releaseBuffer
    track_start: DebugSymbol.fromName("_ZN7android12AudioFlinger14PlaybackThread5Track5startENS_11AudioSystem12sync_event_tE15audio_session_t").address,                 // Track::start (optional)
    track_stop: DebugSymbol.fromName("_ZN7android12AudioFlinger14PlaybackThread5Track4stopEv").address,                  // Track::stop (optional)
    record_stop: DebugSymbol.fromName("_ZN7android12AudioFlinger12RecordThread11RecordTrack4stopEv").address,                  // android::AudioFlinger::RecordThread::RecordTrack::stop(void)
};

function log(msg) { send({ log: msg }); }

function ptrFromBase(lib, offset) {
    if (!offset) return ptr(0);
    const base = Module.findBaseAddress(lib);
    if (!base) return ptr(0);
    return base.add(offset);
}

const BufferStruct = { raw: 0, frameCount: 8 };

function readBufferFields(thisPtr, ptr, frameSizeGuess) {
    // const frameSizeGuess = 2;
    
    const raw = ptr.add(BufferStruct.raw).readPointer();
    const frameCount = ptr.add(BufferStruct.frameCount).readU64();

    
    const frameFields = readFrameSizeCalc(thisPtr);
    log("frameFields: "+frameFields)
    log("frameSize: "+frameFields.frameSize)

    const size = Number(frameCount) * frameFields.frameSize;

    
    log("frameCount: "+frameCount.toString())
    log("size: "+Number(size))
    log("raw ptr: "+raw.toString())
    return { frameCount, size, raw, frameFields };
}

function idForThis(p) { return p.toString(); }

function attach(lib, offset, name, callbacks) {
    // const addr = ptrFromBase(lib, offset);
    // if (!addr.isNull()) {
        Interceptor.attach(offset, callbacks);
        log(`Attached ${name} at ${offset}`);
    // } else {
    //     log(`Skipped ${name}, offset=0 or base not found`);
    // }
}

// ===== MAIN =====
(function main() {
    const lib = "libaudioflinger.so";
    log("Agent loaded for audioserver (libaudioflinger)");
    log(OFFSETS)

    // --- RecordTrack::getNextBuffer (uplink mic) ---
    // attach(lib, OFFSETS.recordtrack_getNextBuffer, "RecordTrack::getNextBuffer", {
    //     onEnter(args) {
    //         this.thisPtr = args[0];
    //         this.bufPtr = args[1];
    //     },
    //     onLeave(retval) {
    //         if ((retval.toInt32() || 0) < 0) return;
    //         const fields = readBufferFields(this.thisPtr, this.bufPtr, 1);
    //         if (!fields || fields.size <= 0) return;
    //         // const bytes = Memory.readByteArray(fields.raw, fields.size);
    //         const bytes = fields.raw.readByteArray(Number(fields.size));


    //         if (fields.frameFields.isOut === 0) { // mic path
    //             log("[MIC] uid=" + readUid(this.thisPtr));
    //             log("[MIC] sampleRate=" + readSampleRate(this.thisPtr));
    //             log("[MIC] format=" + fields.frameFields.format.toString(16));
    //             log("[MIC] channelMask=0x" + fields.frameFields.channelMask.toString(16));
    //             log("[MIC] channelCount=" + fields.frameFields.channelCount);
    //             log("[MIC] frameSize=" + fields.frameFields.frameSize);
    //             log("[MIC] frameCount=" + fields.frameCount);
    //             log("[MIC] bufferSize=" + fields.size);
    //         }
    //         send({
    //             event: "recordGetNextBuffer",
    //             thisPtr: idForThis(this.thisPtr),
    //             uid: readUid(this.thisPtr),
    //             sampleRate: readSampleRate(this.thisPtr),
    //             size: fields.size
    //         }, bytes);
    //         // send({ event: "recordGetNextBuffer", thisPtr: idForThis(this.thisPtr), size: fields.size }, bytes);
    //     }
    // });


    attach(lib, OFFSETS.recordtrack_getNextBuffer, "RecordTrack::getNextBuffer", {
    onEnter(args) {
        this.thisPtr = args[0];
        this.bufPtr = args[1];
        
        // Log entry for debugging
        log(`[ENTER] RecordTrack::getNextBuffer - thisPtr: ${this.thisPtr}`);
    },
    onLeave(retval) {
        if ((retval.toInt32() || 0) < 0) {
            log(`[LEAVE] RecordTrack::getNextBuffer - Error return: ${retval}`);
            return;
        }
        
        // Use debug function
        const fields = debugAudioPath(this.thisPtr, this.bufPtr, "UPLINK/MIC");
        
        // Try multiple interpretations
        const frameCount = this.bufPtr.add(BufferStruct.frameCount).readU64();
        const raw = this.bufPtr.add(BufferStruct.raw).readPointer();
        
        // Send with original calculation
        // const originalSize = Number(frameCount) * fields.frameSize;
        const originalSize = Number(frameCount) * 0.5;
        if (originalSize > 0 && originalSize < 1024*1024) {
            const bytes = raw.readByteArray(originalSize);
            send({
                event: "recordGetNextBuffer",
                thisPtr: idForThis(this.thisPtr),
                uid: readUid(this.thisPtr),
                sampleRate: readSampleRate(this.thisPtr),
                format: fields.format,
                channels: fields.channelCount,
                frameSize: fields.frameSize,
                size: originalSize,
                interpretation: "original"
            }, bytes);
        }
        
        // Also try common Android mic configurations
        const commonConfigs = [
            {channels: 1, bytesPerSample: 2}, // Mono 16-bit (most common for mic)
            {channels: 2, bytesPerSample: 2}, // Stereo 16-bit
            {channels: 1, bytesPerSample: 4}, // Mono 32-bit float
        ];
        
        // Test if different interpretation makes more sense
        for (let config of commonConfigs) {
            const testFrameSize = config.channels * config.bytesPerSample;
            const testSize = Number(frameCount) * testFrameSize;
            
            if (testSize !== originalSize && testSize > 0 && testSize < 1024*1024) {
                try {
                    const testBytes = raw.readByteArray(testSize);
                    send({
                        event: "recordGetNextBuffer_alt",
                        thisPtr: idForThis(this.thisPtr),
                        uid: readUid(this.thisPtr),
                        sampleRate: readSampleRate(this.thisPtr),
                        format: config.bytesPerSample === 4 ? 0x3 : 0x1,
                        channels: config.channels,
                        frameSize: testFrameSize,
                        size: testSize,
                        interpretation: `ch${config.channels}_${config.bytesPerSample*8}bit`
                    }, testBytes);
                } catch(e) {
                    // Size was probably wrong, skip
                }
            }
        }
    }
});

    // --- RecordTrack::releaseBuffer (mic uplink flush) ---
    // attach(lib, OFFSETS.recordtrack_releaseBuffer, "RecordTrack::releaseBuffer", {
    //     onEnter(args) {
    //         const fields = readBufferFields(args[1]);
    //         if (fields && fields.size > 0) {
    //             send({ event: "recordReleaseBuffer", thisPtr: idForThis(args[0]), size: fields.size });
    //         }
    //     }
    // });

    // --- Playback Track::getNextBuffer (downlink speaker) ---
    // attach(lib, OFFSETS.track_getNextBuffer, "Track::getNextBuffer", {
    //     onEnter(args) {
    //         this.thisPtr = args[0];
    //         this.bufPtr = args[1];
    //     },
    //     onLeave(retval) {
    //         if ((retval.toInt32() || 0) < 0) return;
    //         const fields = readBufferFields(this.thisPtr, this.bufPtr, 2);
    //         if (!fields || fields.size <= 0) return;
    //         // const bytes = Memory.readByteArray(fields.raw, fields.size);
    //         const bytes = fields.raw.readByteArray(Number(fields.size));
    //         send({
    //             event: "trackGetNextBuffer",
    //             thisPtr: idForThis(this.thisPtr),
    //             uid: readUid(this.thisPtr),
    //             sampleRate: readSampleRate(this.thisPtr),
    //             size: fields.size
    //         }, bytes);
    //         // send({ event: "trackGetNextBuffer", thisPtr: idForThis(this.thisPtr), size: fields.size }, bytes);
    //     }
    // });

    // // --- Playback Track::releaseBuffer (downlink flush) ---
    // attach(lib, OFFSETS.track_releaseBuffer, "Track::releaseBuffer", {
    //     onEnter(args) {
    //         const fields = readBufferFields(args[0], args[1], 2);
    //         if (fields && fields.size > 0) {
    //             send({ event: "trackReleaseBuffer", thisPtr: idForThis(args[0]), size: fields.size });
    //         }
    //     }
    // });

    // Optional lifecycle
    // attach(lib, OFFSETS.track_start, "Track::start", {
    //     onEnter(args) {
    //         send({ event: "trackStart", thisPtr: idForThis(args[0]) });
    //     }
    // });
    // attach(lib, OFFSETS.track_stop, "Track::stop", {
    //     onEnter(args) {
    //         send({ event: "trackStop", thisPtr: idForThis(args[0]) });
    //     }
    // });
    attach(lib, OFFSETS.record_stop, "RecordTrack::stop", {
        onEnter(args) {
            send({ event: "recordStop", thisPtr: idForThis(args[0]) });
        }
    });

    log("Hooks installed (where offsets provided).");
})();
