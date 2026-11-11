/*
test_engine.cpp — every rule, stated as a test.

Run with: make test

If you change a table in core/config.hpp on purpose, the matching test here
fails and shows what changed. Update the test to say what you want now.
*/

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../core/engine.hpp"

using namespace std;
using namespace interviewprep;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    ++checks;                                                           \
    if (!(cond)) {                                                      \
      ++failures;                                                       \
      printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
    }                                                                   \
  } while (0)

#define CHECK_EQ(a, b)                                                  \
  do {                                                                  \
    ++checks;                                                           \
    auto x = (a);                                                       \
    auto y = (b);                                                       \
    if (!(x == y)) {                                                    \
      ++failures;                                                       \
      printf("FAIL  %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b); \
      print(x);                                                         \
      print(y);                                                         \
    }                                                                   \
  } while (0)

static void print(int v) { printf("      got %d\n", v); }
static void print(double v) { printf("      got %g\n", v); }
static void print(const string& v) { printf("      got %s\n", v.c_str()); }
static void print(Track t) { printf("      got %s\n", trackName(t)); }

static const Click B = Click::Blue, Y = Click::Yellow, R = Click::Red;

// Applies clicks to a card, each on the exact day it falls due.
static CardState walk(initializer_list<Click> clicks) {
  CardState s;
  int day = 1;
  for (Click c : clicks) {
    s = applyClick(s, c, day);
    if (s.track != Track::Retired) day = s.due;
  }
  return s;
}

static void expectPosition(const CardState& s, Track track, int step, int gapFromLastClick) {
  CHECK_EQ(s.track, track);
  CHECK_EQ(s.step, step);
  if (track != Track::Retired) CHECK_EQ(s.due - s.lastDay, gapFromLastClick);
}

// ── Cards, rule 1: Blue ───────────────────────────────────────────────────

static void testBlueExpires() {
  // First Blue: check at day 7.
  expectPosition(walk({B}), Track::Blue, 0, 7);
  // Passed day 7: check at day 15.
  expectPosition(walk({B, B}), Track::Blue, 1, 15);
  // Two consecutive Blues after the first and the card is gone.
  expectPosition(walk({B, B, B}), Track::Retired, 0, 0);
  // Gone means gone: nothing is ever due.
  CHECK(!isDue(walk({B, B, B}), 99999));
}

static void testBlueConsecutiveBreaks() {
  expectPosition(walk({B, Y}), Track::Yellow, 0, 3);
  expectPosition(walk({B, R}), Track::Red, 0, 1);
  expectPosition(walk({B, B, Y}), Track::Yellow, 0, 3);
  expectPosition(walk({B, B, R}), Track::Red, 0, 1);
}

// ── Cards, rule 2: Yellow ─────────────────────────────────────────────────

static void testYellowExpires() {
  expectPosition(walk({Y}), Track::Yellow, 0, 3);          // 3-day check
  expectPosition(walk({Y, B}), Track::Yellow, 1, 7);       // 7-day check
  expectPosition(walk({Y, B, B}), Track::Blue, 0, 7);      // first real Blue
  // And from there it behaves like any first Blue.
  expectPosition(walk({Y, B, B, B}), Track::Blue, 1, 15);
  expectPosition(walk({Y, B, B, B, B}), Track::Retired, 0, 0);
}

static void testYellowFailureGoesRed() {
  // "any failed in 3 day Y becomes R again"
  expectPosition(walk({Y, R}), Track::Red, 0, 1);
  expectPosition(walk({Y, Y}), Track::Red, 0, 1);
  // The 7-day check too.
  expectPosition(walk({Y, B, R}), Track::Red, 0, 1);
  expectPosition(walk({Y, B, Y}), Track::Red, 0, 1);
}

// ── Cards, rule 3: Red ────────────────────────────────────────────────────

static void testRedExpires() {
  expectPosition(walk({R}), Track::Red, 0, 1);              // next day
  expectPosition(walk({R, B}), Track::Red, 1, 3);           // 3-day check
  expectPosition(walk({R, B, B}), Track::Red, 2, 7);        // 7-day check
  expectPosition(walk({R, B, B, B}), Track::Blue, 0, 7);    // first real Blue
  expectPosition(walk({R, B, B, B, B}), Track::Blue, 1, 15);
  expectPosition(walk({R, B, B, B, B, B}), Track::Retired, 0, 0);
}

static void testRedStaysRed() {
  // "next day 3 day 7 day R remains R again": a Red at any check restarts.
  expectPosition(walk({R, R}), Track::Red, 0, 1);
  expectPosition(walk({R, B, R}), Track::Red, 0, 1);
  expectPosition(walk({R, B, B, R}), Track::Red, 0, 1);
}

static void testRedYellowRepeats() {
  /*
  Not stated in the brief. A Yellow on a Red card repeats the same check:
  it does not count as the Blue the check needs, and it does not restart.
  */
  expectPosition(walk({R, Y}), Track::Red, 0, 1);
  expectPosition(walk({R, B, Y}), Track::Red, 1, 3);
  expectPosition(walk({R, B, B, Y}), Track::Red, 2, 7);
}

static void testRetiredCanComeBack() {
  // Retired cards never come back on their own, only if you redo one by hand.
  expectPosition(walk({B, B, B, B}), Track::Retired, 0, 0);
  expectPosition(walk({B, B, B, Y}), Track::Yellow, 0, 3);
  expectPosition(walk({B, B, B, R}), Track::Red, 0, 1);
}

// ── Problems: the schedule chosen on 2026-11-08 ───────────────────────────

// Applies clicks to a problem, each on the exact day it falls due.
static CardState walkProblem(initializer_list<Click> clicks, const string& id = "lc1") {
  CardState s;
  int day = 1;
  for (Click c : clicks) {
    s = applyClick(s, c, day, PROBLEM_SCHEDULE, seedOf(id));
    if (s.track != Track::Retired) day = s.due;
  }
  return s;
}

// Where it stands, with the next check anywhere from `low` to `high` days on.
static void expectProblem(const CardState& s, Track track, int step, int low, int high) {
  CHECK_EQ(s.track, track);
  CHECK_EQ(s.step, step);
  if (track != Track::Retired) {
    CHECK(s.due - s.lastDay >= low);
    CHECK(s.due - s.lastDay <= high);
  }
}

static void testProblemCleanFirstTry() {
  expectProblem(walkProblem({B}), Track::Blue, 0, 10, 20);       // one check after 10 to 20 days
  expectProblem(walkProblem({B, B}), Track::Retired, 0, 0, 0);   // then gone
  CHECK(!isDue(walkProblem({B, B}), 99999));
}

static void testProblemNeededAHint() {
  expectProblem(walkProblem({Y}), Track::Yellow, 0, 5, 10);      // one check after 5 to 10 days
  expectProblem(walkProblem({Y, B}), Track::Retired, 0, 0, 0);
}

static void testProblemMissed() {
  expectProblem(walkProblem({R}), Track::Red, 0, 1, 4);          // after 1 to 4 days
  expectProblem(walkProblem({R, B}), Track::Red, 1, 5, 10);      // then 5 to 10
  expectProblem(walkProblem({R, B, B}), Track::Retired, 0, 0, 0);
}

static void testProblemSlipStartsThatRoutine() {
  // Can't do it at all: the missed routine. Needs a hint: the hint routine.
  for (const CardState& s : {walkProblem({B, R}), walkProblem({Y, R}), walkProblem({R, R}), walkProblem({R, B, R})}) {
    expectProblem(s, Track::Red, 0, 1, 4);
  }
  for (const CardState& s : {walkProblem({B, Y}), walkProblem({Y, Y}), walkProblem({R, Y}), walkProblem({R, B, Y})}) {
    expectProblem(s, Track::Yellow, 0, 5, 10);
  }
  // Gone stays gone unless you redo it by hand.
  expectProblem(walkProblem({B, B, B}), Track::Retired, 0, 0, 0);
  expectProblem(walkProblem({B, B, Y}), Track::Yellow, 0, 5, 10);
  expectProblem(walkProblem({B, B, R}), Track::Red, 0, 1, 4);
}

static void testProblemExtraDaysAreFixedButSpread() {
  // The same problem and grades always give the same day, so a replay agrees with itself.
  CHECK_EQ(walkProblem({B}, "lc76").due, walkProblem({B}, "lc76").due);
  // Across problems the extra days cover the whole spread, so a day's problems come back apart.
  set<int> gaps;
  for (int i = 1; i <= 300; ++i) gaps.insert(walkProblem({B}, "lc" + to_string(i)).due - 1);
  CHECK_EQ(*gaps.begin(), 10);
  CHECK_EQ(*gaps.rbegin(), 20);
  CHECK_EQ(static_cast<int>(gaps.size()), 11);
  // Each check draws again: the second check of a miss isn't tied to the first.
  set<pair<int, int>> pairs;
  for (int i = 1; i <= 300; ++i) {
    const CardState first = walkProblem({R}, "lc" + to_string(i));
    const CardState second = walkProblem({R, B}, "lc" + to_string(i));
    pairs.insert({first.due - 1, second.due - first.due});
  }
  CHECK(pairs.size() > 12);
}

// ── Rule 4: delays ────────────────────────────────────────────────────────

static void testGapCountsFromActualDay() {
  // First Blue on day 1, so the next check is due day 8. Done late, on day 20.
  CardState s = applyClick(CardState(), B, 1);
  CHECK_EQ(s.due, 8);
  s = applyClick(s, B, 20);
  // Next gap runs from day 20, the day it was actually done.
  CHECK_EQ(s.due, 35);
  CHECK_EQ(s.track, Track::Blue);
  CHECK_EQ(s.step, 1);
}

static void testLateCardIsNotPunished() {
  CardState s = applyClick(CardState(), Y, 1);
  CHECK(isDue(s, 4));
  CHECK(isDue(s, 400));           // waits as long as it takes
  CHECK_EQ(s.track, Track::Yellow);  // no decay while waiting
  CHECK_EQ(s.step, 0);
}

static void testStudyClockSkipsIdleDays() {
  const int mon = parseDate("2026-11-02");
  Clock clock(ClockMode::StudyDays);
  clock.markActive(mon);
  CHECK_EQ(clock.day(mon), 1);
  CHECK_EQ(clock.day(mon + 1), 2);   // Tuesday would be day 2

  // Skip Tuesday and Wednesday entirely.
  CHECK_EQ(clock.day(mon + 2), 2);   // Wednesday is still day 2
  CHECK_EQ(clock.day(mon + 3), 2);   // Thursday too
  clock.markActive(mon + 3);
  CHECK_EQ(clock.day(mon + 3), 2);
  CHECK_EQ(clock.day(mon + 4), 3);

  // A Red graded Monday is due on day 2, which only arrives when you return.
  CardState s = applyClick(CardState(), R, clock.day(mon));
  CHECK_EQ(s.due, 2);
}

static void testCalendarClockCountsEveryDay() {
  const int mon = parseDate("2026-11-02");
  Clock clock(ClockMode::CalendarDays);
  clock.markActive(mon);
  CHECK_EQ(clock.day(mon + 3) - clock.day(mon), 3);
}

static void testDayRollover() {
  CHECK_EQ(dateFromNow("2026-11-06T01:30"), parseDate("2026-11-05"));
  CHECK_EQ(dateFromNow("2026-11-06T03:59"), parseDate("2026-11-05"));
  CHECK_EQ(dateFromNow("2026-11-06T04:00"), parseDate("2026-11-06"));
  CHECK_EQ(dateFromNow("2026-11-06"), parseDate("2026-11-06"));
  CHECK_EQ(formatDate(parseDate("2026-02-28") + 1), string("2026-03-01"));
  CHECK_EQ(formatDate(parseDate("2028-02-28") + 1), string("2028-02-29"));
  CHECK_EQ(parseDate("not a date"), -1);
}

// ── Rule 5: queue order ───────────────────────────────────────────────────

static void testQueueOrder() {
  CardState red, yellow, blue;
  red.track = Track::Red; red.due = 10;
  yellow.track = Track::Yellow; yellow.due = 10;
  blue.track = Track::Blue; blue.due = 10;
  CHECK(servedBefore(red, yellow, 10));
  CHECK(servedBefore(yellow, blue, 10));
  // Aging: a Blue that has waited 3 days passes a fresh Red.
  blue.due = 7;
  CHECK(servedBefore(blue, red, 10));
  // Same priority: older due day first.
  CardState a, b;
  a.track = b.track = Track::Red;
  a.due = 5; b.due = 5;
  CHECK(!servedBefore(a, b, 5));
}

// ── Whole engine, over tiny resources ─────────────────────────────────────

// lc1 to lc`googleAsked` are asked at Google, the rest by no company.
static string tinyResources(int googleAsked = 0) {
  string problems;
  for (int i = 1; i <= 40; ++i) {
    const int pattern = i % 4;
    if (i > 1) problems += ',';
    // Two finer topics under heap: lc1, lc5 and lc9 are top-k, lc13 and lc17 two-heaps.
    const string topics = i == 1 || i == 5 || i == 9 ? ",\"u\":[0]" : i == 13 || i == 17 ? ",\"u\":[1]" : "";
    const string google = i <= googleAsked ? ",\"c\":[[0," + to_string(10 + i) + "]]" : "";
    problems += "{\"n\":" + to_string(i) + ",\"t\":\"Problem " + to_string(i) +
                "\",\"s\":\"p" + to_string(i) + "\",\"d\":2,\"r\":1700,\"f\":" +
                to_string(i) + ",\"p\":[" + to_string(pattern) + "],\"g\":[" +
                to_string(pattern) + "],\"m\":\"5555\"" + topics + google + "}";
  }
  string cards;
  for (int i = 1; i <= 40; ++i) {
    if (i > 1) cards += ',';
    cards += "{\"id\":\"signals-" + to_string(i) + "\",\"type\":\"flash\",\"deck\":\"signals\",\"q\":\"Q" +
             to_string(i) + "\",\"a\":\"A\",\"patterns\":[" + to_string(i % 4) +
             "],\"skill\":\"recog\",\"level\":1}";
  }
  return "{\"patterns\":[{\"slug\":\"two-pointers\",\"name\":\"Two pointers\"},"
         "{\"slug\":\"heap\",\"name\":\"Heap\"},{\"slug\":\"trie\",\"name\":\"Trie\"},"
         "{\"slug\":\"greedy\",\"name\":\"Greedy\"}],"
         "\"topics\":[{\"slug\":\"heap/top-k\",\"name\":\"Top k\",\"signal\":\"Keep the k best.\",\"pattern\":1},"
         "{\"slug\":\"heap/two-heaps\",\"name\":\"Two heaps\",\"signal\":\"A running median.\",\"pattern\":1}],"
         "\"tags\":[\"a\",\"b\",\"c\",\"d\"],\"companies\":[{\"slug\":\"google\",\"name\":\"Google\"}],"
         "\"decks\":[{\"slug\":\"signals\",\"title\":\"Pattern signals\"}],"
         "\"problems\":[" + problems + "],\"cards\":[" + cards + "]}";
}

static Json call(Engine& engine, const string& request) {
  string error;
  Json response = Json::parse(engine.call(request), &error);
  CHECK(response["ok"].asBool());
  if (!response["ok"].asBool()) printf("      %s -> %s\n", request.c_str(), response["error"].asStr().c_str());
  return response;
}

static string gradeRequest(const string& id, const char* grade, const char* now) {
  return "{\"cmd\":\"grade\",\"id\":\"" + id + "\",\"grade\":\"" + grade + "\",\"now\":\"" + now + "\"}";
}

static void testBoundedQueueRollsOver() {
  Engine engine;
  CHECK(engine.loadResources(tinyResources()) == 80);
  // The calendar clock, so four days on every miss's first check is due.
  CHECK(engine.loadState("{\"settings\":{\"codeCap\":3,\"codeNew\":1,\"clock\":\"calendar\"}}"));

  // Fail seven problems on one day, so all seven are due four days later.
  for (int i = 1; i <= 7; ++i) {
    call(engine, gradeRequest("lc" + to_string(i), "r", "2026-11-02T20:00"));
  }
  Json later = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-06T20:00\"}");
  CHECK_EQ(static_cast<int>(later["code"]["items"].size()), 3);     // bounded
  CHECK_EQ(later["code"]["overflow"].asInt(), 5);                    // 7 due, 2 served, 5 rolled
  // Two review slots and one reserved new face.
  int reviews = 0;
  for (const Json& item : later["code"]["items"].items()) reviews += item["slot"].asStr() == "review";
  CHECK_EQ(reviews, 2);

  // Do both reviews. Tomorrow the rolled cards are at the front, not lost.
  for (const Json& item : later["code"]["items"].items()) {
    if (item["slot"].asStr() == "review") {
      call(engine, gradeRequest(item["id"].asStr(), "b", "2026-11-06T21:00"));
    }
  }
  Json next = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-07T20:00\"}");
  CHECK_EQ(next["code"]["overflow"].asInt(), 3);                    // 5 waiting, 2 served
}

static void testReviewsADayLimitTheChecks() {
  Engine engine;
  engine.loadResources(tinyResources());
  CHECK(engine.loadState("{\"settings\":{\"codeCap\":8,\"codeNew\":1,\"codeReviews\":3,\"clock\":\"calendar\"}}"));
  for (int i = 1; i <= 7; ++i) call(engine, gradeRequest("lc" + to_string(i), "r", "2026-11-02T20:00"));

  // Seven due and room for eight, but at most three checks. The rest wait.
  const Json day = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-06T20:00\"}");
  int reviews = 0, fresh = 0;
  for (const Json& item : day["code"]["items"].items()) (item["slot"].asStr() == "review" ? reviews : fresh) += 1;
  CHECK_EQ(reviews, 3);
  CHECK_EQ(fresh, 1);
  CHECK_EQ(day["code"]["overflow"].asInt(), 4);

  // A check done today still counts when the queue is rebuilt.
  int graded = 0;
  for (const Json& item : day["code"]["items"].items()) {
    if (item["slot"].asStr() == "review" && graded < 2) {
      call(engine, gradeRequest(item["id"].asStr(), "b", "2026-11-06T20:30"));
      ++graded;
    }
  }
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"codeNew\":2}}");
  const Json rebuilt = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-06T21:00\"}");
  reviews = 0;
  for (const Json& item : rebuilt["code"]["items"].items()) reviews += item["slot"].asStr() == "review";
  CHECK_EQ(reviews, 3);   // the two done, and one more

  // Asking for more serves a waiting check first.
  const Json more = call(engine, "{\"cmd\":\"more\",\"now\":\"2026-11-06T21:30\",\"lane\":\"code\",\"n\":1}");
  CHECK_EQ(more["code"]["items"].items().back()["slot"].asStr(), string("review"));

  // It's a setting like the others, and 0 holds every check back.
  CHECK_EQ(call(engine, "{\"cmd\":\"settings\"}")["settings"]["codeReviews"].asInt(), 3);
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"codeReviews\":0}}");
  const Json none = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-07T20:00\"}");
  for (const Json& item : none["code"]["items"].items()) CHECK(item["slot"].asStr() != "review");
  Engine reloaded;
  reloaded.loadResources(tinyResources());
  CHECK(reloaded.loadState(engine.saveState()));
  CHECK_EQ(call(reloaded, "{\"cmd\":\"settings\"}")["settings"]["codeReviews"].asInt(), 0);
}

