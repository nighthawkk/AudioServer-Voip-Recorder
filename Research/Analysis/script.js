'use strict';


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

function log(msg) {
  console.log(`[${new Date().toISOString()}] ${msg}`);
}

const sym_obtain = "_ZN7android11ServerProxy12obtainBufferEPNS_5Proxy6BufferEb";

function hookAudio() {
  const sym = DebugSymbol.fromName(sym_obtain);
  if (!sym || sym.address.isNull()) {
    log("[-] Symbol not found");
    return;
  }
  log(`[+] Hooking ${sym_obtain} at ${sym.address}`);

  Interceptor.attach(sym.address, {
    onEnter(args) {
      this.proxy = args[0]; // ServerProxy* (this)
      this.buf = args[1];   // Buffer*

    //   try {
    //     const structSize = 0x200; // Buffer = 32 bytes (on 64-bit)
    //     let rawStruct = this.proxy.readByteArray(structSize);

    //     send({
    //     event: "buffer-dump"}, rawStruct);

    // } catch (e) {
    //     log(`hook err: ${e}`);
    // }
    },
//     onLeave(retval) {
//   if (!this.buf || this.buf.isNull()) return;
//   try {
//     const structSize = 0x20; // Buffer = 32 bytes (on 64-bit)
//     let rawStruct = this.buf.readByteArray(structSize);

//     send({
//       event: "buffer-dump",
//       retval: retval.toInt32()
//     }, rawStruct);

//   } catch (e) {
//     log(`hook err: ${e}`);
//   }
// }
    onLeave(retval) {
  if (!this.buf || this.buf.isNull()) return;
  try {

    const b = readBuffer(this.buf);

    // const frameCount = this.buf.readU32();       // offset 0x00

    

    // const mSize      = this.buf.add(4).readU32(); // offset 0x04
    // const rawPtr     = this.buf.add(8).readPointer(); // offset 0x08
    // const sequence   = this.buf.add(0x10).readU32(); // offset 0x10
    // const nonContig  = this.buf.add(0x14).readU32(); // offset 0x14


    // const frameSize = this.proxy.add(0x30).readU32();  // <- validate offset!
    // log("framesize: "+ frameSize)
    // const sizeBytes = frameCount * 2;
    // const sizeBytes = frameCount;

    const chunk = b.rawPtr.readByteArray(Number(b.size));
   

    // log(`frames=${frameCount} size=${mSize} raw=${rawPtr} seq=${sequence} nc=${nonContig} sizeBytes=${Number(b.size)}`);
    log(`frames=${b.frameCount} size=${b.size} raw=${b.rawPtr} sizeBytes=${Number(b.size)}`);

    // if (retval.toInt32() !== 0 || frameCount === 0 || rawPtr.isNull())
    //   return;

    if (b.size > 0) {
      // const data = rawPtr.readByteArray(Number(sizeBytes));
      const data = b.rawPtr.readByteArray(Number(b.size));
      send({ event: "audio-chunk", frames: Number(b.frameCount), bytes: Number(b.size) }, data);
    }
  } catch (e) {
    log(`hook err: ${e}`);
  }
}
});
}

setImmediate(hookAudio);
