#!/usr/bin/env python3


# python host_audioserver.py --usb --name audioserver --agent agent_audioserver.js

import argparse, frida, os, sys, time, random, string

uidMap = {"com.whatsapp": 10191}
FILES = {}
SESSION_TS = time.strftime("%Y%m%d-%H%M%S")
BASE_DIR = os.path.join(os.getcwd(), f"audioserver_session_{SESSION_TS}")
os.makedirs(BASE_DIR, exist_ok=True)

def rand_name(n=10):
    return ''.join(random.choice(string.ascii_letters) for _ in range(n))

def open_if_needed(thisPtr, kind):
    if thisPtr in FILES:
        return FILES[thisPtr]["fh"]
    name = rand_name()
    tmp = os.path.join(BASE_DIR, f"{thisPtr}_{name}.tmp")
    fh = open(tmp, "ab")
    FILES[thisPtr] = {"fh": fh, "tmp": tmp, "kind": kind}
    print(f"[+] Opened {tmp} ({kind})")
    return fh

def close_and_rename(thisPtr, ext):
    info = FILES.pop(thisPtr, None)
    if not info: return
    fh = info["fh"]; tmp = info["tmp"]
    fh.close()
    final = os.path.splitext(tmp)[0] + ext
    os.replace(tmp, final)
    print(f"[+] Closed {thisPtr} -> {final}")

def on_message(message, data):
    if message["type"] == "send":
        payload = message.get("payload", {})
        uid = 0
        if "sampleRate" in payload:
            print("[agent]", "sampleRate: "+str(payload["sampleRate"]))
        if "uid" in payload:
            uid = payload["uid"]
            print("[agent]", "uid: "+str(uid))
            if uid not in uidMap.values():
                return
        if "log" in payload:
            print("[agent]", payload["log"]); return
        event = payload.get("event"); thisPtr = payload.get("thisPtr", "?")
        if event == "recordGetNextBuffer":
            fh = open_if_needed(thisPtr, "uplink")
            if fh and data: fh.write(data)
        elif event == "trackGetNextBuffer":
            fh = open_if_needed(thisPtr, "downlink")
            if fh and data: fh.write(data)
        elif event == "recordReleaseBuffer":
            print("[agent]","recordReleaseBuffer Called")
            pass
        elif event == "trackReleaseBuffer":
            print("[agent]","trackReleaseBuffer Called")
            pass
        elif event == "trackStop":
            print("[agent]","trackStop Called")
            close_and_rename(thisPtr, ".ac")
        elif event == "recordStop":
            print("[agent]","recordStop Called")
            close_and_rename(thisPtr, ".bc")
    elif message["type"] == "error":
        print("[agent-error]", message)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--usb", action="store_true", help="Use USB device")
    ap.add_argument("--pid", type=int, help="PID to attach")
    ap.add_argument("--name", default="audioserver", help="Process name")
    ap.add_argument("--agent", default="agent_audioserver.js", help="Agent JS")
    args = ap.parse_args()

    dev = frida.get_usb_device() if args.usb else frida.get_local_device()
    pid = args.pid or dev.get_process(args.name).pid
    session = dev.attach(pid)

    with open(args.agent) as f:
        script = session.create_script(f.read())
    script.on("message", on_message); script.load()
    print(f"[*] Attached to {args.name} pid={pid}, saving in {BASE_DIR}")
    try: sys.stdin.read()
    except KeyboardInterrupt: pass

if __name__ == "__main__":
    main()