static vector<string> laneIds(const Json& day, const char* lane) {
  vector<string> ids;
  for (const Json& item : day[lane]["items"].items()) ids.push_back(item["id"].asStr());
  return ids;
}

static void testUnfinishedNewFacesStay() {
  Engine engine;
  engine.loadResources(tinyResources());
  CHECK(engine.loadState("{\"settings\":{\"codeCap\":3,\"codeNew\":1}}"));
  auto today = [&](const char* now) { return call(engine, string("{\"cmd\":\"today\",\"now\":\"") + now + "\"}"); };

  // Day one: the new face, plus two you pulled. You grade one of the pulls.
  const auto first = laneIds(today("2026-11-02T09:00"), "code");
  CHECK_EQ(static_cast<int>(first.size()), 1);
  const auto pulled = laneIds(call(engine, "{\"cmd\":\"more\",\"now\":\"2026-11-02T10:00\",\"lane\":\"code\",\"n\":2}"), "code");
  CHECK_EQ(static_cast<int>(pulled.size()), 3);
  call(engine, gradeRequest(pulled[2], "y", "2026-11-02T11:00"));
  set<string> waiting(pulled.begin(), pulled.end());
  waiting.erase(pulled[2]);

  // Day two: the two you didn't grade are still there, and no fresh pick joins them.
  const Json day2 = today("2026-11-03T09:00");
  const auto ids2 = laneIds(day2, "code");
  CHECK(set<string>(ids2.begin(), ids2.end()) == waiting);
  for (const Json& item : day2["code"]["items"].items()) CHECK(item["slot"].asStr() != "review");

  // Skipped days don't pile anything up, and changing a setting keeps them too.
  CHECK(laneIds(today("2026-11-06T09:00"), "code") == ids2);
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"company\":\"google\"}}");
  CHECK(laneIds(today("2026-11-06T10:00"), "code") == ids2);

  // A plan waiting to be rebuilt survives a save and a load.
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"company\":\"\"}}");
  Engine reloaded;
  reloaded.loadResources(tinyResources());
  CHECK(reloaded.loadState(engine.saveState()));
  CHECK(laneIds(call(reloaded, "{\"cmd\":\"today\",\"now\":\"2026-11-06T11:00\"}"), "code") == ids2);

  // Grading one and hiding the other lets fresh picks back in, within the allowance.
  call(engine, gradeRequest(ids2[0], "b", "2026-11-06T12:00"));
  call(engine, string("{\"cmd\":\"hide\",\"id\":\"") + ids2[1] + "\",\"now\":\"2026-11-06T12:00\"}");
  const auto ids7 = laneIds(today("2026-11-07T09:00"), "code");
  CHECK_EQ(static_cast<int>(ids7.size()), 1);
  CHECK(ids7[0] != ids2[0] && ids7[0] != ids2[1]);
}

static void testKeptNewFacesStayInsideTheBound() {
  Engine engine;
  engine.loadResources(tinyResources());
  CHECK(engine.loadState("{\"settings\":{\"codeCap\":3,\"codeNew\":1,\"clock\":\"calendar\"}}"));
  const Json day1 = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-02T09:00\"}");
  const string kept = laneIds(day1, "code")[0];

  // Seven reviews due: the kept new face holds its slot and the reviews take the rest.
  int failed = 0;
  for (int i = 1; failed < 7; ++i) {
    const string id = "lc" + to_string(i);
    if (id == kept) continue;
    call(engine, gradeRequest(id, "r", "2026-11-02T20:00"));
    ++failed;
  }
  const Json day2 = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-06T09:00\"}");
  CHECK_EQ(static_cast<int>(day2["code"]["items"].size()), 3);
  CHECK_EQ(day2["code"]["overflow"].asInt(), 5);
  int reviews = 0;
  bool stayed = false;
  for (const Json& item : day2["code"]["items"].items()) {
    reviews += item["slot"].asStr() == "review";
    stayed = stayed || item["id"].asStr() == kept;
  }
  CHECK_EQ(reviews, 2);
  CHECK(stayed);

  /*
  New faces you pulled go over the cap later on, and never take a review's
  place. A pull serves overdue reviews first, so these are pulled on a day
  with none.
  */
  Engine puller;
  puller.loadResources(tinyResources());
  CHECK(puller.loadState("{\"settings\":{\"codeCap\":3,\"codeNew\":1,\"clock\":\"calendar\"}}"));
  const auto mine = laneIds(call(puller, "{\"cmd\":\"more\",\"now\":\"2026-11-02T09:00\",\"lane\":\"code\",\"n\":2}"), "code");
  CHECK_EQ(static_cast<int>(mine.size()), 3);
  failed = 0;
  for (int i = 1; failed < 7; ++i) {
    const string id = "lc" + to_string(i);
    if (find(mine.begin(), mine.end(), id) != mine.end()) continue;
    call(puller, gradeRequest(id, "r", "2026-11-02T20:00"));
    ++failed;
  }
  const Json next = call(puller, "{\"cmd\":\"today\",\"now\":\"2026-11-06T09:00\"}");
  reviews = 0;
  for (const Json& item : next["code"]["items"].items()) reviews += item["slot"].asStr() == "review";
  CHECK_EQ(reviews, 2);
  CHECK_EQ(static_cast<int>(next["code"]["items"].size()), 5);   // 2 reviews and the 3 you have, 2 of them pulled
  CHECK_EQ(next["code"]["overflow"].asInt(), 5);

  // A deck you turn off takes its waiting cards with it. The tiny deck drills coding, so it starts off.
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"decksOff\":[]}}");
  CHECK(!call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-07T09:00\"}")["cards"]["items"].items().empty());
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"decksOff\":[\"signals\"]}}");
  CHECK(call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-07T10:00\"}")["cards"]["items"].items().empty());
}

static void testPlanIsStableWithinADay() {
  Engine engine;
  engine.loadResources(tinyResources());
  string a = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-02T09:00\"}")["code"]["items"].dump();
  string b = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-02T23:00\"}")["code"]["items"].dump();
  CHECK_EQ(a, b);
}

static void testPlanSurvivesSaveAndLoad() {
  Engine one;
  one.loadResources(tinyResources());
  call(one, "{\"cmd\":\"today\",\"now\":\"2026-11-02T09:00\"}");
  call(one, gradeRequest("lc3", "y", "2026-11-02T09:30"));
  const string saved = one.saveState();

  Engine two;
  two.loadResources(tinyResources());
  CHECK(two.loadState(saved));
  CHECK_EQ(call(two, "{\"cmd\":\"today\",\"now\":\"2026-11-02T10:00\"}").dump(),
           call(one, "{\"cmd\":\"today\",\"now\":\"2026-11-02T10:00\"}").dump());
  CHECK_EQ(two.saveState(), one.saveState());
}

static void testRegradeAndUndo() {
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "r", "2026-11-02T09:00"));
  // A second click the same day corrects the first rather than stacking.
  Json fixed = call(engine, gradeRequest("lc1", "b", "2026-11-02T09:05"));
  CHECK_EQ(fixed["card"]["track"].asStr(), string("blue"));
  CHECK_EQ(fixed["card"]["attempts"].asInt(), 1);
  // Undo removes it entirely.
  call(engine, "{\"cmd\":\"undo\",\"id\":\"lc1\"}");
  Json card = call(engine, "{\"cmd\":\"card\",\"id\":\"lc1\",\"now\":\"2026-11-02T09:06\"}");
  CHECK_EQ(card["card"]["track"].asStr(), string("new"));
}

static void testRecommenderOnlyPicksUnseen() {
  Engine engine;
  engine.loadResources(tinyResources());
  for (int i = 1; i <= 30; ++i) call(engine, gradeRequest("lc" + to_string(i), "b", "2026-11-02T09:00"));
  Json picks = call(engine, "{\"cmd\":\"recommend\",\"now\":\"2026-11-03T09:00\",\"n\":5}");
  CHECK_EQ(static_cast<int>(picks["rows"].size()), 5);
  for (const Json& row : picks["rows"].items()) CHECK_EQ(row["track"].asStr(), string("new"));
}

static void testOldPicksMakeWay() {
  // A queue saved before 2026-11-09, holding new faces the old picker chose.
  Engine engine;
  engine.loadResources(tinyResources());
  CHECK(engine.loadState(R"({"v":2,"settings":{"codeCap":3,"codeNew":1,"decksOff":[]},"plan":{"date":"2026-11-02","pulls":0,
    "code":[{"id":"lc5","slot":"coach","why":"Coach: Heap, 30% of new problems"},{"id":"lc6","slot":"explore","why":"x"}],
    "cards":[{"id":"signals-1","slot":"weak","why":"x"}]}})"));
  // That day stays as it was.
  CHECK(laneIds(call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-02T10:00\"}"), "code") ==
        vector<string>({"lc5", "lc6"}));
  // The next queue lets the new picker choose. Cards you were given stay.
  const Json next = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-03T10:00\"}");
  for (const Json& item : next["code"]["items"].items()) {
    const string slot = item["slot"].asStr();
    CHECK(slot == "new-pattern" || slot == "new-topic" || slot == "practice");
  }
  CHECK(laneIds(next, "cards")[0] == "signals-1");
}

static void testMemorizationSignal() {
  Engine engine;
  engine.loadResources(tinyResources());
  // Fail four two-pointer problems cold, then pass all four on review, twice.
  for (int i : {4, 8, 12, 16}) call(engine, gradeRequest("lc" + to_string(i), "r", "2026-11-02T09:00"));
  for (int i : {4, 8, 12, 16}) call(engine, gradeRequest("lc" + to_string(i), "b", "2026-11-03T09:00"));
  for (int i : {4, 8, 12, 16}) call(engine, gradeRequest("lc" + to_string(i), "b", "2026-11-04T09:00"));
  for (int i : {4, 8, 12, 16}) call(engine, gradeRequest("lc" + to_string(i), "b", "2026-11-05T09:00"));
  Json insights = call(engine, "{\"cmd\":\"insights\",\"now\":\"2026-11-05T10:00\"}");
  CHECK_EQ(insights["patterns"].items()[0]["slug"].asStr(), string("two-pointers"));
  CHECK(insights["patterns"].items()[0]["memorizing"].asBool());
}

static void testWeakSlotAimsAtWeakPatterns() {
  /*
  Regression, now for mock interviews, the one place problems still come from
  the Weak slot. An early version steered it away from the patterns in today's
  reviews, which are usually the ones you are failing.
  */
  Engine engine;
  engine.loadResources(tinyResources());
  int problem = 1;
  for (const char* now : {"2026-11-02T09:00", "2026-11-03T09:00", "2026-11-04T09:00"}) {
    for (int k = 0; k < 8; ++k, ++problem) {
      const bool weakPattern = problem % 4 == 0;  // two-pointers
      call(engine, gradeRequest("lc" + to_string(problem), weakPattern ? "r" : "b", now));
    }
  }
  int onWeak = 0, weakSlots = 0;
  for (int roll = 0; roll < 200; ++roll) {
    Json mock = call(engine, "{\"cmd\":\"mock\",\"now\":\"2026-11-05T09:00\",\"roll\":" + to_string(roll) + "}");
    for (const Json& row : mock["items"].items()) {
      const string slot = row["slot"].asStr();
      CHECK(slot == "weak" || slot == "wildcard" || slot == "explore");   // a mock is never tailored by the queue's kinds
      if (slot != "weak") continue;
      ++weakSlots;
      Json detail = call(engine, "{\"cmd\":\"card\",\"id\":\"" + row["id"].asStr() + "\",\"now\":\"2026-11-05T09:00\"}");
      onWeak += detail["card"]["patterns"].items()[0]["slug"].asStr() == "two-pointers";
    }
  }
  CHECK(weakSlots > 150);
  CHECK(onWeak * 2 > weakSlots);   // well above the one in four of chance
}

static void testTriviaMissesDoNotDriveTheNeeds() {
  // Regression. A missed OS card is not evidence about how you code.
  string json = tinyResources();
  const string needle = "\"cards\":[";
  json.insert(json.find(needle) + needle.size(),
              "{\"id\":\"os-1\",\"type\":\"flash\",\"deck\":\"os\",\"q\":\"Q\",\"a\":\"A\",\"patterns\":[],\"skill\":\"explain\",\"level\":1},");
  Resources resources;
  CHECK(resources.load(json, nullptr) == 81);
  Clock clock;
  clock.markActive(100);
  auto miss = [](const char* id) {
    Event e;
    e.id = id;
    e.click = Click::Red;
    e.date = 100;
    return e;
  };
  const Profile trivia = buildProfile(resources, {miss("os-1")}, clock, clock.day(100));
  for (double n : trivia.needs) CHECK_EQ(n, 0.0);

  // The same miss on a card that drills something does count: a signal card drills seeing the pattern.
  const Profile signal = buildProfile(resources, {miss("os-1"), miss("signals-1")}, clock, clock.day(100));
  CHECK(signal.needs[SEEING_NEED] > 0);
  CHECK_EQ(signal.needShare()[SEEING_NEED], 1.0);
  // And on that card's pattern: signals-1 is heap.
  CHECK(signal.needPatterns[SEEING_NEED][1] > 0);
  CHECK_EQ(signal.needPatterns[SEEING_NEED][0], 0.0);

  // What older versions asked after a miss stays in the log and is never read.
  Event tagged = miss("lc1");
  tagged.gaps = {"impl", "edge"};
  tagged.bug = "off-by-one";
  for (double n : buildProfile(resources, {tagged}, clock, clock.day(100)).needs) CHECK_EQ(n, 0.0);
}

static void testImportOldTracker() {
  // The exact shape and history of the old tracker's log.
  Engine engine;
  engine.loadResources(tinyResources());
  Json result = call(engine,
      "{\"cmd\":\"import\",\"problems\":["
      "{\"id\":38,\"log\":[{\"d\":\"2026-10-31\",\"g\":\"r\"},{\"d\":\"2026-11-04\",\"g\":\"b\"}]},"
      "{\"id\":39,\"log\":[{\"d\":\"2026-10-31\",\"g\":\"b\"}]}]}");
  CHECK_EQ(result["added"].asInt(), 3);
  // Importing again changes nothing.
  CHECK_EQ(call(engine, "{\"cmd\":\"import\",\"problems\":[{\"id\":39,\"log\":[{\"d\":\"2026-10-31\",\"g\":\"b\"}]}]}")["added"].asInt(), 0);
  /*
  Study days so far: Oct 31 is 1, Nov 4 is 2. Passing the first check of a miss
  on day 2 sets the second 5 to 10 days on, and today is day 3.
  */
  Json card = call(engine, "{\"cmd\":\"card\",\"id\":\"lc38\",\"now\":\"2026-11-05T10:00\"}");
  CHECK_EQ(card["card"]["track"].asStr(), string("red"));
  CHECK_EQ(card["card"]["step"].asInt(), 1);
  CHECK(card["card"]["dueIn"].asInt() >= 4 && card["card"]["dueIn"].asInt() <= 9);
}

