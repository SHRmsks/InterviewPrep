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
static std::string defaultStatePath() {
  const char* home = std::getenv("HOME");
  const std::string support = std::string(home ? home : ".") + "/Library/Application Support/";
  if (!exists(support + "InterviewPrep")) std::rename((support + "Coldstart").c_str(), (support + "InterviewPrep").c_str());
  return support + "InterviewPrep/state.json";
}

static std::string defaultResourcesPath() {
  if (const char* env = std::getenv("INTERVIEWPREP_RESOURCES")) return env;
  for (const char* candidate : {"data/resources.json", "resources.json", "/Applications/InterviewPrep.app/Contents/Resources/resources.json"}) {
    if (exists(candidate)) return candidate;
  }
  return "data/resources.json";
}

static const char* dot(const std::string& track) {
  if (track == "red") return "\033[31m●\033[0m";
  if (track == "yellow") return "\033[33m●\033[0m";
  if (track == "blue") return "\033[34m●\033[0m";
  if (track == "retired") return "\033[34m◉\033[0m";
  return "○";
}

static std::string shortened(std::string s, size_t width) {
  if (s.size() > width) s = s.substr(0, width - 3) + "...";
  return s;
}

static void printItem(const Json& v, bool showWhy) {
  const std::string done = v["doneToday"].asStr();
  std::printf("  %s %-12s %-54s", done.empty() ? dot(v["track"].asStr()) : "✓", v["id"].asStr().c_str(),
              shortened(v["title"].asStr(), 54).c_str());
  if (v.has("level")) std::printf(" %-6s", v["level"].asStr().c_str());
  if (showWhy) std::printf("  %s", v["why"].asStr().c_str());
  std::printf("\n");
}

static void printLane(const char* name, const Json& lane) {
  const Json& counts = lane["counts"];
  std::printf("%s  %d of %d done", name, lane["done"].asInt(), lane["total"].asInt());
  if (lane["overflow"].asInt() > 0) std::printf(", %d rolled to tomorrow", lane["overflow"].asInt());
  std::printf("   (red %d, yellow %d, blue %d, retired %d)\n", counts["red"].asInt(), counts["yellow"].asInt(),
              counts["blue"].asInt(), counts["retired"].asInt());
  for (const Json& item : lane["items"].items()) printItem(item, true);
  std::printf("\n");
}

static void printSteps(const Json& steps) {
  for (const Json& s : steps.items()) {
    if (s.has("date")) std::printf("  %s ", s["date"].asStr().c_str());
    std::printf("  day %-4d click %s  %-9s  %-22s → %s", s["day"].asInt(), s["click"].asStr().c_str(),
                s["move"].asStr().c_str(), s["from"].asStr().c_str(), s["to"].asStr().c_str());
    if (s.has("gap")) std::printf("  (next in %d)", s["gap"].asInt());
    if (s.has("coach")) std::printf("  (the coach graded it %s, so it stays)", s["coach"].asStr().c_str());
    std::printf("\n");
  }
}

