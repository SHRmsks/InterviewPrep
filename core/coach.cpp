/*
coach.cpp — what a model may read and write when it coaches you.

The model works through `interviewprep mcp`, which owns the files. The rules for
those files live here, so they can be tested like every other rule. Like the
rest of the core, nothing here touches the disk: a front end passes file
contents in and writes back what it is given.

None of these commands can change your progress. A model's grade sits beside
yours in the log, and the most it can do is keep a problem's checks going on
the check that would retire it (section 3 of config.hpp).

The model names things only from closed lists: the topics in the resources, and
the thinking labels and flaws in config.hpp. It can't invent a label, so the
same mistake keeps the same name from one session to the next. It never writes
a probability: the app decides what comes next from the labels.
*/

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>
#include <utility>

#include "engine.hpp"

namespace interviewprep {

namespace {

Json failure(const std::string& message) {
  return Json::object().set("ok", false).set("error", message);
}

std::string lowered(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
  return text;
}

// Counts characters, not bytes, so a note in any language gets the same room.
int characters(const std::string& text) {
  return static_cast<int>(std::count_if(text.begin(), text.end(), [](unsigned char c) { return (c & 0xC0) != 0x80; }));
}

/*
A topic is a finer topic such as sliding-window/shortest-valid, or an algorithm
pattern. Fills the pattern it belongs to and the topic, -1 for a whole pattern.
*/
bool findTopic(const Resources& resources, const std::string& slug, int* pattern, int* topic) {
  const int t = resources.topicIndex(slug);
  if (t >= 0) {
    *topic = t;
    *pattern = resources.topics()[t].pattern;
    return true;
  }
  const int p = resources.patternIndex(slug);
  if (p < 0 || !resources.patterns()[p].algorithm) return false;
  *topic = -1;
  *pattern = p;
  return true;
}

int flawIndex(const std::string& slug) {
  for (int f = 0; f < FLAW_COUNT; ++f) {
    if (slug == FLAWS[f].slug) return f;
  }
  return -1;
}

std::string notATopic(const std::string& text) {
  return "\"" + text + "\" is not a topic. Use a slug from topics_list.";
}

std::string notAFlaw(const std::string& text) {
  std::string names;
  for (const FlawInfo& f : FLAWS) names += std::string(names.empty() ? "" : ", ") + f.slug;
  return "\"" + text + "\" is not a flaw. Use one of: " + names + ".";
}

// The credit of a thinking label, or -1 for one that isn't given or isn't known.
double recognisedCredit(const std::string& value) {
  if (value == "yes") return CREDIT_RECOGNISED_YES;
  if (value == "partial") return CREDIT_RECOGNISED_PARTIAL;
  if (value == "no") return CREDIT_RECOGNISED_NO;
  return -1;
}

double keyStepCredit(const std::string& value) {
  if (value == "own") return CREDIT_KEY_STEP_OWN;
  if (value == "hint") return CREDIT_KEY_STEP_HINT;
  if (value == "missed") return CREDIT_KEY_STEP_MISSED;
  return -1;
}

/*
The judgement log has one object per line. A line that does not parse is
skipped, so a crash in the middle of a write costs that line and no other.
*/
std::vector<Json> parseLog(const std::string& text) {
  std::vector<Json> lines;
  const std::string_view view(text);
  size_t start = 0;
  while (start < view.size()) {
    size_t end = view.find('\n', start);
    if (end == std::string_view::npos) end = view.size();
    Json line = Json::parse(view.substr(start, end - start));
    if (line.isObject()) lines.push_back(std::move(line));
    start = end + 1;
  }
  return lines;
}

std::string keyOf(const Json& line) { return gradeKey(line["problem"].asStr(), line["date"].asStr()); }

// Every attempt with a line, judged or dropped.
std::set<std::string> judgedKeys(const std::vector<Json>& log) {
  std::set<std::string> keys;
  for (const Json& line : log) keys.insert(keyOf(line));
  return keys;
}

// The judgements from before 2026-11-09, by attempt. A later line for the same attempt wins.
std::map<std::string, Json> byAttempt(const std::vector<Json>& log) {
  std::map<std::string, Json> lines;
  for (const Json& line : log) lines[keyOf(line)] = line;
  return lines;
}

struct Attempt {
  std::string path;   // "lc207/2026-11-06T21-30-2.cpp"
  std::string stamp;  // "2026-11-06T21-30"
  int number = 1;     // the 2 above, for a second save in the same minute
  int date = -1;      // the study day it belongs to
};

// Anything in the folder that isn't an attempt, such as .DS_Store, is ignored.
bool parseAttempt(const std::string& path, std::string* id, Attempt* out) {
  const size_t slash = path.find('/');
  const size_t dot = path.rfind('.');
  if (slash == std::string::npos || dot == std::string::npos || dot < slash) return false;
  const std::string name = path.substr(slash + 1, dot - slash - 1);
  out->date = dateFromNow(name);
  if (out->date < 0) return false;
  const size_t length = name.size() >= 16 && name[10] == 'T' ? 16 : 10;
  *id = path.substr(0, slash);
  out->path = path;
  out->stamp = name.substr(0, length);
  out->number = name.size() > length + 1 ? std::atoi(name.c_str() + length + 1) : 1;
  return true;
}

/*
Each attempt belongs to the next grade of its problem: the code you pasted
is what that click was about. Attempts newer than the last grade wait for
one. The result runs parallel to `events`.
*/
std::vector<std::vector<std::string>> attemptsByEvent(const std::vector<Event>& events, const Json& paths) {
  std::map<std::string, std::vector<Attempt>> byProblem;
  for (const Json& path : paths.items()) {
    std::string id;
    Attempt attempt;
    if (parseAttempt(path.asStr(), &id, &attempt)) byProblem[id].push_back(std::move(attempt));
  }
  for (auto& entry : byProblem) {
    std::sort(entry.second.begin(), entry.second.end(), [](const Attempt& a, const Attempt& b) {
      return a.stamp != b.stamp ? a.stamp < b.stamp : a.number < b.number;
    });
  }

  std::vector<std::vector<std::string>> result(events.size());
  std::map<std::string, size_t> nextUnclaimed;
  for (size_t e = 0; e < events.size(); ++e) {
    const auto found = byProblem.find(events[e].id);
    if (found == byProblem.end()) continue;
    size_t& next = nextUnclaimed[events[e].id];
    while (next < found->second.size() && found->second[next].date <= events[e].date) {
      result[e].push_back(found->second[next++].path);
    }
  }
  return result;
}

Json arrayOf(const std::vector<std::string>& values) {
  Json list = Json::array();
  for (const std::string& value : values) list.push(value);
  return list;
}

// Further from Blue is higher.
int severity(const std::string& grade) { return grade == "b" ? 0 : grade == "y" ? 1 : 2; }

// Returns what is wrong with a judgement's topics, or "".
std::string checkTopics(const Resources& resources, const Json& value) {
  if (value.isNull()) return "";
  if (!value.isArray()) return "topics must be a list.";
  if (static_cast<int>(value.size()) > JUDGEMENT_MAX_TOPICS) {
    return "A judgement can name at most " + std::to_string(JUDGEMENT_MAX_TOPICS) + " topics.";
  }
  for (const Json& name : value.items()) {
    int pattern = -1, topic = -1;
    if (!findTopic(resources, name.asStr(), &pattern, &topic)) return notATopic(name.asStr());
  }
  return "";
}

// Returns what is wrong with a judgement's flaws, or "".
std::string checkFlaws(const Json& value) {
  if (value.isNull()) return "";
  if (!value.isArray()) return "flaws must be a list.";
  std::set<std::string> seen;
  for (const Json& name : value.items()) {
    if (flawIndex(name.asStr()) < 0) return notAFlaw(name.asStr());
    if (!seen.insert(name.asStr()).second) return "flaws lists " + name.asStr() + " twice.";
  }
  return "";
}

// What an older judgement said, without the fields that only repeat the grade.
Json earlierJudgement(const Json& line) {
  Json earlier = Json::object();
  for (const char* field : {"model_grade", "recognised", "topics", "abilities", "patterns", "flaws", "note", "traced"}) {
    if (line.has(field)) earlier.set(field, line[field]);
  }
  return earlier;
}

}  // namespace

std::vector<Engine::Waiting> Engine::waitingForJudgement(const std::set<std::string>& judged) const {
  std::vector<Waiting> waiting;
  std::set<std::string> gradedBefore, listed;
  for (size_t e = 0; e < events_.size(); ++e) {
    const int index = resources_.indexOf(events_[e].id);
    if (index < 0 || resources_.questions()[index].kind != Kind::Problem) continue;
    const bool firstTry = gradedBefore.insert(events_[e].id).second;
    const std::string key = gradeKey(events_[e].id, formatDate(events_[e].date));
    if (judged.count(key) == 0 && listed.insert(key).second) waiting.push_back({e, index, firstTry});
  }
  return waiting;
}

Json Engine::cmdOverview(const Json& r, int) {
  const std::vector<Json> log = parseLog(r["log"].asStr());
  const std::set<std::string> oldKeys = judgedKeys(parseLog(r["old_log"].asStr()));
  const auto waiting = waitingForJudgement(judgedKeys(log));

  int judged = 0, dropped = 0;
  for (const Json& line : log) (line["dropped"].asBool(false) ? dropped : judged) += 1;
  int withEarlier = 0;
  for (const Waiting& w : waiting) withEarlier += oldKeys.count(gradeKey(events_[w.event].id, formatDate(events_[w.event].date)));

  Json reply = Json::object().set("ok", true);
  reply.set("pending", static_cast<int>(waiting.size())).set("batch_size", BATCH_SIZE);
  reply.set("pending_with_earlier_judgement", withEarlier);
  reply.set("judged", judged).set("dropped", dropped);

  /*
  How often each flaw and each thinking label came up in judged code, most
  common first, so a session sees the trend before it reads any code.
  */
  std::array<int, FLAW_COUNT> counts{};
  std::map<std::string, int> recognised, keyStep;
  for (const Json& line : log) {
    if (line["dropped"].asBool(false)) continue;
    for (const Json& name : line["flaws"].items()) {
      const int f = flawIndex(name.asStr());
      if (f >= 0) ++counts[f];
    }
    if (line.has("recognised")) ++recognised[line["recognised"].asStr()];
    if (line.has("key_step")) ++keyStep[line["key_step"].asStr()];
  }
  std::vector<int> order;
  for (int f = 0; f < FLAW_COUNT; ++f) {
    if (counts[f] > 0) order.push_back(f);
  }
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return counts[a] > counts[b]; });
  Json flaws = Json::object();
  for (int f : order) flaws.set(FLAWS[f].slug, counts[f]);
  Json thinking = Json::object();
  Json rec = Json::object(), key = Json::object();
  for (const auto& entry : recognised) rec.set(entry.first, entry.second);
  for (const auto& entry : keyStep) key.set(entry.first, entry.second);
  thinking.set("recognised", rec).set("key_step", key);
  reply.set("flaw_counts", flaws).set("thinking_counts", thinking);
  return reply;
}

