"""Read-only LAN snapshots for AMOLED V2; reuse the LCD-7 collectors."""
import argparse
import hmac
import ipaddress
import json
import math
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from codex_status_probe import collect, find_codex

MAX_BODY = 65536


def number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def make_snapshot(tasks, usage, kimi_available):
    """Titles remain Unicode; reject oversized source data rather than silently truncate."""
    result = []
    priority = {"active": 0, "waiting": 1, "systemError": 2, "completed": 3, "idle": 4}
    ordered = sorted(tasks, key=lambda task: (
        priority.get(task.get("status"), 5),
        -task["updatedAt"] if number(task.get("updatedAt")) else 0))
    for task in ordered[:8]:
        source = task.get("source", "codex")
        if source not in ("codex", "kimi", "dsh"):
            raise ValueError("unknown task source")
        item = {k: task[k] for k in ("id", "title", "status")}
        if not all(isinstance(item[k], str) for k in item):
            raise ValueError("invalid task fields")
        item["title"] = item["title"].replace("\r", " ").replace("\n", " ")
        item["source"] = source
        if len(item["id"].encode()) > 255 or len(item["title"].encode()) > 4096:
            raise ValueError("source title/id exceeds network capacity")
        result.append(item)
    windows = []
    buckets = usage.get("buckets") or {}
    if not isinstance(buckets, dict):
        buckets = {}
    for bucket_id, bucket in buckets.items():
        if bucket_id not in ("codex", "default") or not isinstance(bucket, dict):
            continue
        for name in ("primary", "secondary"):
            window = bucket.get(name)
            if not isinstance(window, dict):
                continue
            used, duration, reset = (window.get(k) for k in ("usedPercent", "windowDurationMins", "resetsAt"))
            if not number(used) or duration != 10080:
                continue
            windows.append({"name": str(bucket.get("limitName") or bucket_id)[:96],
                            "kind": name, "left": max(0, min(100, round(100-used))),
                            "minutes": duration if number(duration) and duration > 0 else None,
                            "resets_at": reset if number(reset) and reset >= 0 else None})
    if len(windows) > 16:
        raise ValueError("too many quota windows")
    return {"version": 1, "tasks": result, "providers": [
        {"id": "codex", "name": "Codex", "available": True, "windows": windows},
        {"id": "kimi", "name": "Kimi", "available": kimi_available, "windows": []},
        {"id": "dsh", "name": "DSH", "available": False, "windows": []}]}


class SnapshotCache:
    def __init__(self):
        self.lock = threading.Lock()
        self.data = None
        self.captured = 0
        self.revision = 0

    def put(self, data):
        # Check size before replacing the last good data.
        if len(json.dumps(data, ensure_ascii=False).encode()) > MAX_BODY-2048:
            raise ValueError("snapshot too large")
        with self.lock:
            self.data, self.captured = data, time.monotonic()
            self.revision += 1

    def get(self):
        with self.lock:
            if self.data is None:
                return None
            return dict(self.data, revision=self.revision,
                        age_seconds=max(0, int(time.monotonic()-self.captured)),
                        server_time=int(time.time()))


def handler_for(cache, token):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path != "/snapshot":
                self.send_error(404)
                return
            if not hmac.compare_digest(self.headers.get("Authorization", ""), "Bearer "+token):
                self.send_error(401)
                return
            data = cache.get()
            if data is None:
                self.send_error(503, "Waiting for first successful collection")
                return
            body = json.dumps(data, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass  # Do not log titles, credentials or per-poll noise.
    return Handler


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default=str(Path(__file__).resolve().parents[1]/"local/amoled-config.json"))
    parser.add_argument("--interval", type=float, default=10)
    parser.add_argument("--codex")
    args = parser.parse_args()
    config = json.loads(Path(args.config).read_text(encoding="utf-8-sig"))
    address = ipaddress.ip_address(config["host"])
    if not (address.is_private and not address.is_unspecified) or len(config["token"]) < 24:
        raise SystemExit("Use a specific LAN address and a token of at least 24 characters")
    codex = find_codex(args.codex)
    cache = SnapshotCache()
    def update():
        while True:
            try:
                data = make_snapshot(*collect(codex, 8))
                cache.put(data)
                print(f"Collected revision={cache.revision} tasks={len(data['tasks'])}", flush=True)
            except (Exception, SystemExit) as error:
                print(f"Collection failed ({type(error).__name__}); retaining previous snapshot", flush=True)
            time.sleep(max(5, args.interval))
    server = ThreadingHTTPServer((config["host"], config.get("port", 8765)), handler_for(cache, config["token"]))
    server.daemon_threads = True
    threading.Thread(target=update, daemon=True).start()
    print(f"AMOLED snapshot service listening on {config['host']}:{server.server_port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
