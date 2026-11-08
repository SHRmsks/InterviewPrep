#include "engine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace interviewprep {

// ── Small helpers ─────────────────────────────────────────────────────────

static Json failure(const std::string& message) {
  return Json::object().set("ok", false).set("error", message);
}

static const char* kindName(Kind kind) {
  switch (kind) {
    case Kind::Problem: return "problem";
    case Kind::Flash: return "flash";
    case Kind::Drill: return "drill";
  }
  return "flash";
}

static const char* levelName(int level) {
  return level == 1 ? "Easy" : level == 3 ? "Hard" : "Medium";
}

static int timeboxMinutes(int level) {
  return level == 1 ? TIMEBOX_EASY : level == 3 ? TIMEBOX_HARD : TIMEBOX_MEDIUM;
}

static std::string capitalized(const char* text) {
  std::string s(text);
  if (!s.empty()) s[0] = static_cast<char>(s[0] - 'a' + 'A');
  return s;
}

// "Red, check 2 of 3". Used wherever a card's position is put into words.
static std::string describe(const CardState& s, const Schedule& schedule) {
  if (s.track == Track::New) return "New";
  if (s.track == Track::Retired) return "Retired";
  return capitalized(trackName(s.track)) + ", check " + std::to_string(s.step + 1) + " of " +
         std::to_string(checkCount(schedule, s.track));
}

// "7d", or "10-20d" for a gap with extra days.
static std::string gapText(const Gap& gap) {
  if (gap.spread <= 0) return std::to_string(gap.base) + "d";
  return std::to_string(gap.base) + "-" + std::to_string(gap.base + gap.spread) + "d";
}

static std::string nextCheck(const CardState& s, int today) {
  if (s.track == Track::New) return "Not started";
  if (s.track == Track::Retired) return "Gone for good";
  const int days = s.due - today;
  if (days <= 0) return "Due now";
  if (days == 1) return "Next check tomorrow";
  return "Next check in " + std::to_string(days) + " days";
}

static uint64_t mixSeed(uint64_t a, uint64_t b) {
  return (a * 0x9E3779B97F4A7C15ull) ^ (b + 0xD1B54A32D192ED03ull + (a << 6) + (a >> 2));
}

// ── Loading and saving ────────────────────────────────────────────────────

int Engine::loadResources(std::string_view json, std::string* error) {
  int count = resources_.load(json, error);
  resourcesLoaded_ = count >= 0;
  if (resourcesLoaded_) rebuild();
  return count;
}

bool Engine::loadState(std::string_view json, std::string* error) {
  std::string parseError;
  Json root = Json::parse(json, &parseError);
  if (!root.isObject()) {
    if (error) *error = "state is not valid JSON: " + parseError;
    return false;
  }

  const Json& s = root["settings"];
  settings_ = Settings();
  settings_.codeCap = s["codeCap"].asInt(CODE_CAP);
  settings_.codeNew = s["codeNew"].asInt(CODE_NEW);
  settings_.codeReviews = s["codeReviews"].asInt(CODE_REVIEWS);
  settings_.cardCap = s["cardCap"].asInt(CARD_CAP);
  settings_.cardNew = s["cardNew"].asInt(CARD_NEW);
  settings_.company = s["company"].asStr();
  settings_.premium = s["premium"].asBool(false);
  if (s.has("clock")) {
    settings_.clock = s["clock"].asStr() == "calendar" ? ClockMode::CalendarDays : ClockMode::StudyDays;
  }
  for (const Json& d : s["decksOff"].items()) settings_.decksOff.push_back(d.asStr());

  events_.clear();
  for (const Json& j : root["events"].items()) {
    Event e;
    e.id = j["id"].asStr();
    e.date = parseDate(j["d"].asStr());
    if (e.id.empty() || e.date < 0 || !parseClick(j["g"].asStr(), &e.click)) continue;
    for (const Json& g : j["gaps"].items()) e.gaps.push_back(g.asStr());
    e.bug = j["bug"].asStr();
    e.secs = j["s"].asInt();
    e.hinted = j["h"].asInt() != 0;
    e.mock = j["m"].asInt() != 0;
    events_.push_back(std::move(e));
  }

  hidden_.clear();
  for (const Json& h : root["hidden"].items()) hidden_.insert(h.asStr());

  plan_ = Plan();
  const Json& p = root["plan"];
  if (p.isObject()) {
    plan_.date = parseDate(p["date"].asStr());
    plan_.pulls = p["pulls"].asInt();
    auto readItems = [](const Json& list, std::vector<PlanItem>& out) {
      for (const Json& j : list.items()) out.push_back({j["id"].asStr(), j["slot"].asStr(), j["why"].asStr()});
    };
    readItems(p["code"], plan_.code);
    readItems(p["cards"], plan_.cards);
  }

  if (resourcesLoaded_) rebuild();
  return true;
}

std::string Engine::saveState() {
  Json s = Json::object();
  s.set("codeCap", settings_.codeCap).set("codeNew", settings_.codeNew).set("codeReviews", settings_.codeReviews);
  s.set("cardCap", settings_.cardCap).set("cardNew", settings_.cardNew);
  s.set("clock", settings_.clock == ClockMode::CalendarDays ? "calendar" : "study");
  s.set("company", settings_.company).set("premium", settings_.premium);
  Json off = Json::array();
  for (const auto& d : settings_.decksOff) off.push(d);
  s.set("decksOff", off);

  Json events = Json::array();
  for (const Event& e : events_) {
    Json j = Json::object();
    j.set("id", e.id).set("g", clickName(e.click)).set("d", formatDate(e.date));
    if (!e.gaps.empty()) {
      Json gaps = Json::array();
      for (const auto& g : e.gaps) gaps.push(g);
      j.set("gaps", gaps);
    }
    if (!e.bug.empty()) j.set("bug", e.bug);
    if (e.secs) j.set("s", e.secs);
    if (e.hinted) j.set("h", 1);
    if (e.mock) j.set("m", 1);
    events.push(std::move(j));
  }

  Json hidden = Json::array();
  for (const auto& h : hidden_) hidden.push(h);

  Json root = Json::object();
  root.set("v", 1).set("settings", s).set("events", events).set("hidden", hidden);
  // A plan waiting to be rebuilt is kept too, with no date, so its new faces aren't lost.
  if (plan_.date >= 0 || !plan_.code.empty() || !plan_.cards.empty()) {
    auto writeItems = [](const std::vector<PlanItem>& items) {
      Json list = Json::array();
      for (const auto& i : items) {
        list.push(Json::object().set("id", i.id).set("slot", i.slot).set("why", i.why));
      }
      return list;
    };
    root.set("plan", Json::object().set("date", plan_.date >= 0 ? formatDate(plan_.date) : std::string()).set("pulls", plan_.pulls)
                         .set("code", writeItems(plan_.code)).set("cards", writeItems(plan_.cards)));
  }
  dirty_ = false;
  return root.dump();
}

/*
── Replay ────────────────────────────────────────────────────────────────
All card state is computed here from the click log. Nothing else writes it.
*/