Json Engine::cmdPending(const Json& r, int date) {
  const auto waiting = waitingForJudgement(judgedKeys(parseLog(r["log"].asStr())));
  const auto earlier = byAttempt(parseLog(r["old_log"].asStr()));
  const auto attempts = attemptsByEvent(events_, r["attempts"]);

  Json items = Json::array();
  for (size_t k = 0; k < waiting.size() && static_cast<int>(k) < BATCH_SIZE; ++k) {
    const Event& e = events_[waiting[k].event];
    const Json card = detail(waiting[k].question, date);
    Json item = Json::object();
    item.set("problem", e.id).set("date", formatDate(e.date)).set("title", card["title"]).set("url", card["url"]);
    item.set("level", card["level"]).set("rating", card["rating"]).set("timebox_min", card["timebox"]);
    item.set("first_try", waiting[k].firstTry).set("grade", clickName(e.click));
    if (e.secs) item.set("secs", e.secs);
    if (e.hinted) item.set("hinted", true);
    if (e.mock) item.set("mock", true);
    // What an older version of the app asked you after a miss, if it did.
    if (!e.gaps.empty()) item.set("your_gaps", arrayOf(e.gaps));
    if (!e.bug.empty()) item.set("your_bug", e.bug);
    Json patterns = Json::array();
    for (const Json& p : card["patterns"].items()) patterns.push(p["slug"]);
    item.set("resources_patterns", patterns);
    Json topics = Json::array();
    for (int t : resources_.questions()[waiting[k].question].topics) topics.push(resources_.topics()[t].slug);
    if (topics.size() > 0) item.set("resources_topics", topics);
    if (!card["idea"].asStr().empty()) item.set("idea", card["idea"]);
    if (!card["pitfall"].asStr().empty()) item.set("pitfall", card["pitfall"]);
    const auto old = earlier.find(gradeKey(e.id, formatDate(e.date)));
    if (old != earlier.end()) item.set("earlier_judgement", earlierJudgement(old->second));
    item.set("attempts", arrayOf(attempts[waiting[k].event]));
    items.push(std::move(item));
  }
  return Json::object().set("ok", true).set("total", static_cast<int>(waiting.size()))
      .set("batch_size", BATCH_SIZE).set("items", items);
}

