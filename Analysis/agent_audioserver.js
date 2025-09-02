// agent_audioserver.js
// Frida agent for audioserver / libaudioflinger
// Hooks RecordTrack::getNextBuffer / releaseBuffer (mic uplink)
// and Track::getNextBuffer / releaseBuffer (speaker downlink).
// Fill in OFFSETS with your reversed offsets.

'use strict';

const OFFSETS = {
    // ===== FILL THESE OFFSETS (hex, relative to libaudioflinger base) =====
    // recordtrack_getNextBuffer: 0x0,   // RecordTrack::getNextBuffer
    // recordtrack_releaseBuffer: 0x0,   // RecordTrack::releaseBuffer
    // track_getNextBuffer: 0x0,         // Playback Track::getNextBuffer
    // track_releaseBuffer: 0x0,         // Playback Track::releaseBuffer
    // track_start: 0x0,                 // Track::start (optional)
    // track_stop: 0x0,                  // Track::stop (optional)


    // recordtrack_getNextBuffer: DebugSymbol.fromName("_ZN7android12AudioFlinger12RecordThread11RecordTrack12getNextBufferEPNS_19AudioBufferProvider6BufferE").address,   // RecordTrack::getNextBuffer
    recordtrack_getNextBuffer: DebugSymbol.fromName("_ZN7android12AudioFlinger12RecordThread11RecordTrack13getNextBufferEPNS_19AudioBufferProvider6BufferE").address,   // RecordTrack::getNextBuffer
    // recordtrack_releaseBuffer: DebugSymbol.fromName(name).address,   // RecordTrack::releaseBuffer
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

// AudioBufferProvider::Buffer layout
// const BufferStruct ={ frameCount: 0, size: 8, raw: 16 };
    
// const BufferStruct = Process.pointerSize === 8
//     ? { frameCount: 0, size: 8, raw: 16 }
//     : { frameCount: 0, size: 4, raw: 8 };

// function readBufferFields(bufPtr) {
//     try {
//         const size = (Process.pointerSize === 8)
//             ? bufPtr.add(BufferStruct.size).readU64()
//             : bufPtr.add(BufferStruct.size).readU32();
//         const raw = bufPtr.add(BufferStruct.raw).readPointer();
//         return { size: Number(size), raw };
//     } catch (_) { return null; }
// }

// const BufferStruct = {
//     frameCount: 0, // size_t
//     size: 8,       // size_t
//     raw: 16        // void* raw
// };

// function readBuffer(ptr) {
//     const frameCount = ptr.add(BufferStruct.frameCount).readU64();
//     const size = ptr.add(BufferStruct.size).readU64();
//     const rawPtr = ptr.add(BufferStruct.raw).readPointer();
    
// }

const BufferStruct = { raw: 0, frameCount: 8 };

// function readAFBuffer(bufPtr, frameSizeGuess = 2) { // guess 2 bytes/sample (mono 16-bit)
//     const raw = bufPtr.add(BufferAF.raw).readPointer();
//     const frames = (Process.pointerSize === 8)
//         ? bufPtr.add(BufferAF.frameCount).readU64()
//         : bufPtr.add(BufferAF.frameCount).readU32();
//     const sizeBytes = Number(frames) * frameSizeGuess;
//     return { raw, frameCount: Number(frames), size: sizeBytes };
// }

function readBufferFields(ptr, frameSizeGuess) {
    // const frameSizeGuess = 2;
    
    const raw = ptr.add(BufferStruct.raw).readPointer();
    const frameCount = ptr.add(BufferStruct.frameCount).readU64();
    const size = Number(frameCount) * frameSizeGuess;
    log("frameCount: "+frameCount.toString())
    log("size: "+Number(size))
    log("raw ptr: "+raw.toString())
    return { frameCount, size, raw };
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
    attach(lib, OFFSETS.recordtrack_getNextBuffer, "RecordTrack::getNextBuffer", {
        onEnter(args) {
            this.thisPtr = args[0];
            this.bufPtr = args[1];
        },
        onLeave(retval) {
            if ((retval.toInt32() || 0) < 0) return;
            const fields = readBufferFields(this.bufPtr, 2);
            if (!fields || fields.size <= 0) return;
            // const bytes = Memory.readByteArray(fields.raw, fields.size);
            const bytes = fields.raw.readByteArray(Number(fields.size));
            send({ event: "recordGetNextBuffer", thisPtr: idForThis(this.thisPtr), size: fields.size }, bytes);
        }
    });

    // --- Playback Track::getNextBuffer (downlink speaker) ---
    attach(lib, OFFSETS.track_getNextBuffer, "Track::getNextBuffer", {
        onEnter(args) {
            this.thisPtr = args[0];
            this.bufPtr = args[1];
        },
        onLeave(retval) {
            if ((retval.toInt32() || 0) < 0) return;
            const fields = readBufferFields(this.bufPtr, 2);
            if (!fields || fields.size <= 0) return;
            // const bytes = Memory.readByteArray(fields.raw, fields.size);
            const bytes = fields.raw.readByteArray(Number(fields.size));
            send({ event: "trackGetNextBuffer", thisPtr: idForThis(this.thisPtr), size: fields.size }, bytes);
        }
    });

    // --- Playback Track::releaseBuffer (downlink flush) ---
    attach(lib, OFFSETS.track_releaseBuffer, "Track::releaseBuffer", {
        onEnter(args) {
            const fields = readBufferFields(args[1], 2);
            if (fields && fields.size > 0) {
                send({ event: "trackReleaseBuffer", thisPtr: idForThis(args[0]), size: fields.size });
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

    // Optional lifecycle
    attach(lib, OFFSETS.track_start, "Track::start", {
        onEnter(args) {
            send({ event: "trackStart", thisPtr: idForThis(args[0]) });
        }
    });
    attach(lib, OFFSETS.track_stop, "Track::stop", {
        onEnter(args) {
            send({ event: "trackStop", thisPtr: idForThis(args[0]) });
        }
    });
    attach(lib, OFFSETS.record_stop, "RecordTrack::stop", {
        onEnter(args) {
            send({ event: "recordStop", thisPtr: idForThis(args[0]) });
        }
    });

    log("Hooks installed (where offsets provided).");
})();
