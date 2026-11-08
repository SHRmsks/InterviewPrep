/*
engine.hpp — the one door into the core.

The CLI, the website and the Mac app all talk to the core the same way: send
a JSON command, get JSON back. No front end contains a rule, so behavior
cannot drift between them.

  {"cmd":"today","now":"2026-11-06T21:30"}
  {"cmd":"grade","now":"2026-11-06T21:40","id":"lc128","grade":"r"}

Every command is listed in Engine::call in engine.cpp.
*/
#pragma once

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "resources.hpp"
#include "json.hpp"
#include "profile.hpp"
#include "recommend.hpp"
#include "schedule.hpp"

namespace interviewprep {

struct Settings {
  int codeCap = CODE_CAP;
  int codeNew = CODE_NEW;
  int codeReviews = CODE_REVIEWS;
  int cardCap = CARD_CAP;
  int cardNew = CARD_NEW;
  ClockMode clock = DEFAULT_CLOCK;
  std::string company;                // target company slug, empty for any
  bool premium = false;               // recommend LeetCode Premium problems
  std::vector<std::string> decksOff;  // decks left out of the daily queue
};

// One line of a day's queue.
struct PlanItem {
  std::string id;
  std::string slot;   // "review", "new-pattern", "new-topic", "practice", or a card's slot
  std::string why;
};

// The queue for one day. Saved, so it stays put when you reopen the app.
struct Plan {
  int date = -1;
  int pulls = 0;                      // times you asked for more today
  std::vector<PlanItem> code;
  std::vector<PlanItem> cards;
};

class Engine {
 public:
  // Returns the number of questions, or -1 with `error` filled.
  int loadResources(std::string_view json, std::string* error = nullptr);
  bool loadState(std::string_view json, std::string* error = nullptr);
  std::string saveState();

  /*
  Runs one command. Always returns a JSON object with an "ok" field. When
  "dirty" is true, the front end should persist saveState().
  */
  std::string call(std::string_view request);

 private:
  Resources resources_;
  bool resourcesLoaded_ = false;
  Settings settings_;
  std::vector<Event> events_;
  std::set<std::string> hidden_;      // never recommend these
  Plan plan_;
  JudgedMap judged_;                  // the coach's judgements, set by the front end. Never saved.

  // Derived from events_ by rebuild(). Never saved.
  Clock clock_;
  std::vector<CardState> states_;     // same order as resources_.questions()
  bool dirty_ = false;

  void rebuild();
  const Schedule& scheduleOf(int index) const;
  Click effectiveClick(const CardState& state, const Event& event, int day, const Schedule& schedule) const;
  Profile profile(int today) const;
  std::vector<Seed> seedsFor(Lane lane, int today) const;
  RecommendContext contextFor(Lane lane, int today, const Profile& profile, std::vector<char>& taken) const;
  std::vector<char> takenSet() const;

  void ensurePlan(int date);
  void fillLane(Lane lane, int date, int cap, int wantNew, int minNew, int maxReviews, const std::vector<PlanItem>& before,
                std::vector<PlanItem>& out, const Profile& profile, std::vector<char>& taken, Rng& rng);

  Json view(int index, int date) const;
  Json detail(int index, int date) const;
  Json planView(const std::vector<PlanItem>& items, int date) const;
  Json laneCounts(Lane lane, int today) const;
  bool inLane(const Question& q, Lane lane) const;

  Json cmdToday(int date);
  Json cmdMore(const Json& request, int date);
  Json cmdGrade(const Json& request, int date);
  Json cmdUndo(const Json& request);
  Json cmdCard(const Json& request, int date);
  Json cmdLibrary(const Json& request, int date);
  Json cmdFacets(int date);
  Json cmdSession(const Json& request, int date);
  Json cmdMock(const Json& request, int date);
  Json cmdInsights(int date);
  Json cmdSimilar(const Json& request, int date);
  Json cmdExplain(const Json& request);
  Json cmdSimulate(const Json& request);
  Json cmdRules();
  Json cmdSettings(const Json& request);
  Json cmdHide(const Json& request, int date);
  Json cmdImport(const Json& request);
  Json cmdRecommend(const Json& request, int date);
  Json cmdDiff(const Json& request);
  Json cmdAttempt(const Json& request, int date);

  // The coach, in coach.cpp.
  struct Waiting {
    size_t event;     // into events_
    int question;     // into resources_.questions()
    bool firstTry;
  };
  std::vector<Waiting> waitingForJudgement(const std::set<std::string>& judged) const;
  Json cmdOverview(const Json& request, int date);
  Json cmdPending(const Json& request, int date);
  Json cmdJudgement(const Json& request, int date);
  Json cmdDrop(const Json& request, int date);
  Json cmdCommit(const Json& request, int date);
  Json cmdJudgements(const Json& request);
  Json cmdSearch(const Json& request);
  Json cmdTopics();
};

}  // namespace interviewprep