Json Engine::cmdJudgement(const Json& r, int) {
  const Json& j = r["judgement"];
  if (!j.isObject()) return failure("A judgement must be an object.");
  for (const auto& field : j.members()) {
    static const char* const allowed[] = {"problem", "date", "model_grade", "recognised", "key_step",
                                          "topics",  "flaws", "note",       "traced",     "by"};
    if (std::find(std::begin(allowed), std::end(allowed), field.first) == std::end(allowed)) {
      return failure("A judgement has no field called " + field.first + ".");
    }
  }

  const std::string& id = j["problem"].asStr();
  const std::string& date = j["date"].asStr();
  const std::vector<Json> log = parseLog(r["log"].asStr());
  const std::set<std::string> judged = judgedKeys(log);
  if (judged.count(gradeKey(id, date))) return failure(id + " on " + date + " is already judged. Judgements are never replaced.");

  const auto waiting = waitingForJudgement(judged);
  const auto found = std::find_if(waiting.begin(), waiting.end(), [&](const Waiting& w) {
    return events_[w.event].id == id && formatDate(events_[w.event].date) == date;
  });
  if (found == waiting.end()) return failure("There is no grade on " + id + " for " + date + ". Use a problem and date from batch_pending.");

  // With no code there is nothing to read, so grades, thinking and notes are optional.
  const bool hadCode = !attemptsByEvent(events_, r["attempts"])[found->event].empty();
  Click click;
  if ((hadCode || j.has("model_grade")) && !parseClick(j["model_grade"].asStr(), &click)) {
    return failure("model_grade must be b, y or r.");
  }
  const std::string& recognised = j["recognised"].asStr();
  if ((hadCode || j.has("recognised")) && recognisedCredit(recognised) < 0) {
    return failure("recognised must be yes, partial or no.");
  }
  const std::string& keyStep = j["key_step"].asStr();
  if ((hadCode || j.has("key_step")) && keyStepCredit(keyStep) < 0) {
    return failure("key_step must be own, hint or missed.");
  }
  std::string problem = checkTopics(resources_, j["topics"]);
  if (problem.empty()) problem = checkFlaws(j["flaws"]);
  if (!problem.empty()) return failure(problem);
  if (!hadCode && j["flaws"].size() > 0) return failure("Flaws are read from code, and this attempt has none saved.");
  const std::string& note = j["note"].asStr();
  if (hadCode && note.empty()) return failure("A judgement of code needs a note saying what you saw.");
  if (characters(note) > NOTE_MAX_CHARS) {
    return failure("The note is " + std::to_string(characters(note)) + " characters. The limit is " +
                   std::to_string(NOTE_MAX_CHARS) + ".");
  }

  const Event& e = events_[found->event];

  /*
  You ran the code and the model only read it, so a grade kinder than yours
  has to show its work. Asking nicely wasn't enough: in a live test a model
  explained away a true Red and called buggy code correct. Working one input
  by hand is what catches that.
  */
  const std::string& traced = j["traced"].asStr();
  if (hadCode && severity(j["model_grade"].asStr()) < severity(clickName(e.click)) && traced.empty()) {
    return failure("Your grade is kinder than theirs, and they ran the code. Work a small input through this code by "
                   "hand and put it in traced: the input, what this code returns, and the right answer.");
  }
  if (characters(traced) > NOTE_MAX_CHARS) {
    return failure("traced is " + std::to_string(characters(traced)) + " characters. The limit is " +
                   std::to_string(NOTE_MAX_CHARS) + ".");
  }

  Json line = Json::object();
  line.set("problem", id).set("title", resources_.questions()[found->question].title).set("date", date);
  line.set("first_try", found->firstTry).set("your_grade", clickName(e.click));
  if (j.has("model_grade")) line.set("model_grade", j["model_grade"]);
  if (j.has("recognised")) line.set("recognised", recognised);
  if (j.has("key_step")) line.set("key_step", keyStep);
  line.set("topics", j["topics"].isArray() ? j["topics"] : Json::array());
  line.set("flaws", j["flaws"].isArray() ? j["flaws"] : Json::array());
  if (!note.empty()) line.set("note", note);
  if (!traced.empty()) line.set("traced", traced);
  line.set("had_code", hadCode);
  if (e.secs) line.set("secs", e.secs);
  if (j.has("by")) line.set("by", j["by"]);
  line.set("ts", r["now"]);
  return Json::object().set("ok", true).set("line", line).set("remaining", static_cast<int>(waiting.size()) - 1);
}

