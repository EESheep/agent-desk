"""Exercise SQLite activity and legacy fallback without reading real sessions."""
import json
import os
import sqlite3
import sys
import tempfile
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from codex_status_probe import rollout_states, normalize, merged_tasks

with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {"CODEX_HOME": tmp}), patch("codex_status_probe.time.time", return_value=2000):
    root = Path(tmp)
    logs = root/"sessions"
    logs.mkdir()
    legacy = logs/"rollout-legacy.jsonl"
    legacy.write_text(json.dumps({"type":"event_msg", "payload":{"type":"task_started", "turn_id":"turn"}})+"\n")
    os.utime(legacy, (1990, 1990))
    assert rollout_states(["legacy", "missing"]) == {"legacy":"active", "missing":"unknown"}
    assert not (root/"thread_history_1.sqlite").exists()
    db = sqlite3.connect(root/"thread_history_1.sqlite")
    db.execute("CREATE TABLE thread_turns(thread_id TEXT, status TEXT, completed_at INTEGER, rollout_ordinal INTEGER)")
    db.executemany("INSERT INTO thread_turns VALUES(?,?,?,?)", [
        ("live", "completed", 1000, 1), ("live", "inProgress", None, 2),
        ("done", "completed", 1990, 1), ("old", "completed", 1000, 1),
        ("failed", "failed", 1990, 1), ("future", "newStatus", None, 1),
        ("legacy", "completed", 1000, 1)])
    db.commit()
    assert rollout_states(["live","done","old","failed","future","legacy"]) == {
        "live":"active", "done":"completed", "old":"idle", "failed":"systemError",
        "future":"unknown", "legacy":"idle"}
    assert normalize({"status":{"type":"notLoaded"}}, "active")["status"] == "active"
    # A resumed desktop task can keep its public ID while using a new history ID.
    history_id = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
    index = sqlite3.connect(root/"state_5.sqlite")
    index.execute("CREATE TABLE threads(id TEXT, rollout_path TEXT)")
    index.execute("INSERT INTO threads VALUES(?,?)", ("old", str(logs/f"rollout-date-old_{history_id}.jsonl")))
    index.commit()
    index.close()
    db.execute("INSERT INTO thread_turns VALUES(?,?,?,?)", (history_id, "inProgress", None, 1))
    db.commit()
    assert rollout_states(["old"])["old"] == "active"
    db.execute("UPDATE thread_turns SET status='completed', completed_at=1995 WHERE thread_id=?", (history_id,))
    db.commit()
    assert rollout_states(["old"])["old"] == "completed"
    db.execute("DROP TABLE thread_turns")
    db.commit()
    db.close()
    assert rollout_states(["legacy"])["legacy"] == "active"
    os.utime(legacy, (1, 1))
    assert rollout_states(["legacy"])["legacy"] == "unknown"
print("PASS: latest SQLite turn, completion/error states, unknown states, legacy/schema fallback")
assert merged_tasks([{"id":"same", "updatedAt":2}, {"id":"same", "updatedAt":1}],
                    [{"id":"same", "source":"kimi", "updatedAt":0}]) == [
    {"id":"same", "source":"codex", "updatedAt":2},
    {"id":"same", "source":"kimi", "updatedAt":0}]
print("PASS: duplicate public IDs removed without merging different providers")