static void testRuleChangeNeedsNoMigration() {
  // Clock mode is re-derived from the same log. Nothing is lost either way.
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "b", "2026-11-02T09:00"));
  call(engine, gradeRequest("lc2", "b", "2026-11-09T09:00"));
  const int study = call(engine, "{\"cmd\":\"card\",\"id\":\"lc1\",\"now\":\"2026-11-10T09:00\"}")["card"]["dueIn"].asInt();
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"clock\":\"calendar\"}}");
  const int calendar = call(engine, "{\"cmd\":\"card\",\"id\":\"lc1\",\"now\":\"2026-11-10T09:00\"}")["card"]["dueIn"].asInt();
  // The same check, with the same extra days: only 2 study days have passed, but 8 calendar days.
  CHECK_EQ(study - calendar, 6);
  CHECK(study >= 8 && study <= 18);
}

static void testDrillDiff() {
  Engine engine;
  engine.loadResources(tinyResources());
  // Spacing and comments never count.
  Json same = call(engine, "{\"cmd\":\"diff\",\"typed\":\"int a=1;\",\"reference\":\"int a = 1; // note\"}");
  CHECK_EQ(same["suggest"].asStr(), string("b"));
  Json wrong = call(engine, "{\"cmd\":\"diff\",\"typed\":\"int a = 2;\",\"reference\":\"int a = 1;\"}");
  CHECK(wrong["score"].asNum() < 1);
  Json empty = call(engine, "{\"cmd\":\"diff\",\"typed\":\"\",\"reference\":\"int a = 1;\"}");
  CHECK_EQ(empty["suggest"].asStr(), string("r"));
}

static void testBadInputDoesNotCrash() {
  Engine engine;
  string response = engine.call("{\"cmd\":\"today\",\"now\":\"2026-11-02\"}");
  CHECK(response.find("not loaded") != string::npos);
  engine.loadResources(tinyResources());
  CHECK(engine.call("not json").find("\"ok\":false") != string::npos);
  CHECK(engine.call("{\"cmd\":\"nope\"}").find("unknown command") != string::npos);
  CHECK(engine.call("{\"cmd\":\"today\",\"now\":\"yesterday\"}").find("\"ok\":false") != string::npos);
  CHECK(engine.call("{\"cmd\":\"grade\",\"id\":\"lc99999\",\"grade\":\"b\",\"now\":\"2026-11-02\"}").find("unknown question") != string::npos);
  CHECK(!engine.loadState("not json"));
  // A log entry with a bad date or grade is dropped, the rest load.
  CHECK(engine.loadState("{\"events\":[{\"id\":\"lc1\",\"g\":\"b\",\"d\":\"bad\"},{\"id\":\"lc2\",\"g\":\"?\",\"d\":\"2026-11-02\"},{\"id\":\"lc3\",\"g\":\"r\",\"d\":\"2026-11-02\"}]}"));
  Json card = call(engine, "{\"cmd\":\"card\",\"id\":\"lc3\",\"now\":\"2026-11-02\"}");
  CHECK_EQ(card["card"]["track"].asStr(), string("red"));
  // A question that left the resources stays in the log untouched.
  CHECK(engine.loadState("{\"events\":[{\"id\":\"gone1\",\"g\":\"b\",\"d\":\"2026-11-02\"}]}"));
  call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-03\"}");
  CHECK(engine.saveState().find("gone1") != string::npos);
}

static void testJson() {
  string error;
  Json j = Json::parse("{\"a\":[1,2.5,\"x\\n\\u00e9\",null,true],\"b\":{\"c\":-3}}", &error);
  CHECK(error.empty());
  CHECK_EQ(j["a"].items()[0].asInt(), 1);
  CHECK_EQ(j["a"].items()[2].asStr(), string("x\n\xC3\xA9"));
  CHECK_EQ(j["b"]["c"].asInt(), -3);
  CHECK_EQ(Json::parse(j.dump()).dump(), j.dump());
  CHECK(Json::parse("{\"a\":").isNull());
  CHECK(Json::parse("[1,2").isNull());
  CHECK(Json::parse("").isNull());
  // Deep nesting is refused, not a stack overflow.
  CHECK(Json::parse(string(100000, '[')).isNull());
}


// ── Attempts ──────────────────────────────────────────────────────────────

static string attemptRequest(const string& id, const string& codeJson, const char* now) {
  return "{\"cmd\":\"attempt\",\"id\":\"" + id + "\",\"code\":\"" + codeJson + "\",\"now\":\"" + now + "\"}";
}

static string attemptWith(const string& id, const string& code, const char* now, const string& existing) {
  return Json::object().set("cmd", "attempt").set("id", id).set("code", code).set("now", now).set("existing", existing).dump();
}

static void testAttemptNamesAFileAndTouchesNoProgress() {
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "r", "2026-11-02"));
  const string before = engine.saveState();

  // One file per problem per study day. The front end appends `block` to it.
  Json first = call(engine, attemptRequest("lc2", "int f() {\\n  return 1;\\n}\\n", "2026-11-06T21:30"));
  CHECK_EQ(first["stem"].asStr(), string("profile/attempts/lc2/2026-11-06"));
  CHECK_EQ(first["ext"].asStr(), string(".cpp"));
  CHECK_EQ(first["paste"].asInt(), 1);
  CHECK_EQ(first["block"].asStr(), string("// ── Paste 1 at 21:30 ──\nint f() {\n  return 1;\n}\n"));
  CHECK_EQ(first["note"].asStr(), string("Saved paste 1 of the day, 3 lines, on Problem 2."));
  CHECK(!first["dirty"].asBool());
  CHECK_EQ(engine.saveState(), before);

  // The next paste is numbered from the file as it stands, after a blank line.
  const string day = first["block"].asStr();
  Json second = call(engine, attemptWith("lc2", "int f();", "2026-11-06T21:42", day));
  CHECK_EQ(second["paste"].asInt(), 2);
  CHECK_EQ(second["block"].asStr(), string("\n// ── Paste 2 at 21:42 ──\nint f();\n"));
  CHECK_EQ(second["note"].asStr(), string("Saved paste 2 of the day, 1 line, on Problem 2."));

  // The same code again adds nothing, whatever the trailing blank lines.
  Json again = call(engine, attemptWith("lc2", "int f();\n\n", "2026-11-06T21:43", day + second["block"].asStr()));
  CHECK_EQ(again["block"].asStr(), string(""));
  CHECK_EQ(again["paste"].asInt(), 2);
  CHECK_EQ(again["note"].asStr(), string("Same as your last paste, so nothing was added."));

  // A file that doesn't end in a newline still gets a blank line before the next paste.
  CHECK_EQ(call(engine, attemptWith("lc2", "y", "2026-11-06T21:50", "x"))["block"].asStr().substr(0, 2), string("\n\n"));

  // Past midnight and before 4 AM, a paste joins the night before.
  CHECK_EQ(call(engine, attemptRequest("lc2", "x", "2026-11-07T01:30"))["stem"].asStr(), string("profile/attempts/lc2/2026-11-06"));

  // Only the date in "now" reaches the path, and only a clock time reaches the file.
  Json hostile = call(engine, attemptRequest("lc2", "x", "2026-11-06T21:30/../../../etc/passwd"));
  CHECK_EQ(hostile["stem"].asStr(), string("profile/attempts/lc2/2026-11-06"));
  Json slashInTime = call(engine, attemptRequest("lc2", "x", "2026-11-06T/.:./"));
  CHECK_EQ(slashInTime["stem"].asStr(), string("profile/attempts/lc2/2026-11-06"));
  CHECK(slashInTime["block"].asStr().find("Paste 1 at --:-- ──") != string::npos);
  CHECK(call(engine, attemptRequest("lc2", "x", "2026-11-06"))["block"].asStr().find("at --:--") != string::npos);
}

static void testAttemptIsRefusedWhenItCannotBeCode() {
  Engine engine;
  engine.loadResources(tinyResources());
  const char* now = "2026-11-06T21:30";
  auto refused = [&](const string& request, const char* reason) {
    const string response = engine.call(request);
    CHECK(response.find("\"ok\":false") != string::npos);
    CHECK(response.find(reason) != string::npos);
  };

  refused(attemptRequest("lc99999", "x", now), "unknown question");
  refused(attemptRequest("../../etc/passwd", "x", now), "unknown question");
  refused(attemptRequest("signals-1", "x", now), "is a card");
  refused(attemptRequest("lc1", "", now), "Nothing to save");
  refused(attemptRequest("lc1", " \\n\\t\\r\\n ", now), "Nothing to save");
  refused("{\"cmd\":\"attempt\",\"id\":\"lc1\",\"now\":\"2026-11-06T21:30\"}", "Nothing to save");
  refused(attemptRequest("lc1", "a\\u0000b", now), "isn't text");
  refused("{\"cmd\":\"attempt\",\"id\":\"lc1\",\"code\":\"x\"}", "needs \\\"now\\\"");

  const string atLimit(ATTEMPT_MAX_BYTES, 'a');
  call(engine, attemptRequest("lc1", atLimit, now));
  refused(attemptRequest("lc1", atLimit + "a", now), "too long");
}

// ── The coach ─────────────────────────────────────────────────────────────

static const char* const COACH_NOW = "2026-11-20T21:30";

static string pendingRequest(const string& log, const string& attempts = "[]",
                                  const string& oldLog = "") {
  return string("{\"cmd\":\"pending\",\"now\":\"") + COACH_NOW + "\",\"log\":" + Json(log).dump() +
         ",\"old_log\":" + Json(oldLog).dump() + ",\"attempts\":" + attempts + "}";
}

static string judgementRequest(const string& judgement, const string& log = "",
                                    const string& attempts = "[]") {
  return string("{\"cmd\":\"judgement\",\"now\":\"") + COACH_NOW + "\",\"judgement\":" + judgement +
         ",\"log\":" + Json(log).dump() + ",\"attempts\":" + attempts + "}";
}

static string dropRequest(const string& drop, const string& log = "", const string& oldLog = "",
                               const string& attempts = "[]") {
  return string("{\"cmd\":\"drop\",\"now\":\"") + COACH_NOW + "\",\"drop\":" + drop + ",\"log\":" + Json(log).dump() +
         ",\"old_log\":" + Json(oldLog).dump() + ",\"attempts\":" + attempts + "}";
}

static string commitRequest(const string& profile) {
  return string("{\"cmd\":\"commit\",\"now\":\"") + COACH_NOW + "\",\"profile\":" + Json(profile).dump() + "}";
}

static Json loadJudgements(Engine& engine, const string& text, const string& old = "") {
  return call(engine, Json::object().set("cmd", "judgements").set("text", text).set("old", old).dump());
}

static void expectRefused(Engine& engine, const string& request, const char* reason) {
  const string response = engine.call(request);
  CHECK(response.find("\"ok\":false") != string::npos);
  CHECK(response.find(reason) != string::npos);
  if (response.find(reason) == string::npos) printf("      wanted \"%s\" in %s\n", reason, response.c_str());
}

static void testPendingListsGradesWithNoJudgement() {
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "r", "2026-11-02"));
  call(engine, gradeRequest("signals-1", "b", "2026-11-02"));  // a card, so not reviewed
  call(engine, gradeRequest("lc2", "b", "2026-11-03"));
  call(engine, gradeRequest("lc1", "b", "2026-11-04"));
  const string before = engine.saveState();

  Json all = call(engine, pendingRequest(""));
  CHECK_EQ(all["total"].asInt(), 3);
  const vector<Json>& items = all["items"].items();
  CHECK_EQ(static_cast<int>(items.size()), 3);
  CHECK_EQ(items[0]["problem"].asStr(), string("lc1"));
  CHECK_EQ(items[0]["date"].asStr(), string("2026-11-02"));
  CHECK_EQ(items[0]["title"].asStr(), string("Problem 1"));
  CHECK_EQ(items[0]["grade"].asStr(), string("r"));
  CHECK_EQ(items[0]["resources_patterns"].items()[0].asStr(), string("heap"));
  CHECK_EQ(items[0]["resources_topics"].items()[0].asStr(), string("heap/top-k"));
  CHECK(!items[1].has("resources_topics"));   // lc2 has no finer topic
  CHECK(items[0]["first_try"].asBool());
  CHECK_EQ(items[2]["problem"].asStr(), string("lc1"));
  CHECK(!items[2]["first_try"].asBool());
  CHECK_EQ(engine.saveState(), before);

  // A judged grade drops out. The same problem on another day still waits.
  const Json rest = call(engine, pendingRequest("{\"problem\":\"lc1\",\"date\":\"2026-11-02\"}\n"));
  CHECK_EQ(rest["total"].asInt(), 2);
  CHECK_EQ(rest["items"].items()[0]["problem"].asStr(), string("lc2"));
  CHECK(!rest["items"].items()[1]["first_try"].asBool());

  // A line cut short by a crash costs that line and nothing else.
  const Json torn = call(engine, pendingRequest("{\"problem\":\"lc1\",\"date\":\"2026-11-02\"}\n{\"problem\":\"lc2\",\"da"));
  CHECK_EQ(torn["total"].asInt(), 2);
}

static void testPendingShowsTheEarlierJudgement() {
  // An attempt judged before 2026-11-09 waits for a judgement in the new shape, with the old one beside it.
  Engine engine;
  engine.loadResources(tinyResources());
  CHECK(engine.loadState(R"({"events":[{"id":"lc1","g":"r","d":"2026-11-02","gaps":["speed"],"bug":"overflow"},
    {"id":"lc2","g":"b","d":"2026-11-03"}]})"));
  const string old =
      "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"your_grade\":\"r\",\"model_grade\":\"r\",\"patterns\":[\"heap\"],"
      "\"flaws\":[\"bound-wrong\"],\"note\":\"Old.\",\"had_code\":false,\"ts\":\"2026-11-07T09:35\"}\n";
  const Json pending = call(engine, pendingRequest("", "[]", old));
  CHECK_EQ(pending["total"].asInt(), 2);
  const Json& first = pending["items"].items()[0];
  CHECK_EQ(first["earlier_judgement"]["note"].asStr(), string("Old."));
  CHECK_EQ(first["earlier_judgement"]["flaws"].items()[0].asStr(), string("bound-wrong"));
  CHECK(!first["earlier_judgement"].has("ts"));
  CHECK_EQ(first["your_gaps"].items()[0].asStr(), string("speed"));
  CHECK_EQ(first["your_bug"].asStr(), string("overflow"));
  CHECK(!pending["items"].items()[1].has("earlier_judgement"));
  // Once the new log has it, judged or dropped, it waits no more.
  CHECK_EQ(call(engine, pendingRequest("{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"dropped\":true}\n", "[]", old))["total"].asInt(), 1);
}

static void testPendingStopsAtOneBatch() {
  Engine engine;
  engine.loadResources(tinyResources());
  for (int i = 1; i <= BATCH_SIZE + 3; ++i) call(engine, gradeRequest("lc" + to_string(i), "b", "2026-11-02"));
  const Json pending = call(engine, pendingRequest(""));
  CHECK_EQ(pending["total"].asInt(), BATCH_SIZE + 3);
  CHECK_EQ(static_cast<int>(pending["items"].size()), BATCH_SIZE);
  CHECK_EQ(pending["items"].items().back()["problem"].asStr(), "lc" + to_string(BATCH_SIZE));
}

static void testAttemptsBelongToTheNextGrade() {
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "r", "2026-11-02T22:00"));
  call(engine, gradeRequest("lc1", "y", "2026-11-04T22:00"));
  const string attempts =
      "[\"lc1/2026-11-05T10-00.cpp\",\"lc1/2026-11-02T21-30-10.cpp\",\"lc1/2026-11-02T21-30-2.cpp\","
      "\"lc1/2026-11-02T21-30.cpp\",\"lc1/2026-11-03T01-30.cpp\",\"lc1/2026-11-04T09-00.cpp\","
      "\"lc1/2026-11-04.cpp\",\"lc1/notes.txt\",\".DS_Store\",\"gone/2026-11-02T21-30.cpp\"]";
  const Json pending = call(engine, pendingRequest("", attempts));
  const vector<Json>& first = pending["items"].items()[0]["attempts"].items();
  CHECK_EQ(static_cast<int>(first.size()), 4);
  CHECK_EQ(first[0].asStr(), string("lc1/2026-11-02T21-30.cpp"));
  CHECK_EQ(first[1].asStr(), string("lc1/2026-11-02T21-30-2.cpp"));
  CHECK_EQ(first[2].asStr(), string("lc1/2026-11-02T21-30-10.cpp"));
  CHECK_EQ(first[3].asStr(), string("lc1/2026-11-03T01-30.cpp"));  // 1:30 AM is still the night of the 2nd
  // A day's file, named by the date alone, goes with that day's grade, ahead of a timed file from the same day.
  const vector<Json>& second = pending["items"].items()[1]["attempts"].items();
  CHECK_EQ(static_cast<int>(second.size()), 2);
  CHECK_EQ(second[0].asStr(), string("lc1/2026-11-04.cpp"));
  CHECK_EQ(second[1].asStr(), string("lc1/2026-11-04T09-00.cpp"));
}