/*
Leaves an attempt out of the new log, so its grade alone counts for it. Only for
an attempt with nothing to add: no code, or an older judgement that already
says it. New code always gets a judgement.
*/
Json Engine::cmdDrop(const Json& r, int) {
  const Json& d = r["drop"];
  if (!d.isObject()) return failure("A drop must be an object.");
  for (const auto& field : d.members()) {
    static const char* const allowed[] = {"problem", "date", "reason", "by"};
    if (std::find(std::begin(allowed), std::end(allowed), field.first) == std::end(allowed)) {
      return failure("A drop has no field called " + field.first + ".");
    }
  }
  const std::string& id = d["problem"].asStr();
  const std::string& date = d["date"].asStr();
  const std::set<std::string> judged = judgedKeys(parseLog(r["log"].asStr()));
  if (judged.count(gradeKey(id, date))) return failure(id + " on " + date + " is already judged or dropped.");

  const auto waiting = waitingForJudgement(judged);
  const auto found = std::find_if(waiting.begin(), waiting.end(), [&](const Waiting& w) {
    return events_[w.event].id == id && formatDate(events_[w.event].date) == date;
  });
  if (found == waiting.end()) return failure("There is no grade on " + id + " for " + date + ". Use a problem and date from batch_pending.");

  const bool hadCode = !attemptsByEvent(events_, r["attempts"])[found->event].empty();
  const bool hadEarlier = judgedKeys(parseLog(r["old_log"].asStr())).count(gradeKey(id, date)) > 0;
  if (hadCode && !hadEarlier) {
    return failure("This attempt has code and no earlier judgement, so it needs a judgement. Drop only an attempt with "
                   "nothing to add: no code, or an earlier judgement that already says it.");
  }
  const std::string& reason = d["reason"].asStr();
  if (reason.find_first_not_of(" \t\r\n") == std::string::npos) return failure("Say in reason why it has nothing to add.");
  if (characters(reason) > NOTE_MAX_CHARS) {
    return failure("The reason is " + std::to_string(characters(reason)) + " characters. The limit is " +
                   std::to_string(NOTE_MAX_CHARS) + ".");
  }

  const Event& e = events_[found->event];
  Json line = Json::object();
  line.set("problem", id).set("title", resources_.questions()[found->question].title).set("date", date);
  line.set("first_try", found->firstTry).set("your_grade", clickName(e.click));
  line.set("dropped", true).set("reason", reason).set("had_code", hadCode);
  if (d.has("by")) line.set("by", d["by"]);
  line.set("ts", r["now"]);
  return Json::object().set("ok", true).set("line", line).set("remaining", static_cast<int>(waiting.size()) - 1);
}