void Engine::rebuild() {
  std::stable_sort(events_.begin(), events_.end(),
                   [](const Event& a, const Event& b) { return a.date < b.date; });
  clock_ = Clock(settings_.clock);
  for (const Event& e : events_) clock_.markActive(e.date);

  states_.assign(resources_.questions().size(), CardState());
  for (const Event& e : events_) {
    const int i = resources_.indexOf(e.id);
    if (i < 0) continue;
    const Schedule& schedule = scheduleOf(i);
    const int day = clock_.day(e.date);
    states_[i] = applyClick(states_[i], effectiveClick(states_[i], e, day, schedule), day, schedule, seedOf(e.id));
    states_[i].lastClick = e.click;
    states_[i].lastDate = e.date;
  }
}

const Schedule& Engine::scheduleOf(int index) const {
  return resources_.questions()[index].kind == Kind::Problem ? PROBLEM_SCHEDULE : CARD_SCHEDULE;
}

/*
Your click, except on the check that would retire a problem: there a coach
grade stricter than yours keeps it going (section 3 of config.hpp). The coach
can add a check this way but never remove one.
*/
Click Engine::effectiveClick(const CardState& s, const Event& e, int day, const Schedule& schedule) const {
  if (s.track == Track::Retired || applyClick(s, e.click, day, schedule).track != Track::Retired) return e.click;
  const auto found = judged_.find(gradeKey(e.id, formatDate(e.date)));
  if (found == judged_.end() || found->second.dropped || !found->second.hasModelGrade) return e.click;
  return found->second.modelGrade == Click::Blue ? e.click : found->second.modelGrade;
}

Profile Engine::profile(int today) const { return buildProfile(resources_, events_, clock_, today, &judged_); }

bool Engine::inLane(const Question& q, Lane lane) const {
  return lane == Lane::Code ? q.kind == Kind::Problem : q.kind != Kind::Problem;
}

// ── Building a day's queue ────────────────────────────────────────────────

std::vector<char> Engine::takenSet() const {
  std::vector<char> taken(resources_.questions().size(), 0);
  auto take = [&](const std::string& id) {
    int i = resources_.indexOf(id);
    if (i >= 0) taken[i] = 1;
  };
  for (const auto& id : hidden_) take(id);
  for (const auto& item : plan_.code) take(item.id);
  for (const auto& item : plan_.cards) take(item.id);
  return taken;
}

std::vector<Seed> Engine::seedsFor(Lane lane, int today) const {
  const auto& questions = resources_.questions();
  std::vector<Seed> seeds;

  if (lane == Lane::Code) {
    // Problems in play, most urgent first: due now, then recent misses.
    std::vector<std::pair<double, int>> ranked;
    for (int i = 0; i < static_cast<int>(questions.size()); ++i) {
      const CardState& s = states_[i];
      if (questions[i].kind != Kind::Problem || s.attempts == 0 || s.track == Track::Retired) continue;
      double score = -0.2 * (today - s.lastDay);
      if (isDue(s, today)) score += 3;
      if (s.lastClick == Click::Red) score += 2;
      else if (s.lastClick == Click::Yellow) score += 1;
      ranked.emplace_back(-score, i);
    }
    std::sort(ranked.begin(), ranked.end());
    for (size_t k = 0; k < ranked.size() && k < 8; ++k) seeds.push_back({ranked[k].second});
    return seeds;
  }

  // Cards follow your recent misses on problems.
  std::vector<char> added(questions.size(), 0);
  for (auto e = events_.rbegin(); e != events_.rend() && seeds.size() < 8; ++e) {
    if (e->click == Click::Blue || today - clock_.day(e->date) > 7) continue;
    const int i = resources_.indexOf(e->id);
    if (i < 0 || added[i] || questions[i].kind != Kind::Problem) continue;
    added[i] = 1;
    seeds.push_back({i});
  }
  for (int i = 0; i < static_cast<int>(questions.size()) && seeds.size() < 12; ++i) {
    const CardState& s = states_[i];
    if (questions[i].kind == Kind::Problem || s.attempts == 0) continue;
    if (s.track == Track::Red || s.track == Track::Yellow) seeds.push_back({i});
  }
  return seeds;
}

RecommendContext Engine::contextFor(Lane lane, int today, const Profile& profile, std::vector<char>& taken) const {
  RecommendContext context;
  context.resources = &resources_;
  context.states = &states_;
  context.profile = &profile;
  context.seeds = seedsFor(lane, today);
  context.taken = &taken;
  context.includePremium = settings_.premium;
  context.company = settings_.company.empty() ? -1 : resources_.companyIndex(settings_.company);
  context.slotOffset = today;
  context.deckOff.assign(resources_.decks().size(), 0);
  for (const auto& slug : settings_.decksOff) {
    int d = resources_.deckIndex(slug);
    if (d >= 0) context.deckOff[d] = 1;
  }
  return context;
}

/*
Keeps priority order but avoids two neighbors with the same pattern, because
mixed practice trains telling patterns apart, and that is the skill that
failing to recognize a problem is missing.
*/
static void interleave(std::vector<PlanItem>& items, const Resources& resources) {
  auto keyOf = [&](const PlanItem& item) -> int {
    int i = resources.indexOf(item.id);
    if (i < 0) return -1;
    const Question& q = resources.questions()[i];
    return q.patterns.empty() ? -2 - resources.deckIndex(q.deck) : q.patterns[0];
  };
  for (size_t i = 1; i < items.size(); ++i) {
    const int previous = keyOf(items[i - 1]);
    if (keyOf(items[i]) != previous) continue;
    for (size_t j = i + 1; j < items.size(); ++j) {
      if (keyOf(items[j]) != previous) {
        std::rotate(items.begin() + static_cast<long>(i), items.begin() + static_cast<long>(j),
                    items.begin() + static_cast<long>(j) + 1);
        break;
      }
    }
  }
}

