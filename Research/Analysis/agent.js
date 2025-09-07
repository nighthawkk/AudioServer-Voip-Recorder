'use strict';

// ---- Buffer struct offsets (64-bit Android) ----
const BufferStruct = {
    frameCount: 0, // size_t
    size: 8,       // size_t
    raw: 16        // void* raw
};

function readBuffer(ptr) {
    const frameCount = ptr.add(BufferStruct.frameCount).readU64();
    const size = ptr.add(BufferStruct.size).readU64();
    const rawPtr = ptr.add(BufferStruct.raw).readPointer();
    return { frameCount, size, rawPtr };
}

// Utility to send a text event
function logEvent(event, thisPtr) {
    send({event, thisPtr: thisPtr.toString()});
}

// ---- AudioTrack::start ----
{
    // const sym = Module.findExportByName("libaudioclient.so", "_ZN7android10AudioTrack5startEv");
    const sym = DebugSymbol.fromName("_ZN7android10AudioTrack5startEv").address;
    if (sym) {
        Interceptor.attach(sym, {
            onEnter(args) {
                logEvent("trackStart", args[0]);
            }
        });
    }
}

// ---- AudioTrack::stop ----
{
    // const sym = Module.findExportByName("libaudioclient.so", "_ZN7android10AudioTrack4stopEv");
    const sym = DebugSymbol.fromName("_ZN7android10AudioTrack4stopEv").address;
    if (sym) {
        Interceptor.attach(sym, {
            onEnter(args) {
                logEvent("trackStop", args[0]);
            }
        });
    }
}

// ---- AudioRecord::stop ----
{
    // const sym = Module.findExportByName("libaudioclient.so", "_ZN7android11AudioRecord4stopEv");
    const sym = DebugSymbol.fromName("_ZN7android11AudioRecord4stopEv").address;
    if (sym) {
        Interceptor.attach(sym, {
            onEnter(args) {
                logEvent("recordStop", args[0]);
            }
        });
    }
}

// ---- AudioTrack::releaseBuffer(Buffer*) ----
{
    // const sym = Module.findExportByName("libaudioclient.so",
    //     "_ZN7android10AudioTrack13releaseBufferEPKNS0_6BufferE");
    const sym = DebugSymbol.fromName("_ZN7android10AudioTrack13releaseBufferEPKNS0_6BufferE").address;
    if (sym) {
        Interceptor.attach(sym, {
            onEnter(args) {
                const thisPtr = args[0];
                const bufPtr = args[1];
                const b = readBuffer(bufPtr);
                if (b.size > 0) {
                    const chunk = b.rawPtr.readByteArray(Number(b.size));
                    send({event: "trackReleaseBuffer", thisPtr: thisPtr.toString(), size: b.size}, chunk);
                }
            }
        });
    }
}

// ---- AudioRecord::obtainBuffer(Buffer*, timespec*, timespec*, size_t*) ----
{
    // const sym = Module.findExportByName("libaudioclient.so",
    //     "_ZN7android11AudioRecord12obtainBufferEPNS0_6BufferEPK8timespecPS3_Pm");
    const sym = DebugSymbol.fromName("_ZN7android11AudioRecord12obtainBufferEPNS0_6BufferEPK8timespecPS3_Pm").address;
    if (sym) {
        Interceptor.attach(sym, {
            onEnter(args) {
                this.buf = args[1];
                this.thisPtr = args[0];
            },
            onLeave(retval) {
                if (retval.toInt32() >= 0 && this.buf) {
                    const b = readBuffer(this.buf);
                    if (b.size > 0) {
                        const chunk = b.rawPtr.readByteArray(Number(b.size));
                        send({event: "recordObtainBuffer", thisPtr: this.thisPtr.toString(), size: b.size}, chunk);
                    }
                }
            }
        });
    }
}

console.log("[*] All audio hooks installed");