Json Engine::cmdCommit(const Json& r, int) {
  const std::string& profile = r["profile"].asStr();
  if (profile.find_first_not_of(" \t\r\n") == std::string::npos) return failure("The profile is empty.");
  if (static_cast<int>(profile.size()) > PROFILE_MAX_BYTES) {
    return failure("The profile is " + std::to_string(profile.size()) + " bytes. The limit is " +
                   std::to_string(PROFILE_MAX_BYTES) + ". Rewrite it shorter and keep what still matters: every judgement stays in the log.");
  }
  return Json::object().set("ok", true);
}

/*
Takes the contents of the judgement log, so an unreadable file is judged here
and not in each front end. Reading it can't fail: a line that doesn't parse is
skipped, and an attempt with no line is counted by your grade alone.

`old` is the log from before 2026-11-09. Until the coach judges or drops one
of those attempts again, only its grade counts, and only to keep a check
going (section 3 of config.hpp). Its older labels count for nothing.
*/
Json Engine::cmdJudgements(const Json& r) {
  judged_.clear();
  for (const Json& line : parseLog(r["old"].asStr())) {
    Judged j;
    j.hadCode = line["had_code"].asBool(false);
    Click click;
    if (parseClick(line["model_grade"].asStr(), &click)) {
      j.hasModelGrade = true;
      j.modelGrade = click;
    }
    judged_[keyOf(line)] = std::move(j);
  }
  for (const Json& line : parseLog(r["text"].asStr())) {
    Judged j;
    j.dropped = line["dropped"].asBool(false);
    j.hadCode = line["had_code"].asBool(false);
    Click click;
    if (parseClick(line["model_grade"].asStr(), &click)) {
      j.hasModelGrade = true;
      j.modelGrade = click;
    }
    const double recognised = recognisedCredit(line["recognised"].asStr());
    const double keyStep = keyStepCredit(line["key_step"].asStr());
    if (recognised >= 0 && keyStep >= 0) j.thinking = recognised * keyStep;
    else if (recognised >= 0) j.thinking = recognised;
    else if (keyStep >= 0) j.thinking = keyStep;
    for (const Json& name : line["flaws"].items()) {
      const int f = flawIndex(name.asStr());
      if (f >= 0) j.flaws.push_back(f);
    }
    judged_[keyOf(line)] = std::move(j);   // a new line replaces an older one
  }
  rebuild();
  return Json::object().set("ok", true).set("lines", static_cast<int>(judged_.size()));
}