static void testJudgementBecomesALogLine() {
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "r", "2026-11-02"));
  call(engine, gradeRequest("lc2", "y", "2026-11-03"));
  const string before = engine.saveState();
  const string attempts = "[\"lc1/2026-11-02T21-30.cpp\"]";
  const string good =
      "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"model_grade\":\"y\",\"recognised\":\"yes\",\"key_step\":\"own\","
      "\"topics\":[\"heap/top-k\"],\"flaws\":[\"off-by-one\"],\"note\":\"Right idea, wrong bound.\","
      "\"traced\":\"[1] returns 0, should be 1\",\"by\":\"test\"}";

  const Json saved = call(engine, judgementRequest(good, "", attempts));
  const Json& line = saved["line"];
  CHECK_EQ(line["title"].asStr(), string("Problem 1"));
  CHECK_EQ(line["your_grade"].asStr(), string("r"));
  CHECK_EQ(line["model_grade"].asStr(), string("y"));
  CHECK_EQ(line["recognised"].asStr(), string("yes"));
  CHECK_EQ(line["key_step"].asStr(), string("own"));
  CHECK_EQ(line["topics"].items()[0].asStr(), string("heap/top-k"));
  CHECK_EQ(line["flaws"].items()[0].asStr(), string("off-by-one"));
  CHECK_EQ(line["traced"].asStr(), string("[1] returns 0, should be 1"));
  CHECK_EQ(line["ts"].asStr(), string(COACH_NOW));
  CHECK(line["first_try"].asBool());
  CHECK(line["had_code"].asBool());
  CHECK(!line.has("abilities"));
  CHECK_EQ(saved["remaining"].asInt(), 1);
  CHECK(!saved["dirty"].asBool());
  CHECK_EQ(engine.saveState(), before);

  // With no code there is nothing to read, so only the id is needed, and flaws can't be named.
  const Json bare = call(engine, judgementRequest("{\"problem\":\"lc2\",\"date\":\"2026-11-03\"}"));
  CHECK(!bare["line"]["had_code"].asBool());
  CHECK(!bare["line"].has("model_grade"));
  CHECK_EQ(bare["line"]["your_grade"].asStr(), string("y"));
  expectRefused(engine, judgementRequest("{\"problem\":\"lc2\",\"date\":\"2026-11-03\",\"flaws\":[\"overflow\"]}"), "read from code");
  call(engine, judgementRequest("{\"problem\":\"lc2\",\"date\":\"2026-11-03\",\"recognised\":\"no\",\"flaws\":[]}"));

  const string log = line.dump() + "\n";
  expectRefused(engine, judgementRequest(good, log, attempts), "already judged");
  expectRefused(engine, judgementRequest("{\"problem\":\"lc1\",\"date\":\"2026-11-09\"}"), "no grade");
  expectRefused(engine, judgementRequest("{\"problem\":\"lc9\",\"date\":\"2026-11-02\"}"), "no grade");
  expectRefused(engine, judgementRequest("{\"date\":\"2026-11-02\"}"), "no grade");

  auto refusedWithCode = [&](const char* fields, const char* reason) {
    const string judgement = string("{\"problem\":\"lc1\",\"date\":\"2026-11-02\",") + fields + "}";
    expectRefused(engine, judgementRequest(judgement, "", attempts), reason);
  };
  refusedWithCode("\"recognised\":\"yes\",\"key_step\":\"own\",\"note\":\"x\"", "model_grade");
  refusedWithCode("\"model_grade\":\"g\",\"recognised\":\"yes\",\"key_step\":\"own\",\"note\":\"x\"", "model_grade");
  refusedWithCode("\"model_grade\":\"r\",\"key_step\":\"own\",\"note\":\"x\"", "recognised");
  refusedWithCode("\"model_grade\":\"r\",\"recognised\":\"maybe\",\"key_step\":\"own\",\"note\":\"x\"", "recognised");
  refusedWithCode("\"model_grade\":\"r\",\"recognised\":\"yes\",\"note\":\"x\"", "key_step");
  refusedWithCode("\"model_grade\":\"r\",\"recognised\":\"yes\",\"key_step\":\"mostly\",\"note\":\"x\"", "key_step");
  refusedWithCode("\"model_grade\":\"r\",\"recognised\":\"yes\",\"key_step\":\"own\"", "note");
  // Only names from the closed lists. Free labels, and the fields that held them, are gone.
  const string base = "\"model_grade\":\"r\",\"recognised\":\"yes\",\"key_step\":\"own\",\"note\":\"x\",";
  auto refusedField = [&](const string& field, const char* reason) { refusedWithCode((base + field).c_str(), reason); };
  refusedField("\"flaws\":\"off-by-one\"", "must be a list");
  refusedField("\"flaws\":[\"edge\"]", "is not a flaw");
  refusedField("\"flaws\":[\"bfs-minute-count-off-by-one\"]", "is not a flaw");
  refusedField("\"flaws\":[\"overflow\",\"overflow\"]", "twice");
  refusedField("\"topics\":\"heap\"", "must be a list");
  refusedField("\"topics\":[\"heap/nope\"]", "is not a topic");
  refusedField("\"topics\":[\"dp-base-case-late\"]", "is not a topic");
  refusedField("\"patterns\":[\"heap\"]", "no field called patterns");
  refusedField("\"abilities\":[\"edge\"]", "no field called abilities");
  refusedField("\"extra\":1", "extra");
  call(engine, judgementRequest("{\"problem\":\"lc1\",\"date\":\"2026-11-02\"," + base +
                                "\"topics\":[\"heap\",\"trie\"],\"flaws\":[]}", "", attempts));

  string tooMany = "\"topics\":[";
  for (int i = 0; i <= JUDGEMENT_MAX_TOPICS; ++i) tooMany += i ? ",\"heap\"" : "\"heap\"";
  refusedField(tooMany + "]", "at most");

  // The note limit counts characters, so a note in any language gets the same room.
  string fits;
  for (int i = 0; i < NOTE_MAX_CHARS; ++i) fits += "\xE9\x9D\x9E";
  const string longNote = "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"model_grade\":\"b\",\"recognised\":\"yes\","
                               "\"key_step\":\"own\",\"traced\":\"x\",\"note\":\"";
  call(engine, judgementRequest(longNote + fits + "\"}", "", attempts));
  expectRefused(engine, judgementRequest(longNote + fits + "x\"}", "", attempts), "280");

  /*
  You ran the code, so a grade kinder than yours has to show its work. Agreeing
  with you or being harder on the code doesn't.
  */
  refusedWithCode("\"model_grade\":\"b\",\"recognised\":\"yes\",\"key_step\":\"own\",\"note\":\"x\"", "traced");
  refusedWithCode("\"model_grade\":\"y\",\"recognised\":\"yes\",\"key_step\":\"own\",\"note\":\"x\"", "traced");
  call(engine, judgementRequest("{\"problem\":\"lc1\",\"date\":\"2026-11-02\"," + base + "\"flaws\":[]}", "", attempts));

  call(engine, gradeRequest("lc3", "y", "2026-11-04"));
  const string attemptsToo = "[\"lc1/2026-11-02T21-30.cpp\",\"lc3/2026-11-04T09-00.cpp\"]";
  const string onYellow = "{\"problem\":\"lc3\",\"date\":\"2026-11-04\",\"recognised\":\"yes\",\"key_step\":\"hint\",\"note\":\"x\",";
  expectRefused(engine, judgementRequest(onYellow + "\"model_grade\":\"b\"}", "", attemptsToo), "traced");
  call(engine, judgementRequest(onYellow + "\"model_grade\":\"y\"}", "", attemptsToo));
  call(engine, judgementRequest(onYellow + "\"model_grade\":\"r\"}", "", attemptsToo));

  // Too long a trace is refused like too long a note.
  const string longTrace(NOTE_MAX_CHARS + 1, 'x');
  expectRefused(engine, judgementRequest(onYellow + "\"model_grade\":\"b\",\"traced\":\"" + longTrace + "\"}", "", attemptsToo), "traced is");
}

static void testCommitSavesOnlyTheProfile() {
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "b", "2026-11-02"));
  const string before = engine.saveState();
  const Json saved = call(engine, commitRequest("# Profile"));
  CHECK(!saved.has("table"));   // the coach writes no probabilities
  CHECK(!saved["dirty"].asBool());
  CHECK_EQ(engine.saveState(), before);

  // Profile size, right at the limit and one past it.
  call(engine, commitRequest(string(PROFILE_MAX_BYTES, 'x')));
  expectRefused(engine, commitRequest(string(PROFILE_MAX_BYTES + 1, 'x')), "2048");
  expectRefused(engine, commitRequest("  \n "), "empty");
}

static void testDropIsOnlyForNothingToAdd() {
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "r", "2026-11-02"));
  call(engine, gradeRequest("lc2", "b", "2026-11-03"));
  call(engine, gradeRequest("lc3", "y", "2026-11-04"));
  const string before = engine.saveState();
  const string attempts = "[\"lc1/2026-11-02T21-30.cpp\",\"lc3/2026-11-04T09-00.cpp\"]";
  const string old = "{\"problem\":\"lc3\",\"date\":\"2026-11-04\",\"model_grade\":\"y\",\"note\":\"Said then.\"}\n";
  auto drop = [](const char* problem, const char* date, const char* reason) {
    return string("{\"problem\":\"") + problem + "\",\"date\":\"" + date + "\",\"reason\":\"" + reason + "\",\"by\":\"test\"}";
  };

  // No code: nothing to read, so it can go.
  const Json bare = call(engine, dropRequest(drop("lc2", "2026-11-03", "No code, the grade says it."), "", old, attempts));
  CHECK(bare["line"]["dropped"].asBool());
  CHECK(!bare["line"]["had_code"].asBool());
  CHECK_EQ(bare["line"]["reason"].asStr(), string("No code, the grade says it."));
  CHECK_EQ(bare["line"]["your_grade"].asStr(), string("b"));
  CHECK_EQ(bare["remaining"].asInt(), 2);
  CHECK(!bare["dirty"].asBool());
  CHECK_EQ(engine.saveState(), before);

  // Code with an earlier judgement that already says it: the coach may let it go.
  call(engine, dropRequest(drop("lc3", "2026-11-04", "The earlier judgement covers it."), "", old, attempts));
  // Code nobody has judged must be judged.
  expectRefused(engine, dropRequest(drop("lc1", "2026-11-02", "x"), "", old, attempts), "needs a judgement");

  const string log = bare["line"].dump() + "\n";
  expectRefused(engine, dropRequest(drop("lc2", "2026-11-03", "again"), log, old, attempts), "already judged or dropped");
  expectRefused(engine, dropRequest(drop("lc2", "2026-11-09", "x"), "", old, attempts), "no grade");
  expectRefused(engine, dropRequest("{\"problem\":\"lc2\",\"date\":\"2026-11-03\",\"reason\":\" \"}", "", old, attempts), "reason");
  expectRefused(engine, dropRequest("{\"problem\":\"lc2\",\"date\":\"2026-11-03\",\"reason\":\"x\",\"note\":\"y\"}", "", old, attempts),
                "no field called note");
  // A dropped attempt waits no more.
  CHECK_EQ(call(engine, pendingRequest(log, attempts, old))["total"].asInt(), 2);
}

static string logLine(const char* problem, const char* date, bool firstTry, const char* grade,
                           const char* topics, const char* flaws, const char* note) {
  return string("{\"problem\":\"") + problem + "\",\"title\":\"Title " + problem + "\",\"date\":\"" + date +
         "\",\"first_try\":" + (firstTry ? "true" : "false") + ",\"your_grade\":\"b\",\"model_grade\":\"" + grade +
         "\",\"topics\":" + topics + ",\"flaws\":" + flaws + ",\"note\":\"" + note + "\"}\n";
}

static string searchRequest(const string& fields, const string& log, const string& oldLog = "") {
  return "{\"cmd\":\"search\"," + fields + (fields.empty() ? "" : ",") + "\"log\":" + Json(log).dump() +
         ",\"old_log\":" + Json(oldLog).dump() + "}";
}

static void testSearchRecallsPastJudgements() {
  Engine engine;
  engine.loadResources(tinyResources());
  string log;
  log += logLine("lc1", "2026-11-01", true, "b", "[\"heap/top-k\"]", "[]", "Clean.");
  log += logLine("lc2", "2026-11-02", true, "r", "[\"heap\"]", "[\"off-by-one\"]", "Bound was wrong.");
  log += "this line was cut short by a crash\n\n";
  log += logLine("lc3", "2026-11-03", false, "y", "[\"trie\"]", "[\"off-by-one\"]", "Bound again, on a BOUNDARY.");
  log += logLine("lc1", "2026-11-04", false, "b", "[\"heap/top-k\"]", "[]", "Solid.");

  auto problems = [](const Json& reply) {
    string out;
    for (const Json& row : reply["rows"].items()) out += row["problem"].asStr() + " ";
    return out;
  };

  // Newest first, and the counts cover every match. A pattern also finds its finer topics.
  const Json heap = call(engine, searchRequest("\"label\":\"heap\"", log));
  CHECK_EQ(problems(heap), string("lc1 lc2 lc1 "));
  CHECK_EQ(heap["total"].asInt(), 3);
  CHECK_EQ(heap["first_tries"].asInt(), 2);
  CHECK_EQ(heap["first_try_passes"].asInt(), 1);

  // A pass needs both grades. You ran the code and the model only read it.
  auto passes = [&](const char* line) { return call(engine, searchRequest("", line))["first_try_passes"].asInt(); };
  CHECK_EQ(passes("{\"first_try\":true,\"your_grade\":\"r\",\"model_grade\":\"b\"}"), 0);
  CHECK_EQ(passes("{\"first_try\":true,\"your_grade\":\"b\",\"model_grade\":\"y\"}"), 0);
  CHECK_EQ(passes("{\"first_try\":true,\"your_grade\":\"b\",\"model_grade\":\"b\"}"), 1);
  CHECK_EQ(passes("{\"first_try\":true,\"your_grade\":\"b\"}"), 1);  // no code, so yours is all there is

  CHECK_EQ(problems(call(engine, searchRequest("\"label\":\"heap/top-k\"", log))), string("lc1 lc1 "));
  CHECK_EQ(problems(call(engine, searchRequest("\"label\":\"heap/two\"", log))), string(""));  // whole names only
  CHECK_EQ(problems(call(engine, searchRequest("\"label\":\"hea\"", log))), string(""));
  CHECK_EQ(problems(call(engine, searchRequest("\"label\":\"off-by-one\"", log))), string("lc3 lc2 "));
  CHECK_EQ(problems(call(engine, searchRequest("\"problem\":\"lc1\"", log))), string("lc1 lc1 "));
  CHECK_EQ(problems(call(engine, searchRequest("\"text\":\"bound\"", log))), string("lc3 lc2 "));
  CHECK_EQ(problems(call(engine, searchRequest("\"text\":\"TITLE LC3\"", log))), string("lc3 "));
  CHECK_EQ(problems(call(engine, searchRequest("\"text\":\"bound\",\"label\":\"trie\"", log))), string("lc3 "));
  CHECK_EQ(call(engine, searchRequest("\"label\":\"nothing-here\"", log))["total"].asInt(), 0);
  CHECK_EQ(call(engine, searchRequest("", ""))["total"].asInt(), 0);
  CHECK_EQ(call(engine, searchRequest("", log))["total"].asInt(), 4);

  /*
  The judgements from before 2026-11-09 answer too, older than everything in the
  new log and marked as such. Their old labels still match. Once the new log has
  an attempt, judged or dropped, its old line stops counting.
  */
  const string old = "{\"problem\":\"lc6\",\"date\":\"2026-10-30\",\"patterns\":[\"trie\"],\"flaws\":[\"bound-wrong\"],\"note\":\"Old.\"}\n"
                          "{\"problem\":\"lc7\",\"date\":\"2026-10-30\",\"topics\":[\"trie\"],\"abilities\":[\"edge\"],\"note\":\"Older.\"}\n";
  const Json trie = call(engine, searchRequest("\"label\":\"trie\"", log, old));
  CHECK_EQ(problems(trie), string("lc3 lc7 lc6 "));
  CHECK(trie["rows"].items()[1]["earlier_shape"].asBool());
  CHECK(!trie["rows"].items()[0].has("earlier_shape"));
  CHECK_EQ(problems(call(engine, searchRequest("\"label\":\"edge\"", log, old))), string("lc7 "));
  CHECK_EQ(problems(call(engine, searchRequest("\"label\":\"bound-wrong\"", log, old))), string("lc6 "));
  const string carried = log + "{\"problem\":\"lc6\",\"date\":\"2026-10-30\",\"dropped\":true}\n";
  CHECK_EQ(problems(call(engine, searchRequest("\"label\":\"trie\"", carried, old))), string("lc3 lc7 "));

  // The cap holds, however much is asked for.
  string longLog;
  for (int i = 0; i < SEARCH_MAX_LINES + 20; ++i) longLog += logLine("lc1", "2026-11-01", true, "b", "[]", "[]", "x");
  CHECK_EQ(static_cast<int>(call(engine, searchRequest("\"limit\":9999", longLog))["rows"].size()), SEARCH_MAX_LINES);
  CHECK_EQ(call(engine, searchRequest("\"limit\":9999", longLog))["total"].asInt(), SEARCH_MAX_LINES + 20);
  CHECK_EQ(static_cast<int>(call(engine, searchRequest("", longLog))["rows"].size()), SEARCH_DEFAULT_LINES);
  CHECK_EQ(static_cast<int>(call(engine, searchRequest("\"limit\":3", longLog))["rows"].size()), 3);
  CHECK_EQ(static_cast<int>(call(engine, searchRequest("\"limit\":0", longLog))["rows"].size()), 1);
}