static void printRate(const Json& evidence) {
  if (evidence.has("rate")) std::printf(" %4d%%", static_cast<int>(evidence["rate"].asNum() * 100 + 0.5));
  else std::printf("     -");
}

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  std::string statePath;  // the default is looked up below, only if --state is missing
  std::string resourcesPath = defaultResourcesPath();
  std::string now = nowString();
  std::string fixedNow;
  int secs = 0;

  std::vector<std::string> positional;
  for (size_t i = 0; i < args.size(); ++i) {
    const bool hasValue = i + 1 < args.size();
    if (args[i] == "--state" && hasValue) statePath = args[++i];
    else if (args[i] == "--resources" && hasValue) resourcesPath = args[++i];
    else if (args[i] == "--now" && hasValue) now = fixedNow = args[++i];
    else if (args[i] == "--secs" && hasValue) secs = std::atoi(args[++i].c_str());
    else positional.push_back(args[i]);
  }

  const std::string command = positional.empty() ? "today" : positional[0];
  auto arg = [&](size_t i) { return i < positional.size() ? positional[i] : std::string(); };
  if (command == "help" || command == "--help" || command == "-h") {
    std::fputs(kHelp, stdout);
    return 0;
  }
  if (statePath.empty()) statePath = defaultStatePath();

  bool ok = false;
  const std::string resourcesText = readFile(resourcesPath, &ok);
  if (!ok) {
    std::fprintf(stderr, "Can't read the resources at %s. Run make resources, or pass --resources <file>.\n", resourcesPath.c_str());
    return 1;
  }
  Engine engine;
  std::string error;
  if (engine.loadResources(resourcesText, &error) < 0) {
    std::fprintf(stderr, "Can't load the resources: %s\n", error.c_str());
    return 1;
  }
  if (exists(statePath) && !engine.loadState(readFile(statePath), &error)) {
    std::fprintf(stderr, "Can't load progress from %s: %s\n", statePath.c_str(), error.c_str());
    return 1;
  }
  if (command == "mcp") return runMcpServer(engine, statePath, fixedNow);

  // The coach's judgements, so its labels count and its stricter grades can keep a check going.
  const std::string folder = directoryOf(statePath) + "/";
  engine.call(Json::object().set("cmd", "judgements").set("text", readFile(folder + JUDGEMENTS_FILE))
                  .set("old", readFile(folder + OLD_JUDGEMENTS_FILE)).dump());

  auto run = [&](Json request) {
    request.set("now", now);
    Json response = Json::parse(engine.call(request.dump()));
    if (!response["ok"].asBool()) {
      std::fprintf(stderr, "%s\n", response["error"].asStr("Command failed.").c_str());
      std::exit(1);
    }
    if (response["dirty"].asBool()) {
      makeDirectories(statePath);
      const std::string temporary = statePath + ".tmp";
      std::ofstream out(temporary, std::ios::binary);
      out << engine.saveState();
      out.close();
      /*
      Only a complete file may replace the old one: a full disk must not cost
      you your history.
      */
      if (!out.good() || std::rename(temporary.c_str(), statePath.c_str()) != 0) {
        std::fprintf(stderr, "Couldn't save your progress to %s: %s\n", statePath.c_str(), std::strerror(errno));
        std::exit(1);
      }
    }
    return response;
  };
  auto cmd = [](const char* name) { return Json::object().set("cmd", name); };
  auto laneArg = [&](size_t i) {
    const std::string lane = arg(i);
    return lane == "cards" ? "cards" : "code";
  };

  if (command == "today") {
    Json r = run(cmd("today"));
    std::printf("%s, study day %d\n\n", r["date"].asStr().c_str(), r["day"].asInt());
    printLane("Problems", r["code"]);
    printLane("Cards", r["cards"]);
  } else if (command == "more") {
    Json request = cmd("more").set("lane", laneArg(1));
    if (!arg(2).empty()) request.set("n", std::atoi(arg(2).c_str()));
    Json r = run(request);
    printLane("Problems", r["code"]);
    printLane("Cards", r["cards"]);
  } else if (command == "grade") {
    if (arg(1).empty() || arg(2).empty()) { std::fputs("Usage: interviewprep grade <id> <b|y|r>\n", stderr); return 1; }
    Json request = cmd("grade").set("id", arg(1)).set("grade", arg(2));
    if (secs) request.set("secs", secs);
    Json r = run(request);
    std::printf("%s → %s. %s.\n", r["before"].asStr().c_str(), r["after"].asStr().c_str(), r["next"].asStr().c_str());
  } else if (command == "attempt") {
    if (arg(1).empty() || (arg(2).empty() && isatty(STDIN_FILENO))) {
      std::fputs("Usage: interviewprep attempt <id> [file], or pipe it in: interviewprep attempt lc207 < sol.cpp\n", stderr);
      return 1;
    }
    std::string code;
    if (arg(2).empty()) {
      std::stringstream buffer;
      buffer << std::cin.rdbuf();
      code = buffer.str();
    } else {
      bool readOk = false;
      code = readFile(arg(2), &readOk);
      if (!readOk) { std::fprintf(stderr, "Can't read %s.\n", arg(2).c_str()); return 1; }
    }
    // The day's file as it stands, so the core can number this paste. The paste is appended.
    Json request = cmd("attempt").set("id", arg(1)).set("code", code);
    Json r = run(request);
    const std::string path = directoryOf(statePath) + "/" + r["stem"].asStr() + r["ext"].asStr();
    if (exists(path)) {
      bool readOk = false;
      const std::string existing = readFile(path, &readOk);
      if (!readOk) { std::fprintf(stderr, "Can't read %s, so the paste wasn't saved.\n", path.c_str()); return 1; }
      r = run(request.set("existing", existing));
    }
    if (!r["block"].asStr().empty() && !appendText(path, r["block"].asStr())) {
      std::fprintf(stderr, "Couldn't save the attempt in %s: %s\n", path.c_str(), std::strerror(errno));
      return 1;
    }
    std::printf("%s\n%s\n", r["note"].asStr().c_str(), path.c_str());
  } else if (command == "undo") {
    Json request = cmd("undo");
    if (!arg(1).empty()) request.set("id", arg(1));
    std::printf("Removed the last grade on %s.\n", run(request)["id"].asStr().c_str());
  } else if (command == "mock") {
    Json r = run(cmd("mock"));
    std::printf("%d minutes, labels hidden:\n", r["minutes"].asInt());
    for (const Json& item : r["items"].items()) std::printf("  %s\n", item["url"].asStr().c_str());
  } else if (command == "show") {
    const Json c = run(cmd("card").set("id", arg(1)))["card"];
    std::printf("%s  %s\n%s. %s.\n", c["id"].asStr().c_str(), c["title"].asStr().c_str(),
                c["position"].asStr().c_str(), c["next"].asStr().c_str());
    if (c.has("url")) std::printf("%s\n", c["url"].asStr().c_str());
    std::string patterns;
    for (const Json& p : c["patterns"].items()) patterns += (patterns.empty() ? "" : ", ") + p["name"].asStr();
    if (!patterns.empty()) std::printf("Patterns: %s\n", patterns.c_str());
    if (!c["idea"].asStr().empty()) std::printf("Idea: %s\nPitfall: %s\n", c["idea"].asStr().c_str(), c["pitfall"].asStr().c_str());
    if (!c["answer"].asStr().empty()) std::printf("Answer: %s\n", c["answer"].asStr().c_str());
    for (const Json& h : c["history"].items()) std::printf("  %s  %s\n", h["date"].asStr().c_str(), h["grade"].asStr().c_str());
  } else if (command == "explain") {
    Json r = run(cmd("explain").set("id", arg(1)));
    if (r["steps"].size() == 0) std::puts("No grades yet.");
    printSteps(r["steps"]);
  } else if (command == "find") {
    Json r = run(cmd("library").set("q", arg(1)).set("limit", 25));
    std::printf("%d found\n", r["total"].asInt());
    for (const Json& row : r["rows"].items()) printItem(row, false);
  } else if (command == "similar") {
    Json request = cmd("similar").set("id", arg(1));
    if (!arg(2).empty()) request.set("n", std::atoi(arg(2).c_str()));
    const Json r = run(request);
    for (const Json& row : r["rows"].items()) {
      std::printf("  %.2f ", row["similarity"].asNum());
      printItem(row, false);
    }
  } else if (command == "recommend") {
    Json request = cmd("recommend").set("lane", laneArg(1));
    if (!arg(2).empty()) request.set("n", std::atoi(arg(2).c_str()));
    const Json r = run(request);
    for (const Json& row : r["rows"].items()) printItem(row, true);
  } else if (command == "profile") {
    Json r = run(cmd("insights"));
    for (const Json& note : r["notes"].items()) std::printf("- %s\n", note["text"].asStr().c_str());
    std::printf("\n%-34s %5s %5s %5s %5s %5s %s\n", "Pattern", "cold", "rev", "drill", "card", "mast", "seen");
    for (const Json& p : r["patterns"].items()) {
      std::printf("%-34s", p["name"].asStr().c_str());
      printRate(p["cold"]); printRate(p["review"]); printRate(p["drill"]); printRate(p["flash"]);
      std::printf(" %4d%% %d/%d%s\n", static_cast<int>(p["mastery"].asNum() * 100 + 0.5), p["seen"].asInt(), p["total"].asInt(),
                  p["memorizing"].asBool() ? "  memorizing" : "");
    }
    std::puts("\nWhat your judged code shows most");
    if (r["needs"].size() == 0) std::puts("  Nothing yet: the coach fills this in after it reviews code you saved.");
    for (const Json& a : r["needs"].items()) {
      std::printf("  %-56s %4d%%\n", a["label"].asStr().c_str(), static_cast<int>(a["share"].asNum() * 100 + 0.5));
    }
  } else if (command == "rules") {
    Json r = run(cmd("rules"));
    // "1-4, 5-10" days, or "1, 3, 7" when no gap has extra days.
    auto list = [](const Json& checks) {
      std::string s;
      for (const Json& gap : checks.items()) {
        const int base = gap["base"].asInt(), spread = gap["spread"].asInt();
        s += (s.empty() ? "" : ", ") + std::to_string(base) + (spread > 0 ? "-" + std::to_string(base + spread) : "");
      }
      return s;
    };
    for (const char* kind : {"problems", "cards"}) {
      const Json& schedule = r[kind];
      std::printf("%s%s\n", kind[0] == 'p' ? "Problems" : "Cards",
                  schedule["blueAfterLast"].asBool() ? ": past the last Red or Yellow check come Blue's" : ": gone after the last check");
      for (const char* track : {"red", "yellow", "blue"}) {
        std::printf("  %-7s checks after %s days\n", track, list(schedule["checks"][track]).c_str());
      }
      std::printf("  %-10s %-10s %-10s %-10s\n", "", "click B", "click Y", "click R");
      for (const auto& row : schedule["rules"].members()) {
        std::printf("  %-10s %-10s %-10s %-10s\n", row.first.c_str(), row.second["b"].asStr().c_str(),
                    row.second["y"].asStr().c_str(), row.second["r"].asStr().c_str());
      }
      std::puts("");
    }
    std::printf("Clock: %s days. A day starts at %d:00.\n", r["clock"].asStr().c_str(), r["rollover"].asInt());
    std::printf("Daily queue: %d problems (%d new, at most %d checks), %d cards (%d new).\n", r["codeCap"].asInt(),
                r["codeNew"].asInt(), r["codeReviews"].asInt(), r["cardCap"].asInt(), r["cardNew"].asInt());
  } else if (command == "simulate") {
    Json clicks = Json::array();
    std::stringstream stream(arg(1));
    std::string click;
    while (std::getline(stream, click, ',')) clicks.push(click);
    printSteps(run(cmd("simulate").set("clicks", clicks).set("kind", arg(2) == "card" ? "card" : "problem"))["steps"]);
  } else if (command == "set") {
    const std::string key = arg(1), value = arg(2);
    Json fields = Json::object();
    if (key == "clock" || key == "company") fields.set(key, value);
    else if (key == "premium") fields.set(key, value == "true" || value == "1" || value == "on");
    else fields.set(key, std::atoi(value.c_str()));
    Json r = run(cmd("settings").set("set", fields));
    std::puts(r["settings"].dump().c_str());
  } else if (command == "import") {
    bool readOk = false;
    Json problems = Json::parse(readFile(arg(1), &readOk));
    if (!readOk || !problems.isArray()) { std::fprintf(stderr, "Can't read %s as a tracker export.\n", arg(1).c_str()); return 1; }
    Json r = run(cmd("import").set("problems", problems));
    std::printf("Added %d grades, skipped %d.\n", r["added"].asInt(), r["skipped"].asInt());
  } else if (command == "export") {
    std::puts(engine.saveState().c_str());
  } else {
    std::fprintf(stderr, "Unknown command: %s. Run interviewprep help.\n", command.c_str());
    return 1;
  }
  return 0;
}
