import frida, sys


outfile = "buffer_dumps.bin"

# def on_message(msg, data):
#     if msg["type"] == "send" and msg["payload"]["event"] == "buffer-dump":
#         with open(outfile, "ab") as f:
#             f.write(data)
#         print(f"Dumped {len(data)} bytes")
#     else:
#         print(msg)

def on_message(message, data):
    if message["type"] == "send":
        print("Got Audio Message")
        if message["payload"]["event"] == "audio-chunk":
            with open("dump.raw", "ab") as f:
                f.write(data)
    else:
        print(message)

device = frida.get_usb_device()
session = device.attach("audioserver")
with open("script.js") as f:
    script = session.create_script(f.read())

script.on("message", on_message)
script.load()
sys.stdin.read()
