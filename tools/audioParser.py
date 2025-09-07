#!/usr/bin/env python3
import os
import wave
import sys

def convert_to_wav(infile, outfile, samplerate, channels):
    """Convert raw PCM 16-bit little-endian audio to WAV."""
    with open(infile, "rb") as f:
        raw = f.read()

    nframes = len(raw) // (2 * channels)  # 16-bit samples

    with wave.open(outfile, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(2)  # 16-bit PCM
        wf.setframerate(samplerate)
        wf.writeframes(raw)

    print(f"[+] {infile} → {outfile} | {channels}ch @ {samplerate}Hz ({nframes} frames)")


def process_file(path):
    filename = os.path.basename(path)
    name, ext = os.path.splitext(filename)
    if ext not in [".ac", ".bc"]:
        return

    try:
        pkg, sr_str, sec_str, usec_str = name.split("_")
        samplerate = int(sr_str)
        ts_sec = int(sec_str)
        ts_usec = int(usec_str)
    except Exception as e:
        print(f"[!] Skipping {filename}, parse error: {e}")
        return

    if ext == ".ac":  # downlink (stereo)
        channels = 2
        wav_rate = samplerate // 2
    else:  # ".bc" uplink (mono)
        channels = 1
        wav_rate = samplerate

    outfile = path.replace('.','_') + ".wav"
    convert_to_wav(path, outfile, wav_rate, channels)


def main(root):
    for dirpath, _, files in os.walk(root):
        for f in files:
            if f.endswith(".ac") or f.endswith(".bc"):
                process_file(os.path.join(dirpath, f))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} /path/to/search")
        sys.exit(1)

    rootdir = sys.argv[1]
    if not os.path.isdir(rootdir):
        print(f"[!] {rootdir} is not a valid directory")
        sys.exit(1)

    main(rootdir)
