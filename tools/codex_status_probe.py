"""Send Codex and Kimi Code task activity and quota snapshots to the desk panel."""

import argparse
import json
import math
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


def kimi_turn_state(session_dir, now, active_window=15 * 60, completed_window=2 * 60):
    # ponytail: per-agent log freshness is a heuristic, not process liveness.
    states = {"idle": 0, "completed": 1, "active": 2, "waiting": 3}
    status, latest_mtime = "idle", None
    try:
        wires = list((session_dir / "agents").glob("*/wire.jsonl"))
    except OSError:
        return status, latest_mtime
    for wire in wires:
        pending_prompts, waiting, finished = set(), set(), False
        try:
            mtime = wire.stat().st_mtime
            age = now - mtime
            if age > active_window:
                latest_mtime = max(latest_mtime or mtime, mtime)
                continue
            with wire.open(encoding="utf-8") as stream:
                for line in stream:
                    try:
                        record = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if not isinstance(record, dict):
                        continue
                    kind, prompt_id = record.get("type"), record.get("promptId")
                    interaction_id = record.get("id")
                    if isinstance(prompt_id, str) and prompt_id:
                        if kind == "turn.prompt":
                            pending_prompts.add(prompt_id)
                        elif kind in {"prompt.completed", "prompt.aborted"}:
                            pending_prompts.discard(prompt_id)
                            finished = True
                    if isinstance(interaction_id, str) and interaction_id:
                        if kind == "interaction.request":
                            waiting.add(interaction_id)
                        elif kind == "interaction.resolved":
                            waiting.discard(interaction_id)
        except (OSError, UnicodeError):
            continue  # One disappearing/unreadable agent must not stop either source.
        latest_mtime = max(latest_mtime or mtime, mtime)
        agent_status = ("waiting" if waiting else "active" if pending_prompts else
                        "completed" if finished and age <= completed_window else "idle")
        if states[agent_status] > states[status]:
            status = agent_status
    return status, latest_mtime


def kimi_sessions(limit, now=None):
    # ponytail: web and CLI sessions both land under KIMI_CODE_HOME; state.json
    # updatedAt is epoch milliseconds while Codex updatedAt is seconds.
    root = Path(os.environ.get("KIMI_CODE_HOME", Path.home() / ".kimi-code"))
    now = now if now is not None else time.time()
    sessions = []
    skipped_invalid = False
    try:
        lines = (root / "session_index.jsonl").read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError):
        return None  # Unavailable is different from a successfully read empty index.
    for line in lines:
        if not line.strip():
            continue
        try:
            entry = json.loads(line)
            if not isinstance(entry, dict):
                raise ValueError("session index entry must be an object")
            session_id, session_dir = entry.get("sessionId"), entry.get("sessionDir")
            if not all(isinstance(value, str) and value.strip() and "\0" not in value
                       for value in (session_id, session_dir)):
                raise ValueError("sessionId and sessionDir must be nonempty strings")
            session_id.encode("utf-8")
            state = json.loads((Path(session_dir) / "state.json").read_text(encoding="utf-8"))
            if not isinstance(state, dict):
                raise ValueError("session state must be an object")
            title = state.get("title") or "Kimi session"
            if not isinstance(title, str):
                raise ValueError("session title must be a string")
            title.encode("utf-8")
        except (OSError, ValueError):  # Includes JSONDecodeError and UnicodeError.
            skipped_invalid = True
            continue
        if state.get("archived"):
            continue
        status, mtime = kimi_turn_state(Path(session_dir), now)
        updated = state.get("updatedAt")
        try:
            updated = updated / 1000 if type(updated) in (int, float) else None
            if updated is not None and not math.isfinite(updated):
                updated = None
        except OverflowError:
            updated = None
        sessions.append({
            "id": session_id,
            "title": title,
            "status": status,
            "updatedAt": updated if updated is not None else mtime,
            "source": "kimi",
        })
    sessions.sort(key=lambda session: session["updatedAt"] or 0, reverse=True)
    return sessions[:limit] if sessions or not skipped_invalid else None