void Engine::fillLane(Lane lane, int date, int cap, int wantNew, int minNew, int maxReviews,
                      const std::vector<PlanItem>& before, std::vector<PlanItem>& out, const Profile& profile,
                      std::vector<char>& taken, Rng& rng) {
  const auto& questions = resources_.questions();
  const int today = clock_.day(date);

  // A queue rebuilt mid-day keeps what you already finished today.
  int doneReviews = 0;
  for (int i = 0; i < static_cast<int>(questions.size()); ++i) {
    const CardState& s = states_[i];
    if (inLane(questions[i], lane) && s.attempts > 0 && s.lastDate == date) {
      out.push_back({questions[i].id, "review", describe(s, scheduleOf(i))});
      taken[i] = 1;
      doneReviews += s.attempts > 1;   // graded before today too
    }
  }

  /*
  A new face stays until you grade it or hide it, in the order it had. Only
  one your settings now rule out (a Premium problem, a deck you turned off)
  drops away, and a coding problem the picker from before 2026-11-09 chose,
  which makes way for one the picker in section 7 chooses.
  */
  static const char* const oldPicks[] = {"coach", "explore", "sibling", "weak", "wildcard"};
  std::vector<PlanItem> kept;
  for (const PlanItem& item : before) {
    const int i = resources_.indexOf(item.id);
    if (item.slot == "review" || i < 0 || taken[i] || !inLane(questions[i], lane)) continue;
    const Question& q = questions[i];
    if (states_[i].track != Track::New) continue;
    if (lane == Lane::Code && std::find(std::begin(oldPicks), std::end(oldPicks), item.slot) != std::end(oldPicks)) continue;
    if (q.kind == Kind::Problem && q.premium && !settings_.premium) continue;
    if (std::find(settings_.decksOff.begin(), settings_.decksOff.end(), q.deck) != settings_.decksOff.end()) continue;
    kept.push_back(item);
    taken[i] = 1;
  }

  std::vector<int> due;
  for (int i = 0; i < static_cast<int>(questions.size()); ++i) {
    if (inLane(questions[i], lane) && isDue(states_[i], today) && !taken[i]) due.push_back(i);
  }

  /*
  This sort plus the cut below is the whole bounded queue. Whatever is left
  out stays due, is a day older tomorrow, and so moves up the line.
  */
  std::sort(due.begin(), due.end(), [&](int a, int b) {
    if (states_[a].due == states_[b].due && states_[a].track == states_[b].track) return a < b;
    return servedBefore(states_[a], states_[b], today);
  });

  /*
  New faces you kept take the day's new slots first, up to the day's
  allowance, so fresh picks only fill what is left and skipped days never pile
  up. Any beyond the allowance are ones you pulled yourself: they stay on top
  of the cap and never push a review out.
  */
  const int keep = static_cast<int>(kept.size());
  const int room = std::max(0, cap - static_cast<int>(out.size()));
  const int reserve = std::max(minNew, std::min(keep, wantNew));
  // At most `maxReviews` checks a day. The rest wait, and keep their place in line.
  const int reviewSlots = std::min({static_cast<int>(due.size()), std::max(0, maxReviews - doneReviews),
                                    std::max(0, room - reserve)});
  const int newSlots = std::min(wantNew, room - reviewSlots);

  for (int k = 0; k < reviewSlots; ++k) {
    out.push_back({questions[due[k]].id, "review", describe(states_[due[k]], scheduleOf(due[k]))});
    taken[due[k]] = 1;
  }
  out.insert(out.end(), kept.begin(), kept.end());
  if (newSlots - keep > 0) {
    RecommendContext context = contextFor(lane, today, profile, taken);
    for (const Pick& pick : recommend(lane, newSlots - keep, context, rng)) {
      out.push_back({questions[pick.question].id, pick.badge(), pick.why});
    }
  }
  interleave(out, resources_);
}

void Engine::ensurePlan(int date) {
  if (plan_.date == date) return;
  // What was queued before, so the new faces you haven't graded can stay.
  const Plan before = std::move(plan_);
  plan_ = Plan();
  plan_.date = date;
  const Profile p = profile(clock_.day(date));
  std::vector<char> taken = takenSet();
  Rng rng(mixSeed(static_cast<uint64_t>(date), 1));
  fillLane(Lane::Code, date, settings_.codeCap, settings_.codeNew, CODE_NEW_MIN, settings_.codeReviews, before.code,
           plan_.code, p, taken, rng);
  fillLane(Lane::Cards, date, settings_.cardCap, settings_.cardNew, CARD_NEW_MIN, settings_.cardCap, before.cards,
           plan_.cards, p, taken, rng);
  dirty_ = true;
}

// ── Views ─────────────────────────────────────────────────────────────────

Json Engine::view(int index, int date) const {
  const Question& q = resources_.questions()[index];
  const CardState& s = states_[index];
  const Schedule& schedule = scheduleOf(index);
  const int today = clock_.day(date);
  Json v = Json::object();
  v.set("id", q.id).set("kind", kindName(q.kind)).set("title", q.title).set("deck", q.deck);
  v.set("track", trackName(s.track)).set("step", s.step).set("steps", checkCount(schedule, s.track));
  v.set("attempts", s.attempts).set("position", describe(s, schedule)).set("next", nextCheck(s, today));
  if (s.track != Track::New && s.track != Track::Retired) {
    v.set("dueIn", s.due - today);
    /*
    The checks on its way out, each with its gap, and which one is next. A card's
    Red or Yellow goes on to Blue's checks; a problem is gone after its own.
    */
    Json rail = Json::array();
    int at = 0;
    auto add = [&](Track track) {
      for (int k = 0; k < checkCount(schedule, track); ++k) {
        if (track == s.track && k == s.step) at = static_cast<int>(rail.size());
        rail.push(Json::object().set("track", trackName(track)).set("gap", gapText(checkGap(schedule, track, k))));
      }
    };
    add(s.track);
    if (schedule.blueAfterLast && s.track != Track::Blue) add(Track::Blue);
    v.set("rail", rail).set("railAt", at);
  }
  if (s.attempts > 0 && s.lastDate == date) v.set("doneToday", clickName(s.lastClick));
  if (q.kind == Kind::Problem) {
    v.set("number", q.number).set("level", levelName(q.level)).set("rating", q.rating);
    v.set("freq", static_cast<int>(std::round(q.freq))).set("premium", q.premium);
    v.set("url", "https://leetcode.com/problems/" + q.slug + "/").set("tier", q.tier);
  }
  return v;
}

Json Engine::detail(int index, int date) const {
  const Question& q = resources_.questions()[index];
  Json v = view(index, date);
  v.set("timebox", timeboxMinutes(q.level));
  if (q.kind == Kind::Problem) {
    v.set("idea", q.idea).set("pitfall", q.pitfall).set("time", q.time).set("space", q.space);
  } else {
    v.set("qcode", q.qcode).set("answer", q.answer).set("code", q.code).set("bug", q.bug);
    int deck = resources_.deckIndex(q.deck);
    if (deck >= 0) v.set("deckTitle", resources_.decks()[deck].title);
  }

  Json patterns = Json::array();
  for (int p : q.patterns) {
    if (p < static_cast<int>(resources_.patterns().size())) {
      patterns.push(Json::object().set("slug", resources_.patterns()[p].slug).set("name", resources_.patterns()[p].name));
    }
  }
  v.set("patterns", patterns);

  Json companies = Json::array();
  for (const auto& c : q.companies) {
    if (c.first < static_cast<int>(resources_.companies().size())) {
      companies.push(Json::object().set("name", resources_.companies()[c.first].name).set("freq", static_cast<int>(std::round(c.second))));
    }
  }
  v.set("companies", companies);

  Json history = Json::array();
  for (const Event& e : events_) {
    if (e.id != q.id) continue;
    Json h = Json::object();
    h.set("date", formatDate(e.date)).set("grade", clickName(e.click));
    if (e.secs) h.set("secs", e.secs);
    history.push(std::move(h));
  }
  v.set("history", history);
  return v;
}

Json Engine::planView(const std::vector<PlanItem>& items, int date) const {
  Json list = Json::array();
  for (const PlanItem& item : items) {
    int i = resources_.indexOf(item.id);
    if (i < 0) continue;  // a question since removed from the resources
    Json v = view(i, date);
    v.set("slot", item.slot).set("why", item.why);
    list.push(std::move(v));
  }
  return list;
}

Json Engine::laneCounts(Lane lane, int today) const {
  int counts[5] = {0, 0, 0, 0, 0};
  int due = 0;
  const auto& questions = resources_.questions();
  for (size_t i = 0; i < questions.size(); ++i) {
    if (!inLane(questions[i], lane)) continue;
    ++counts[static_cast<int>(states_[i].track)];
    due += isDue(states_[i], today);
  }
  return Json::object().set("new", counts[0]).set("red", counts[1]).set("yellow", counts[2])
      .set("blue", counts[3]).set("retired", counts[4]).set("due", due);
}

// ── Commands ──────────────────────────────────────────────────────────────