static void testTopicListIsClosed() {
  Engine engine;
  engine.loadResources(tinyResources());
  const Json list = call(engine, "{\"cmd\":\"topics\"}");
  const vector<Json>& patterns = list["patterns"].items();
  CHECK_EQ(static_cast<int>(patterns.size()), 4);
  CHECK_EQ(patterns[1]["slug"].asStr(), string("heap"));
  CHECK_EQ(patterns[1]["topics"].items()[1]["slug"].asStr(), string("heap/two-heaps"));
  CHECK_EQ(patterns[1]["topics"].items()[1]["signal"].asStr(), string("A running median."));
  CHECK(!patterns[2].has("topics"));   // trie has no finer topics, so it is a topic itself

  // Every flaw but the one the clock sets, in the order you solve a problem.
  const vector<Json>& flaws = list["flaws"].items();
  CHECK_EQ(static_cast<int>(flaws.size()), FLAW_COUNT - 1);
  CHECK_EQ(flaws[0]["slug"].asStr(), string("misread-problem"));
  CHECK_EQ(flaws[2]["slug"].asStr(), string("missing-piece"));
  for (const Json& f : flaws) {
    CHECK(f["slug"].asStr() != "slow-to-write");
    CHECK(!f.has("drilled_by"));   // a slip is practised on a problem, so the coach is told of no deck
  }
  const vector<Json>& recognised = list["thinking"]["recognised"].items();
  CHECK_EQ(static_cast<int>(recognised.size()), 4);
  CHECK_EQ(recognised[2].asStr(), string("wrong"));
  CHECK(list["thinking"].has("reached_for"));
  CHECK_EQ(list["thinking"]["key_step"].items()[1].asStr(), string("hint"));
  CHECK(!list.has("abilities"));

  // The command that let a model attach problems by hand is gone, and so is the one that loaded tables.
  CHECK(engine.call("{\"cmd\":\"candidates\",\"text\":\"x\",\"now\":\"2026-11-02\"}").find("\"ok\":false") != string::npos);
  CHECK(engine.call("{\"cmd\":\"table\",\"text\":\"{}\"}").find("unknown command") != string::npos);
}

static void testOverviewSaysWhereThingsStand() {
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "b", "2026-11-02"));
  call(engine, gradeRequest("lc2", "b", "2026-11-03"));
  auto overview = [&](const string& log, const string& old = "") {
    return call(engine, string("{\"cmd\":\"overview\",\"now\":\"") + COACH_NOW + "\",\"log\":" + Json(log).dump() +
                            ",\"old_log\":" + Json(old).dump() + "}");
  };

  // The app asks where these files live, so the layout stays in one place.
  const Json rules = call(engine, "{\"cmd\":\"rules\"}");
  CHECK_EQ(rules["judgementsFile"].asStr(), string(JUDGEMENTS_FILE));
  CHECK_EQ(rules["oldJudgementsFile"].asStr(), string(OLD_JUDGEMENTS_FILE));
  CHECK_EQ(rules["lastReviewFile"].asStr(), string(LAST_REVIEW_FILE));
  CHECK(!rules.has("labelsFile"));

  const string old = "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"model_grade\":\"b\"}\n";
  const Json first = overview("", old);
  CHECK_EQ(first["pending"].asInt(), 2);
  CHECK_EQ(first["pending_with_earlier_judgement"].asInt(), 1);
  CHECK_EQ(first["batch_size"].asInt(), BATCH_SIZE);
  CHECK_EQ(first["judged"].asInt(), 0);
  CHECK_EQ(static_cast<int>(first["flaw_counts"].size()), 0);

  // One judged and one dropped: nothing waits.
  const string log = "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"flaws\":[\"overflow\"]}\n"
                          "{\"problem\":\"lc2\",\"date\":\"2026-11-03\",\"dropped\":true}\n";
  const Json later = overview(log, old);
  CHECK_EQ(later["pending"].asInt(), 0);
  CHECK_EQ(later["pending_with_earlier_judgement"].asInt(), 0);
  CHECK_EQ(later["judged"].asInt(), 1);
  CHECK_EQ(later["dropped"].asInt(), 1);

  // How often each flaw and thinking label came up, most common flaw first. Old free flaws aren't flaws.
  const string judged = "{\"flaws\":[\"overflow\",\"off-by-one\"],\"recognised\":\"yes\",\"key_step\":\"hint\"}\n"
                             "{\"flaws\":[\"off-by-one\"],\"recognised\":\"no\",\"key_step\":\"missed\"}\n"
                             "{\"flaws\":[\"bfs-minute-count\"]}\n{\"flaws\":[\"overflow\"],\"dropped\":true}\n";
  const Json counted = overview(judged);
  const vector<Json::Member>& flaws = counted["flaw_counts"].members();
  CHECK_EQ(static_cast<int>(flaws.size()), 2);
  CHECK_EQ(flaws[0].first, string("off-by-one"));
  CHECK_EQ(flaws[0].second.asInt(), 2);
  CHECK_EQ(flaws[1].first, string("overflow"));
  CHECK_EQ(flaws[1].second.asInt(), 1);
  CHECK_EQ(counted["thinking_counts"]["key_step"]["missed"].asInt(), 1);
  CHECK_EQ(counted["thinking_counts"]["recognised"]["yes"].asInt(), 1);
}

static void testCoachCanOnlyAddChecks() {
  Engine engine;
  engine.loadResources(tinyResources());
  // A clean first try, then a clean check: gone.
  call(engine, gradeRequest("lc1", "b", "2026-11-02T20:00"));
  call(engine, gradeRequest("lc1", "b", "2026-11-20T20:00"));
  const string before = engine.saveState();
  auto track = [&]() {
    return call(engine, "{\"cmd\":\"card\",\"id\":\"lc1\",\"now\":\"2026-11-21T09:00\"}")["card"]["track"].asStr();
  };
  CHECK_EQ(track(), string("retired"));

  // The coach read the code of that last check and found a bug: it keeps going, as the coach graded it.
  const Json loaded = loadJudgements(engine, "{\"problem\":\"lc1\",\"date\":\"2026-11-20\",\"model_grade\":\"y\",\"had_code\":true}\n");
  CHECK(!loaded["dirty"].asBool());
  CHECK_EQ(track(), string("yellow"));
  const Json steps = call(engine, "{\"cmd\":\"explain\",\"id\":\"lc1\"}")["steps"];
  CHECK_EQ(steps.items().back()["click"].asStr(), string("b"));     // your click stays yours
  CHECK_EQ(steps.items().back()["coach"].asStr(), string("y"));

  // A coach grade as kind as yours changes nothing, and neither does one on a check that wasn't the last.
  loadJudgements(engine, "{\"problem\":\"lc1\",\"date\":\"2026-11-20\",\"model_grade\":\"b\"}\n");
  CHECK_EQ(track(), string("retired"));
  loadJudgements(engine, "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"model_grade\":\"r\"}\n");
  CHECK_EQ(track(), string("retired"));

  // A judgement from before 2026-11-09 counts by its grade, until the new log judges or drops that attempt.
  const string old = "{\"problem\":\"lc1\",\"date\":\"2026-11-20\",\"model_grade\":\"r\",\"recognised\":\"no\"}\n";
  loadJudgements(engine, "", old);
  CHECK_EQ(track(), string("red"));
  loadJudgements(engine, "{\"problem\":\"lc1\",\"date\":\"2026-11-20\",\"dropped\":true}\n", old);
  CHECK_EQ(track(), string("retired"));

  // None of it is ever written to your progress.
  CHECK_EQ(engine.saveState(), before);
}

// ── Randomized: thousands of clicks, checked against invariants ───────────

static void testRandomClicksKeepInvariants() {
  Rng rng(12345);
  for (const Schedule* schedule : {&CARD_SCHEDULE, &PROBLEM_SCHEDULE}) {
    for (int run = 0; run < 2000; ++run) {
      CardState s;
      int day = 1;
      int consecutiveBluesOnBlueTrack = 0;
      const uint64_t seed = rng.next();
      for (int i = 0; i < 40; ++i) {
        day += rng.below(4);
        const Click click = static_cast<Click>(rng.below(3));
        const CardState before = s;
        s = applyClick(s, click, day, *schedule, seed);
        CHECK(s.track != Track::New);
        if (s.track == Track::Retired) {
          CHECK_EQ(s.due, 0);
          CHECK(!isDue(s, 1000000));
        } else {
          CHECK(s.step >= 0 && s.step < checkCount(*schedule, s.track));
          // The gap always runs from the day the click really happened, extra days and all.
          const Gap gap = checkGap(*schedule, s.track, s.step);
          CHECK(s.due >= day + gap.base && s.due <= day + gap.base + gap.spread);
        }
        // Only a Blue on the last check of a track ever retires anything.
        if (s.track == Track::Retired && before.track != Track::Retired) {
          CHECK(click == Click::Blue);
          CHECK_EQ(before.step, checkCount(*schedule, before.track) - 1);
          // A card retires only from Blue, never skipping its two checks.
          if (schedule->blueAfterLast) CHECK(before.track == Track::Blue);
        }
        if (schedule->blueAfterLast) {
          consecutiveBluesOnBlueTrack = s.track == Track::Blue && before.track == Track::Blue ? consecutiveBluesOnBlueTrack + 1 : 0;
          CHECK(consecutiveBluesOnBlueTrack < 2);
        }
      }
    }
  }
}

static string nowOn(int date) { return formatDate(date) + "T20:00"; }

static void testRandomSessionsKeepInvariants() {
  Rng rng(777);
  for (int run = 0; run < 12; ++run) {
    Engine engine;
    engine.loadResources(tinyResources());
    const int cap = 2 + rng.below(4);
    engine.loadState("{\"settings\":{\"codeCap\":" + to_string(cap) + ",\"codeNew\":1,\"cardCap\":" +
                     to_string(cap) + ",\"cardNew\":1}}");
    int date = parseDate("2026-11-02");
    set<string> pulled;   // every problem a pull added
    for (int step = 0; step < 90; ++step) {
      date += 1 + (rng.below(5) == 0 ? rng.below(6) : 0);  // some days get skipped
      const string now = nowOn(date);
      Json day = Json::parse(engine.call("{\"cmd\":\"today\",\"now\":\"" + now + "\"}"));
      CHECK(day["ok"].asBool());

      // The bound holds on every single day. Only pulls you haven't finished go over it.
      int waitingPulls = 0;
      for (const Json& item : day["code"]["items"].items()) {
        waitingPulls += pulled.count(item["id"].asStr()) && item["track"].asStr() == "new";
      }
      CHECK(static_cast<int>(day["code"]["items"].size()) <= cap + waitingPulls);
      CHECK(static_cast<int>(day["cards"]["items"].size()) <= cap);

      // Nothing appears twice, and nothing retired is ever queued.
      set<string> seen;
      for (const Json& item : day["code"]["items"].items()) {
        CHECK(seen.insert(item["id"].asStr()).second);
        CHECK(item["track"].asStr() != "retired");
      }

      for (const char* lane : {"code", "cards"}) {
        for (const Json& item : day[lane]["items"].items()) {
          if (rng.below(3) == 0) continue;  // leave some undone, so they roll over
          const char* grade = rng.below(2) ? "b" : rng.below(2) ? "y" : "r";
          engine.call(gradeRequest(item["id"].asStr(), grade, now.c_str()));
        }
      }
      if (rng.below(4) == 0) engine.call("{\"cmd\":\"undo\"}");
      if (rng.below(6) == 0) {
        set<string> queued;
        const Json before = Json::parse(engine.call("{\"cmd\":\"today\",\"now\":\"" + now + "\"}"));
        for (const Json& item : before["code"]["items"].items()) queued.insert(item["id"].asStr());
        const Json more = Json::parse(engine.call("{\"cmd\":\"more\",\"lane\":\"code\",\"now\":\"" + now + "\"}"));
        for (const Json& item : more["code"]["items"].items()) {
          if (!queued.count(item["id"].asStr())) pulled.insert(item["id"].asStr());
        }
      }

      // Saving and loading never changes anything.
      const string saved = engine.saveState();
      Engine copy;
      copy.loadResources(tinyResources());
      CHECK(copy.loadState(saved));
      CHECK_EQ(copy.saveState(), saved);
      const string request = "{\"cmd\":\"insights\",\"now\":\"" + now + "\"}";
      CHECK_EQ(copy.call(request), engine.call(request));
    }
  }
}

static void testEveryDueCardGetsServedEventually() {
  /*
  Bounded means delayed, never dropped: fail 30 cards at once, then only ever
  serve what the queue offers, passing everything, and they must all advance.
  */
  Engine engine;
  engine.loadResources(tinyResources());
  engine.loadState("{\"settings\":{\"codeCap\":3,\"codeNew\":0,\"cardCap\":1,\"cardNew\":0}}");
  int date = parseDate("2026-11-02");
  for (int i = 1; i <= 30; ++i) call(engine, gradeRequest("lc" + to_string(i), "r", nowOn(date).c_str()));

  int served = 0;
  for (int day = 0; day < 40 && served < 30; ++day) {
    date += 1;
    Json today = call(engine, "{\"cmd\":\"today\",\"now\":\"" + nowOn(date) + "\"}");
    for (const Json& item : today["code"]["items"].items()) {
      if (item["slot"].asStr() != "review" || item["step"].asInt() != 0 || item["track"].asStr() != "red") continue;
      call(engine, gradeRequest(item["id"].asStr(), "b", nowOn(date).c_str()));
      ++served;
    }
  }
  CHECK_EQ(served, 30);
}

// ── The new-problem picker ────────────────────────────────────────────────

// The patterns in tinyResources go in a cycle: lc1 heap, lc2 trie, lc3 greedy, lc4 two-pointers.
static string tinyPattern(const string& id) {
  static const char* const names[] = {"two-pointers", "heap", "trie", "greedy"};
  return names[atoi(id.c_str() + 2) % 4];
}

static Json onePick(Engine& engine, const char* now, int roll) {
  const Json picks = call(engine, string("{\"cmd\":\"recommend\",\"now\":\"") + now + "\",\"n\":1,\"roll\":" + to_string(roll) + "}");
  return picks["rows"].items().empty() ? Json() : picks["rows"].items()[0];
}

static void testNewPatternsTakeThreeInFive() {
  Engine engine;
  engine.loadResources(tinyResources());
  // Nothing tried, so nothing to pull from: four picks, four patterns, each a first meeting.
  const Json picks = call(engine, "{\"cmd\":\"recommend\",\"now\":\"2026-11-02T09:00\",\"n\":4}");
  set<string> patterns;
  for (const Json& row : picks["rows"].items()) {
    CHECK_EQ(row["slot"].asStr(), string("new-pattern"));
    CHECK(row["why"].asStr().rfind("New pattern: ", 0) == 0);
    patterns.insert(tinyPattern(row["id"].asStr()));
  }
  CHECK_EQ(static_cast<int>(patterns.size()), 4);

  /*
  One pattern tried, three still new. NEW_PATTERN_SHARE of the new problems go to a new one, evenly
  spaced: with one problem graded these are numbers 1 to 4, and at 60% numbers 1, 3 and 4 explore.
  Number 2 comes from the pull, which has only two-pointers to draw from.
  */
  call(engine, gradeRequest("lc4", "r", "2026-11-02T09:00"));   // two-pointers
  const Json next = call(engine, "{\"cmd\":\"recommend\",\"now\":\"2026-11-03T09:00\",\"n\":5}");
  const vector<Json>& rows = next["rows"].items();
  CHECK_EQ(static_cast<int>(rows.size()), 5);
  set<string> fresh;
  for (size_t k = 0; k < 4; ++k) {
    const bool explores = k != 1;
    const string pattern = tinyPattern(rows[k]["id"].asStr());
    CHECK_EQ(rows[k]["slot"].asStr(), string(explores ? "new-pattern" : "practice"));
    CHECK_EQ(pattern == "two-pointers", !explores);
    if (explores) fresh.insert(pattern);
  }
  CHECK_EQ(static_cast<int>(fresh.size()), 3);   // each new pattern once
  CHECK(rows[0]["why"].asStr().find("60% of new problems go to one until none is left.") != string::npos);
  // By the fifth, every pattern holds a place in the queue. Only then does one get a second problem, and it says so.
  CHECK_EQ(rows[4]["slot"].asStr(), string("practice"));
  CHECK(rows[4]["why"].asStr().find("Every pattern you could get already has a problem waiting") != string::npos);
  for (size_t k = 0; k < 4; ++k) CHECK(rows[k]["why"].asStr().find("already has a problem waiting") == string::npos);
}

static void testAWaitingProblemHoldsItsPlace() {
  /*
  Being picked is not a result. A new problem waiting in the queue holds its pattern's place, so
  that pattern isn't picked again, and nothing else about it counts until it is graded. Every
  pattern was missed once on 11-02, and lc5, a heap problem, is waiting in the next day's queue.
  */
  auto heapPicks = [](bool waiting, const char* gradeIt) {
    Engine engine;
    engine.loadResources(tinyResources());
    const string plan = waiting ? R"(,"plan":{"date":"2026-11-03","pulls":0,"code":[{"id":"lc5","slot":"practice","why":"x"}],"cards":[]})" : "";
    CHECK(engine.loadState(R"({"v":2,"settings":{},"events":[{"id":"lc1","g":"r","d":"2026-11-02"},{"id":"lc2","g":"r","d":"2026-11-02"},
      {"id":"lc3","g":"r","d":"2026-11-02"},{"id":"lc4","g":"r","d":"2026-11-02"}])" + plan + "}"));
    if (gradeIt) call(engine, gradeRequest("lc5", gradeIt, "2026-11-03T09:00"));
    int heap = 0;
    for (int roll = 0; roll < 400; ++roll) heap += tinyPattern(onePick(engine, "2026-11-03T10:00", roll)["id"].asStr()) == "heap";
    return heap;
  };
  const int before = heapPicks(false, nullptr);
  CHECK(before > 60);                          // about a quarter, with nothing waiting
  CHECK_EQ(heapPicks(true, nullptr), 0);       // held while lc5 waits
  // The grade is what counts. Missed, heap is back and comes up more. Solved, it steps back.
  CHECK(heapPicks(true, "r") > before);
  CHECK(heapPicks(true, "b") * 2 < before);
}