Json Engine::cmdSearch(const Json& r) {
  const std::string& label = r["label"].asStr();
  // A pattern also finds its finer topics: sliding-window finds sliding-window/fixed-size.
  const auto isLabel = [&](const std::string& name) {
    return name == label || (name.size() > label.size() && name.compare(0, label.size(), label) == 0 && name[label.size()] == '/');
  };
  // Older lines kept patterns and abilities, or patterns and free-text flaws.
  static const char* const fields[] = {"topics", "flaws", "patterns", "abilities"};

  const std::string& problem = r["problem"].asStr();
  const std::string text = lowered(r["text"].asStr());
  const auto mentions = [&](const Json& line) {
    for (const char* field : fields) {
      for (const Json& name : line[field].items()) {
        if (isLabel(name.asStr())) return true;
      }
    }
    return false;
  };
  const auto saysIt = [&](const Json& line) {
    std::string all = line["title"].asStr() + " " + line["note"].asStr();
    for (const char* field : fields) {
      for (const Json& name : line[field].items()) all += " " + name.asStr();
    }
    return lowered(std::move(all)).find(text) != std::string::npos;
  };

  /*
  Oldest first: the older lines, then the new log. An older line whose attempt
  has a new one, judged or dropped, is left out, so nothing counts twice.
  */
  const std::vector<Json> log = parseLog(r["log"].asStr());
  const std::set<std::string> carried = judgedKeys(log);
  std::vector<Json> lines;
  for (Json& line : parseLog(r["old_log"].asStr())) {
    if (carried.count(keyOf(line))) continue;
    line.set("earlier_shape", true);
    lines.push_back(std::move(line));
  }
  for (const Json& line : log) {
    if (!line["dropped"].asBool(false)) lines.push_back(line);
  }

  std::vector<const Json*> matches;
  int firstTries = 0, firstTryPasses = 0;
  for (const Json& line : lines) {
    if (!problem.empty() && line["problem"].asStr() != problem) continue;
    if (!label.empty() && !mentions(line)) continue;
    if (!text.empty() && !saysIt(line)) continue;
    matches.push_back(&line);
    if (line["first_try"].asBool()) {
      /*
      A pass needs both grades. You ran the code and the model only read it,
      so each can catch what the other can't.
      */
      ++firstTries;
      firstTryPasses += line["your_grade"].asStr() == "b" && line["model_grade"].asStr("b") == "b";
    }
  }

  const int limit = std::min(SEARCH_MAX_LINES, std::max(1, r["limit"].asInt(SEARCH_DEFAULT_LINES)));
  Json rows = Json::array();
  for (auto it = matches.rbegin(); it != matches.rend() && static_cast<int>(rows.size()) < limit; ++it) rows.push(**it);
  return Json::object().set("ok", true).set("total", static_cast<int>(matches.size()))
      .set("first_tries", firstTries).set("first_try_passes", firstTryPasses).set("rows", rows);
}