Json Engine::cmdToday(int date) {
  ensurePlan(date);
  const int today = clock_.day(date);

  auto lane = [&](Lane which, const std::vector<PlanItem>& items) {
    Json list = planView(items, date);
    int done = 0;
    std::vector<char> listed(resources_.questions().size(), 0);
    for (const PlanItem& item : items) {
      int i = resources_.indexOf(item.id);
      if (i < 0) continue;
      listed[i] = 1;
      done += states_[i].attempts > 0 && states_[i].lastDate == date;
    }
    int overflow = 0;
    for (size_t i = 0; i < resources_.questions().size(); ++i) {
      if (!listed[i] && inLane(resources_.questions()[i], which) && isDue(states_[i], today)) ++overflow;
    }
    return Json::object().set("items", list).set("done", done).set("total", static_cast<int>(items.size()))
        .set("overflow", overflow).set("counts", laneCounts(which, today));
  };

  return Json::object().set("ok", true).set("date", formatDate(date)).set("day", today)
      .set("studyDays", clock_.activeDays())
      .set("code", lane(Lane::Code, plan_.code)).set("cards", lane(Lane::Cards, plan_.cards));
}

Json Engine::cmdMore(const Json& r, int date) {
  ensurePlan(date);
  const Lane lane = r["lane"].asStr() == "cards" ? Lane::Cards : Lane::Code;
  auto& items = lane == Lane::Code ? plan_.code : plan_.cards;
  int want = std::max(1, r["n"].asInt(lane == Lane::Code ? 1 : 5));
  const int today = clock_.day(date);
  const auto& questions = resources_.questions();
  std::vector<char> taken = takenSet();

  // Anything that rolled over goes first.
  std::vector<int> due;
  for (int i = 0; i < static_cast<int>(questions.size()); ++i) {
    if (inLane(questions[i], lane) && isDue(states_[i], today) && !taken[i]) due.push_back(i);
  }
  std::sort(due.begin(), due.end(), [&](int a, int b) {
    if (states_[a].due == states_[b].due && states_[a].track == states_[b].track) return a < b;
    return servedBefore(states_[a], states_[b], today);
  });
  for (int i : due) {
    if (want == 0) break;
    items.push_back({questions[i].id, "review", describe(states_[i], scheduleOf(i))});
    taken[i] = 1;
    --want;
  }
  if (want > 0) {
    plan_.pulls += 1;
    const Profile p = profile(today);
    Rng rng(mixSeed(static_cast<uint64_t>(date), 100 + static_cast<uint64_t>(plan_.pulls)));
    RecommendContext context = contextFor(lane, today, p, taken);
    context.slotOffset = today + plan_.pulls;
    for (const Pick& pick : recommend(lane, want, context, rng)) {
      items.push_back({questions[pick.question].id, pick.badge(), pick.why});
    }
  }
  dirty_ = true;
  return cmdToday(date);
}

Json Engine::cmdGrade(const Json& r, int date) {
  const std::string& id = r["id"].asStr();
  const int index = resources_.indexOf(id);
  if (index < 0) return failure("unknown question: " + id);
  Click click;
  if (!parseClick(r["grade"].asStr(), &click)) return failure("grade must be b, y or r");

  // Grading again on the same day corrects the earlier click.
  for (size_t i = events_.size(); i-- > 0;) {
    if (events_[i].date != date) break;
    if (events_[i].id == id) {
      events_.erase(events_.begin() + static_cast<long>(i));
      rebuild();
      break;
    }
  }

  const CardState before = states_[index];

  Event e;
  e.id = id;
  e.click = click;
  e.date = date;
  e.secs = r["secs"].asInt();
  e.hinted = r["hinted"].asBool(false);
  e.mock = r["mock"].asBool(false);
  events_.push_back(std::move(e));
  rebuild();
  dirty_ = true;

  // A problem graded outside the plan still belongs to today's work.
  const CardState& after = states_[index];
  return Json::object().set("ok", true).set("card", view(index, date))
      .set("before", describe(before, scheduleOf(index))).set("after", describe(after, scheduleOf(index)))
      .set("next", nextCheck(after, clock_.day(date)));
}

Json Engine::cmdUndo(const Json& r) {
  if (events_.empty()) return failure("nothing to undo");
  std::string id = r["id"].asStr();
  size_t at = events_.size() - 1;
  if (!id.empty()) {
    bool found = false;
    for (size_t i = events_.size(); i-- > 0;) {
      if (events_[i].id == id) { at = i; found = true; break; }
    }
    if (!found) return failure("no grades recorded for " + id);
  }
  id = events_[at].id;
  events_.erase(events_.begin() + static_cast<long>(at));
  rebuild();
  dirty_ = true;
  return Json::object().set("ok", true).set("id", id);
}

Json Engine::cmdCard(const Json& r, int date) {
  const int index = resources_.indexOf(r["id"].asStr());
  if (index < 0) return failure("unknown question: " + r["id"].asStr());
  return Json::object().set("ok", true).set("card", detail(index, date));
}

Json Engine::cmdLibrary(const Json& r, int date) {
  const std::string kind = r["kind"].asStr();      // "problem", "card", or empty for all
  const std::string deck = r["deck"].asStr();
  const std::string status = r["status"].asStr();  // a track name or "due"
  const int pattern = r.has("pattern") ? resources_.patternIndex(r["pattern"].asStr()) : -1;
  const bool curatedOnly = r["curated"].asBool(false);
  std::string search = r["q"].asStr();
  std::transform(search.begin(), search.end(), search.begin(), [](unsigned char c) { return std::tolower(c); });
  if (!search.empty() && search[0] == '#') search.erase(0, 1);

  const int today = clock_.day(date);
  const int company = settings_.company.empty() ? -1 : resources_.companyIndex(settings_.company);
  const auto& questions = resources_.questions();
  std::vector<int> rows;
  for (int i = 0; i < static_cast<int>(questions.size()); ++i) {
    const Question& q = questions[i];
    const CardState& s = states_[i];
    if (kind == "problem" && q.kind != Kind::Problem) continue;
    if (kind == "card" && q.kind == Kind::Problem) continue;
    if (!deck.empty() && q.deck != deck) continue;
    if (curatedOnly && q.tier == 0) continue;
    if (r.has("pattern") && std::find(q.patterns.begin(), q.patterns.end(), pattern) == q.patterns.end()) continue;
    if (status == "due") { if (!isDue(s, today)) continue; }
    else if (!status.empty() && status != trackName(s.track)) continue;
    if (!search.empty()) {
      std::string title = q.title;
      std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) { return std::tolower(c); });
      if (title.find(search) == std::string::npos && std::to_string(q.number) != search) continue;
    }
    rows.push_back(i);
  }

  // Your list first, then what gets asked most.
  std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) {
    const Question& x = questions[a];
    const Question& y = questions[b];
    if ((x.kind == Kind::Problem) != (y.kind == Kind::Problem)) return x.kind == Kind::Problem;
    const int tx = x.tier ? x.tier : 9, ty = y.tier ? y.tier : 9;
    if (tx != ty) return tx < ty;
    return resources_.frequency(a, company) > resources_.frequency(b, company);
  });

  const int offset = std::max(0, r["offset"].asInt(0));
  const int limit = std::min(500, std::max(1, r["limit"].asInt(100)));
  Json list = Json::array();
  for (int k = offset; k < static_cast<int>(rows.size()) && k < offset + limit; ++k) list.push(view(rows[k], date));
  return Json::object().set("ok", true).set("total", static_cast<int>(rows.size())).set("rows", list);
}

