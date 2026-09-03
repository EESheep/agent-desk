"""Send Codex task activity and quota snapshots to the desk panel."""

import argparse
import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path


def find_codex(explicit=None):
    if explicit:
        return explicit
    if found := shutil.which("codex"):
        return found
    root = Path(os.environ.get("LOCALAPPDATA", "")) / "OpenAI" / "Codex" / "bin"
    candidates = sorted(root.glob("*/codex.exe"), key=lambda p: p.stat().st_mtime, reverse=True)
    if candidates:
        return str(candidates[0])
    raise SystemExit("codex executable not found; pass --codex PATH")


def find_panel_port(ports=None):
    if ports is None:
        from serial.tools import list_ports
        ports = list_ports.comports()
    ports = list(ports)
    # ponytail: match the observed CH343 USB ID, not a firmware identity;
    # multiple matching adapters require --port instead of guessing.
    candidates = [p for p in ports if (p.vid, p.pid) == (0x1A86, 0x55D3)]
    if len(candidates) == 1:
        return candidates[0].device
    inventory = "; ".join(f"{p.device}: {p.description}" for p in ports) or "none"
    reason = "Multiple CH343 adapters found" if candidates else "No matching CH343 adapter found"
    raise SystemExit(f"{reason}. Connect the panel's UART1 USB port or specify --port COMx. "
                     f"Available ports: {inventory}")


def normalize(thread, rollout_state="idle"):
    status = thread.get("status") or {"type": "unknown"}
    flags = status.get("activeFlags") or []
    title = thread.get("name") or (thread.get("preview") or "Untitled task").splitlines()[0]
    live_status = status.get("type", "unknown")
    derived = live_status if live_status in {"active", "idle", "systemError"} else rollout_state
    return {
        "id": thread.get("id", ""),
        "title": title,
        "status": "waiting" if "waitingOnUserInput" in flags else derived,
        "updatedAt": thread.get("updatedAt"),
    }


def rollout_states(thread_ids, active_window=15 * 60, completed_window=2 * 60):
    wanted = set(thread_ids)
    states = {thread_id: "idle" for thread_id in wanted}
    root = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex")) / "sessions"
    if not root.exists():
        return states
    now = time.time()
    for path in root.rglob("*.jsonl"):
        thread_id = next((item for item in wanted if path.stem.endswith(item)), None)
        if not thread_id:
            continue
        try:
            age = now - path.stat().st_mtime
            latest_turn = None
            terminal = set()
            with path.open(encoding="utf-8") as stream:
                for line in stream:
                    try:
                        record = json.loads(line)
                        payload = record.get("payload") or {}
                    except (json.JSONDecodeError, AttributeError):
                        continue
                    if record.get("type") != "event_msg":
                        continue
                    event, turn = payload.get("type"), payload.get("turn_id") or payload.get("turnId")
                    if event == "task_started" and turn:
                        latest_turn = turn
                    elif event in {"task_complete", "turn_aborted"} and turn:
                        terminal.add(turn)
            if latest_turn and latest_turn not in terminal and age <= active_window:
                states[thread_id] = "active"
            elif latest_turn and latest_turn in terminal and age <= completed_window:
                states[thread_id] = "completed"
        except OSError:
            pass
    return states


def normalize_usage(result):
    preferred = (result or {}).get("rateLimits") or {}
    window = preferred.get("primary") or {}
    used = window.get("usedPercent")
    resets_at = window.get("resetsAt")
    return {
        "left": max(0, min(100, round(100 - used))) if isinstance(used, (int, float)) else None,
        "windowMins": window.get("windowDurationMins"),
        "resetsIn": max(0, round(resets_at - time.time())) if isinstance(resets_at, (int, float)) else None,
    }