/*
The closed lists the coach chooses from. Topics come under their pattern, each
with the signal that points to it. A pattern with no finer topics is a topic
itself, and so is a whole pattern when a problem fits none of its topics.
*/
Json Engine::cmdTopics() {
  Json patterns = Json::array();
  for (int p = 0; p < static_cast<int>(resources_.patterns().size()); ++p) {
    const Pattern& pattern = resources_.patterns()[p];
    if (!pattern.algorithm) continue;
    Json finer = Json::array();
    for (const Topic& t : resources_.topics()) {
      if (t.pattern == p) finer.push(Json::object().set("slug", t.slug).set("name", t.name).set("signal", t.signal));
    }
    Json entry = Json::object().set("slug", pattern.slug).set("name", pattern.name);
    if (finer.size() > 0) entry.set("topics", finer);
    patterns.push(std::move(entry));
  }
  Json flaws = Json::array();
  for (const FlawInfo& f : FLAWS) {
    flaws.push(Json::object().set("slug", f.slug).set("label", f.label).set("drilled_by", f.deck));
  }
  Json recognised = Json::array(), keyStep = Json::array();
  for (const char* value : {"yes", "partial", "no"}) recognised.push(value);
  for (const char* value : {"own", "hint", "missed"}) keyStep.push(value);
  return Json::object().set("ok", true).set("patterns", patterns).set("flaws", flaws)
      .set("thinking", Json::object().set("recognised", recognised).set("key_step", keyStep));
}

}  // namespace interviewprep