Json Engine::cmdFacets(int date) {
  const int today = clock_.day(date);
  Json patterns = Json::array();
  for (const Pattern& p : resources_.patterns()) {
    patterns.push(Json::object().set("slug", p.slug).set("name", p.name).set("algo", p.algorithm));
  }
  Json decks = Json::array();
  for (const Deck& d : resources_.decks()) {
    int seen = 0, retired = 0, dueNow = 0;
    for (int i : d.cards) {
      seen += states_[i].attempts > 0;
      retired += states_[i].track == Track::Retired;
      dueNow += isDue(states_[i], today);
    }
    bool off = std::find(settings_.decksOff.begin(), settings_.decksOff.end(), d.slug) != settings_.decksOff.end();
    decks.push(Json::object().set("slug", d.slug).set("title", d.title).set("cards", static_cast<int>(d.cards.size()))
                   .set("seen", seen).set("retired", retired).set("due", dueNow).set("off", off));
  }
  Json companies = Json::array();
  for (const Company& c : resources_.companies()) companies.push(Json::object().set("slug", c.slug).set("name", c.name));
  return Json::object().set("ok", true).set("patterns", patterns).set("decks", decks)
      .set("companies", companies).set("questions", static_cast<int>(resources_.questions().size()));
}

Json Engine::cmdSession(const Json& r, int date) {
  /*
  Study one deck or pattern on demand, the way you would open a set on a
  flashcard site. Due cards first, then cards you have not seen.
  */
  const std::string deck = r["deck"].asStr();
  const int pattern = r.has("pattern") ? resources_.patternIndex(r["pattern"].asStr()) : -1;
  const int count = std::min(100, std::max(1, r["n"].asInt(20)));
  const int today = clock_.day(date);
  const auto& questions = resources_.questions();

  std::vector<int> due, unseen;
  for (int i = 0; i < static_cast<int>(questions.size()); ++i) {
    const Question& q = questions[i];
    if (!deck.empty() && q.deck != deck) continue;
    if (pattern >= 0 && std::find(q.patterns.begin(), q.patterns.end(), pattern) == q.patterns.end()) continue;
    if (deck.empty() && q.kind == Kind::Problem) continue;
    if (isDue(states_[i], today)) due.push_back(i);
    else if (states_[i].track == Track::New) unseen.push_back(i);
  }
  std::sort(due.begin(), due.end(), [&](int a, int b) {
    if (states_[a].due == states_[b].due && states_[a].track == states_[b].track) return a < b;
    return servedBefore(states_[a], states_[b], today);
  });
  Rng rng(mixSeed(static_cast<uint64_t>(date), 7));
  for (int i = static_cast<int>(unseen.size()) - 1; i > 0; --i) std::swap(unseen[i], unseen[rng.below(i + 1)]);

  Json items = Json::array();
  int added = 0;
  auto add = [&](const std::vector<int>& list, const char* slot) {
    for (int i : list) {
      if (added >= count) return;
      Json v = view(i, date);
      v.set("slot", slot).set("why", "");
      items.push(std::move(v));
      ++added;
    }
  };
  add(due, "review");
  add(unseen, "new");
  return Json::object().set("ok", true).set("items", items);
}

Json Engine::cmdMock(const Json& r, int date) {
  // New faces only, no labels shown, one clock. The UI runs the timer.
  const int today = clock_.day(date);
  const Profile p = profile(today);
  std::vector<char> taken = takenSet();
  // The queue's picks stay out of the mock, but your reviews are irrelevant here.
  Rng rng(mixSeed(static_cast<uint64_t>(date), 500 + static_cast<uint64_t>(r["roll"].asInt())));
  RecommendContext context = contextFor(Lane::Code, today, p, taken);
  context.seeds.clear();          // no sibling priming: a mock points nowhere
  context.slotOffset = 1;         // Weak first, then Wildcard
  context.mock = true;            // a real interview doesn't follow your coach
  Json items = Json::array();
  for (const Pick& pick : recommend(Lane::Code, MOCK_PROBLEMS, context, rng)) {
    Json v = view(pick.question, date);
    v.set("slot", pick.badge()).set("why", pick.why);
    items.push(std::move(v));
  }
  return Json::object().set("ok", true).set("minutes", MOCK_MINUTES).set("items", items);
}

Json Engine::cmdInsights(int date) {
  const int today = clock_.day(date);
  const Profile p = profile(today);
  const auto& patterns = resources_.patterns();

  std::vector<int> seen(patterns.size(), 0), total(patterns.size(), 0);
  for (size_t i = 0; i < resources_.questions().size(); ++i) {
    const Question& q = resources_.questions()[i];
    if (q.kind != Kind::Problem) continue;
    for (int p : q.patterns) {
      if (p >= static_cast<int>(patterns.size())) continue;
      ++total[p];
      seen[p] += states_[i].attempts > 0;
    }
  }

  auto evidence = [](const Evidence& e) {
    Json j = Json::object().set("n", std::round(e.total * 10) / 10);
    if (e.total > 0) j.set("rate", std::round(e.rate() * 100) / 100);
    return j;
  };

  std::vector<int> order;
  for (int i = 0; i < static_cast<int>(patterns.size()); ++i) {
    if (p.patterns[i].attempts > 0) order.push_back(i);
  }
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    if (p.patterns[a].attempts != p.patterns[b].attempts) return p.patterns[a].attempts > p.patterns[b].attempts;
    return a < b;
  });

  Json rows = Json::array();
  for (int i : order) {
    const PatternStats& s = p.patterns[i];
    rows.push(Json::object().set("slug", patterns[i].slug).set("name", patterns[i].name)
                  .set("attempts", s.attempts).set("mastery", std::round(s.mastery() * 100) / 100)
                  .set("cold", evidence(s.cold)).set("review", evidence(s.review))
                  .set("drill", evidence(s.drill)).set("flash", evidence(s.flash))
                  .set("memorizing", s.hasMemorizeSignal())
                  .set("seen", seen[i]).set("total", total[i]));
  }

  /*
  What your judged code shows most: each flaw, and the thinking side. Empty
  until the coach has judged code or you have missed a drill card.
  */
  const auto share = p.needShare();
  std::vector<int> needed;
  for (int n = 0; n < NEED_COUNT; ++n) {
    if (p.needs[n] > 0) needed.push_back(n);
  }
  std::stable_sort(needed.begin(), needed.end(), [&](int a, int b) { return share[a] > share[b]; });
  Json needs = Json::array();
  for (int n : needed) {
    Json row = Json::object().set("slug", n == THINKING_NEED ? "thinking" : FLAWS[n].slug).set("label", needLabel(n))
                   .set("share", std::round(share[n] * 100) / 100);
    if (n < FLAW_COUNT) row.set("count", p.flawCounts[n]);
    needs.push(std::move(row));
  }

  /*
  Plain sentences for the top of the page. Each one needs enough evidence
  to be worth saying, so a new user sees none of them.
  */
  Json notes = Json::array();
  for (int n : needed) {
    if (n == THINKING_NEED || p.flawCounts[n] == 0) continue;
    notes.push(Json::object().set("kind", "flaw").set("text",
        std::string("The slip your coach sees most: ") + needLabel(n) + ", in " + std::to_string(p.flawCounts[n]) +
        " of " + std::to_string(p.judgedWithCode) + " judged attempts with code."));
    break;
  }
  for (int i : order) {
    const PatternStats& s = p.patterns[i];
    const int review = static_cast<int>(std::round(s.review.rate() * 100));
    const int cold = static_cast<int>(std::round(s.cold.rate() * 100));
    if (s.hasMemorizeSignal()) {
      notes.push(Json::object().set("kind", "memorize").set("text",
          patterns[i].name + ": " + std::to_string(review) + "% on reviews, " + std::to_string(cold) +
          "% on new faces. You may be memorizing answers."));
    }
    const double know = s.flash.rate();
    if (s.flash.total >= 1.0 && s.cold.total >= MEMORIZE_MIN_EVIDENCE && know - s.cold.rate() >= MEMORIZE_MIN_GAP) {
      notes.push(Json::object().set("kind", "explain").set("text",
          patterns[i].name + ": " + std::to_string(static_cast<int>(std::round(know * 100))) +
          "% on cards, " + std::to_string(cold) + "% coding it cold. The idea is there, the code is not."));
    }
  }
  return Json::object().set("ok", true).set("patterns", rows).set("needs", needs).set("notes", notes)
      .set("graded", static_cast<int>(events_.size()));
}