def probe(codex, limit):
    messages = [
        {"method": "initialize", "id": 1, "params": {"clientInfo": {
            "name": "desk_panel_probe", "title": "Desk Panel Probe", "version": "0.1.0"}}},
        {"method": "initialized", "params": {}},
        {"method": "thread/list", "id": 2, "params": {
            "limit": limit, "sortKey": "updated_at", "sortDirection": "desc", "sourceKinds": []}},
        {"method": "account/rateLimits/read", "id": 3},
        {"method": "account/read", "id": 4, "params": {"refreshToken": False}},
    ]
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    process = subprocess.Popen(
        [codex, "app-server", "--stdio"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", creationflags=flags,
    )
    replies = queue.Queue()
    threading.Thread(target=lambda: [replies.put(line) for line in process.stdout], daemon=True).start()
    try:
        for message in messages:
            process.stdin.write(json.dumps(message, separators=(",", ":")) + "\n")
        process.stdin.flush()
        replies_by_id = {}
        while 2 not in replies_by_id or 3 not in replies_by_id or 4 not in replies_by_id:
            line = replies.get(timeout=15)
            if line.startswith("{"):
                message = json.loads(line)
                if message.get("id") in {2, 3, 4}:
                    replies_by_id[message["id"]] = message
    except queue.Empty:
        raise SystemExit("Codex App Server timed out waiting for thread/list")
    finally:
        process.terminate()
        process.wait(timeout=5)
    if "error" in replies_by_id[2]:
        raise SystemExit(f"thread/list failed: {replies_by_id[2]['error']}")
    threads = replies_by_id[2]["result"]["data"]
    activity = rollout_states(thread.get("id", "") for thread in threads)
    tasks = [normalize(thread, activity.get(thread.get("id", ""), "idle")) for thread in threads]
    usage = normalize_usage(replies_by_id[3].get("result")) if "error" not in replies_by_id[3] else {}
    account = replies_by_id[4].get("result", {}).get("account") or {}
    usage["plan"] = account.get("planType")
    return tasks, usage


def format_duration(seconds):
    if seconds is None:
        return "UNKNOWN"
    days, remainder = divmod(seconds, 86400)
    hours = remainder // 3600
    return f"{days}d {hours}h" if days else f"{hours}h"


def snapshot_wire(tasks, usage=None):
    def clipped_hex(value, limit):
        data = value.encode("utf-8")[:limit]
        while data:
            try:
                return data.decode("utf-8").encode("utf-8").hex()
            except UnicodeDecodeError:
                data = data[:-1]
        return ""

    states = {"idle": 1, "active": 2, "waiting": 3, "completed": 4, "systemError": 5}
    lines = ["BEGIN"]
    for task in tasks[:8]:
        # ponytail: use a readable ASCII alias until a real CJK font is added to firmware.
        ascii_title = " ".join("".join(c if c.isascii() and c.isprintable() else " "
                                       for c in task["title"]).split())
        display_title = f"{ascii_title or 'Codex task'} / {task['id'][:8]}"
        lines.append(f"TASK\t{clipped_hex(task['id'], 63)}\t{clipped_hex(display_title, 95)}\t"
                     f"{states.get(task['status'], 1)}\t0")
    usage = usage or {}
    running = sum(task["status"] in {"active", "waiting"} for task in tasks)
    cards = [
        ("ACTIVITY", f"{running} RUNNING" if running else "ALL QUIET", "Local Codex rollout monitor"),
        ("CODEX LEFT", f"{usage['left']}%" if usage.get("left") is not None else "UNKNOWN",
         f"{usage.get('plan') or 'Codex'} / {usage.get('windowMins') or '?'} min window"),
        ("RESET IN", format_duration(usage.get("resetsIn")), "Official App Server rate limit"),
        ("LINK", "USB UART", "Task activity + quota snapshot"),
    ]
    for title, value, detail in cards:
        lines.append(f"CARD\t{clipped_hex(title, 31)}\t{clipped_hex(value, 47)}\t{clipped_hex(detail, 95)}")
    lines.append("END")
    wire = ("\n".join(lines) + "\n").encode("ascii")
    if len(wire) >= 4096:
        raise SystemExit("snapshot exceeds the firmware's 4095-byte UART limit")
    return wire


def send_serial(port, codex, limit, watch, interval):
    import serial  # Already installed with ESP-IDF/esptool; no extra dependency.
    if port.lower() == "auto":
        port = find_panel_port()
        print(f"Auto-selected CH343 serial port: {port}", flush=True)
    connection = serial.Serial()
    connection.port, connection.baudrate, connection.timeout = port, 115200, 0.2
    connection.dtr = connection.rts = False
    connection.open()
    try:
        while True:
            tasks, usage = probe(codex, limit)
            connection.write(snapshot_wire(tasks, usage))
            connection.flush()
            print(f"sent {len(tasks)} real Codex task(s) to {port}", flush=True)
            time.sleep(0.3)
            for line in connection.read_all().decode("utf-8", errors="replace").splitlines():
                if "REAL snapshot" in line:
                    print(f"panel confirmed: {line.strip()}", flush=True)
            if not watch:
                return
            time.sleep(interval)
    finally:
        connection.close()


def self_test():
    from types import SimpleNamespace
    board = SimpleNamespace(device="COM7", vid=0x1A86, pid=0x55D3, description="CH343")
    other = SimpleNamespace(device="COM3", vid=None, pid=None, description="Other serial port")
    assert find_panel_port([other, board]) == "COM7"
    board.device = "COM12"
    assert find_panel_port([board]) == "COM12"
    second = SimpleNamespace(device="COM8", vid=0x1A86, pid=0x55D3, description="CH343")
    for ports, reason in [([], "No matching"), ([other], "No matching"),
                          ([board, second], "Multiple")]:
        try:
            find_panel_port(ports)
        except SystemExit as error:
            assert reason in str(error) and "--port" in str(error)
        else:
            raise AssertionError("Ambiguous or missing port must not be auto-selected")
    active = normalize({"id": "t1", "name": "Build", "status": {
        "type": "active", "activeFlags": ["waitingOnApproval"]}})
    assert active == {"id": "t1", "title": "Build", "status": "active", "updatedAt": None}
    unloaded = normalize({"id": "t2", "preview": "First line\nSecond", "status": {"type": "notLoaded"}})
    assert unloaded["title"] == "First line" and unloaded["status"] == "idle"
    wire = snapshot_wire([active], {"left": 78, "resetsIn": 3600}).decode("ascii")
    assert wire.startswith("BEGIN\nTASK\t") and "\t2\t0\nCARD\t" in wire and wire.endswith("END\n")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser()
    parser.add_argument("--codex", help="path to codex executable")
    parser.add_argument("--limit", type=int, default=8)
    parser.add_argument("--port", help="send snapshot to COMx or auto-detect with 'auto'")
    parser.add_argument("--watch", action="store_true", help="refresh continuously; auto-detect port if omitted")
    parser.add_argument("--interval", type=float, default=10.0)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("self-test OK")
    elif args.port or args.watch:
        send_serial(args.port or "auto", find_codex(args.codex), max(1, min(args.limit, 8)),
                    args.watch, max(2.0, args.interval))
    else:
        print(json.dumps(probe(find_codex(args.codex), max(1, min(args.limit, 100))),
                         ensure_ascii=False, indent=2))
