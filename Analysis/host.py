import argparse, os, sys, time
import frida

FILES = {}  # maps thisPtr -> (file, basepath)

SESSION_TS = time.strftime("%Y%m%d-%H%M%S")
base_dir = os.path.join(os.getcwd(), f"session_{SESSION_TS}")
os.makedirs(base_dir, exist_ok=True)

def on_message(message, data):
    global FILES
    if message['type'] != 'send':
        print(message)
        return

    payload = message['payload']
    event = payload['event']
    thisPtr = payload.get('thisPtr')

    # Ensure session folder
    # ts = time.strftime("%Y%m%d-%H%M%S")
    # base_dir = os.path.join(os.getcwd(), f"session_{ts}")
    # os.makedirs(base_dir, exist_ok=True)

    # Handle events
    if event in ("trackStart", "recordObtainBuffer"):
        # open new file if not already
        if thisPtr not in FILES:
            fname = f"{thisPtr}.tmp"
            path = os.path.join(base_dir, fname)
            f = open(path, "wb")
            FILES[thisPtr] = (f, base_dir, path)
            print(f"[+] Opened {path}")

    if event in ("trackReleaseBuffer", "recordObtainBuffer"):
        entry = FILES.get(thisPtr)
        if entry and data:
            f, _, _ = entry
            f.write(data)

    if event == "trackStop":
        entry = FILES.pop(thisPtr, None)
        if entry:
            f, folder, tmp_path = entry
            f.close()
            final_path = os.path.splitext(tmp_path)[0] + ".ac"
            os.rename(tmp_path, final_path)
            print(f"[+] Track closed -> {final_path}")

    if event == "recordStop":
        entry = FILES.pop(thisPtr, None)
        if entry:
            f, folder, tmp_path = entry
            f.close()
            final_path = os.path.splitext(tmp_path)[0] + ".bc"
            os.rename(tmp_path, final_path)
            print(f"[+] Record closed -> {final_path}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--usb", action="store_true", help="USB device (Android)")
    ap.add_argument("--package", help="Target package name")
    ap.add_argument("--pid", type=int, help="Target PID")
    ap.add_argument("--script", default="agent.js", help="Path to agent.js")
    args = ap.parse_args()

    dev = frida.get_usb_device(timeout=10) if args.usb else frida.get_local_device()

    if args.pid:
        session = dev.attach(args.pid)
    elif args.package:
        pid = dev.spawn([args.package])
        session = dev.attach(pid)
        dev.resume(pid)
    else:
        print("[-] Need --pid or --package")
        sys.exit(1)

    with open(args.script) as f:
        source = f.read()

    script = session.create_script(source)
    script.on("message", on_message)
    script.load()

    print("[*] Hooks active, press Ctrl+C to quit")
    try:
        while True:
            time.sleep(0.25)
    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    main()