Json Engine::cmdSimilar(const Json& r, int date) {
  const int index = resources_.indexOf(r["id"].asStr());
  if (index < 0) return failure("unknown question: " + r["id"].asStr());
  const int count = std::min(50, std::max(1, r["n"].asInt(8)));
  const bool unseenOnly = r["unseen"].asBool(false);
  const auto& questions = resources_.questions();

  std::vector<std::pair<double, int>> scored;
  for (int i = 0; i < static_cast<int>(questions.size()); ++i) {
    if (i == index || questions[i].kind != questions[index].kind) continue;
    if (unseenOnly && states_[i].attempts > 0) continue;
    if (!settings_.premium && questions[i].premium) continue;
    double s = resources_.similarity(index, i);
    if (s > 0) scored.emplace_back(-s, i);
  }
  const size_t top = std::min(scored.size(), static_cast<size_t>(count));
  std::partial_sort(scored.begin(), scored.begin() + static_cast<long>(top), scored.end());
  Json rows = Json::array();
  for (size_t k = 0; k < top; ++k) {
    Json v = view(scored[k].second, date);
    v.set("similarity", std::round(-scored[k].first * 100) / 100);
    rows.push(std::move(v));
  }
  return Json::object().set("ok", true).set("rows", rows);
}

Json Engine::cmdExplain(const Json& r) {
  const std::string& id = r["id"].asStr();
  const int index = resources_.indexOf(id);
  if (index < 0) return failure("unknown question: " + id);
  const Schedule& schedule = scheduleOf(index);
  CardState s;
  Json steps = Json::array();
  for (const Event& e : events_) {
    if (e.id != id) continue;
    const int day = clock_.day(e.date);
    const Click click = effectiveClick(s, e, day, schedule);
    const Move move = schedule.rules[static_cast<int>(s.track)][static_cast<int>(click)];
    const std::string from = describe(s, schedule);
    s = applyClick(s, click, day, schedule, seedOf(id));
    Json step = Json::object().set("date", formatDate(e.date)).set("day", day).set("click", clickName(e.click))
                    .set("from", from).set("move", moveName(move)).set("to", describe(s, schedule))
                    .set("due", s.track == Track::Retired ? Json() : Json(s.due));
    if (click != e.click) step.set("coach", clickName(click));   // the coach's stricter grade kept it going
    steps.push(std::move(step));
  }
  return Json::object().set("ok", true).set("id", id).set("steps", steps);
}

Json Engine::cmdSimulate(const Json& r) {
  /*
  Walks a problem through a list of clicks, each done the day it falls due, or
  a card with "kind":"card". "id" picks whose extra days to draw.
  */
  const Schedule& schedule = r["kind"].asStr() == "card" ? CARD_SCHEDULE : PROBLEM_SCHEDULE;
  const uint64_t seed = seedOf(r["id"].asStr());
  CardState s;
  int day = 1;
  Json steps = Json::array();
  for (const Json& c : r["clicks"].items()) {
    Click click;
    if (!parseClick(c.asStr(), &click)) return failure("clicks must be b, y or r");
    const Move move = schedule.rules[static_cast<int>(s.track)][static_cast<int>(click)];
    const std::string from = describe(s, schedule);
    s = applyClick(s, click, day, schedule, seed);
    Json step = Json::object().set("day", day).set("click", clickName(click)).set("from", from)
                    .set("move", moveName(move)).set("to", describe(s, schedule));
    if (s.track != Track::Retired) {
      step.set("gap", s.due - day);
      day = s.due;
    }
    steps.push(std::move(step));
  }
  return Json::object().set("ok", true).set("steps", steps);
}

Json Engine::cmdRules() {
  // Each track's checks as {base, spread}, and the click table, for one schedule.
  auto described = [](const Schedule& schedule) {
    Json checks = Json::object();
    for (Track track : {Track::Red, Track::Yellow, Track::Blue}) {
      Json list = Json::array();
      for (int k = 0; k < checkCount(schedule, track); ++k) {
        const Gap gap = checkGap(schedule, track, k);
        list.push(Json::object().set("base", gap.base).set("spread", gap.spread));
      }
      checks.set(trackName(track), list);
    }
    Json table = Json::object();
    const char* tracks[] = {"new", "red", "yellow", "blue", "retired"};
    for (int t = 0; t < 5; ++t) {
      table.set(tracks[t], Json::object().set("b", moveName(schedule.rules[t][0])).set("y", moveName(schedule.rules[t][1]))
                               .set("r", moveName(schedule.rules[t][2])));
    }
    return Json::object().set("checks", checks).set("rules", table).set("blueAfterLast", schedule.blueAfterLast);
  };
  return Json::object().set("ok", true)
      .set("problems", described(PROBLEM_SCHEDULE)).set("cards", described(CARD_SCHEDULE))
      .set("clock", settings_.clock == ClockMode::CalendarDays ? "calendar" : "study")
      .set("rollover", DAY_ROLLOVER_HOUR)
      .set("codeCap", settings_.codeCap).set("codeNew", settings_.codeNew).set("codeReviews", settings_.codeReviews)
      .set("cardCap", settings_.cardCap).set("cardNew", settings_.cardNew)
      .set("judgementsFile", JUDGEMENTS_FILE).set("oldJudgementsFile", OLD_JUDGEMENTS_FILE)
      .set("lastReviewFile", LAST_REVIEW_FILE);
}

