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

#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "resources.hpp"
#include "json.hpp"
#include "profile.hpp"
#include "recommend.hpp"
#include "schedule.hpp"

using namespace std;

namespace interviewprep {

struct Settings {
  int codeCap = CODE_CAP;
  int codeNew = CODE_NEW;
  int codeReviews = CODE_REVIEWS;
  int cardCap = CARD_CAP;
  int cardNew = CARD_NEW;
  ClockMode clock = DEFAULT_CLOCK;
  string company;                // target company slug, empty for any
  bool premium = false;               // recommend LeetCode Premium problems
  // Decks left out of the daily queue. The coding decks start there (section 5 of config.hpp).
  vector<string> decksOff = {begin(CODING_DECKS), end(CODING_DECKS)};
  string interviewDate;          // "2026-12-15", or empty for none: sets the exploring curve (section 7)
};

// One line of a day's queue.
struct PlanItem {
  string id;
  string slot;   // "review", "new-pattern", "new-topic", "practice", or a card's slot
  string why;
};

// The queue for one day. Saved, so it stays put when you reopen the app.
struct Plan {
  int date = -1;
  int pulls = 0;                      // times you asked for more today
  vector<PlanItem> code;
  vector<PlanItem> cards;
};

class Engine {
 public:
  // Returns the number of questions, or -1 with `error` filled.
  int loadResources(string_view json, string* error = nullptr);
  bool loadState(string_view json, string* error = nullptr);
  string saveState();

  /*
  Runs one command. Always returns a JSON object with an "ok" field. When
  "dirty" is true, the front end should persist saveState().
  */
  string call(string_view request);

 private:
  Resources resources_;
  bool resourcesLoaded_ = false;
  Settings settings_;
  vector<Event> events_;
  set<string> hidden_;      // never recommend these
  Plan plan_;
  JudgedMap judged_;                  // the coach's judgements, set by the front end. Never saved.

  // Derived from events_ by rebuild(). Never saved.
  Clock clock_;
  vector<CardState> states_;     // same order as resources_.questions()
  bool dirty_ = false;

  void rebuild();
  const Schedule& scheduleOf(int index) const;
  Click effectiveClick(const CardState& state, const Event& event, int day, const Schedule& schedule) const;
  Profile profile(int today) const;
  vector<Seed> seedsFor(Lane lane, int today) const;
  RecommendContext contextFor(Lane lane, int date, const Profile& profile, vector<char>& taken) const;
  // Today's share of new problems that explore, or -1 with no interview date.
  double exploreToday(int date) const;
  vector<char> takenSet() const;

  void ensurePlan(int date);
  void fillLane(Lane lane, int date, int cap, int wantNew, int minNew, int maxReviews, const vector<PlanItem>& before,
                vector<PlanItem>& out, const Profile& profile, vector<char>& taken, Rng& rng);

  Json view(int index, int date) const;
  Json detail(int index, int date) const;
  Json planView(const vector<PlanItem>& items, int date) const;
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
  Json cmdSettings(const Json& request, int date);
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
  vector<Waiting> waitingForJudgement(const set<string>& judged) const;
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
