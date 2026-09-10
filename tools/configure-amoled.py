"""Provision only the selected AMOLED V2 over native USB; never print secrets."""
import argparse
import json
import time
from pathlib import Path
import serial
from serial.tools import list_ports

parser = argparse.ArgumentParser()
parser.add_argument("--port", required=True)
parser.add_argument("--config", default=str(Path(__file__).resolve().parents[1]/"local/amoled-config.json"))
args = parser.parse_args()
ports = [p for p in list_ports.comports() if p.device == args.port and (p.vid, p.pid) == (0x303A, 0x1001)]
if len(ports) != 1:
    raise SystemExit("Selected port is not the expected ESP32-S3 native USB device")
data = json.loads(Path(args.config).read_text(encoding="utf-8-sig"))
payload = {key: data[key] for key in ("ssid", "password", "url", "token")}
if not payload["password"]:
    raise SystemExit("Fill password in the local configuration first")
connection = serial.Serial()
connection.port, connection.baudrate, connection.timeout = args.port, 115200, .25
connection.dtr = connection.rts = False
connection.open()
with connection:
    connection.write(b'\nIDENTIFY\n')
    deadline = time.monotonic()+5
    while time.monotonic() < deadline:
        if b'AGENT_DESK_AMOLED_V2' in connection.readline():
            break
    else:
        raise SystemExit("AMOLED V2 application did not identify itself; configuration not sent")
    connection.write(b'CONFIG '+json.dumps(payload, ensure_ascii=False).encode()+b'\n')
    deadline = time.monotonic()+8
    while time.monotonic() < deadline:
        line = connection.readline()
        if b'CONFIG_SAVED' in line:
            print("Configuration saved; device restarting to connect")
            break
        if b'CONFIG_REJECTED' in line:
            raise SystemExit("Device rejected configuration")
    else:
        raise SystemExit("No configuration acknowledgement")