Json Engine::cmdSettings(const Json& r) {
  const Json& s = r["set"];
  if (s.isObject()) {
    bool changed = false;
    auto setInt = [&](const char* key, int low, int high, int& field) {
      if (!s.has(key)) return;
      const int value = std::min(high, std::max(low, s[key].asInt(field)));
      changed = changed || value != field;
      field = value;
    };
    setInt("codeCap", 1, 50, settings_.codeCap);
    setInt("codeNew", 0, 20, settings_.codeNew);
    setInt("codeReviews", 0, 50, settings_.codeReviews);
    setInt("cardCap", 1, 200, settings_.cardCap);
    setInt("cardNew", 0, 100, settings_.cardNew);
    if (s.has("company")) { settings_.company = s["company"].asStr(); changed = true; }
    if (s.has("premium")) { settings_.premium = s["premium"].asBool(false); changed = true; }
    if (s.has("clock")) {
      settings_.clock = s["clock"].asStr() == "calendar" ? ClockMode::CalendarDays : ClockMode::StudyDays;
      changed = true;
    }
    if (s.has("decksOff")) {
      settings_.decksOff.clear();
      for (const Json& d : s["decksOff"].items()) settings_.decksOff.push_back(d.asStr());
      changed = true;
    }
    if (changed) {
      rebuild();
      plan_.date = -1;  // rebuilt on the next "today", keeping what is done and the new faces still waiting
      dirty_ = true;
    }
  }
  Json off = Json::array();
  for (const auto& d : settings_.decksOff) off.push(d);
  return Json::object().set("ok", true).set("settings", Json::object()
      .set("codeCap", settings_.codeCap).set("codeNew", settings_.codeNew).set("codeReviews", settings_.codeReviews)
      .set("cardCap", settings_.cardCap).set("cardNew", settings_.cardNew)
      .set("clock", settings_.clock == ClockMode::CalendarDays ? "calendar" : "study")
      .set("company", settings_.company).set("premium", settings_.premium).set("decksOff", off));
}

Json Engine::cmdHide(const Json& r, int date) {
  const std::string& id = r["id"].asStr();
  const int index = resources_.indexOf(id);
  if (index < 0) return failure("unknown question: " + id);
  hidden_.insert(id);
  ensurePlan(date);
  const Lane lane = resources_.questions()[index].kind == Kind::Problem ? Lane::Code : Lane::Cards;
  auto& items = lane == Lane::Code ? plan_.code : plan_.cards;
  auto it = std::find_if(items.begin(), items.end(), [&](const PlanItem& item) { return item.id == id; });
  if (it != items.end()) {
    const size_t position = static_cast<size_t>(it - items.begin());
    items.erase(it);
    // Fill the hole with another new face in the same place.
    plan_.pulls += 1;
    const int today = clock_.day(date);
    const Profile p = profile(today);
    std::vector<char> taken = takenSet();
    Rng rng(mixSeed(static_cast<uint64_t>(date), 100 + static_cast<uint64_t>(plan_.pulls)));
    RecommendContext context = contextFor(lane, today, p, taken);
    context.slotOffset = today + plan_.pulls;
    auto picks = recommend(lane, 1, context, rng);
    if (!picks.empty()) {
      const Question& q = resources_.questions()[picks[0].question];
      items.insert(items.begin() + static_cast<long>(position), PlanItem{q.id, picks[0].badge(), picks[0].why});
    }
  }
  dirty_ = true;
  return cmdToday(date);
}

Json Engine::cmdImport(const Json& r) {
  // Reads the older tracker's export: [{"id":128,"log":[{"d":"...","g":"r"}]}]
  int added = 0, skipped = 0;
  for (const Json& entry : r["problems"].items()) {
    const std::string id = "lc" + std::to_string(entry["id"].asInt());
    for (const Json& log : entry["log"].items()) {
      Event e;
      e.id = id;
      e.date = parseDate(log["d"].asStr());
      if (e.date < 0 || !parseClick(log["g"].asStr(), &e.click)) { ++skipped; continue; }
      const bool exists = std::any_of(events_.begin(), events_.end(), [&](const Event& x) {
        return x.id == e.id && x.date == e.date;
      });
      if (exists) { ++skipped; continue; }
      events_.push_back(std::move(e));
      ++added;
    }
  }
  if (added) {
    rebuild();
    plan_.date = -1;  // rebuilt on the next "today", keeping the new faces still waiting
    dirty_ = true;
  }
  return Json::object().set("ok", true).set("added", added).set("skipped", skipped);
}

Json Engine::cmdRecommend(const Json& r, int date) {
  // A dry run: shows what would be picked and changes nothing.
  const Lane lane = r["lane"].asStr() == "cards" ? Lane::Cards : Lane::Code;
  const int today = clock_.day(date);
  const Profile p = profile(today);
  std::vector<char> taken = takenSet();
  Rng rng(mixSeed(static_cast<uint64_t>(date), 900 + static_cast<uint64_t>(r["roll"].asInt())));
  RecommendContext context = contextFor(lane, today, p, taken);
  Json rows = Json::array();
  for (const Pick& pick : recommend(lane, std::min(50, std::max(1, r["n"].asInt(5))), context, rng)) {
    Json v = view(pick.question, date);
    v.set("slot", pick.badge()).set("why", pick.why);
    rows.push(std::move(v));
  }
  return Json::object().set("ok", true).set("rows", rows);
}

/*
── Attempts ──────────────────────────────────────────────────────────────
The core decides whether an attempt counts and what it is called. The front
end writes the file, because the core has no file system in a browser.
*/

/*
"2026-11-06T21:30" as a file name. Only the date in `now` has been checked,
so anything but digits and the T becomes a dash, which keeps the rest of the
string out of the path.
*/
Json Engine::cmdAttempt(const Json& r, int date) {
  const std::string& id = r["id"].asStr();
  const int index = resources_.indexOf(id);
  if (index < 0) return failure("unknown question: " + id);
  const Question& q = resources_.questions()[index];
  if (q.kind != Kind::Problem) return failure(id + " is a card. Attempts are for coding problems.");

  const std::string& code = r["code"].asStr();
  if (code.find_first_not_of(" \t\r\n") == std::string::npos) return failure("Nothing to save, the code is empty.");
  if (code.find('\0') != std::string::npos) return failure("That isn't text, so it can't be saved as code.");
  if (static_cast<int>(code.size()) > ATTEMPT_MAX_BYTES) {
    return failure("That's " + std::to_string(code.size() / 1024) + " KB, too long to be a solution. The limit is " +
                   std::to_string(ATTEMPT_MAX_BYTES / 1024) + " KB.");
  }

  /*
  One file per problem per study day, with every paste in order. The front end
  sends the day's file as it stands and appends `block` to it, so a failed
  read can never cost an earlier paste.
  */
  const std::string& existing = r["existing"].asStr();
  int pastes = 0;
  size_t lastStart = std::string::npos;   // where the last paste's code begins
  for (size_t at = 0; at < existing.size();) {
    size_t end = existing.find('\n', at);
    if (end == std::string::npos) end = existing.size();
    if (existing.compare(at, std::strlen(PASTE_HEADER), PASTE_HEADER) == 0) {
      ++pastes;
      lastStart = std::min(end + 1, existing.size());
    }
    at = end + 1;
  }
  const std::string stem = std::string(ATTEMPT_FOLDER) + "/" + id + "/" + formatDate(date);
  auto trimmed = [](std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    return text;
  };

  // A second press of the same paste adds nothing.
  if (lastStart != std::string::npos && trimmed(existing.substr(lastStart)) == trimmed(code)) {
    return Json::object().set("ok", true).set("id", id).set("stem", stem).set("ext", ATTEMPT_EXTENSION)
        .set("paste", pastes).set("block", "").set("note", "Same as your last paste, so nothing was added.");
  }

  // Only a clock time from "now" goes in the file, and only the date in the path.
  const std::string& now = r["now"].asStr();
  std::string clock = now.size() >= 16 ? now.substr(11, 5) : "";
  const auto digit = [&](size_t k) { return std::isdigit(static_cast<unsigned char>(clock[k])) != 0; };
  if (clock.size() != 5 || !digit(0) || !digit(1) || clock[2] != ':' || !digit(3) || !digit(4)) clock = "--:--";

  std::string block;
  if (!existing.empty()) block += existing.back() == '\n' ? "\n" : "\n\n";
  block += std::string(PASTE_HEADER) + std::to_string(pastes + 1) + " at " + clock + " ──\n";
  block += code;
  if (code.back() != '\n') block += '\n';

  const int lines = static_cast<int>(std::count(code.begin(), code.end(), '\n')) + (code.back() == '\n' ? 0 : 1);
  return Json::object().set("ok", true).set("id", id).set("stem", stem).set("ext", ATTEMPT_EXTENSION)
      .set("paste", pastes + 1).set("block", block)
      .set("note", "Saved paste " + std::to_string(pastes + 1) + " of the day, " + std::to_string(lines) +
                       (lines == 1 ? " line, on " : " lines, on ") + q.title + ".");
}