// Twelve patterns of three problems each, all asked alike: lc1 to lc3 are the first pattern, and so on.
static string twelvePatterns() {
  string patterns, problems;
  for (int p = 0; p < 12; ++p) {
    patterns += string(p ? "," : "") + "{\"slug\":\"pattern-" + to_string(p) + "\",\"name\":\"Pattern " + to_string(p) + "\"}";
    for (int k = 1; k <= 3; ++k) {
      const int n = p * 3 + k;
      problems += string(n > 1 ? "," : "") + "{\"n\":" + to_string(n) + ",\"t\":\"Problem " + to_string(n) +
                  "\",\"d\":2,\"r\":1700,\"f\":50,\"p\":[" + to_string(p) + "]}";
    }
  }
  return "{\"patterns\":[" + patterns + "],\"tags\":[],\"companies\":[],\"decks\":[],\"cards\":[],\"problems\":[" + problems + "]}";
}

static void testAskingForMoreNeverRepeatsANewPattern() {
  /*
  Regression. Each pull builds its picks afresh, and a pattern whose problem was waiting ungraded
  still counted as new, so the most asked one came again and again. Now what waits holds its place.
  */
  Engine engine;
  engine.loadResources(twelvePatterns());
  Json day = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-02T09:00\"}");
  for (int pull = 0; pull < 10; ++pull) {
    day = call(engine, "{\"cmd\":\"more\",\"now\":\"2026-11-02T10:00\",\"lane\":\"code\",\"n\":1}");
  }
  set<int> patterns;
  const vector<Json>& items = day["code"]["items"].items();
  CHECK_EQ(static_cast<int>(items.size()), CODE_NEW + 10);
  for (const Json& item : items) {
    CHECK_EQ(item["slot"].asStr(), string("new-pattern"));
    patterns.insert((atoi(item["id"].asStr().c_str() + 2) - 1) / 3);
  }
  CHECK_EQ(static_cast<int>(patterns.size()), 12);   // twelve problems, twelve patterns

  // With every pattern waiting, one more can't be a new pattern, and says so.
  day = call(engine, "{\"cmd\":\"more\",\"now\":\"2026-11-02T11:00\",\"lane\":\"code\",\"n\":1}");
  CHECK_EQ(day["code"]["items"].items().back()["slot"].asStr(), string("practice"));

  // The dry run sees the queue too: nothing it shows is a pattern already waiting as new.
  Engine dry;
  dry.loadResources(twelvePatterns());
  const auto waiting = laneIds(call(dry, "{\"cmd\":\"today\",\"now\":\"2026-11-02T09:00\"}"), "code");
  set<int> taken;
  for (const string& id : waiting) taken.insert((atoi(id.c_str() + 2) - 1) / 3);
  for (int roll = 0; roll < 50; ++roll) {
    const Json pick = onePick(dry, "2026-11-02T09:30", roll);
    CHECK(!taken.count((atoi(pick["id"].asStr().c_str() + 2) - 1) / 3));
  }
}

static void testPullsKeepTheSpacing() {
  /*
  The spacing of new patterns counts what is waiting too, so it holds from one pull to the next. One
  problem graded, two in the day's queue and two pulled make these new problems 1 to 4, as in the
  test above: numbers 1, 3 and 4 go to a new pattern.
  */
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc4", "r", "2026-11-02T09:00"));   // two-pointers
  Json day = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-02T10:00\"}");
  for (int pull = 0; pull < 2; ++pull) {
    day = call(engine, "{\"cmd\":\"more\",\"now\":\"2026-11-02T10:00\",\"lane\":\"code\",\"n\":1}");
  }
  vector<string> slots;
  for (const Json& item : day["code"]["items"].items()) {
    if (item["id"].asStr() != "lc4") slots.push_back(item["slot"].asStr());
  }
  sort(slots.begin(), slots.end());
  CHECK(slots == vector<string>({"new-pattern", "new-pattern", "new-pattern", "practice"}));
}

static void testNewTopicsKeepComing() {
  Engine engine;
  engine.loadResources(tinyResources());
  for (const char* id : {"lc1", "lc2", "lc3", "lc4"}) call(engine, gradeRequest(id, "b", "2026-11-02T09:00"));
  /*
  Every pattern tried, so the pull decides, except that one pick in
  NEW_GROUND_EVERY goes to a finer topic never done. This is study day 2, so of
  four picks it's the third: two heaps, the one topic left.
  */
  int topical = 0;
  for (int roll = 0; roll < 50; ++roll) {
    const Json picks = call(engine, "{\"cmd\":\"recommend\",\"now\":\"2026-11-03T09:00\",\"n\":4,\"roll\":" + to_string(roll) + "}");
    const vector<Json>& rows = picks["rows"].items();
    CHECK_EQ(static_cast<int>(rows.size()), 4);
    for (size_t k = 0; k < rows.size(); ++k) {
      if (rows[k]["slot"].asStr() != "new-topic") {
        CHECK_EQ(rows[k]["slot"].asStr(), string("practice"));
        continue;
      }
      ++topical;
      CHECK_EQ(static_cast<int>(k), 2);
      CHECK(rows[k]["id"].asStr() == "lc13" || rows[k]["id"].asStr() == "lc17");
      CHECK(rows[k]["why"].asStr().rfind("New topic: Two heaps, in Heap.", 0) == 0);
    }
  }
  // Unless an earlier practice pick already landed on that topic, which subpatterns make likely: a
  // practice pick on heap goes to the subpattern it hasn't had, not to a problem with none.
  CHECK(topical > 25);

  // With every finer topic done, that place goes to practice too.
  call(engine, gradeRequest("lc13", "b", "2026-11-02T09:00"));
  const Json after = call(engine, "{\"cmd\":\"recommend\",\"now\":\"2026-11-03T09:00\",\"n\":4}");
  for (const Json& row : after["rows"].items()) {
    CHECK_EQ(row["slot"].asStr(), string("practice"));
  }
}

static void testSolvedProblemsStepBackAndMissesDoNot() {
  // Every pattern tried once; heap has `moreHeap` further first tries. All of them graded `grade`, on day one.
  auto heapPicks = [](const char* grade, int moreHeap, int idleDays) {
    Engine engine;
    engine.loadResources(tinyResources());
    for (const char* id : {"lc1", "lc2", "lc3", "lc4"}) call(engine, gradeRequest(id, grade, "2026-11-02T09:00"));
    for (int k = 1; k <= moreHeap; ++k) call(engine, gradeRequest("lc" + to_string(1 + 4 * k), grade, "2026-11-02T09:00"));
    // Study days pass only when something is graded, so a card a day moves the clock on.
    int date = parseDate("2026-11-03");
    for (int d = 0; d < idleDays; ++d, ++date) {
      call(engine, gradeRequest("signals-" + to_string(d + 1), "b", nowOn(date).c_str()));
    }
    int heap = 0;
    for (int roll = 0; roll < 400; ++roll) {
      const Json pick = onePick(engine, nowOn(date).c_str(), roll);
      CHECK_EQ(pick["slot"].asStr(), string("practice"));
      heap += tinyPattern(pick["id"].asStr()) == "heap";
    }
    return heap;
  };
  const int fresh = heapPicks("b", 0, 0), solved = heapPicks("b", 3, 0);
  CHECK(fresh > 60);            // about a quarter
  CHECK(solved * 3 < fresh);    // three more solved lately: an eighth of the pull, and it went best besides
  // A week of study days later it is back in play, though still behind the others: it went best.
  const int later = heapPicks("b", 3, 8);
  CHECK(later > solved * 2);
  CHECK(later < fresh);

  // Misses are the opposite. Three more on heap step nothing back, and its weaker code brings it up a little more.
  const int missed = heapPicks("r", 0, 0), missedMore = heapPicks("r", 3, 0);
  CHECK(abs(missed - fresh) < 40);   // one miss each: the same quarter
  CHECK(missedMore > missed + 10);
  CHECK(missedMore > solved * 5);
}

static void testThePullFollowsWorkingCode() {
  // Heap: easy problems, all missed. Trie: hard ones, all solved. Greedy: one in the middle, to be picked from.
  string problems;
  for (int n = 1; n <= 8; ++n) {
    problems += "{\"n\":" + to_string(n) + ",\"t\":\"Heap\",\"d\":1,\"r\":1100,\"f\":50,\"p\":[0]},";
    problems += "{\"n\":" + to_string(10 + n) + ",\"t\":\"Trie\",\"d\":3,\"r\":2300,\"f\":50,\"p\":[1]},";
  }
  const string json = "{\"patterns\":[{\"slug\":\"heap\",\"name\":\"Heap\"},{\"slug\":\"trie\",\"name\":\"Trie\"}],"
                      "\"tags\":[],\"companies\":[],\"decks\":[{\"slug\":\"os\",\"title\":\"Operating systems\"}],"
                      "\"cards\":[{\"id\":\"os-1\",\"type\":\"flash\",\"deck\":\"os\",\"q\":\"Q\",\"a\":\"A\",\"patterns\":[],\"level\":1}],"
                      "\"problems\":[" + problems.substr(0, problems.size() - 1) + "]}";
  Engine engine;
  engine.loadResources(json);
  string log;
  for (int n = 1; n <= 5; ++n) {
    call(engine, gradeRequest("lc" + to_string(n), "r", "2026-11-02T09:00"));
    call(engine, gradeRequest("lc" + to_string(10 + n), "b", "2026-11-02T09:00"));
  }
  // The coach names the slip on the first heap miss.
  loadJudgements(engine, "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"model_grade\":\"r\",\"recognised\":\"yes\","
                         "\"key_step\":\"own\",\"flaws\":[\"off-by-one\"],\"had_code\":true}\n");
  // Eight study days on, nothing recent is stepping either pattern back.
  int date = parseDate("2026-11-03");
  for (int d = 0; d < 8; ++d, ++date) call(engine, gradeRequest("os-1", "b", nowOn(date).c_str()));

  int heap = 0;
  string heapWhy, trieWhy;
  for (int roll = 0; roll < 400; ++roll) {
    const Json pick = onePick(engine, nowOn(date).c_str(), roll);
    CHECK_EQ(pick["slot"].asStr(), string("practice"));
    const bool onHeap = atoi(pick["id"].asStr().c_str() + 2) < 10;
    heap += onHeap;
    (onHeap ? heapWhy : trieWhy) = pick["why"].asStr();
  }
  // Asked alike, so by that alone it would be half and half. The limits make it WEAK_PULL_MOST against WEAK_PULL_LEAST.
  const double share = WEAK_PULL_MOST / (WEAK_PULL_MOST + WEAK_PULL_LEAST);
  CHECK(abs(heap - 400 * share) < 30);
  CHECK(heapWhy.find("below your overall") != string::npos);
  CHECK(heapWhy.find("so it comes up 4.0 times as often.") != string::npos);
  CHECK(trieWhy.find("above your overall") != string::npos);
  CHECK(trieWhy.find("so it comes up half as often.") != string::npos);
  // The slip is named where it showed, so you know what to watch for.
  CHECK(heapWhy.find("The slip that shows most here: Off by one, or wrong bounds.") != string::npos);
  CHECK(trieWhy.find("The slip that shows most here") == string::npos);
}

static void testTargetCompanyNarrowsThePicks() {
  Engine engine;
  engine.loadResources(tinyResources(6));   // lc1 to lc6 are asked at Google
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"company\":\"google\"}}");
  auto picked = [&] {
    set<string> ids;
    for (int roll = 0; roll < 100; ++roll) {
      const Json picks = call(engine, "{\"cmd\":\"recommend\",\"now\":\"2026-11-02T09:00\",\"n\":4,\"roll\":" + to_string(roll) + "}");
      for (const Json& row : picks["rows"].items()) ids.insert(row["id"].asStr());
    }
    return ids;
  };
  const auto google = picked();
  CHECK(google.size() >= 4);
  for (const string& id : google) CHECK(atoi(id.c_str() + 2) <= 6);
  // Any company: everything is back.
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"company\":\"\"}}");
  const auto any = picked();
  CHECK(any_of(any.begin(), any.end(), [](const string& id) { return atoi(id.c_str() + 2) > 6; }));
}

static void testSlipsNeverCountAgainstThePattern() {
  // Two misses on heap. Judged, the coach says the thinking was right and the code slipped.
  auto insights = [](bool judged) {
    Engine engine;
    engine.loadResources(tinyResources());
    call(engine, gradeRequest("lc1", "r", "2026-11-02T09:00"));
    call(engine, gradeRequest("lc5", "r", "2026-11-02T09:00"));
    if (judged) {
      loadJudgements(engine,
          "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"recognised\":\"yes\",\"key_step\":\"own\",\"flaws\":[\"off-by-one\"],\"had_code\":true}\n"
          "{\"problem\":\"lc5\",\"date\":\"2026-11-02\",\"recognised\":\"yes\",\"key_step\":\"own\",\"flaws\":[\"overflow\"],\"had_code\":true}\n");
    }
    return call(engine, "{\"cmd\":\"insights\",\"now\":\"2026-11-02T10:00\"}");
  };
  auto heapMastery = [](const Json& reply) {
    for (const Json& row : reply["patterns"].items()) {
      if (row["slug"].asStr() == "heap") return row["mastery"].asNum();
    }
    return -1.0;
  };
  const Json graded = insights(false), judged = insights(true);
  CHECK(heapMastery(graded) < 0.3);   // two Reds, by your grades alone
  CHECK(heapMastery(judged) > 0.7);   // the thinking was right both times
  // The slips land on the flaws instead.
  CHECK_EQ(static_cast<int>(graded["needs"].size()), 0);
  const vector<Json>& needs = judged["needs"].items();
  CHECK_EQ(static_cast<int>(needs.size()), 2);
  CHECK_EQ(needs[0]["slug"].asStr(), string("off-by-one"));
  CHECK_EQ(needs[0]["count"].asInt(), 1);
  CHECK_EQ(needs[1]["share"].asNum(), 0.5);
  CHECK(judged["notes"].items()[0]["text"].asStr().find("Off by one, or wrong bounds, in 1 of 2 judged attempts") != string::npos);
}

static void testSlipsDoNotChooseCards() {
  // A blank on heap, judged: recognised no. That is a need, and no card follows it: a slip is practised on a problem.
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"decksOff\":[]}}");   // the tiny deck drills coding, so it starts off
  call(engine, gradeRequest("lc1", "r", "2026-11-02T09:00"));
  loadJudgements(engine, "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"recognised\":\"no\",\"key_step\":\"missed\",\"had_code\":true}\n");
  const Json cards = call(engine, "{\"cmd\":\"recommend\",\"lane\":\"cards\",\"now\":\"2026-11-03T09:00\",\"n\":6}");
  CHECK_EQ(static_cast<int>(cards["rows"].size()), 6);
  for (const Json& row : cards["rows"].items()) {
    CHECK(row["why"].asStr().find("Drills:") == string::npos);
    CHECK(row["why"].asStr().find("of what your attempts show") == string::npos);
  }
}

static int flawAt(const char* slug) {
  for (int f = 0; f < FLAW_COUNT; ++f) {
    if (string(FLAWS[f].slug) == slug) return f;
  }
  return -1;
}

