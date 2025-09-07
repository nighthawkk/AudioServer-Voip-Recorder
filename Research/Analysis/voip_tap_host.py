# voip_tap_host.py
# Usage:
#   python3 voip_tap_host.py -n audioserver    # attach by process name on device (-U implied)
#   python3 voip_tap_host.py -p 412            # attach by pid
#
# Requires: pip install frida
# This writes files locally, e.g. ./voip_<EPOCH>/*.tmp then renames to .bc / .ac on stops.

import argparse
import frida
import os
import random
import string
import sys
import time

# --------- local file handling (on PC) ---------
class RecorderHost:
    def __init__(self):
        self.rec_map = {}  # id -> {"fh": file, "path": str, "kind": "uplink"/"downlink"}
        self.ts = None
        self.reset_time = True  # matches C code initial state
        # When True and downlink arrives first, we skip creating a file (like getFile(..., isAC==1) returning NULL)

    @staticmethod
    def _rand_name(n=10):
        alphabet = string.ascii_letters[:36]  # match-ish the C charset length
        return ''.join(random.choice(alphabet) for _ in range(n))

    def _ensure_folder(self):
        if self.ts is None or self.reset_time:
            self.ts = int(time.time())
            self.reset_time = False
        folder = f"voip_{self.ts}"
        if not os.path.isdir(folder):
            os.makedirs(folder, exist_ok=True)
        return folder

    def _open_file_if_needed(self, rec_id: str, is_ac: bool):
        """
        Mimic C getFile(this,isAC):
          - creates timestamped folder on first open of a session.
          - if is_ac==1 and reset_time==1 => return None (skip first downlink-only cases)
        """
        if rec_id in self.rec_map:
            return self.rec_map[rec_id]["fh"]

        if is_ac and self.reset_time:
            # Skip creating a file for downlink until mic has been seen
            return None

        folder = self._ensure_folder()
        name = self._rand_name(10)
        path = os.path.join(folder, name)  # temp path without extension
        fh = open(path, "ab")
        self.rec_map[rec_id] = {"fh": fh, "path": path, "kind": "downlink" if is_ac else "uplink"}
        return fh

    def write_chunk(self, rec_id: str, is_ac: bool, data: bytes):
        fh = self._open_file_if_needed(rec_id, is_ac)
        if fh is None:
            return  # honoring skip rule for initial downlink
        fh.write(data)
        fh.flush()

    def close_and_rename(self, rec_id: str, ext: str):
        info = self.rec_map.pop(rec_id, None)
        if not info:
            return
        fh = info["fh"]
        path = info["path"]
        fh.flush()
        fh.close()
        new_path = path + ext
        try:
            os.replace(path, new_path)
        except OSError:
            # If already exists or any rename issue, just try writing a duplicate with suffix
            alt = path + f"-{int(time.time())}" + ext
            os.replace(path, alt)

    def on_record_stop(self, rec_id: str):
        # uplink end: rename to .bc and set reset_time
        self.close_and_rename(rec_id, ".bc")
        self.reset_time = True  # matches original logic

    def on_track_stop(self, rec_id: str):
        # downlink end: rename to .ac
        self.close_and_rename(rec_id, ".ac")


# --------- Frida plumbing ----------
def load_agent():
    with open("voip_tap_agent.js", "r", encoding="utf-8") as f:
        return f.read()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-U", action="store_true", help="USB device (default)")
    parser.add_argument("-n", "--name", help="Process name to attach (e.g., audioserver)")
    parser.add_argument("-p", "--pid", type=int, help="PID to attach")
    args = parser.parse_args()

    dev = frida.get_usb_device(timeout=5)  # -U default
    session = None

    if args.pid:
        session = dev.attach(args.pid)
    elif args.name:
        pid = dev.get_process(args.name).pid
        session = dev.attach(pid)
    else:
        print("Specify -n <process> or -p <pid>. Example: -n audioserver")
        sys.exit(1)

    agent_code = load_agent()
    script = session.create_script(agent_code)

    recorder = RecorderHost()

    def on_message(msg, data):
        try:
            if msg["type"] == "send":
                payload = msg.get("payload", {})
                event = payload.get("event")
                if "log" in payload:
                    print("[agent]", payload["log"])
                    return

                rec_id = payload.get("id")

                if event == "uplink_chunk":
                    if data:
                        recorder.write_chunk(rec_id, is_ac=False, data=bytes(data))
                elif event == "downlink_chunk":
                    if data:
                        recorder.write_chunk(rec_id, is_ac=True, data=bytes(data))
                elif event == "record_stop":
                    recorder.on_record_stop(rec_id)
                elif event == "track_stop":
                    recorder.on_track_stop(rec_id)
                elif event in ("track_start",):
                    # Nothing to do, but helpful if you want to log
                    pass
            elif msg["type"] == "error":
                print("[agent-error]", msg.get("stack") or msg)
            else:
                print("[agent-msg]", msg)
        except Exception as e:
            print("Host on_message error:", e)

    script.on("message", on_message)
    script.load()

    print("[*] Attached. Writing files locally in ./voip_<timestamp>/")
    print("[*] Ctrl-C to quit.")
    try:
        sys.stdin.read()
    except KeyboardInterrupt:
        pass
    finally:
        script.unload()
        session.detach()

if __name__ == "__main__":
    main()