def merged_tasks(codex_tasks, kimi_tasks):
    # ponytail: keep each source's internal order; interleave by recency only.
    merged, i, j = [], 0, 0
    while i < len(codex_tasks) and j < len(kimi_tasks):
        if (codex_tasks[i].get("updatedAt") or 0) >= (kimi_tasks[j].get("updatedAt") or 0):
            merged.append(dict(codex_tasks[i], source="codex"))
            i += 1
        else:
            merged.append(kimi_tasks[j])
            j += 1
    merged.extend(dict(task, source="codex") for task in codex_tasks[i:])
    merged.extend(kimi_tasks[j:])
    return merged


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


def snapshot_wire(tasks, usage=None, kimi_collected=False):
    tasks = tasks[:8]  # TASK rows and ACTIVITY cards describe the same displayed set.
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
    for task in tasks:
        source = task.get("source", "codex")
        # ponytail: use a readable ASCII alias until a real CJK font is added to firmware.
        ascii_title = " ".join("".join(c if c.isascii() and c.isprintable() else " "
                                       for c in task["title"]).split())
        short_id = task["id"].removeprefix("session_")[:8]
        fallback = "Kimi session" if source == "kimi" else "Codex task"
        display_title = f"{ascii_title or fallback} / {short_id}"
        lines.append(f"TASK\t{clipped_hex(task['id'], 63)}\t{clipped_hex(display_title, 95)}\t"
                     f"{states.get(task['status'], 1)}\t0\t{source}")
    usage = usage or {}

    def activity_card(source, detail):
        running = sum(t["status"] in {"active", "waiting"}
                      for t in tasks if t.get("source", "codex") == source)
        return ("ACTIVITY", f"{running} RUNNING" if running else "ALL QUIET", detail, source)

    # ponytail: the firmware holds only 4 cards total; a Kimi card replaces LINK,
    # which merely restates the USB UART transport.
    cards = [activity_card("codex", "Local Codex rollout monitor")]
    if kimi_collected:
        cards.append(activity_card("kimi", "Local Kimi Code session monitor"))
    cards += [
        ("CODEX LEFT", f"{usage['left']}%" if usage.get("left") is not None else "UNKNOWN",
         f"{usage.get('plan') or 'Codex'} / {usage.get('windowMins') or '?'} min window", "codex"),
        ("RESET IN", format_duration(usage.get("resetsIn")), "Official App Server rate limit", "codex"),
    ]
    if not kimi_collected:
        cards.append(("LINK", "USB UART", "Task activity + quota snapshot", "codex"))
    for title, value, detail, source in cards[:4]:
        lines.append(f"CARD\t{clipped_hex(title, 31)}\t{clipped_hex(value, 47)}\t"
                     f"{clipped_hex(detail, 95)}\t{source}")
    lines.append("END")
    wire = ("\n".join(lines) + "\n").encode("ascii")
    if len(wire) >= 4096:
        raise SystemExit("snapshot exceeds the firmware's 4095-byte UART limit")
    return wire


def collect(codex, limit, with_kimi=True):
    tasks, usage = probe(codex, limit)
    kimi = kimi_sessions(limit) if with_kimi else None
    return merged_tasks(tasks, kimi or [])[:limit], usage, kimi is not None