static void testNeedsLandWhereTheyShowedUp() {
  Resources resources;
  CHECK(resources.load(tinyResources(), nullptr) == 80);
  Clock clock;
  clock.markActive(100);
  const int heap = 1, trie = 2, greedy = 3;   // the tiny resources: two-pointers, heap, trie, greedy
  auto attempt = [](const char* id, int secs) {
    Event e;
    e.id = id;
    e.click = Click::Red;
    e.date = 100;
    e.secs = secs;
    return e;
  };
  auto judgedAt = [](const char* id) { return gradeKey(id, formatDate(100)); };
  JudgedMap judged;

  // lc1 is listed as heap, but the coach says it really uses trie: the slip goes there.
  Judged trieSlip;
  trieSlip.hadCode = true;
  trieSlip.recognised = 1.0;
  trieSlip.keyStep = 0.5;
  trieSlip.thinking = 0.5;
  trieSlip.keyStepOwn = false;
  trieSlip.patterns = {trie};
  trieSlip.flaws = {flawAt("off-by-one")};
  judged[judgedAt("lc1")] = trieSlip;
  Profile p = buildProfile(resources, {attempt("lc1", 0)}, clock, clock.day(100), &judged);
  CHECK(p.needPatterns[flawAt("off-by-one")][trie] > 0);
  CHECK_EQ(p.needPatterns[flawAt("off-by-one")][heap], 0.0);
  CHECK_EQ(p.needs[KEY_STEP_NEED], 0.5);   // saw the pattern, needed a hint for the key step
  CHECK_EQ(p.needs[SEEING_NEED], 0.0);

  // A wrong pick drills the pattern it was and the one they went for, and has no key step to miss.
  Judged wrongPick;
  wrongPick.hadCode = true;
  wrongPick.recognised = 0.0;
  wrongPick.keyStep = 0.0;
  wrongPick.thinking = 0.0;
  wrongPick.missedPattern = true;
  wrongPick.patterns = {heap};
  wrongPick.reachedFor = greedy;
  judged.clear();
  judged[judgedAt("lc5")] = wrongPick;
  p = buildProfile(resources, {attempt("lc5", 0)}, clock, clock.day(100), &judged);
  CHECK(p.needPatterns[SEEING_NEED][heap] > 0);
  CHECK(p.needPatterns[SEEING_NEED][greedy] > 0);
  CHECK_EQ(p.needs[KEY_STEP_NEED], 0.0);
  CHECK(!p.patterns[heap].startingOver());   // wrong went for something; only no means zero clue

  // Zero clue starts the pattern over at Easy; each key step found on your own climbs a level back.
  Judged blank = wrongPick;
  blank.reachedFor = -1;
  blank.noApproach = true;
  blank.patterns = {trie};
  judged.clear();
  judged[judgedAt("lc2")] = blank;
  p = buildProfile(resources, {attempt("lc2", 0)}, clock, clock.day(100), &judged);
  CHECK_EQ(p.patterns[trie].startOverStep, 1);
  Judged found = trieSlip;
  found.keyStep = 1.0;
  found.keyStepOwn = true;
  judged[judgedAt("lc6")] = found;   // lc6 is trie too
  p = buildProfile(resources, {attempt("lc2", 0), attempt("lc6", 0)}, clock, clock.day(100), &judged);
  CHECK_EQ(p.patterns[trie].startOverStep, 2);   // up to Medium
  judged[judgedAt("lc10")] = found;               // lc10 is trie too
  p = buildProfile(resources, {attempt("lc2", 0), attempt("lc6", 0), attempt("lc10", 0)}, clock, clock.day(100), &judged);
  CHECK_EQ(p.patterns[trie].startOverStep, 0);   // back to the aim
  p = buildProfile(resources, {attempt("lc2", 0), attempt("lc6", 0), attempt("lc10", 0), attempt("lc14", 0)}, clock,
                   clock.day(100), &judged);
  CHECK_EQ(p.patterns[trie].startOverStep, 0);   // lc14 has no judgement: nothing changes

  // The clock alone sets slow-to-write, strictly over the box, on the problem's patterns.
  const int slow = flawAt("slow-to-write"), box = TIMEBOX_MEDIUM * 60;
  p = buildProfile(resources, {attempt("lc9", box + 1), attempt("lc13", box)}, clock, clock.day(100));
  CHECK_EQ(p.flawCounts[slow], 1);
  CHECK(p.needPatterns[slow][heap] > 0);
  CHECK_EQ(p.needShare()[slow], 1.0);
}

static void testExploringEasesTowardTheInterview() {
  // The cosine: 60% on the first day, 35% halfway, 10% on the interview and after.
  CHECK(abs(exploreShare(0, 0, 100) - 0.6) < 1e-9);
  CHECK(abs(exploreShare(0, 50, 100) - 0.35) < 1e-9);
  CHECK(abs(exploreShare(0, 100, 100) - 0.1) < 1e-9);
  CHECK(abs(exploreShare(0, 300, 100) - 0.1) < 1e-9);
  CHECK(exploreShare(0, 25, 100) > exploreShare(0, 26, 100));   // only ever down
  CHECK(abs(exploreShare(50, 50, 40) - 0.1) < 1e-9);             // a date already past: the end of the curve

  // Evenly spaced, no coin flip: exactly the share, and below one half never two in a row.
  for (double share : {0.1, 0.35, 0.6}) {
    int explored = 0, inARow = 0, longest = 0;
    for (long slot = 0; slot < 100; ++slot) {
      const bool explores = exploresAt(slot, share);
      explored += explores;
      inARow = explores ? inARow + 1 : 0;
      longest = max(longest, inARow);
    }
    CHECK_EQ(explored, static_cast<int>(round(share * 100)));
    if (share < 0.5) CHECK_EQ(longest, 1);
  }
}

static void testInterviewDateIsASetting() {
  Engine engine;
  engine.loadResources(tinyResources());
  const char* const now = ",\"now\":\"2026-11-17T09:00\"";
  Json s = call(engine, string("{\"cmd\":\"settings\"") + now + "}")["settings"];
  CHECK_EQ(s["interviewDate"].asStr(), string(""));
  CHECK(!s.has("exploreToday"));   // no date, no curve

  // Practice began 11-02 and the interview is 12-02, so 11-17 is halfway.
  call(engine, gradeRequest("lc4", "b", "2026-11-02T09:00"));
  call(engine, gradeRequest("lc2", "b", "2026-11-02T09:00"));
  s = call(engine, string("{\"cmd\":\"settings\",\"set\":{\"interviewDate\":\"2026-12-02\"}") + now + "}")["settings"];
  CHECK_EQ(s["interviewDate"].asStr(), string("2026-12-02"));
  CHECK_EQ(s["exploreToday"].asNum(), 0.35);
  CHECK_EQ(s["exploreEnd"].asNum(), EXPLORE_END);

  // A bad date is refused and changes nothing; it survives a save; empty clears it.
  expectRefused(engine, "{\"cmd\":\"settings\",\"set\":{\"interviewDate\":\"soon\",\"codeNew\":9}}", "2026-12-15");
  expectRefused(engine, "{\"cmd\":\"settings\",\"set\":{\"interviewDate\":\"2026-13-40\"}}", "2026-12-15");
  CHECK_EQ(call(engine, "{\"cmd\":\"settings\"}")["settings"]["codeNew"].asInt(), CODE_NEW);
  Engine reloaded;
  reloaded.loadResources(tinyResources());
  reloaded.loadState(engine.saveState());
  CHECK_EQ(call(reloaded, "{\"cmd\":\"settings\"}")["settings"]["interviewDate"].asStr(), string("2026-12-02"));
  call(reloaded, "{\"cmd\":\"settings\",\"set\":{\"interviewDate\":\"\"}}");
  CHECK_EQ(call(reloaded, "{\"cmd\":\"settings\"}")["settings"]["interviewDate"].asStr(), string(""));
}

static void testTheCurveSpacesExploringPicks() {
  // Two patterns met (two-pointers, trie), two never tried (heap, greedy), halfway to the interview: 35%.
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc4", "b", "2026-11-02T09:00"));
  call(engine, gradeRequest("lc2", "b", "2026-11-02T09:00"));
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"interviewDate\":\"2026-12-02\"}}");
  const Json rows = call(engine, "{\"cmd\":\"recommend\",\"now\":\"2026-11-17T09:00\",\"n\":6}")["rows"];
  CHECK_EQ(static_cast<int>(rows.size()), 6);
  // Two problems graded so far, so new problems 2 to 7: at 35% numbers 2 and 5 explore. (The 8th would
  // too, but by then practice picks may have used up heap's two subpatterns, and exploring falls back.)
  for (int k = 0; k < static_cast<int>(rows.size()); ++k) {
    const string slot = rows.items()[k]["slot"].asStr();
    const bool explores = k == 0 || k == 3;
    CHECK_EQ(explores, slot == "new-pattern" || slot == "new-topic");
    if (!explores) CHECK_EQ(slot, string("practice"));
  }
  CHECK(rows.items()[0]["why"].asStr().find("35% of today's new problems go to new ground, falling to 10% by your interview.") !=
        string::npos);

  // With no date, NEW_PATTERN_SHARE of them go to a new pattern while one is left: of new problems 2 and 3, the second.
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"interviewDate\":\"\"}}");
  const Json fixed = call(engine, "{\"cmd\":\"recommend\",\"now\":\"2026-11-17T09:00\",\"n\":2}")["rows"];
  CHECK_EQ(fixed.items()[0]["slot"].asStr(), string("practice"));
  CHECK_EQ(fixed.items()[1]["slot"].asStr(), string("new-pattern"));
  CHECK(fixed.items()[1]["why"].asStr().find("60% of new problems go to one until none is left.") != string::npos);
}

// Heap: two Easy, three Medium (lc3 is done first), one Hard, all asked alike.
static const char* const LADDER_JSON =
    "{\"patterns\":[{\"slug\":\"heap\",\"name\":\"Heap\"}],\"tags\":[],\"companies\":[],\"decks\":[],\"cards\":[],"
    "\"problems\":[{\"n\":1,\"t\":\"Easy one\",\"d\":1,\"r\":1100,\"f\":50,\"p\":[0]},"
    "{\"n\":2,\"t\":\"Easy two\",\"d\":1,\"r\":1200,\"f\":50,\"p\":[0]},"
    "{\"n\":3,\"t\":\"Done one\",\"d\":2,\"r\":1700,\"f\":50,\"p\":[0]},"
    "{\"n\":4,\"t\":\"Medium one\",\"d\":2,\"r\":1650,\"f\":50,\"p\":[0]},"
    "{\"n\":5,\"t\":\"Medium two\",\"d\":2,\"r\":1750,\"f\":50,\"p\":[0]},"
    "{\"n\":6,\"t\":\"Hard one\",\"d\":3,\"r\":2300,\"f\":50,\"p\":[0]}]}";

static string judgedLine(const char* id, const char* date, const char* thinking) {
  return string("{\"problem\":\"") + id + "\",\"date\":\"" + date + "\"," + thinking + ",\"had_code\":true}\n";
}

// The ids picked over many rolls, and one pick's reason.
static set<string> picksOver(Engine& engine, const char* now, string* why = nullptr) {
  set<string> ids;
  for (int roll = 0; roll < 200; ++roll) ids.insert(onePick(engine, now, roll)["id"].asStr());
  if (why) *why = onePick(engine, now, 0)["why"].asStr();
  return ids;
}

static void testZeroClueStartsOver() {
  const char* const noClue = "\"recognised\":\"no\",\"key_step\":\"missed\"";
  const char* const ownStep = "\"recognised\":\"yes\",\"key_step\":\"own\"";
  Engine engine;
  engine.loadResources(LADDER_JSON);
  call(engine, gradeRequest("lc3", "r", "2026-11-02T09:00"));
  string log = judgedLine("lc3", "2026-11-02", noClue);
  loadJudgements(engine, log);
  string why;
  CHECK(picksOver(engine, "2026-11-03T09:00", &why) == (set<string>{"lc1", "lc2"}));   // Easy only
  CHECK(why.find("Easy: you showed zero clue on Heap, so it starts over.") != string::npos);

  // A key step found on your own: one Medium.
  call(engine, gradeRequest("lc1", "b", "2026-11-03T09:00"));
  log += judgedLine("lc1", "2026-11-03", ownStep);
  loadJudgements(engine, log);
  CHECK(picksOver(engine, "2026-11-04T09:00", &why) == (set<string>{"lc4", "lc5"}));
  CHECK(why.find("Medium: you found a key step yourself after starting over.") != string::npos);

  // And the next: back to the aim, any level.
  call(engine, gradeRequest("lc4", "b", "2026-11-04T09:00"));
  log += judgedLine("lc4", "2026-11-04", ownStep);
  loadJudgements(engine, log);
  picksOver(engine, "2026-11-05T09:00", &why);
  CHECK(why.find(", aimed at ") != string::npos);
  CHECK(why.find("starts over") == string::npos);

  // Seeing the pattern but missing the step is not zero clue.
  Engine seen;
  seen.loadResources(LADDER_JSON);
  call(seen, gradeRequest("lc3", "r", "2026-11-02T09:00"));
  loadJudgements(seen, judgedLine("lc3", "2026-11-02", "\"recognised\":\"yes\",\"key_step\":\"missed\""));
  picksOver(seen, "2026-11-03T09:00", &why);
  CHECK(why.find("starts over") == string::npos);
}

static void testOverTheTimeBoxAimsEasier() {
  // Heap: a hard Easy, a Hard, and three Mediums you did, each timed against the 25-minute box. Your
  // aim lands between the Easy and the Hard, so 250 lower moves picks toward the Easy.
  const string heapProblems =
      "{\"n\":1,\"t\":\"Easy one\",\"d\":1,\"r\":1600,\"f\":50,\"p\":[0]},"
      "{\"n\":2,\"t\":\"Hard one\",\"d\":3,\"r\":2300,\"f\":50,\"p\":[0]},"
      "{\"n\":3,\"t\":\"Done one\",\"d\":2,\"r\":1700,\"f\":50,\"p\":[0]},"
      "{\"n\":4,\"t\":\"Done two\",\"d\":2,\"r\":1700,\"f\":50,\"p\":[0]},"
      "{\"n\":5,\"t\":\"Done three\",\"d\":2,\"r\":1700,\"f\":50,\"p\":[0]}";
  const string json = "{\"patterns\":[{\"slug\":\"heap\",\"name\":\"Heap\"}],\"tags\":[],\"companies\":[],"
                      "\"decks\":[],\"cards\":[],\"problems\":[" + heapProblems + "]}";
  auto easyPicks = [&](int secs, const char* flaws, string* why) {
    Engine engine;
    engine.loadResources(json);
    string log;
    for (const char* id : {"lc3", "lc4", "lc5"}) {
      call(engine, string("{\"cmd\":\"grade\",\"id\":\"") + id + "\",\"grade\":\"b\",\"now\":\"2026-11-02T09:00\",\"secs\":" +
                       to_string(secs) + "}");
      log += string("{\"problem\":\"") + id + "\",\"date\":\"2026-11-02\",\"recognised\":\"yes\",\"key_step\":\"own\","
             "\"had_code\":true,\"flaws\":" + flaws + "}\n";
    }
    loadJudgements(engine, log);
    int easy = 0;
    for (int roll = 0; roll < 400; ++roll) {
      const Json pick = onePick(engine, "2026-11-03T09:00", roll);
      if (pick["id"].asStr() == "lc1") ++easy;
      if (why && why->empty() && pick["why"].asStr().find("less 250") != string::npos) *why = pick["why"].asStr();
    }
    return easy;
  };
  const int box = TIMEBOX_MEDIUM * 60;
  string why;
  const int plain = easyPicks(box, "[]", nullptr),   // right on the box is inside it
            slow = easyPicks(box + 1, "[]", &why),     // no slack: one second over is over
            other = easyPicks(600, "[\"off-by-one\"]", nullptr),
            coachSaid = easyPicks(600, "[\"slow-to-write\"]", nullptr);
  CHECK(slow > plain + 40);              // speed is built on problems you can finish
  CHECK(abs(other - plain) < 40);        // other flaws leave the level alone
  CHECK(abs(coachSaid - plain) < 40);    // only the clock sets it; an old line naming it counts for nothing
  CHECK(why.find("less 250: over the time box on 3 of your 3 timed tries here.") != string::npos);

  // Only where it happened: trie was never slow, so its problems aim as usual.
  Engine mixed;
  mixed.loadResources("{\"patterns\":[{\"slug\":\"heap\",\"name\":\"Heap\"},{\"slug\":\"trie\",\"name\":\"Trie\"}],"
                      "\"tags\":[],\"companies\":[],\"decks\":[],\"cards\":[],\"problems\":[" + heapProblems +
                      ",{\"n\":6,\"t\":\"Trie easy\",\"d\":1,\"r\":1100,\"f\":50,\"p\":[1]},"
                      "{\"n\":7,\"t\":\"Trie hard\",\"d\":3,\"r\":2300,\"f\":50,\"p\":[1]}]}");
  int triePicks = 0;
  for (const char* id : {"lc3", "lc4", "lc5"}) {
    call(mixed, string("{\"cmd\":\"grade\",\"id\":\"") + id + "\",\"grade\":\"b\",\"now\":\"2026-11-02T09:00\",\"secs\":" +
                    to_string(box + 1) + "}");
  }
  for (int roll = 0; roll < 200; ++roll) {
    const Json pick = onePick(mixed, "2026-11-03T09:00", roll);
    if (pick["id"].asStr() != "lc6" && pick["id"].asStr() != "lc7") continue;
    ++triePicks;
    CHECK(pick["why"].asStr().find("less 250") == string::npos);
  }
  CHECK(triePicks > 100);   // trie is the pattern not yet tried, so most picks are its

  // A pattern never tried aims at your overall rating + 150 like any other: no easy first meeting, and
  // the Hard at 2300 stays rare.
  Engine fresh;
  fresh.loadResources(json);
  int hard = 0;
  for (int roll = 0; roll < 400; ++roll) hard += onePick(fresh, "2026-11-02T09:00", roll)["id"].asStr() == "lc2";
  CHECK(hard < 40);
  CHECK(onePick(fresh, "2026-11-02T09:00", 0)["why"].asStr().find("aimed at 1650: your idea rating on Heap 1500 + 150.") !=
        string::npos);
}

static Json insights(Engine& engine, const char* now) {
  return call(engine, string("{\"cmd\":\"insights\",\"now\":\"") + now + "\"}");
}

static int ratingOf(const Json& insight, const string& slug) {
  for (const Json& row : insight["patterns"].items()) {
    if (row["slug"].asStr() == slug) return row["rating"].asInt();
  }
  return -1;
}

static int codeRatingOf(const Json& insight, const string& slug) {
  for (const Json& row : insight["patterns"].items()) {
    if (row["slug"].asStr() == slug) return row["codeRating"].asInt();
  }
  return -1;
}