/*
── Code diff for typed drills ────────────────────────────────────────────
Compares tokens, so spacing and line breaks never count against you.
*/

static std::string stripComments(const std::string& code) {
  std::string out;
  for (size_t i = 0; i < code.size(); ++i) {
    if (code[i] == '/' && i + 1 < code.size() && code[i + 1] == '/') {
      while (i < code.size() && code[i] != '\n') ++i;
      out += '\n';
    } else if (code[i] == '/' && i + 1 < code.size() && code[i + 1] == '*') {
      i += 2;
      while (i + 1 < code.size() && !(code[i] == '*' && code[i + 1] == '/')) ++i;
      ++i;
    } else {
      out += code[i];
    }
  }
  return out;
}

static std::vector<std::string> tokenize(const std::string& code) {
  std::vector<std::string> tokens;
  const std::string text = stripComments(code);
  auto isWord = [](unsigned char c) { return std::isalnum(c) || c == '_'; };
  for (size_t i = 0; i < text.size();) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (std::isspace(c)) { ++i; continue; }
    if (isWord(c)) {
      size_t j = i;
      while (j < text.size() && isWord(static_cast<unsigned char>(text[j]))) ++j;
      tokens.push_back(text.substr(i, j - i));
      i = j;
    } else {
      tokens.push_back(std::string(1, text[i]));
      ++i;
    }
  }
  return tokens;
}

Json Engine::cmdDiff(const Json& r) {
  std::vector<std::string> typed = tokenize(r["typed"].asStr());
  std::vector<std::string> reference = tokenize(r["reference"].asStr());
  if (typed.size() > 3000) typed.resize(3000);
  if (reference.size() > 3000) reference.resize(3000);
  const size_t n = typed.size(), m = reference.size();

  // Longest common subsequence over tokens.
  std::vector<std::vector<uint16_t>> lcs(n + 1, std::vector<uint16_t>(m + 1, 0));
  for (size_t i = n; i-- > 0;) {
    for (size_t j = m; j-- > 0;) {
      lcs[i][j] = typed[i] == reference[j] ? static_cast<uint16_t>(lcs[i + 1][j + 1] + 1)
                                           : std::max(lcs[i + 1][j], lcs[i][j + 1]);
    }
  }

  // Walk it, merging runs. Status 0 = match, 1 = you missed it, 2 = you added it.
  Json ops = Json::array();
  std::string run;
  int runStatus = -1;
  auto emit = [&](const std::string& token, int status) {
    if (status != runStatus && runStatus >= 0) {
      ops.push(Json::object().set("s", runStatus).set("t", run));
      run.clear();
    }
    runStatus = status;
    if (!run.empty()) run += ' ';
    run += token;
  };
  size_t i = 0, j = 0;
  while (i < n || j < m) {
    if (i < n && j < m && typed[i] == reference[j]) { emit(typed[i], 0); ++i; ++j; }
    else if (j < m && (i == n || lcs[i][j + 1] >= lcs[i + 1][j])) { emit(reference[j], 1); ++j; }
    else { emit(typed[i], 2); ++i; }
  }
  if (runStatus >= 0) ops.push(Json::object().set("s", runStatus).set("t", run));

  const double score = n + m == 0 ? 0 : 2.0 * lcs[0][0] / static_cast<double>(n + m);
  const char* suggest = score >= 0.999 ? "b" : score >= 0.9 ? "y" : "r";
  return Json::object().set("ok", true).set("score", std::round(score * 1000) / 1000)
      .set("suggest", suggest).set("ops", ops);
}

// ── The one door ──────────────────────────────────────────────────────────

std::string Engine::call(std::string_view request) {
  std::string error;
  Json r = Json::parse(request, &error);
  if (!r.isObject()) return failure("request is not valid JSON: " + error).dump();
  if (!resourcesLoaded_) return failure("the resources are not loaded").dump();

  const std::string& cmd = r["cmd"].asStr();
  int date = -1;
  if (r.has("now")) {
    date = dateFromNow(r["now"].asStr());
    if (date < 0) return failure("now must look like 2026-11-06T21:30").dump();
  }

  auto needsDate = [&](Json (Engine::*handler)(int)) {
    return date < 0 ? failure("this command needs \"now\"") : (this->*handler)(date);
  };
  auto needsDateAnd = [&](Json (Engine::*handler)(const Json&, int)) {
    return date < 0 ? failure("this command needs \"now\"") : (this->*handler)(r, date);
  };

  Json response;
  if (cmd == "today") response = needsDate(&Engine::cmdToday);
  else if (cmd == "more") response = needsDateAnd(&Engine::cmdMore);
  else if (cmd == "grade") response = needsDateAnd(&Engine::cmdGrade);
  else if (cmd == "undo") response = cmdUndo(r);
  else if (cmd == "card") response = needsDateAnd(&Engine::cmdCard);
  else if (cmd == "library") response = needsDateAnd(&Engine::cmdLibrary);
  else if (cmd == "facets") response = needsDate(&Engine::cmdFacets);
  else if (cmd == "session") response = needsDateAnd(&Engine::cmdSession);
  else if (cmd == "mock") response = needsDateAnd(&Engine::cmdMock);
  else if (cmd == "insights") response = needsDate(&Engine::cmdInsights);
  else if (cmd == "similar") response = needsDateAnd(&Engine::cmdSimilar);
  else if (cmd == "explain") response = cmdExplain(r);
  else if (cmd == "simulate") response = cmdSimulate(r);
  else if (cmd == "rules") response = cmdRules();
  else if (cmd == "settings") response = cmdSettings(r);
  else if (cmd == "hide") response = needsDateAnd(&Engine::cmdHide);
  else if (cmd == "import") response = cmdImport(r);
  else if (cmd == "recommend") response = needsDateAnd(&Engine::cmdRecommend);
  else if (cmd == "diff") response = cmdDiff(r);
  else if (cmd == "attempt") response = needsDateAnd(&Engine::cmdAttempt);
  else if (cmd == "overview") response = needsDateAnd(&Engine::cmdOverview);
  else if (cmd == "pending") response = needsDateAnd(&Engine::cmdPending);
  else if (cmd == "judgement") response = needsDateAnd(&Engine::cmdJudgement);
  else if (cmd == "drop") response = needsDateAnd(&Engine::cmdDrop);
  else if (cmd == "commit") response = needsDateAnd(&Engine::cmdCommit);
  else if (cmd == "judgements") response = cmdJudgements(r);
  else if (cmd == "search") response = cmdSearch(r);
  else if (cmd == "topics") response = cmdTopics();
  else response = failure("unknown command: " + cmd);

  response.set("dirty", dirty_);
  return response.dump();
}

}  // namespace interviewprep
