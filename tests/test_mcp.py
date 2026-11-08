#!/usr/bin/env python3
"""The MCP server, driven the way a client drives it: over stdin and stdout.

Run with: make test

Every check runs the real binary against the real resources, in a scratch folder.
The rules themselves are tested in test_engine.cpp. This file tests the protocol
and what actually lands on disk.
"""

import hashlib
import json
import os
import re
import select
import subprocess
import sys
import tempfile

BINARY = "build/interviewprep"
RESOURCES = "data/resources.json"
NOW = "2026-11-20T21:30"
MODERN = "2026-07-28"
LEGACY = "2025-11-25"
META = {
    "io.modelcontextprotocol/protocolVersion": MODERN,
    "io.modelcontextprotocol/clientCapabilities": {},
    "io.modelcontextprotocol/clientInfo": {"name": "test", "version": "1"},
}
TOOLS = ["profile_read", "topics_list", "batch_pending", "judgements_search", "judgement_write", "judgement_drop",
         "batch_commit"]

checks = 0
failures = 0


def check(name, condition, detail=""):
    global checks, failures
    checks += 1
    if not condition:
        failures += 1
        print(f"FAIL  {name}  {detail}")


class Client:
    def __init__(self, folder):
        self.process = subprocess.Popen(
            [BINARY, "--state", f"{folder}/state.json", "--resources", RESOURCES, "--now", NOW, "mcp"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8",
        )
        self.next_id = 0

    def write(self, text):
        self.process.stdin.write(text + "\n")
        self.process.stdin.flush()

    def read(self):
        ready, _, _ = select.select([self.process.stdout], [], [], 10)
        if not ready:
            raise TimeoutError("the server did not answer")
        return json.loads(self.process.stdout.readline())

    def request(self, method, params=None, modern=True):
        self.next_id += 1
        params = dict(params or {})
        if modern:
            params["_meta"] = META
        self.write(json.dumps({"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params}))
        reply = self.read()
        check(f"{method} echoes the id", reply.get("id") == self.next_id, reply)
        return reply

    def tool(self, name, modern=True, **arguments):
        reply = self.request("tools/call", {"name": name, "arguments": arguments}, modern)
        result = reply["result"]
        return result.get("isError", False), [block["text"] for block in result["content"]]

    def tool_json(self, name, **arguments):
        is_error, text = self.tool(name, **arguments)
        check(f"{name} succeeds", not is_error, text)
        return json.loads(text[-1])

    def refused(self, name, wanted, **arguments):
        is_error, text = self.tool(name, **arguments)
        check(f"{name} refuses: {wanted}", is_error and wanted in text[0], text)

    def close(self):
        self.process.stdin.close()
        code = self.process.wait(timeout=5)
        self.process.stdout.close()
        self.process.stderr.close()
        return code


def cli(folder, *args, code=None):
    stdin = code if code is not None else ""
    result = subprocess.run(
        [BINARY, "--state", f"{folder}/state.json", "--resources", RESOURCES, *args],
        input=stdin, capture_output=True, text=True, encoding="utf-8",
    )
    check(f"interviewprep {args[0]} works", result.returncode == 0, result.stderr)


def graded(folder, problem, grade, now, code=None):
    if code is not None:
        cli(folder, "attempt", problem, "--now", now, code=code)
    cli(folder, "grade", problem, grade, "--now", now)


def digest(path):
    with open(path, "rb") as file:
        return hashlib.sha256(file.read()).hexdigest()


def read_lines(path):
    with open(path, encoding="utf-8") as file:
        return [json.loads(line) for line in file]


def test_modern_protocol(folder):
    client = Client(folder)

    discover = client.request("server/discover")["result"]
    check("discover lists this revision", MODERN in discover["supportedVersions"])
    check("discover lists a legacy revision", LEGACY in discover["supportedVersions"])
    check("discover has a result type", discover["resultType"] == "complete")
    check("discover has caching hints", discover["ttlMs"] > 0 and discover["cacheScope"] == "public")
    check("discover declares tools", "tools" in discover["capabilities"])
    check("discover says who it is", discover["_meta"]["io.modelcontextprotocol/serverInfo"]["name"] == "interviewprep")
    check("instructions fit Claude Code's limit", 0 < len(discover["instructions"]) < 2048)
    for tool in TOOLS:
        check(f"instructions name {tool}", tool in discover["instructions"])

    listed = client.request("tools/list")["result"]
    check("tools arrive in a fixed order", [t["name"] for t in listed["tools"]] == TOOLS)
    check("tools have caching hints", listed["ttlMs"] > 0 and listed["cacheScope"] == "public")
    check("tools have a result type", listed["resultType"] == "complete")
    check("the list never changes", client.request("tools/list")["result"]["tools"] == listed["tools"])
    for tool in listed["tools"]:
        check(f"{tool['name']} is a safe name", re.fullmatch(r"[a-zA-Z0-9_-]{1,64}", tool["name"]))
        check(f"{tool['name']} has a schema", tool["inputSchema"]["type"] == "object")
        check(f"{tool['name']} fits the description limit", 0 < len(tool["description"]) < 2048)
    read_only = {t["name"]: t["annotations"]["readOnlyHint"] for t in listed["tools"]}
    check("only three tools write", [n for n, flag in read_only.items() if not flag] == ["judgement_write", "judgement_drop", "batch_commit"])

    called = client.request("tools/call", {"name": "batch_pending", "arguments": {}})["result"]
    check("a tool result has a result type", called["resultType"] == "complete")
    check("no handshake was needed", "items" in json.loads(called["content"][0]["text"]))

    client.write(json.dumps({"jsonrpc": "2.0", "id": 90, "method": "tools/list",
                             "params": {"_meta": {**META, "io.modelcontextprotocol/protocolVersion": "1900-01-01"}}}))
    reply = client.read()
    check("an unknown version gets the modern error", reply["error"]["code"] == -32022, reply)
    check("that error lists what works", MODERN in reply["error"]["data"]["supported"])
    check("that error says what was asked", reply["error"]["data"]["requested"] == "1900-01-01")

    client.write(json.dumps({"jsonrpc": "2.0", "id": 91, "method": "tools/list", "params": {
        "_meta": {"io.modelcontextprotocol/protocolVersion": MODERN}}}))
    check("missing capabilities are refused", client.read()["error"]["code"] == -32602)

    client.write(json.dumps({"jsonrpc": "2.0", "id": 92, "method": "tools/list"}))
    check("no version and no handshake is refused", client.read()["error"]["code"] == -32602)

    check("an unknown method is refused", client.request("nope")["error"]["code"] == -32601)
    check("ping is gone in this revision", client.request("ping")["error"]["code"] == -32601)
    check("initialize with _meta is unknown", client.request("initialize")["error"]["code"] == -32601)
    unknown = client.request("tools/call", {"name": "nope", "arguments": {}})
    check("an unknown tool is a protocol error", unknown["error"]["code"] == -32602)

    # Bad input never ends the conversation.
    client.write("this is not json")
    reply = client.read()
    check("bad JSON is a parse error", reply["error"]["code"] == -32700 and "id" not in reply, reply)
    client.write('{"jsonrpc":"2.0","id":null,"method":"tools/list"}')
    check("a null id is refused", client.read()["error"]["code"] == -32600)
    client.write('{"jsonrpc":"2.0","id":93}')
    check("a request needs a method", client.read()["error"]["code"] == -32600)

    # Notifications get no answer: the next line must be for the next request.
    client.write('{"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":1}}')
    client.write('{"jsonrpc":"2.0","method":"notifications/made-up"}')
    client.write("")
    check("a string id comes back as a string", _string_id(client) == "abc")

    check("closing stdin ends the server cleanly", client.close() == 0)


def _string_id(client):
    client.write(json.dumps({"jsonrpc": "2.0", "id": "abc", "method": "tools/list", "params": {"_meta": META}}))
    return client.read()["id"]


def test_legacy_protocol(folder):
    client = Client(folder)
    opened = client.request("initialize", {"protocolVersion": LEGACY, "capabilities": {},
                                           "clientInfo": {"name": "old", "version": "1"}}, modern=False)["result"]
    check("initialize echoes a known version", opened["protocolVersion"] == LEGACY)
    check("initialize names the server", opened["serverInfo"]["name"] == "interviewprep")
    check("initialize declares tools", "tools" in opened["capabilities"])
    check("initialize gives the instructions", "profile_read" in opened["instructions"])
    check("legacy results stay legacy", "resultType" not in opened)

    client.write('{"jsonrpc":"2.0","method":"notifications/initialized"}')
    check("ping works after a handshake", client.request("ping", modern=False)["result"] == {})
    listed = client.request("tools/list", modern=False)["result"]
    check("tools list without _meta", [t["name"] for t in listed["tools"]] == TOOLS)
    is_error, text = client.tool("batch_pending", modern=False)
    check("tools run without _meta", not is_error and "items" in json.loads(text[0]))
    check("discover is modern only here", client.request("server/discover", modern=False)["error"]["code"] == -32601)

    # The spec lets one process serve both eras at once.
    check("the same process serves modern requests", "supportedVersions" in client.request("server/discover")["result"])
    client.close()

    client = Client(folder)
    newer = client.request("initialize", {"protocolVersion": "2031-01-01", "capabilities": {}}, modern=False)["result"]
    check("an unknown version gets the newest handshake", newer["protocolVersion"] == LEGACY)
    client.close()

    client = Client(folder)
    older = client.request("initialize", {"protocolVersion": "2024-11-05", "capabilities": {}}, modern=False)["result"]
    check("an old version is kept", older["protocolVersion"] == "2024-11-05")
    client.close()


def test_a_whole_review(folder):
    client = Client(folder)

    # A first session on an empty folder: nothing to show, and nothing written.
    is_error, text = client.tool("profile_read")
    check("a first read works", not is_error)
    check("a first read says there is no profile", text[0].startswith("No profile yet"))
    status = json.loads(text[1])
    check("nothing is pending yet", status["pending"] == 0 and status["judged"] == 0)
    check("no flaws yet", status["flaw_counts"] == {})
    check("no tables any more", "topics" not in status and "abilities" not in status)
    check("the batch size is stated", status["batch_size"] == 10)
    check("an empty batch is fine", client.tool_json("batch_pending") == {"total": 0, "batch_size": 10, "items": []})
    check("reading writes nothing", os.listdir(folder) == [])

    # Practice happens while the server stays running, as it would beside the app.
    code = '#include <vector>\n// café 日本語 "quoted" \\back\\slash\n\tint solve();\n'
    graded(folder, "lc207", "y", "2026-11-02T21:30", code)
    graded(folder, "lc1", "b", "2026-11-03T21:30")
    graded(folder, "lc128", "r", "2026-11-04T21:30", "int a;\n")
    state = digest(f"{folder}/state.json")

    pending = client.tool_json("batch_pending")
    check("the running server sees new grades", pending["total"] == 3)
    first, second, third = pending["items"]
    check("oldest comes first", first["problem"] == "lc207")
    check("the title is there", first["title"] == "Course Schedule")
    check("the link is there", first["url"].endswith("/course-schedule/"))
    check("the grade is there", first["grade"] == "y" and first["first_try"])
    check("code arrives byte for byte, under its paste line", first["attempts"][0]["code"] == "// ── Paste 1 at 21:30 ──\n" + code, first["attempts"])
    check("code names its day's file", first["attempts"][0]["file"] == "lc207/2026-11-02.cpp")
    check("no code means an empty list", second["attempts"] == [])
    check("finer topics come along when a problem has them", third["resources_topics"] == ["arrays-hashing/set-lookup"])

    # The closed lists.
    lists = client.tool_json("topics_list")
    graphs = next(p for p in lists["patterns"] if p["slug"] == "graph-traversal")
    check("topics come under their pattern", "graph-traversal/multi-source-bfs" in [t["slug"] for t in graphs["topics"]])
    check("each topic says what points to it", all(t["signal"] for t in graphs["topics"]))
    check("flaws are listed", [f["slug"] for f in lists["flaws"]][:2] == ["off-by-one", "wrong-state-update"])
    check("each flaw names its deck", all(f["drilled_by"] for f in lists["flaws"]))
    check("the thinking labels are listed", lists["thinking"]["key_step"] == ["own", "hint", "missed"])

    # Judging.
    log = f"{folder}/profile/judged-attempts.jsonl"
    judged = {"problem": "lc207", "date": "2026-11-02", "model_grade": "r", "recognised": "partial", "key_step": "hint",
              "topics": ["topo-sort"], "flaws": ["wrong-state-update"],
              "note": "Marks visited at the wrong moment.", "by": "test"}
    check("a judgement says what is left", client.tool_json("judgement_write", **judged) == {"saved": True, "remaining": 2})
    line = read_lines(log)[0]
    check("the log holds the model's grade", line["model_grade"] == "r")
    check("the log keeps your grade beside it", line["your_grade"] == "y")
    check("the log has the time", line["ts"] == NOW)
    check("the log has the title", line["title"] == "Course Schedule")
    check("the log has the thinking", line["recognised"] == "partial" and line["key_step"] == "hint")
    check("the log has the topics and flaws", line["topics"] == ["topo-sort"] and line["flaws"] == ["wrong-state-update"])

    client.refused("judgement_write", "already judged", **judged)
    client.refused("judgement_write", "no grade", problem="lc207", date="2026-11-09")
    client.refused("judgement_write", "is not a topic", **{**judged, "problem": "lc128", "date": "2026-11-04", "topics": ["visited-timing"]})
    client.refused("judgement_write", "is not a flaw", **{**judged, "problem": "lc128", "date": "2026-11-04", "flaws": ["visited-timing"]})
    client.refused("judgement_write", "model_grade", problem="lc128", date="2026-11-04", recognised="no", key_step="missed", note="x")
    client.refused("judgement_write", "key_step", problem="lc128", date="2026-11-04", model_grade="r", recognised="no", note="x")
    client.refused("judgement_write", "traced", problem="lc128", date="2026-11-04", model_grade="b", recognised="yes",
                   key_step="own", note="Fine.")
    client.refused("judgement_drop", "needs a judgement", problem="lc128", date="2026-11-04", reason="x")
    check("refusals leave the log alone", len(read_lines(log)) == 1)

    check("a problem with no code can be dropped",
          client.tool_json("judgement_drop", problem="lc1", date="2026-11-03", reason="No code; the grade says it.", by="test")
          == {"dropped": True, "remaining": 1})
    check("that line says so", read_lines(log)[1]["dropped"] is True and read_lines(log)[1]["had_code"] is False)
    check("judged problems leave the batch", [i["problem"] for i in client.tool_json("batch_pending")["items"]] == ["lc128"])

    # Search.
    found = client.tool_json("judgements_search", label="wrong-state-update")
    check("search finds a flaw", found["total"] == 1 and found["rows"][0]["problem"] == "lc207")
    check("search counts first tries", found["first_tries"] == 1 and found["first_try_passes"] == 0)
    check("search finds words", client.tool_json("judgements_search", text="WRONG MOMENT")["total"] == 1)
    check("search leaves dropped lines out", client.tool_json("judgements_search", problem="lc1")["total"] == 0)
    check("search finds a topic", client.tool_json("judgements_search", label="topo-sort")["total"] == 1)

    # Committing writes the profile and nothing else.
    profile_file = f"{folder}/profile/profile.md"
    client.refused("batch_commit", "2048", profile="x" * 2049)
    client.refused("batch_commit", "empty", profile="  ")
    check("a refused commit writes nothing", not os.path.exists(profile_file))
    check("a good commit says so", client.tool_json("batch_commit", profile="# Me\nSlow on graphs.") == {"saved": True})
    check("the profile is on disk", open(profile_file, encoding="utf-8").read() == "# Me\nSlow on graphs.")
    check("no table is written", not os.path.exists(f"{folder}/profile/labels.json"))
    client.tool_json("batch_commit", profile="# Second")
    check("the old profile is kept once", open(profile_file + ".bak", encoding="utf-8").read() == "# Me\nSlow on graphs.")
    client.tool_json("batch_commit", profile="# Me\nSlow on graphs.")
    client.close()

    # A fresh process is a new session: all it has is what the last one left.
    client = Client(folder)
    is_error, text = client.tool("profile_read")
    check("a new session reads the profile", text[0] == "# Me\nSlow on graphs.", text)
    status = json.loads(text[1])
    check("a new session sees what is left", status["pending"] == 1 and status["judged"] == 1 and status["dropped"] == 1)
    check("a new session sees which flaws came up", status["flaw_counts"] == {"wrong-state-update": 1})
    check("a new session sees the thinking", status["thinking_counts"]["key_step"] == {"hint": 1})
    check("the new session can recall the old one", client.tool_json("judgements_search", label="wrong-state-update")["total"] == 1)
    client.close()

    check("no tool ever touched state.json", digest(f"{folder}/state.json") == state)


def test_judgements_from_before_stay_as_they_were(folder):
    # Someone who used the app before 2026-11-09: an old judgement log and an old table.
    graded(folder, "lc1", "r", "2026-11-02T21:30")
    graded(folder, "lc2", "b", "2026-11-03T21:30", "int a;\n")
    graded(folder, "lc3", "y", "2026-11-04T21:30", "int b;\n")
    os.makedirs(f"{folder}/profile", exist_ok=True)
    old_log = f"{folder}/profile/judgements.jsonl"
    old_table = f"{folder}/profile/labels.json"
    with open(old_log, "w", encoding="utf-8") as file:
        file.write(json.dumps({"problem": "lc1", "date": "2026-11-02", "your_grade": "r", "model_grade": "r",
                               "patterns": ["two-pointers"], "flaws": ["moved-wrong-pointer"], "note": "Old note.",
                               "had_code": False}) + "\n")
        file.write(json.dumps({"problem": "lc2", "date": "2026-11-03", "your_grade": "b", "model_grade": "y",
                               "recognised": "yes", "topics": ["two-pointers"], "abilities": ["edge"],
                               "note": "Missed the empty input.", "had_code": True}) + "\n")
    with open(old_table, "w", encoding="utf-8") as file:
        file.write('{"version":2,"topics":{"unknown":{"p":1}},"abilities":{}}\n')
    before = (digest(old_log), digest(old_table))

    client = Client(folder)
    status = json.loads(client.tool("profile_read")[1][1])
    check("old attempts wait for the new shape", status["pending"] == 3 and status["pending_with_earlier_judgement"] == 2)
    items = client.tool_json("batch_pending")["items"]
    check("each shows what was said then", items[0]["earlier_judgement"]["note"] == "Old note."
          and items[1]["earlier_judgement"]["abilities"] == ["edge"], items[:2])
    check("a new attempt has none", "earlier_judgement" not in items[2])
    check("old lines still answer a search", client.tool_json("judgements_search", label="moved-wrong-pointer")["total"] == 1)

    # The coach decides: drop the one with nothing to add, judge the other again.
    client.tool_json("judgement_drop", problem="lc1", date="2026-11-02", reason="No code; the old note says it.")
    client.tool_json("judgement_write", problem="lc2", date="2026-11-03", model_grade="y", recognised="yes", key_step="own",
                     topics=["two-pointers"], flaws=["missed-edge-input"], note="Empty input returns garbage.")
    check("the carried-over lines don't count twice",
          client.tool_json("judgements_search", label="two-pointers")["total"] == 1)
    check("one attempt is left", client.tool_json("batch_pending")["total"] == 1)
    client.close()
    check("the old files are never written", (digest(old_log), digest(old_table)) == before)


def test_big_and_broken_files(folder):
    client = Client(folder)

    # A long paste keeps its start and its end, cut between two characters in the middle.
    graded(folder, "lc2", "r", "2026-11-05T21:30", "非" * 5000)
    item = client.tool_json("batch_pending")["items"][0]
    shown = item["attempts"][0]["code"]
    marker = re.search(r"\n// … (\d+) bytes cut here …\n", shown)
    kept = shown.replace(marker.group(0), "") if marker else shown
    check("a long attempt is cut", item["code_cut_short"] and marker is not None, shown[:80])
    check("the cut keeps the paste line", shown.startswith("// ── Paste 1 at 21:30 ──\n非"))
    check("the cut keeps the end", shown.endswith("非\n"))
    check("the cut is near the limit", 4090 <= len(kept.encode()) <= 4096, len(kept.encode()))
    check("the cut says how much", int(marker.group(1)) + len(kept.encode()) == len(("// ── Paste 1 at 21:30 ──\n" + "非" * 5000 + "\n").encode()))

    # A day's pastes share one file: the first and the last win, the middle goes.
    cli(folder, "attempt", "lc3", "--now", "2026-11-05T21:30", code="a" * 3000)
    cli(folder, "attempt", "lc3", "--now", "2026-11-05T21:31", code="b" * 3000)
    cli(folder, "attempt", "lc3", "--now", "2026-11-05T21:32", code="c" * 3000)
    cli(folder, "grade", "lc3", "y", "--now", "2026-11-05T21:40")
    item = client.tool_json("batch_pending")["items"][1]
    day = item["attempts"]
    check("one file for the day", [a["file"] for a in day] == ["lc3/2026-11-05.cpp"], day)
    check("the dry paste comes first", day[0]["code"].startswith("// ── Paste 1 at 21:30 ──\naaa"))
    check("the last paste ends it", day[0]["code"].endswith("ccc\n") and "b" * 100 not in day[0]["code"])
    check("the cut is flagged", item["code_cut_short"])

    # Across days too: a dry paste one day and the fix the next both go with the next grade.
    cli(folder, "attempt", "lc14", "--now", "2026-11-04T21:30", code="d" * 3000)
    cli(folder, "attempt", "lc14", "--now", "2026-11-05T09:00", code="e" * 3000)
    cli(folder, "grade", "lc14", "b", "--now", "2026-11-05T09:30")
    item = next(i for i in client.tool_json("batch_pending")["items"] if i["problem"] == "lc14")
    files = item["attempts"]
    check("both days arrive, oldest first", [a["file"] for a in files] == ["lc14/2026-11-04.cpp", "lc14/2026-11-05.cpp"], files)
    check("the first day keeps its start", files[0]["code"].startswith("// ── Paste 1 at 21:30 ──\nddd") and "cut here" in files[0]["code"])
    check("the last day keeps its end", files[1]["code"].endswith("eee\n") and "cut here" in files[1]["code"])

    # A full batch of real-looking code stays inline in Claude Code.
    line = "    if (nums[i] > best) best = nums[i];  // keep the largest\n"
    for number in range(4, 14):
        graded(folder, f"lc{number}", "y", "2026-11-06T21:30", line * 200)
    is_error, text = client.tool("batch_pending")
    check("a full batch stays under Claude Code's limit", len(text[0]) < 50000, len(text[0]))

    # Things left in the folder by hand, or by a crash, cost nothing.
    os.makedirs(f"{folder}/profile/attempts/lc2", exist_ok=True)
    open(f"{folder}/profile/attempts/.DS_Store", "w").close()
    open(f"{folder}/profile/attempts/lc2/notes.txt", "w").close()
    with open(f"{folder}/profile/judged-attempts.jsonl", "w", encoding="utf-8") as log:
        log.write('{"problem":"lc2","date":"2026-11-05","note":"fine"}\n{"problem":"lc3","da')
    pending = client.tool_json("batch_pending")
    check("stray files are ignored", pending["items"][0]["problem"] == "lc3")
    check("a torn line costs only itself", client.tool_json("judgements_search")["total"] == 1)

    with open(f"{folder}/profile/judgements.jsonl", "w", encoding="utf-8") as old:
        old.write("{ this is not json")
    is_error, text = client.tool("profile_read")
    check("a broken old log reads as empty", not is_error and json.loads(text[1])["pending_with_earlier_judgement"] == 0)

    with open(f"{folder}/state.json", "w", encoding="utf-8") as state:
        state.write("{ this is not json")
    is_error, text = client.tool("batch_pending")
    check("a broken progress file is reported", is_error and "progress" in text[0], text)
    check("the server survives it", client.request("tools/list")["result"]["tools"])
    client.close()


def run(folder, now, *args):
    result = subprocess.run(
        [BINARY, "--state", f"{folder}/state.json", "--resources", RESOURCES, "--now", now, *args],
        capture_output=True, text=True, encoding="utf-8",
    )
    check(f"interviewprep {args[0]} works", result.returncode == 0, result.stderr)
    return result.stdout


def test_the_coach_can_only_add_checks(folder):
    # A clean first try and a clean check: gone.
    graded(folder, "lc1", "b", "2026-11-02T09:00")
    graded(folder, "lc1", "b", "2026-11-15T09:00", "int c;\n")
    state = digest(f"{folder}/state.json")
    check("first the problem is gone", run(folder, NOW, "show", "lc1").splitlines()[1].startswith("Retired"))
    first = run(folder, NOW, "recommend", "code", "4")
    check("new problems come from patterns not tried yet", first.count("New pattern:") == 4, first)

    # The coach finds a bug on that last check: the app reads its log and keeps the problem going.
    client = Client(folder)
    client.tool_json("judgement_write", problem="lc1", date="2026-11-02")
    client.tool_json("judgement_write", problem="lc1", date="2026-11-15", model_grade="y", recognised="yes",
                     key_step="own", flaws=["off-by-one"], note="Loop stops one short.")
    client.close()
    check("the coach's stricter grade keeps it", run(folder, NOW, "show", "lc1").splitlines()[1].startswith("Yellow"))
    check("explain says why", "the coach graded it y" in run(folder, NOW, "explain", "lc1"))
    check("the slip shows as a flaw", "Off by one, or wrong bounds" in run(folder, NOW, "profile"))

    # An unreadable log changes nothing and is never a crash.
    with open(f"{folder}/profile/judged-attempts.jsonl", "w", encoding="utf-8") as file:
        file.write("not json")
    check("an unreadable log reads as none", run(folder, NOW, "show", "lc1").splitlines()[1].startswith("Retired"))
    check("the coach never touched progress", digest(f"{folder}/state.json") == state)


def test_a_failed_save_keeps_the_old_file(folder):
    graded(folder, "lc1", "b", "2026-11-02T09:00")
    path = f"{folder}/state.json"
    before = digest(path)
    os.chmod(folder, 0o500)
    try:
        result = subprocess.run(
            [BINARY, "--state", path, "--resources", RESOURCES, "grade", "lc2", "r", "--now", "2026-11-03T09:00"],
            capture_output=True, text=True, encoding="utf-8",
        )
    finally:
        os.chmod(folder, 0o700)
    check("a save that can't happen says so", result.returncode != 0 and "Couldn't save" in result.stderr, result.stderr)
    check("the old progress survives", digest(path) == before)


def test_a_damaged_file_is_left_alone(folder):
    path = f"{folder}/state.json"
    with open(path, "w", encoding="utf-8") as file:
        file.write('{"events":[{"id":"lc1"')
    before = digest(path)
    result = subprocess.run(
        [BINARY, "--state", path, "--resources", RESOURCES, "today", "--now", NOW],
        capture_output=True, text=True, encoding="utf-8",
    )
    check("a damaged file stops the command", result.returncode != 0 and "Can't load progress" in result.stderr, result.stderr)
    check("a damaged file isn't saved over", digest(path) == before)


def main():
    for test in (test_modern_protocol, test_legacy_protocol, test_a_whole_review,
                 test_judgements_from_before_stay_as_they_were, test_big_and_broken_files,
                 test_the_coach_can_only_add_checks, test_a_failed_save_keeps_the_old_file,
                 test_a_damaged_file_is_left_alone):
        with tempfile.TemporaryDirectory() as folder:
            try:
                test(folder)
            except Exception as error:
                check(f"{test.__name__} finished", False, repr(error))
    print(f"{checks} protocol checks, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