static void testRatingFromFirstTries() {
  // Heap and trie at several ratings; greedy never tried.
  const string json =
      "{\"patterns\":[{\"slug\":\"heap\",\"name\":\"Heap\"},{\"slug\":\"trie\",\"name\":\"Trie\"},"
      "{\"slug\":\"greedy\",\"name\":\"Greedy\"}],\"tags\":[],\"companies\":[],\"decks\":[],\"cards\":[],"
      "\"problems\":[{\"n\":1,\"t\":\"H1\",\"d\":2,\"r\":1600,\"f\":50,\"p\":[0]},"
      "{\"n\":2,\"t\":\"H2\",\"d\":2,\"r\":1800,\"f\":50,\"p\":[0]},"
      "{\"n\":4,\"t\":\"T1\",\"d\":2,\"r\":1600,\"f\":50,\"p\":[1]},"
      "{\"n\":5,\"t\":\"T2\",\"d\":2,\"r\":1800,\"f\":50,\"p\":[1]},"
      "{\"n\":6,\"t\":\"G1\",\"d\":2,\"r\":1700,\"f\":50,\"p\":[2]},"
      "{\"n\":7,\"t\":\"G2\",\"d\":2,\"r\":1700,\"f\":50,\"p\":[2]}]}";
  Engine engine;
  engine.loadResources(json);
  Json before = insights(engine, "2026-11-02T09:00");
  CHECK_EQ(before["rating"]["you"].asInt(), static_cast<int>(RATING_START));   // nothing tried: the starting belief
  CHECK_EQ(before["rating"]["tries"].asInt(), 0);

  call(engine, gradeRequest("lc1", "b", "2026-11-02T09:00"));
  call(engine, gradeRequest("lc2", "b", "2026-11-02T09:00"));
  call(engine, gradeRequest("lc4", "r", "2026-11-02T09:00"));
  call(engine, gradeRequest("lc5", "r", "2026-11-02T09:00"));
  call(engine, gradeRequest("lc6", "b", "2026-11-02T09:00"));
  const Json after = insights(engine, "2026-11-03T09:00");
  const int you = after["rating"]["you"].asInt();
  CHECK_EQ(after["rating"]["tries"].asInt(), 5);
  CHECK(after["rating"]["spread"].asInt() < static_cast<int>(RATING_START_SPREAD));   // tries narrow it
  CHECK(ratingOf(after, "heap") > you);   // solved both
  CHECK(ratingOf(after, "trie") < you);   // missed both
  CHECK(ratingOf(after, "heap") - you < static_cast<int>(RATING_NUDGE_SPREAD));   // two tries only nudge it
  CHECK(after["notes"].dump().find("New problems aim 150 above it on their pattern") != string::npos);
  // With no coach, your grade is both results, so the two ratings agree.
  CHECK_EQ(after["rating"]["code"].asInt(), you);
  CHECK_EQ(codeRatingOf(after, "heap"), ratingOf(after, "heap"));

  // A re-solve says more about memory than level: it leaves the rating alone.
  call(engine, gradeRequest("lc4", "b", "2026-11-03T09:00"));
  const Json again = insights(engine, "2026-11-03T09:00");
  CHECK_EQ(again["rating"]["you"].asInt(), you);
  CHECK_EQ(again["rating"]["tries"].asInt(), 5);

  // Once the coach has judged a try, its thinking labels decide: a Red with the right idea and the
  // key step found counts toward the rating, the way it counts toward the thinking score.
  auto heapRating = [&](bool judged) {
    Engine e;
    e.loadResources(json);
    call(e, gradeRequest("lc2", "r", "2026-11-02T09:00"));
    if (judged) loadJudgements(e, judgedLine("lc2", "2026-11-02", "\"recognised\":\"yes\",\"key_step\":\"own\""));
    return ratingOf(insights(e, "2026-11-03T09:00"), "heap");
  };
  CHECK(heapRating(false) < static_cast<int>(RATING_START));
  CHECK(heapRating(true) > static_cast<int>(RATING_START));

  /*
  The working-code rating counts whether the code worked: your grade, or the coach's when it is
  stricter. So a Red with the right idea lifts the idea rating and lowers this one, your Blue over
  code the coach calls Red is a miss, and the coach's Blue can't undo your Red.
  */
  const int start = static_cast<int>(RATING_START);
  auto heapRatings = [&](const char* yours, const char* coach) {
    Engine e;
    e.loadResources(json);
    call(e, gradeRequest("lc2", yours, "2026-11-02T09:00"));
    loadJudgements(e, judgedLine("lc2", "2026-11-02", (string("\"model_grade\":\"") + coach +
                                                       "\",\"recognised\":\"yes\",\"key_step\":\"own\"").c_str()));
    const Json seen = insights(e, "2026-11-03T09:00");
    CHECK(seen["notes"].dump().find("Your rating for working code: " + to_string(seen["rating"]["code"].asInt())) != string::npos);
    return pair<int, int>(ratingOf(seen, "heap"), codeRatingOf(seen, "heap"));
  };
  CHECK(heapRatings("r", "r") == make_pair(heapRating(true), heapRating(false)));   // the idea up, the code down
  CHECK(heapRatings("b", "r").second < start);
  CHECK(heapRatings("r", "b").second < start);
  CHECK(heapRatings("b", "b").second > start);
}

static void testOnlyTheMainPatternCounts() {
  // lc1 is mainly heap, with trie on the side. Missing it says something about heap alone.
  const string json =
      "{\"patterns\":[{\"slug\":\"heap\",\"name\":\"Heap\"},{\"slug\":\"trie\",\"name\":\"Trie\"}],"
      "\"tags\":[],\"companies\":[],\"decks\":[],\"cards\":[],"
      "\"problems\":[{\"n\":1,\"t\":\"Both\",\"d\":2,\"r\":1700,\"f\":50,\"p\":[0,1]},"
      "{\"n\":2,\"t\":\"Heap\",\"d\":2,\"r\":1700,\"f\":50,\"p\":[0]},"
      "{\"n\":3,\"t\":\"Trie\",\"d\":2,\"r\":1700,\"f\":50,\"p\":[1]}]}";
  Engine engine;
  engine.loadResources(json);
  call(engine, gradeRequest("lc1", "r", "2026-11-02T09:00"));
  const Json seen = insights(engine, "2026-11-03T09:00");
  // The side pattern has a thinking score from it, and that is all: its ratings sit at your overall ones.
  CHECK(codeRatingOf(seen, "heap") < seen["rating"]["code"].asInt());
  CHECK_EQ(codeRatingOf(seen, "trie"), seen["rating"]["code"].asInt());
  CHECK_EQ(ratingOf(seen, "trie"), seen["rating"]["you"].asInt());
  // And it is still new: with one problem graded, the next one explores, and trie is what is left.
  const Json pick = onePick(engine, "2026-11-03T09:00", 0);
  CHECK_EQ(pick["id"].asStr(), string("lc3"));
  CHECK(pick["why"].asStr().rfind("New pattern: Trie. It has never been the main pattern of a problem you tried", 0) == 0);

  // A blank the coach couldn't place starts the main pattern over, not the one on the side.
  Resources resources;
  CHECK(resources.load(json, nullptr) == 3);
  Clock clock;
  clock.markActive(100);
  Event miss;
  miss.id = "lc1";
  miss.click = Click::Red;
  miss.date = 100;
  Judged blank;
  blank.recognised = 0.0;
  blank.keyStep = 0.0;
  blank.thinking = 0.0;
  blank.missedPattern = true;
  blank.noApproach = true;
  JudgedMap judged;
  judged[gradeKey("lc1", formatDate(100))] = blank;
  const Profile p = buildProfile(resources, {miss}, clock, clock.day(100), &judged);
  CHECK_EQ(p.patterns[0].startOverStep, 1);
  CHECK_EQ(p.patterns[1].startOverStep, 0);
}

static void testALevelSitsAtWhatAPickCanReach() {
  /*
  Heap has one finer topic. Its Medium is right at a newcomer's aim, 1650, and nine more Mediums with
  no finer topic sit far below. Those nine can't be picked while the topic has a Medium left, so they
  don't decide where the level sits: Medium is at the aim and nearly always chosen over the far Hard.
  */
  string problems = "{\"n\":1,\"t\":\"Medium in the topic\",\"d\":2,\"r\":1650,\"f\":50,\"p\":[0],\"u\":[0]},"
                    "{\"n\":2,\"t\":\"Hard in the topic\",\"d\":3,\"r\":2100,\"f\":50,\"p\":[0],\"u\":[0]}";
  for (int n = 3; n <= 11; ++n) {
    problems += ",{\"n\":" + to_string(n) + ",\"t\":\"Medium outside\",\"d\":2,\"r\":1100,\"f\":50,\"p\":[0]}";
  }
  Engine engine;
  engine.loadResources("{\"patterns\":[{\"slug\":\"heap\",\"name\":\"Heap\"}],"
                       "\"topics\":[{\"slug\":\"heap/top-k\",\"name\":\"Top k\",\"signal\":\"\",\"pattern\":0}],"
                       "\"tags\":[],\"companies\":[],\"decks\":[],\"cards\":[],\"problems\":[" + problems + "]}");
  int medium = 0;
  for (int roll = 0; roll < 400; ++roll) medium += onePick(engine, "2026-11-02T09:00", roll)["id"].asStr() == "lc1";
  CHECK(medium > 290);   // by the middle of all ten Mediums, 1100, it would be about 180
}

static void testCodingDecksStartSwitchedOff() {
  // The tiny resources have one deck, signals, and it drills coding: nothing from it reaches the queue.
  Engine engine;
  engine.loadResources(tinyResources());
  auto cards = [](Engine& e) { return laneIds(call(e, "{\"cmd\":\"today\",\"now\":\"2026-11-02T09:00\"}"), "cards"); };
  CHECK(cards(engine).empty());
  // Switched on in settings, it joins like any other deck.
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"decksOff\":[]}}");
  CHECK(!cards(engine).empty());

  // A progress file from before version 2 has the coding decks switched off, and a card of theirs that was waiting makes way.
  Engine old;
  old.loadResources(tinyResources());
  CHECK(old.loadState(R"({"v":1,"settings":{"decksOff":[]},"plan":{"date":"2026-11-02","pulls":0,"code":[],
    "cards":[{"id":"signals-1","slot":"weak","why":"x"}]}})"));
  CHECK(cards(old).empty());
  // That happens once. Saved again the file is version 2, and what you switch on stays on.
  call(old, "{\"cmd\":\"settings\",\"set\":{\"decksOff\":[]}}");
  Engine again;
  again.loadResources(tinyResources());
  CHECK(again.loadState(old.saveState()));
  CHECK(!cards(again).empty());
}

static void testLevelFirstIgnoresHowManyProblems() {
  // One Medium at the aim and twenty Hards far above it, all asked alike.
  string problems = "{\"n\":1,\"t\":\"Medium\",\"d\":2,\"r\":1650,\"f\":50,\"p\":[0]}";
  for (int n = 2; n <= 21; ++n) {
    problems += ",{\"n\":" + to_string(n) + ",\"t\":\"Hard\",\"d\":3,\"r\":2300,\"f\":50,\"p\":[0]}";
  }
  Engine engine;
  engine.loadResources("{\"patterns\":[{\"slug\":\"heap\",\"name\":\"Heap\"}],\"tags\":[],\"companies\":[],"
                       "\"decks\":[],\"cards\":[],\"problems\":[" + problems + "]}");
  int medium = 0;
  for (int roll = 0; roll < 400; ++roll) medium += onePick(engine, "2026-11-02T09:00", roll)["id"].asStr() == "lc1";
  CHECK(medium > 340);   // weighing each problem alone, the twenty Hards would take most picks
}

static void testSubpatternBeforeProblem() {
  // Heap's two subpatterns and one problem with none, all Medium. lc4 (top k) was done yesterday.
  const string json =
      "{\"patterns\":[{\"slug\":\"heap\",\"name\":\"Heap\"}],"
      "\"topics\":[{\"slug\":\"heap/top-k\",\"name\":\"Top k\",\"signal\":\"\",\"pattern\":0},"
      "{\"slug\":\"heap/two-heaps\",\"name\":\"Two heaps\",\"signal\":\"\",\"pattern\":0}],"
      "\"tags\":[],\"companies\":[],\"decks\":[],\"cards\":[],"
      "\"problems\":[{\"n\":1,\"t\":\"Top one\",\"d\":2,\"r\":1700,\"f\":50,\"p\":[0],\"u\":[0]},"
      "{\"n\":2,\"t\":\"Two one\",\"d\":2,\"r\":1700,\"f\":50,\"p\":[0],\"u\":[1]},"
      "{\"n\":3,\"t\":\"Plain one\",\"d\":2,\"r\":1700,\"f\":90,\"p\":[0]},"
      "{\"n\":4,\"t\":\"Top done\",\"d\":2,\"r\":1700,\"f\":0,\"p\":[0],\"u\":[0]}]}";
  Engine engine;
  engine.loadResources(json);
  // An interview date already past keeps exploring at its floor, so these picks come from the pull.
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"interviewDate\":\"2026-11-01\"}}");
  call(engine, gradeRequest("lc4", "b", "2026-11-02T09:00"));
  int top = 0, two = 0, plain = 0;
  string why;
  for (int roll = 0; roll < 400; ++roll) {
    const Json pick = onePick(engine, "2026-11-03T09:00", roll);
    if (pick["slot"].asStr() != "practice") continue;   // the occasional new-topic pick is exploring's
    const string id = pick["id"].asStr();
    top += id == "lc1";
    two += id == "lc2";
    plain += id == "lc3";
    if (id == "lc2") why = pick["why"].asStr();
  }
  CHECK_EQ(plain, 0);          // a subpattern with a problem left at the level comes first
  CHECK(two > top + 100);      // top k was just done, and went well
  CHECK(why.find("Subpattern: Two heaps.") != string::npos);

  // With both subpatterns used up at that level, the problem with none comes up.
  call(engine, gradeRequest("lc1", "b", "2026-11-03T09:00"));
  call(engine, gradeRequest("lc2", "b", "2026-11-03T09:00"));
  CHECK_EQ(onePick(engine, "2026-11-04T09:00", 0)["id"].asStr(), string("lc3"));
}

int main() {
  testBlueExpires();
  testBlueConsecutiveBreaks();
  testYellowExpires();
  testYellowFailureGoesRed();
  testRedExpires();
  testRedStaysRed();
  testRedYellowRepeats();
  testRetiredCanComeBack();
  testProblemCleanFirstTry();
  testProblemNeededAHint();
  testProblemMissed();
  testProblemSlipStartsThatRoutine();
  testProblemExtraDaysAreFixedButSpread();
  testGapCountsFromActualDay();
  testLateCardIsNotPunished();
  testStudyClockSkipsIdleDays();
  testCalendarClockCountsEveryDay();
  testDayRollover();
  testQueueOrder();
  testBoundedQueueRollsOver();
  testReviewsADayLimitTheChecks();
  testPlanIsStableWithinADay();
  testUnfinishedNewFacesStay();
  testKeptNewFacesStayInsideTheBound();
  testOldPicksMakeWay();
  testPlanSurvivesSaveAndLoad();
  testRegradeAndUndo();
  testRecommenderOnlyPicksUnseen();
  testMemorizationSignal();
  testWeakSlotAimsAtWeakPatterns();
  testTriviaMissesDoNotDriveTheNeeds();
  testImportOldTracker();
  testRuleChangeNeedsNoMigration();
  testDrillDiff();
  testAttemptNamesAFileAndTouchesNoProgress();
  testAttemptIsRefusedWhenItCannotBeCode();
  testPendingListsGradesWithNoJudgement();
  testPendingShowsTheEarlierJudgement();
  testPendingStopsAtOneBatch();
  testAttemptsBelongToTheNextGrade();
  testJudgementBecomesALogLine();
  testCommitSavesOnlyTheProfile();
  testDropIsOnlyForNothingToAdd();
  testSearchRecallsPastJudgements();
  testTopicListIsClosed();
  testOverviewSaysWhereThingsStand();
  testCoachCanOnlyAddChecks();
  testNewPatternsTakeThreeInFive();
  testAWaitingProblemHoldsItsPlace();
  testAskingForMoreNeverRepeatsANewPattern();
  testPullsKeepTheSpacing();
  testNewTopicsKeepComing();
  testSolvedProblemsStepBackAndMissesDoNot();
  testThePullFollowsWorkingCode();
  testTargetCompanyNarrowsThePicks();
  testSlipsNeverCountAgainstThePattern();
  testSlipsDoNotChooseCards();
  testNeedsLandWhereTheyShowedUp();
  testZeroClueStartsOver();
  testExploringEasesTowardTheInterview();
  testInterviewDateIsASetting();
  testTheCurveSpacesExploringPicks();
  testOverTheTimeBoxAimsEasier();
  testRatingFromFirstTries();
  testOnlyTheMainPatternCounts();
  testALevelSitsAtWhatAPickCanReach();
  testCodingDecksStartSwitchedOff();
  testLevelFirstIgnoresHowManyProblems();
  testSubpatternBeforeProblem();
  testBadInputDoesNotCrash();
  testJson();
  testRandomClicksKeepInvariants();
  testRandomSessionsKeepInvariants();
  testEveryDueCardGetsServedEventually();

  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