def send_serial(port, codex, limit, watch, interval, with_kimi=True):
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
            tasks, usage, kimi_collected = collect(codex, limit, with_kimi)
            connection.write(snapshot_wire(tasks, usage, kimi_collected))
            connection.flush()
            counts = {}
            for task in tasks[:8]:
                counts[task.get("source", "codex")] = counts.get(task.get("source", "codex"), 0) + 1
            summary = " + ".join(f"{count} {source}" for source, count in sorted(counts.items()))
            print(f"sent {summary or '0'} task(s) to {port}", flush=True)
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
    import tempfile
    from types import SimpleNamespace
    from unittest.mock import patch
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
    assert wire.startswith("BEGIN\nTASK\t") and "\t2\t0\tcodex\n" in wire and wire.endswith("END\n")
    assert "LINK".encode().hex() in wire  # Legacy 4-card layout when Kimi is absent.

    now = time.time()
    with tempfile.TemporaryDirectory() as tmp:
        session_dir = Path(tmp)
        wire_dir = session_dir / "agents" / "main"
        wire_dir.mkdir(parents=True)
        wire_file = wire_dir / "wire.jsonl"
        wire_file.write_text('{"type":"turn.prompt","promptId":"p1"}\n', encoding="utf-8")
        os.utime(wire_file, (now, now))
        assert kimi_turn_state(session_dir, now)[0] == "active"
        with wire_file.open("a", encoding="utf-8") as stream:
            stream.write('{"type":"interaction.request","id":"a1"}\n')
        os.utime(wire_file, (now, now))
        assert kimi_turn_state(session_dir, now)[0] == "waiting"
        with wire_file.open("a", encoding="utf-8") as stream:
            stream.write('{"type":"interaction.resolved","id":"a1"}\n'
                         '{"type":"prompt.completed","promptId":"p1"}\n')
        os.utime(wire_file, (now, now))
        assert kimi_turn_state(session_dir, now)[0] == "completed"
        assert kimi_turn_state(session_dir, now + 3 * 60)[0] == "idle"

        child = session_dir / "agents" / "agent-0" / "wire.jsonl"
        child.parent.mkdir()
        # Same prompt id in another agent must not be resolved by the main agent.
        child.write_text('{"type":"turn.prompt","promptId":"p1"}\n', encoding="utf-8")
        os.utime(child, (now, now))
        assert kimi_turn_state(session_dir, now)[0] == "active"
        with child.open("a", encoding="utf-8") as stream:
            stream.write('{"type":"interaction.request","id":"a1"}\n')
        os.utime(child, (now, now))
        assert kimi_turn_state(session_dir, now)[0] == "waiting"
        # An old waiting agent is not revived by another agent's fresh writes.
        os.utime(child, (now - 901, now - 901))
        assert kimi_turn_state(session_dir, now)[0] == "completed"
        with child.open("a", encoding="utf-8") as stream:
            stream.write('{"type":"interaction.resolved","id":"a1"}\n'
                         '{"type":"prompt.aborted","promptId":"p1"}\n')
        os.utime(child, (now, now))
        assert kimi_turn_state(session_dir, now)[0] == "completed"
        assert kimi_turn_state(session_dir, now + 901)[0] == "idle"
        real_stat, real_open = Path.stat, Path.open
        def disappearing_stat(path, *args, **kwargs):
            if path == child:
                raise FileNotFoundError("removed after enumeration")
            return real_stat(path, *args, **kwargs)
        def unreadable_open(path, *args, **kwargs):
            if path == child:
                raise PermissionError("agent log temporarily unavailable")
            return real_open(path, *args, **kwargs)
        with patch.object(Path, "stat", disappearing_stat):
            assert kimi_turn_state(session_dir, now)[0] == "completed"
        with patch.object(Path, "open", unreadable_open):
            assert kimi_turn_state(session_dir, now)[0] == "completed"
        with patch.object(Path, "glob", side_effect=OSError("directory unavailable")):
            assert kimi_turn_state(session_dir, now) == ("idle", None)

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        session_dir = root / "sessions" / "wd_x" / "session_abc123"
        (session_dir / "agents" / "main").mkdir(parents=True)
        (session_dir / "state.json").write_text(json.dumps(
            {"title": "Kimi task", "updatedAt": 1788445147207, "archived": False}), encoding="utf-8")
        (root / "session_index.jsonl").write_text(json.dumps(
            {"sessionId": "session_abc123", "sessionDir": str(session_dir)}) + "\n", encoding="utf-8")
        with patch.dict(os.environ, {"KIMI_CODE_HOME": str(root)}):
            found = kimi_sessions(8)
        assert found[0]["title"] == "Kimi task" and found[0]["updatedAt"] == 1788445147.207
        with patch.dict(os.environ, {"KIMI_CODE_HOME": str(root / "missing")}):
            assert kimi_sessions(8) is None

        index_file = root / "session_index.jsonl"
        state_file = session_dir / "state.json"
        good_entry = {"sessionId": "session_abc123", "sessionDir": str(session_dir)}
        good_state = {"title": "Kimi task", "updatedAt": 1788445147207}
        with patch.dict(os.environ, {"KIMI_CODE_HOME": str(root)}):
            for bad_state in (None, [], 1, "bad", {"title": ["bad"]}):
                state_file.write_text(json.dumps(bad_state), encoding="utf-8")
                assert kimi_sessions(8) is None
            state_file.write_bytes(b"\xff")
            assert kimi_sessions(8) is None
            state_file.write_text(json.dumps(good_state), encoding="utf-8")
            bad_entries = [None, [], 1, {}, {"sessionId": 1, "sessionDir": str(session_dir)},
                           {"sessionId": "s", "sessionDir": []},
                           {"sessionId": "", "sessionDir": str(session_dir)}]
            index_file.write_text("\n".join(json.dumps(e) for e in bad_entries) +
                                  '\n{"incomplete":\n' + json.dumps(good_entry), encoding="utf-8")
            assert len(kimi_sessions(8)) == 1  # Bad rows never hide a healthy sibling.
            index_file.write_bytes(b"\xff")
            assert kimi_sessions(8) is None
            index_file.write_text("", encoding="utf-8")
            assert kimi_sessions(8) == []
            index_file.write_text(json.dumps(good_entry), encoding="utf-8")
            state_file.write_text(json.dumps(dict(good_state, archived=True)), encoding="utf-8")
            assert kimi_sessions(8) == []
            for timestamp in ([], True, float("nan"), float("inf"), 10 ** 400):
                state_file.write_text(json.dumps(dict(good_state, updatedAt=timestamp)), encoding="utf-8")
                assert kimi_sessions(8)[0]["updatedAt"] is None
            real_read_text = Path.read_text
            def unreadable_index(path, *args, **kwargs):
                if path == index_file:
                    raise PermissionError("index unavailable")
                return real_read_text(path, *args, **kwargs)
            with patch.object(Path, "read_text", unreadable_index), \
                    patch(__name__ + ".probe", return_value=([active], {})):
                kept, usage, available = collect("unused", 8)
                assert len(kept) == 1 and kept[0]["source"] == "codex" and not available
                fallback = snapshot_wire(kept, usage, available).decode()
                assert "\tkimi\n" not in fallback and "LINK".encode().hex() in fallback

    older = {"id": "c1", "title": "Old", "status": "idle", "updatedAt": 100}
    newer = {"id": "c2", "title": "New", "status": "idle", "updatedAt": 300}
    kimi = [{"id": "session_k1", "title": "K", "status": "active", "updatedAt": 200, "source": "kimi"}]
    merged = merged_tasks([newer, older], kimi)
    assert [task["id"] for task in merged] == ["c2", "session_k1", "c1"]
    kimi_wire = snapshot_wire(merged, {"left": 78}, kimi_collected=True).decode("ascii")
    assert "\t0\tkimi\n" in kimi_wire and "LINK".encode().hex() not in kimi_wire
    assert kimi_wire.count("CARD\t") == 4

    with patch(__name__ + ".probe", return_value=([newer, older], {})), \
            patch(__name__ + ".kimi_sessions", return_value=kimi) as read_kimi:
        limited, _, available = collect("unused", 1)
        assert available and [t["id"] for t in limited] == ["c2"]
        assert snapshot_wire(limited, kimi_collected=True).count(b"TASK\t") == 1
        codex_only, _, available = collect("unused", 2, with_kimi=False)
        assert not available and len(codex_only) == 2 and read_kimi.call_count == 1
        assert all(t["source"] == "codex" for t in codex_only)

    many_codex = [dict(newer, id=f"c{i}", status="active") for i in range(8)]
    many_kimi = [dict(kimi[0], id=f"k{i}") for i in range(8)]
    capped = snapshot_wire(merged_tasks(many_codex, many_kimi), kimi_collected=True).decode()
    rows = [line.split("\t") for line in capped.splitlines()]
    assert sum(row[0] == "TASK" for row in rows) == 8
    assert not any(row[0] == "TASK" and row[-1] == "kimi" for row in rows)
    activity = {row[-1]: bytes.fromhex(row[2]).decode() for row in rows
                if row[0] == "CARD" and bytes.fromhex(row[1]).decode() == "ACTIVITY"}
    assert activity == {"codex": "8 RUNNING", "kimi": "ALL QUIET"}


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser()
    parser.add_argument("--codex", help="path to codex executable")
    parser.add_argument("--no-kimi", action="store_true", help="skip Kimi Code session collection")
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
                    args.watch, max(2.0, args.interval), with_kimi=not args.no_kimi)
    else:
        tasks, usage, _ = collect(find_codex(args.codex), max(1, min(args.limit, 100)),
                                  with_kimi=not args.no_kimi)
        print(json.dumps((tasks, usage), ensure_ascii=False, indent=2))
