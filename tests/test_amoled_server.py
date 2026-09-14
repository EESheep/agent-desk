"""python tests/test_amoled_server.py — no accounts or device required."""
import json
import sys
import threading
from http.server import ThreadingHTTPServer
from pathlib import Path
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import Request, urlopen
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from amoled_server import make_snapshot, SnapshotCache, handler_for
from codex_status_probe import normalize_usage

title="中文长标题："+"会话标题不会因列表省略而丢失。"*30
usage=normalize_usage({"rateLimits":{"primary":{"usedPercent":22,"windowDurationMins":10080,"resetsAt":12345},"secondary":{"usedPercent":10,"windowDurationMins":300}}})
data=make_snapshot([{"id":"abc","title":title,"status":"active","source":"codex"}],usage,True)
assert data["tasks"][0]["title"] == title
assert [w["left"] for w in data["providers"][0]["windows"]] == [78]
named_usage=normalize_usage({"rateLimitsByLimitId": {
    "codex": {"primary": {"usedPercent":35,"windowDurationMins":10080}},
    "codex_other": {"limitName":"GPT-5.3-Codex-Spark",
                    "primary":{"usedPercent":0,"windowDurationMins":300},
                    "secondary":{"usedPercent":0,"windowDurationMins":10080}}}})
weekly=make_snapshot([],named_usage,False)["providers"][0]["windows"]
assert len(weekly)==1 and weekly[0]["name"]=="codex" and weekly[0]["left"]==65
assert make_snapshot([],normalize_usage({"rateLimits":{"primary":{"usedPercent":0,"windowDurationMins":300}}}),False)["providers"][0]["windows"]==[]
assert data["providers"][1]["windows"] == []
assert data["providers"][1]["available"] is True
ordered=make_snapshot([{"id":s,"title":s,"status":s} for s in ("idle","active","waiting")],{},False)
assert [t['status'] for t in ordered['tasks']] == ['active','waiting','idle']
cross_project = [
    {"id":"old-active", "title":"Project A", "status":"active", "updatedAt":100},
    {"id":"recent-idle", "title":"Project B", "status":"idle", "updatedAt":300},
    {"id":"new-active", "title":"Project C", "status":"active", "updatedAt":200},
]
assert [t['id'] for t in make_snapshot(cross_project,{},False)['tasks']] == [
    'new-active','old-active','recent-idle']
cross_project[1]['status'] = 'active'
assert make_snapshot(cross_project,{},False)['tasks'][0]['id'] == 'recent-idle'
cache=SnapshotCache()
assert cache.get() is None
with patch("amoled_server.time.monotonic",return_value=100): cache.put(data)
with patch("amoled_server.time.monotonic",return_value=145):
    first,second=cache.get(),cache.get()
    assert first["age_seconds"] == second["age_seconds"] == 45
    assert first["revision"] == second["revision"] == 1
try:
    cache.put({"bad":"x"*65536})
except ValueError:
    assert cache.revision == 1
else: raise AssertionError("Oversized data must not replace last good snapshot")
token="test-token-at-least-24-characters"
server=ThreadingHTTPServer(("127.0.0.1",0),handler_for(cache,token))
thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
url=f'http://127.0.0.1:{server.server_port}/snapshot'
try:
    try: urlopen(url)
    except HTTPError as e: assert e.code == 401
    else: raise AssertionError("Unauthenticated request accepted")
    with urlopen(Request(url,headers={"Authorization":"Bearer "+token})) as response:
        assert json.load(response)["tasks"][0]["title"] == title
finally:
    server.shutdown(); server.server_close(); thread.join()
print("PASS: Unicode titles, independent windows, unavailable quota, stale cache, size bound, HTTP auth")
