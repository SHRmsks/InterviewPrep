/*
interviewprep — the core, from the terminal.

Reads and writes the same progress file as the Mac app, so anything you do
here shows up there. Run `interviewprep help` for the command list.
*/

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "../core/engine.hpp"
#include "files.hpp"
#include "mcp.hpp"

using namespace std;
using namespace interviewprep;

static const char* kHelp = R"(interviewprep: spaced repetition and new-face practice for coding interviews

Daily
  today                         what is queued today
  more [code|cards] [n]         pull more into today's queue
  grade <id> <b|y|r> [--secs 900]
                                record a result, e.g.  grade lc128 r --secs 1500
  attempt <id> [file]           save your code before you run it, from a file
                                or piped in, e.g.  attempt lc207 < sol.cpp
  undo [id]                     remove the last grade
  mock                          two new faces for a 45 minute mock

Look things up
  show <id>                     one card: where it stands and its history
  explain <id>                  replay how a card got to where it is
  find <text>                   search titles or numbers
  similar <id> [n]              nearest questions by label
  recommend [code|cards] [n]    what would be picked next (changes nothing)
  profile                       your patterns, and the slips your coach sees most

Coach
  mcp                           serve your attempts to a model over MCP

Rules
  rules                         when each check comes due, and the click tables
  simulate r,b,b [card]         walk a problem (or a card) through clicks, no data touched

Setup
  set <key> <value>             codeCap codeNew codeReviews cardCap cardNew clock company premium
                                interviewDate, e.g.  set interviewDate 2026-12-15  (or none)
  import <file>                 load an old tracker export
  export                        print your progress as JSON

Options
  --state <file>                progress file (default: the Mac app's)
  --resources <file>            problems, cards and drills (default: data/resources.json)
  --now 2026-11-06T21:30        pretend it is this time

Edit the rules in core/config.hpp, then: make test
)";

// The app was called Coldstart until November 2026. Its folder moves over once,
// whole, the first time the app or this tool looks for it. A run with --state
// never gets here, so tests and scratch runs can't move it.
static string defaultStatePath() {
  const char* home = getenv("HOME");
  const string support = string(home ? home : ".") + "/Library/Application Support/";
  if (!exists(support + "InterviewPrep")) rename((support + "Coldstart").c_str(), (support + "InterviewPrep").c_str());
  return support + "InterviewPrep/state.json";
}

static string defaultResourcesPath() {
  if (const char* env = getenv("INTERVIEWPREP_RESOURCES")) return env;
  for (const char* candidate : {"data/resources.json", "resources.json", "/Applications/InterviewPrep.app/Contents/Resources/resources.json"}) {
    if (exists(candidate)) return candidate;
  }
  return "data/resources.json";
}

static const char* dot(const string& track) {
  if (track == "red") return "\033[31m●\033[0m";
  if (track == "yellow") return "\033[33m●\033[0m";
  if (track == "blue") return "\033[34m●\033[0m";
  if (track == "retired") return "\033[34m◉\033[0m";
  return "○";
}

static string shortened(string s, size_t width) {
  if (s.size() > width) s = s.substr(0, width - 3) + "...";
  return s;
}

static void printItem(const Json& v, bool showWhy) {
  const string done = v["doneToday"].asStr();
  printf("  %s %-12s %-54s", done.empty() ? dot(v["track"].asStr()) : "✓", v["id"].asStr().c_str(),
              shortened(v["title"].asStr(), 54).c_str());
  if (v.has("level")) printf(" %-6s", v["level"].asStr().c_str());
  if (showWhy) printf("  %s", v["why"].asStr().c_str());
  printf("\n");
}

static void printLane(const char* name, const Json& lane) {
  const Json& counts = lane["counts"];
  printf("%s  %d of %d done", name, lane["done"].asInt(), lane["total"].asInt());
  if (lane["overflow"].asInt() > 0) printf(", %d rolled to tomorrow", lane["overflow"].asInt());
  printf("   (red %d, yellow %d, blue %d, retired %d)\n", counts["red"].asInt(), counts["yellow"].asInt(),
              counts["blue"].asInt(), counts["retired"].asInt());
  for (const Json& item : lane["items"].items()) printItem(item, true);
  printf("\n");
}

