'use strict';


// android::AudioFlinger::ThreadBase::TrackBase::TrackBase(android::AudioFlinger::ThreadBase*,android::sp<android::AudioFlinger::Client> const&,audio_attributes_t const&,uint,audio_format_t,audio_channel_mask_t,ulong,void *,ulong,audio_session_t,int,uint,bool,android::AudioFlinger::ThreadBase::TrackBase::alloc_type,android::AudioFlinger::ThreadBase::TrackBase::track_type,int,std::__1::basic_string<char,std::__1::char_traits<char>,std::__1::allocator<char>>)
const SAMPLERATE_OFFSET = 0x178;
const UID_OFFSET        = 0x234;
const FRAMESIZE_OFFSET = 0x184;  
const ISOUT_OFFSET = 0x1C8;  
const FORMAT_OFFSET = 0x17C;
const CHANNELMASK_OFFSET = 0x180;
const ATTR_OFFSET = 0x68;


function readAttr(thisPtr) {
    try {
        const attr = thisPtr.add(ATTR_OFFSET);
        const contentType = attr.readS32();
        const usage = attr.add(4).readS32();
        const source = attr.add(8).readS32();
        const flags = attr.add(12).readU32();
        return { contentType, usage, source, flags };
    } catch (e) {
        return null;
    }
}

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

function popcount(x) {
  let count = 0;
  while (x) {
    x &= x - 1;
    count++;
  }
  return count;
}

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

    let channelCount;
    if (isOut === 1) {
        channelCount = audioChannelCountFromOutMask(channelMask);
        log("channelCount1: " + channelCount);
    } else {
        channelCount = audioChannelCountFromInMask(channelMask);
        log("channelCount0: " + channelCount);
    }

    const format = thisPtr.add(FORMAT_OFFSET).readU32();
    log("format: " + format);

    
    let frameSize = channelCount * bytesPerSample(format);
    if (isOut === 0){
        frameSize = 1;
    }
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
    track_start: DebugSymbol.fromName("_ZN7android12AudioFlinger14PlaybackThread5Track5startENS_11AudioSystem12sync_event_tE15audio_session_t").address,                 // Track::start
    track_stop: DebugSymbol.fromName("_ZN7android12AudioFlinger14PlaybackThread5Track4stopEv").address,                  // Track::stop
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
    Interceptor.attach(offset, callbacks);
    log(`Attached ${name} at ${offset}`);
}

(function main() {
    const lib = "libaudioflinger.so";
    log("Agent loaded for audioserver (libaudioflinger)");
    log(OFFSETS)

    //  RecordTrack::getNextBuffer (uplink)
    attach(lib, OFFSETS.recordtrack_getNextBuffer, "RecordTrack::getNextBuffer", {
        onEnter(args) {
            this.thisPtr = args[0];
            this.bufPtr = args[1];
        },
        onLeave(retval) {
            if ((retval.toInt32() || 0) < 0) return;

            const attr = readAttr(this.thisPtr);
            if (!attr) return;

            // mic filtering AUDIO_SOURCE_VOICE_COMMUNICATION = 7
            if (attr.source !== 7) return;

            const fields = readBufferFields(this.thisPtr, this.bufPtr, 1);
            if (!fields || fields.size <= 0) return;
            const bytes = fields.raw.readByteArray(Number(fields.size));


            if (fields.frameFields.isOut === 0) { // mic path
                log("[MIC] uid=" + readUid(this.thisPtr));
                log("[MIC] sampleRate=" + readSampleRate(this.thisPtr));
                log("[MIC] format=" + fields.frameFields.format.toString(16));
                log("[MIC] channelMask=0x" + fields.frameFields.channelMask.toString(16));
                log("[MIC] channelCount=" + fields.frameFields.channelCount);
                log("[MIC] frameSize=" + fields.frameFields.frameSize);
                log("[MIC] frameCount=" + fields.frameCount);
                log("[MIC] bufferSize=" + fields.size);
            }
            send({
                event: "recordGetNextBuffer",
                thisPtr: idForThis(this.thisPtr),
                uid: readUid(this.thisPtr),
                sampleRate: readSampleRate(this.thisPtr),
                size: fields.size
            }, bytes);
        }
    });

    // Playback Track::getNextBuffer (downlink)
    attach(lib, OFFSETS.track_getNextBuffer, "Track::getNextBuffer", {
        onEnter(args) {
            this.thisPtr = args[0];
            this.bufPtr = args[1];
        },
        onLeave(retval) {
            if ((retval.toInt32() || 0) < 0) return;

            const attr = readAttr(this.thisPtr);
            if (!attr) return;

            // mic filtering AUDIO_USAGE_VOICE_COMMUNICATION = 2
            if (attr.usage !== 2) return;

            const fields = readBufferFields(this.thisPtr, this.bufPtr, 2);
            if (!fields || fields.size <= 0) return;
            const bytes = fields.raw.readByteArray(Number(fields.size));
            send({
                event: "trackGetNextBuffer",
                thisPtr: idForThis(this.thisPtr),
                uid: readUid(this.thisPtr),
                sampleRate: readSampleRate(this.thisPtr),
                size: fields.size
            }, bytes);
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