static void printSteps(const Json& steps) {
  for (const Json& s : steps.items()) {
    if (s.has("date")) printf("  %s ", s["date"].asStr().c_str());
    printf("  day %-4d click %s  %-9s  %-22s → %s", s["day"].asInt(), s["click"].asStr().c_str(),
                s["move"].asStr().c_str(), s["from"].asStr().c_str(), s["to"].asStr().c_str());
    if (s.has("gap")) printf("  (next in %d)", s["gap"].asInt());
    if (s.has("coach")) printf("  (the coach graded it %s, so it stays)", s["coach"].asStr().c_str());
    printf("\n");
  }
}

static void printRate(const Json& evidence) {
  if (evidence.has("rate")) printf(" %4d%%", static_cast<int>(evidence["rate"].asNum() * 100 + 0.5));
  else printf("     -");
}

int main(int argc, char** argv) {
  vector<string> args(argv + 1, argv + argc);
  string statePath;  // the default is looked up below, only if --state is missing
  string resourcesPath = defaultResourcesPath();
  string now = nowString();
  string fixedNow;
  int secs = 0;

  vector<string> positional;
  for (size_t i = 0; i < args.size(); ++i) {
    const bool hasValue = i + 1 < args.size();
    if (args[i] == "--state" && hasValue) statePath = args[++i];
    else if (args[i] == "--resources" && hasValue) resourcesPath = args[++i];
    else if (args[i] == "--now" && hasValue) now = fixedNow = args[++i];
    else if (args[i] == "--secs" && hasValue) secs = atoi(args[++i].c_str());
    else positional.push_back(args[i]);
  }

  const string command = positional.empty() ? "today" : positional[0];
  auto arg = [&](size_t i) { return i < positional.size() ? positional[i] : string(); };
  if (command == "help" || command == "--help" || command == "-h") {
    fputs(kHelp, stdout);
    return 0;
  }
  if (statePath.empty()) statePath = defaultStatePath();

  bool ok = false;
  const string resourcesText = readFile(resourcesPath, &ok);
  if (!ok) {
    fprintf(stderr, "Can't read the resources at %s. Run make resources, or pass --resources <file>.\n", resourcesPath.c_str());
    return 1;
  }
  Engine engine;
  string error;
  if (engine.loadResources(resourcesText, &error) < 0) {
    fprintf(stderr, "Can't load the resources: %s\n", error.c_str());
    return 1;
  }
  if (exists(statePath) && !engine.loadState(readFile(statePath), &error)) {
    fprintf(stderr, "Can't load progress from %s: %s\n", statePath.c_str(), error.c_str());
    return 1;
  }
  if (command == "mcp") return runMcpServer(engine, statePath, fixedNow);

  // The coach's judgements, so its labels count and its stricter grades can keep a check going.
  const string folder = directoryOf(statePath) + "/";
  engine.call(Json::object().set("cmd", "judgements").set("text", readFile(folder + JUDGEMENTS_FILE))
                  .set("old", readFile(folder + OLD_JUDGEMENTS_FILE)).dump());

  auto run = [&](Json request) {
    request.set("now", now);
    Json response = Json::parse(engine.call(request.dump()));
    if (!response["ok"].asBool()) {
      fprintf(stderr, "%s\n", response["error"].asStr("Command failed.").c_str());
      exit(1);
    }
    if (response["dirty"].asBool()) {
      makeDirectories(statePath);
      const string temporary = statePath + ".tmp";
      ofstream out(temporary, ios::binary);
      out << engine.saveState();
      out.close();
      /*
      Only a complete file may replace the old one: a full disk must not cost
      you your history.
      */
      if (!out.good() || rename(temporary.c_str(), statePath.c_str()) != 0) {
        fprintf(stderr, "Couldn't save your progress to %s: %s\n", statePath.c_str(), strerror(errno));
        exit(1);
      }
    }
    return response;
  };
  auto cmd = [](const char* name) { return Json::object().set("cmd", name); };
  auto laneArg = [&](size_t i) {
    const string lane = arg(i);
    return lane == "cards" ? "cards" : "code";
  };

  if (command == "today") {
    Json r = run(cmd("today"));
    printf("%s, study day %d\n\n", r["date"].asStr().c_str(), r["day"].asInt());
    printLane("Problems", r["code"]);
    printLane("Cards", r["cards"]);
  } else if (command == "more") {
    Json request = cmd("more").set("lane", laneArg(1));
    if (!arg(2).empty()) request.set("n", atoi(arg(2).c_str()));
    Json r = run(request);
    printLane("Problems", r["code"]);
    printLane("Cards", r["cards"]);
  } else if (command == "grade") {
    if (arg(1).empty() || arg(2).empty()) { fputs("Usage: interviewprep grade <id> <b|y|r>\n", stderr); return 1; }
    Json request = cmd("grade").set("id", arg(1)).set("grade", arg(2));
    if (secs) request.set("secs", secs);
    Json r = run(request);
    printf("%s → %s. %s.\n", r["before"].asStr().c_str(), r["after"].asStr().c_str(), r["next"].asStr().c_str());
  } else if (command == "attempt") {
    if (arg(1).empty() || (arg(2).empty() && isatty(STDIN_FILENO))) {
      fputs("Usage: interviewprep attempt <id> [file], or pipe it in: interviewprep attempt lc207 < sol.cpp\n", stderr);
      return 1;
    }
    string code;
    if (arg(2).empty()) {
      stringstream buffer;
      buffer << cin.rdbuf();
      code = buffer.str();
    } else {
      bool readOk = false;
      code = readFile(arg(2), &readOk);
      if (!readOk) { fprintf(stderr, "Can't read %s.\n", arg(2).c_str()); return 1; }
    }
    // The day's file as it stands, so the core can number this paste. The paste is appended.
    Json request = cmd("attempt").set("id", arg(1)).set("code", code);
    Json r = run(request);
    const string path = directoryOf(statePath) + "/" + r["stem"].asStr() + r["ext"].asStr();
    if (exists(path)) {
      bool readOk = false;
      const string existing = readFile(path, &readOk);
      if (!readOk) { fprintf(stderr, "Can't read %s, so the paste wasn't saved.\n", path.c_str()); return 1; }
      r = run(request.set("existing", existing));
    }
    if (!r["block"].asStr().empty() && !appendText(path, r["block"].asStr())) {
      fprintf(stderr, "Couldn't save the attempt in %s: %s\n", path.c_str(), strerror(errno));
      return 1;
    }
    printf("%s\n%s\n", r["note"].asStr().c_str(), path.c_str());
  } else if (command == "undo") {
    Json request = cmd("undo");
    if (!arg(1).empty()) request.set("id", arg(1));
    printf("Removed the last grade on %s.\n", run(request)["id"].asStr().c_str());
  } else if (command == "mock") {
    Json r = run(cmd("mock"));
    printf("%d minutes, labels hidden:\n", r["minutes"].asInt());
    for (const Json& item : r["items"].items()) printf("  %s\n", item["url"].asStr().c_str());
  } else if (command == "show") {
    const Json c = run(cmd("card").set("id", arg(1)))["card"];
    printf("%s  %s\n%s. %s.\n", c["id"].asStr().c_str(), c["title"].asStr().c_str(),
                c["position"].asStr().c_str(), c["next"].asStr().c_str());
    if (c.has("url")) printf("%s\n", c["url"].asStr().c_str());
    string patterns;
    for (const Json& p : c["patterns"].items()) patterns += (patterns.empty() ? "" : ", ") + p["name"].asStr();
    if (!patterns.empty()) printf("Patterns: %s\n", patterns.c_str());
    if (!c["idea"].asStr().empty()) printf("Idea: %s\nPitfall: %s\n", c["idea"].asStr().c_str(), c["pitfall"].asStr().c_str());
    if (!c["answer"].asStr().empty()) printf("Answer: %s\n", c["answer"].asStr().c_str());
    for (const Json& h : c["history"].items()) printf("  %s  %s\n", h["date"].asStr().c_str(), h["grade"].asStr().c_str());
  } else if (command == "explain") {
    Json r = run(cmd("explain").set("id", arg(1)));
    if (r["steps"].size() == 0) puts("No grades yet.");
    printSteps(r["steps"]);
  } else if (command == "find") {
    Json r = run(cmd("library").set("q", arg(1)).set("limit", 25));
    printf("%d found\n", r["total"].asInt());
    for (const Json& row : r["rows"].items()) printItem(row, false);
  } else if (command == "similar") {
    Json request = cmd("similar").set("id", arg(1));
    if (!arg(2).empty()) request.set("n", atoi(arg(2).c_str()));
    const Json r = run(request);
    for (const Json& row : r["rows"].items()) {
      printf("  %.2f ", row["similarity"].asNum());
      printItem(row, false);
    }
  } else if (command == "recommend") {
    Json request = cmd("recommend").set("lane", laneArg(1));
    if (!arg(2).empty()) request.set("n", atoi(arg(2).c_str()));
    const Json r = run(request);
    for (const Json& row : r["rows"].items()) printItem(row, true);
  } else if (command == "profile") {
    Json r = run(cmd("insights"));
    for (const Json& note : r["notes"].items()) printf("- %s\n", note["text"].asStr().c_str());
    // The two ratings: `idea` sets how hard a pattern's next problem is, `code` how often it comes up.
    printf("\n%-34s %5s %5s %5s %5s %5s %6s %6s %s\n", "Pattern", "cold", "rev", "drill", "card", "mast", "idea", "code", "seen");
    for (const Json& p : r["patterns"].items()) {
      printf("%-34s", p["name"].asStr().c_str());
      printRate(p["cold"]); printRate(p["review"]); printRate(p["drill"]); printRate(p["flash"]);
      printf(" %4d%% %6d %6d %d/%d%s\n", static_cast<int>(p["mastery"].asNum() * 100 + 0.5), p["rating"].asInt(),
             p["codeRating"].asInt(), p["seen"].asInt(), p["total"].asInt(),
                  p["memorizing"].asBool() ? "  memorizing" : "");
    }
    puts("\nWhat your judged code shows most");
    if (r["needs"].size() == 0) puts("  Nothing yet: the coach fills this in after it reviews code you saved.");
    for (const Json& a : r["needs"].items()) {
      printf("  %-56s %4d%%\n", a["label"].asStr().c_str(), static_cast<int>(a["share"].asNum() * 100 + 0.5));
    }
  } else if (command == "rules") {
    Json r = run(cmd("rules"));
    // "1-4, 5-10" days, or "1, 3, 7" when no gap has extra days.
    auto list = [](const Json& checks) {
      string s;
      for (const Json& gap : checks.items()) {
        const int base = gap["base"].asInt(), spread = gap["spread"].asInt();
        s += (s.empty() ? "" : ", ") + to_string(base) + (spread > 0 ? "-" + to_string(base + spread) : "");
      }
      return s;
    };
    for (const char* kind : {"problems", "cards"}) {
      const Json& schedule = r[kind];
      printf("%s%s\n", kind[0] == 'p' ? "Problems" : "Cards",
                  schedule["blueAfterLast"].asBool() ? ": past the last Red or Yellow check come Blue's" : ": gone after the last check");
      for (const char* track : {"red", "yellow", "blue"}) {
        printf("  %-7s checks after %s days\n", track, list(schedule["checks"][track]).c_str());
      }
      printf("  %-10s %-10s %-10s %-10s\n", "", "click B", "click Y", "click R");
      for (const auto& row : schedule["rules"].members()) {
        printf("  %-10s %-10s %-10s %-10s\n", row.first.c_str(), row.second["b"].asStr().c_str(),
                    row.second["y"].asStr().c_str(), row.second["r"].asStr().c_str());
      }
      puts("");
    }
    printf("Clock: %s days. A day starts at %d:00.\n", r["clock"].asStr().c_str(), r["rollover"].asInt());
    printf("Daily queue: %d problems (%d new, at most %d checks), %d cards (%d new).\n", r["codeCap"].asInt(),
                r["codeNew"].asInt(), r["codeReviews"].asInt(), r["cardCap"].asInt(), r["cardNew"].asInt());
  } else if (command == "simulate") {
    Json clicks = Json::array();
    stringstream stream(arg(1));
    string click;
    while (getline(stream, click, ',')) clicks.push(click);
    printSteps(run(cmd("simulate").set("clicks", clicks).set("kind", arg(2) == "card" ? "card" : "problem"))["steps"]);
  } else if (command == "set") {
    const string key = arg(1), value = arg(2);
    Json fields = Json::object();
    if (key == "clock" || key == "company") fields.set(key, value);
    else if (key == "interviewDate") fields.set(key, value == "none" ? "" : value);
    else if (key == "premium") fields.set(key, value == "true" || value == "1" || value == "on");
    else fields.set(key, atoi(value.c_str()));
    Json r = run(cmd("settings").set("set", fields));
    puts(r["settings"].dump().c_str());
  } else if (command == "import") {
    bool readOk = false;
    Json problems = Json::parse(readFile(arg(1), &readOk));
    if (!readOk || !problems.isArray()) { fprintf(stderr, "Can't read %s as a tracker export.\n", arg(1).c_str()); return 1; }
    Json r = run(cmd("import").set("problems", problems));
    printf("Added %d grades, skipped %d.\n", r["added"].asInt(), r["skipped"].asInt());
  } else if (command == "export") {
    puts(engine.saveState().c_str());
  } else {
    fprintf(stderr, "Unknown command: %s. Run interviewprep help.\n", command.c_str());
    return 1;
  }
  return 0;
}
