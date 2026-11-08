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

using namespace interviewprep;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    ++checks;                                                           \
    if (!(cond)) {                                                      \
      ++failures;                                                       \
      std::printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
    }                                                                   \
  } while (0)

#define CHECK_EQ(a, b)                                                  \
  do {                                                                  \
    ++checks;                                                           \
    auto x = (a);                                                       \
    auto y = (b);                                                       \
    if (!(x == y)) {                                                    \
      ++failures;                                                       \
      std::printf("FAIL  %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b); \
      print(x);                                                         \
      print(y);                                                         \
    }                                                                   \
  } while (0)

static void print(int v) { std::printf("      got %d\n", v); }
static void print(double v) { std::printf("      got %g\n", v); }
static void print(const std::string& v) { std::printf("      got %s\n", v.c_str()); }
static void print(Track t) { std::printf("      got %s\n", trackName(t)); }

static const Click B = Click::Blue, Y = Click::Yellow, R = Click::Red;

// Applies clicks to a card, each on the exact day it falls due.
static CardState walk(std::initializer_list<Click> clicks) {
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
static CardState walkProblem(std::initializer_list<Click> clicks, const std::string& id = "lc1") {
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
  std::set<int> gaps;
  for (int i = 1; i <= 300; ++i) gaps.insert(walkProblem({B}, "lc" + std::to_string(i)).due - 1);
  CHECK_EQ(*gaps.begin(), 10);
  CHECK_EQ(*gaps.rbegin(), 20);
  CHECK_EQ(static_cast<int>(gaps.size()), 11);
  // Each check draws again: the second check of a miss isn't tied to the first.
  std::set<std::pair<int, int>> pairs;
  for (int i = 1; i <= 300; ++i) {
    const CardState first = walkProblem({R}, "lc" + std::to_string(i));
    const CardState second = walkProblem({R, B}, "lc" + std::to_string(i));
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
  CHECK_EQ(formatDate(parseDate("2026-02-28") + 1), std::string("2026-03-01"));
  CHECK_EQ(formatDate(parseDate("2028-02-28") + 1), std::string("2028-02-29"));
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
static std::string tinyResources(int googleAsked = 0) {
  std::string problems;
  for (int i = 1; i <= 40; ++i) {
    const int pattern = i % 4;
    if (i > 1) problems += ',';
    // Two finer topics under heap: lc1, lc5 and lc9 are top-k, lc13 and lc17 two-heaps.
    const std::string topics = i == 1 || i == 5 || i == 9 ? ",\"u\":[0]" : i == 13 || i == 17 ? ",\"u\":[1]" : "";
    const std::string google = i <= googleAsked ? ",\"c\":[[0," + std::to_string(10 + i) + "]]" : "";
    problems += "{\"n\":" + std::to_string(i) + ",\"t\":\"Problem " + std::to_string(i) +
                "\",\"s\":\"p" + std::to_string(i) + "\",\"d\":2,\"r\":1700,\"f\":" +
                std::to_string(i) + ",\"p\":[" + std::to_string(pattern) + "],\"g\":[" +
                std::to_string(pattern) + "],\"m\":\"5555\"" + topics + google + "}";
  }
  std::string cards;
  for (int i = 1; i <= 40; ++i) {
    if (i > 1) cards += ',';
    cards += "{\"id\":\"signals-" + std::to_string(i) + "\",\"type\":\"flash\",\"deck\":\"signals\",\"q\":\"Q" +
             std::to_string(i) + "\",\"a\":\"A\",\"patterns\":[" + std::to_string(i % 4) +
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

static Json call(Engine& engine, const std::string& request) {
  std::string error;
  Json response = Json::parse(engine.call(request), &error);
  CHECK(response["ok"].asBool());
  if (!response["ok"].asBool()) std::printf("      %s -> %s\n", request.c_str(), response["error"].asStr().c_str());
  return response;
}

static std::string gradeRequest(const std::string& id, const char* grade, const char* now) {
  return "{\"cmd\":\"grade\",\"id\":\"" + id + "\",\"grade\":\"" + grade + "\",\"now\":\"" + now + "\"}";
}

static void testBoundedQueueRollsOver() {
  Engine engine;
  CHECK(engine.loadResources(tinyResources()) == 80);
  // The calendar clock, so four days on every miss's first check is due.
  CHECK(engine.loadState("{\"settings\":{\"codeCap\":3,\"codeNew\":1,\"clock\":\"calendar\"}}"));

  // Fail seven problems on one day, so all seven are due four days later.
  for (int i = 1; i <= 7; ++i) {
    call(engine, gradeRequest("lc" + std::to_string(i), "r", "2026-11-02T20:00"));
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
  for (int i = 1; i <= 7; ++i) call(engine, gradeRequest("lc" + std::to_string(i), "r", "2026-11-02T20:00"));

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
  CHECK_EQ(more["code"]["items"].items().back()["slot"].asStr(), std::string("review"));

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

static std::vector<std::string> laneIds(const Json& day, const char* lane) {
  std::vector<std::string> ids;
  for (const Json& item : day[lane]["items"].items()) ids.push_back(item["id"].asStr());
  return ids;
}

static void testUnfinishedNewFacesStay() {
  Engine engine;
  engine.loadResources(tinyResources());
  CHECK(engine.loadState("{\"settings\":{\"codeCap\":3,\"codeNew\":1}}"));
  auto today = [&](const char* now) { return call(engine, std::string("{\"cmd\":\"today\",\"now\":\"") + now + "\"}"); };

  // Day one: the new face, plus two you pulled. You grade one of the pulls.
  const auto first = laneIds(today("2026-11-02T09:00"), "code");
  CHECK_EQ(static_cast<int>(first.size()), 1);
  const auto pulled = laneIds(call(engine, "{\"cmd\":\"more\",\"now\":\"2026-11-02T10:00\",\"lane\":\"code\",\"n\":2}"), "code");
  CHECK_EQ(static_cast<int>(pulled.size()), 3);
  call(engine, gradeRequest(pulled[2], "y", "2026-11-02T11:00"));
  std::set<std::string> waiting(pulled.begin(), pulled.end());
  waiting.erase(pulled[2]);

  // Day two: the two you didn't grade are still there, and no fresh pick joins them.
  const Json day2 = today("2026-11-03T09:00");
  const auto ids2 = laneIds(day2, "code");
  CHECK(std::set<std::string>(ids2.begin(), ids2.end()) == waiting);
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
  call(engine, std::string("{\"cmd\":\"hide\",\"id\":\"") + ids2[1] + "\",\"now\":\"2026-11-06T12:00\"}");
  const auto ids7 = laneIds(today("2026-11-07T09:00"), "code");
  CHECK_EQ(static_cast<int>(ids7.size()), 1);
  CHECK(ids7[0] != ids2[0] && ids7[0] != ids2[1]);
}

static void testKeptNewFacesStayInsideTheBound() {
  Engine engine;
  engine.loadResources(tinyResources());
  CHECK(engine.loadState("{\"settings\":{\"codeCap\":3,\"codeNew\":1,\"clock\":\"calendar\"}}"));
  const Json day1 = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-02T09:00\"}");
  const std::string kept = laneIds(day1, "code")[0];

  // Seven reviews due: the kept new face holds its slot and the reviews take the rest.
  int failed = 0;
  for (int i = 1; failed < 7; ++i) {
    const std::string id = "lc" + std::to_string(i);
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
    const std::string id = "lc" + std::to_string(i);
    if (std::find(mine.begin(), mine.end(), id) != mine.end()) continue;
    call(puller, gradeRequest(id, "r", "2026-11-02T20:00"));
    ++failed;
  }
  const Json next = call(puller, "{\"cmd\":\"today\",\"now\":\"2026-11-06T09:00\"}");
  reviews = 0;
  for (const Json& item : next["code"]["items"].items()) reviews += item["slot"].asStr() == "review";
  CHECK_EQ(reviews, 2);
  CHECK_EQ(static_cast<int>(next["code"]["items"].size()), 5);   // 2 reviews and the 3 you have, 2 of them pulled
  CHECK_EQ(next["code"]["overflow"].asInt(), 5);

  // A deck you turn off takes its waiting cards with it.
  CHECK(!call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-07T09:00\"}")["cards"]["items"].items().empty());
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"decksOff\":[\"signals\"]}}");
  CHECK(call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-07T10:00\"}")["cards"]["items"].items().empty());
}

static void testPlanIsStableWithinADay() {
  Engine engine;
  engine.loadResources(tinyResources());
  std::string a = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-02T09:00\"}")["code"]["items"].dump();
  std::string b = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-02T23:00\"}")["code"]["items"].dump();
  CHECK_EQ(a, b);
}

static void testPlanSurvivesSaveAndLoad() {
  Engine one;
  one.loadResources(tinyResources());
  call(one, "{\"cmd\":\"today\",\"now\":\"2026-11-02T09:00\"}");
  call(one, gradeRequest("lc3", "y", "2026-11-02T09:30"));
  const std::string saved = one.saveState();

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
  CHECK_EQ(fixed["card"]["track"].asStr(), std::string("blue"));
  CHECK_EQ(fixed["card"]["attempts"].asInt(), 1);
  // Undo removes it entirely.
  call(engine, "{\"cmd\":\"undo\",\"id\":\"lc1\"}");
  Json card = call(engine, "{\"cmd\":\"card\",\"id\":\"lc1\",\"now\":\"2026-11-02T09:06\"}");
  CHECK_EQ(card["card"]["track"].asStr(), std::string("new"));
}

static void testRecommenderOnlyPicksUnseen() {
  Engine engine;
  engine.loadResources(tinyResources());
  for (int i = 1; i <= 30; ++i) call(engine, gradeRequest("lc" + std::to_string(i), "b", "2026-11-02T09:00"));
  Json picks = call(engine, "{\"cmd\":\"recommend\",\"now\":\"2026-11-03T09:00\",\"n\":5}");
  CHECK_EQ(static_cast<int>(picks["rows"].size()), 5);
  for (const Json& row : picks["rows"].items()) CHECK_EQ(row["track"].asStr(), std::string("new"));
}

static void testOldPicksMakeWay() {
  // A queue saved before 2026-11-09, holding new faces the old picker chose.
  Engine engine;
  engine.loadResources(tinyResources());
  CHECK(engine.loadState(R"({"settings":{"codeCap":3,"codeNew":1},"plan":{"date":"2026-11-02","pulls":0,
    "code":[{"id":"lc5","slot":"coach","why":"Coach: Heap, 30% of new problems"},{"id":"lc6","slot":"explore","why":"x"}],
    "cards":[{"id":"signals-1","slot":"weak","why":"x"}]}})"));
  // That day stays as it was.
  CHECK(laneIds(call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-02T10:00\"}"), "code") ==
        std::vector<std::string>({"lc5", "lc6"}));
  // The next queue lets the new picker choose. Cards you were given stay.
  const Json next = call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-03T10:00\"}");
  for (const Json& item : next["code"]["items"].items()) {
    const std::string slot = item["slot"].asStr();
    CHECK(slot == "new-pattern" || slot == "new-topic" || slot == "practice");
  }
  CHECK(laneIds(next, "cards")[0] == "signals-1");
}

static void testMemorizationSignal() {
  Engine engine;
  engine.loadResources(tinyResources());
  // Fail four two-pointer problems cold, then pass all four on review, twice.
  for (int i : {4, 8, 12, 16}) call(engine, gradeRequest("lc" + std::to_string(i), "r", "2026-11-02T09:00"));
  for (int i : {4, 8, 12, 16}) call(engine, gradeRequest("lc" + std::to_string(i), "b", "2026-11-03T09:00"));
  for (int i : {4, 8, 12, 16}) call(engine, gradeRequest("lc" + std::to_string(i), "b", "2026-11-04T09:00"));
  for (int i : {4, 8, 12, 16}) call(engine, gradeRequest("lc" + std::to_string(i), "b", "2026-11-05T09:00"));
  Json insights = call(engine, "{\"cmd\":\"insights\",\"now\":\"2026-11-05T10:00\"}");
  CHECK_EQ(insights["patterns"].items()[0]["slug"].asStr(), std::string("two-pointers"));
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
      call(engine, gradeRequest("lc" + std::to_string(problem), weakPattern ? "r" : "b", now));
    }
  }
  int onWeak = 0, weakSlots = 0;
  for (int roll = 0; roll < 200; ++roll) {
    Json mock = call(engine, "{\"cmd\":\"mock\",\"now\":\"2026-11-05T09:00\",\"roll\":" + std::to_string(roll) + "}");
    for (const Json& row : mock["items"].items()) {
      const std::string slot = row["slot"].asStr();
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
  std::string json = tinyResources();
  const std::string needle = "\"cards\":[";
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

  // The same miss on a deck that drills something does count: signals drill the thinking side.
  const Profile signal = buildProfile(resources, {miss("os-1"), miss("signals-1")}, clock, clock.day(100));
  CHECK(signal.needs[THINKING_NEED] > 0);
  CHECK_EQ(signal.needShare()[THINKING_NEED], 1.0);

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
  CHECK_EQ(card["card"]["track"].asStr(), std::string("red"));
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
  CHECK_EQ(same["suggest"].asStr(), std::string("b"));
  Json wrong = call(engine, "{\"cmd\":\"diff\",\"typed\":\"int a = 2;\",\"reference\":\"int a = 1;\"}");
  CHECK(wrong["score"].asNum() < 1);
  Json empty = call(engine, "{\"cmd\":\"diff\",\"typed\":\"\",\"reference\":\"int a = 1;\"}");
  CHECK_EQ(empty["suggest"].asStr(), std::string("r"));
}

static void testBadInputDoesNotCrash() {
  Engine engine;
  std::string response = engine.call("{\"cmd\":\"today\",\"now\":\"2026-11-02\"}");
  CHECK(response.find("not loaded") != std::string::npos);
  engine.loadResources(tinyResources());
  CHECK(engine.call("not json").find("\"ok\":false") != std::string::npos);
  CHECK(engine.call("{\"cmd\":\"nope\"}").find("unknown command") != std::string::npos);
  CHECK(engine.call("{\"cmd\":\"today\",\"now\":\"yesterday\"}").find("\"ok\":false") != std::string::npos);
  CHECK(engine.call("{\"cmd\":\"grade\",\"id\":\"lc99999\",\"grade\":\"b\",\"now\":\"2026-11-02\"}").find("unknown question") != std::string::npos);
  CHECK(!engine.loadState("not json"));
  // A log entry with a bad date or grade is dropped, the rest load.
  CHECK(engine.loadState("{\"events\":[{\"id\":\"lc1\",\"g\":\"b\",\"d\":\"bad\"},{\"id\":\"lc2\",\"g\":\"?\",\"d\":\"2026-11-02\"},{\"id\":\"lc3\",\"g\":\"r\",\"d\":\"2026-11-02\"}]}"));
  Json card = call(engine, "{\"cmd\":\"card\",\"id\":\"lc3\",\"now\":\"2026-11-02\"}");
  CHECK_EQ(card["card"]["track"].asStr(), std::string("red"));
  // A question that left the resources stays in the log untouched.
  CHECK(engine.loadState("{\"events\":[{\"id\":\"gone1\",\"g\":\"b\",\"d\":\"2026-11-02\"}]}"));
  call(engine, "{\"cmd\":\"today\",\"now\":\"2026-11-03\"}");
  CHECK(engine.saveState().find("gone1") != std::string::npos);
}

static void testJson() {
  std::string error;
  Json j = Json::parse("{\"a\":[1,2.5,\"x\\n\\u00e9\",null,true],\"b\":{\"c\":-3}}", &error);
  CHECK(error.empty());
  CHECK_EQ(j["a"].items()[0].asInt(), 1);
  CHECK_EQ(j["a"].items()[2].asStr(), std::string("x\n\xC3\xA9"));
  CHECK_EQ(j["b"]["c"].asInt(), -3);
  CHECK_EQ(Json::parse(j.dump()).dump(), j.dump());
  CHECK(Json::parse("{\"a\":").isNull());
  CHECK(Json::parse("[1,2").isNull());
  CHECK(Json::parse("").isNull());
  // Deep nesting is refused, not a stack overflow.
  CHECK(Json::parse(std::string(100000, '[')).isNull());
}


// ── Attempts ──────────────────────────────────────────────────────────────

static std::string attemptRequest(const std::string& id, const std::string& codeJson, const char* now) {
  return "{\"cmd\":\"attempt\",\"id\":\"" + id + "\",\"code\":\"" + codeJson + "\",\"now\":\"" + now + "\"}";
}

static std::string attemptWith(const std::string& id, const std::string& code, const char* now, const std::string& existing) {
  return Json::object().set("cmd", "attempt").set("id", id).set("code", code).set("now", now).set("existing", existing).dump();
}

static void testAttemptNamesAFileAndTouchesNoProgress() {
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "r", "2026-11-02"));
  const std::string before = engine.saveState();

  // One file per problem per study day. The front end appends `block` to it.
  Json first = call(engine, attemptRequest("lc2", "int f() {\\n  return 1;\\n}\\n", "2026-11-06T21:30"));
  CHECK_EQ(first["stem"].asStr(), std::string("profile/attempts/lc2/2026-11-06"));
  CHECK_EQ(first["ext"].asStr(), std::string(".cpp"));
  CHECK_EQ(first["paste"].asInt(), 1);
  CHECK_EQ(first["block"].asStr(), std::string("// ── Paste 1 at 21:30 ──\nint f() {\n  return 1;\n}\n"));
  CHECK_EQ(first["note"].asStr(), std::string("Saved paste 1 of the day, 3 lines, on Problem 2."));
  CHECK(!first["dirty"].asBool());
  CHECK_EQ(engine.saveState(), before);

  // The next paste is numbered from the file as it stands, after a blank line.
  const std::string day = first["block"].asStr();
  Json second = call(engine, attemptWith("lc2", "int f();", "2026-11-06T21:42", day));
  CHECK_EQ(second["paste"].asInt(), 2);
  CHECK_EQ(second["block"].asStr(), std::string("\n// ── Paste 2 at 21:42 ──\nint f();\n"));
  CHECK_EQ(second["note"].asStr(), std::string("Saved paste 2 of the day, 1 line, on Problem 2."));

  // The same code again adds nothing, whatever the trailing blank lines.
  Json again = call(engine, attemptWith("lc2", "int f();\n\n", "2026-11-06T21:43", day + second["block"].asStr()));
  CHECK_EQ(again["block"].asStr(), std::string(""));
  CHECK_EQ(again["paste"].asInt(), 2);
  CHECK_EQ(again["note"].asStr(), std::string("Same as your last paste, so nothing was added."));

  // A file that doesn't end in a newline still gets a blank line before the next paste.
  CHECK_EQ(call(engine, attemptWith("lc2", "y", "2026-11-06T21:50", "x"))["block"].asStr().substr(0, 2), std::string("\n\n"));

  // Past midnight and before 4 AM, a paste joins the night before.
  CHECK_EQ(call(engine, attemptRequest("lc2", "x", "2026-11-07T01:30"))["stem"].asStr(), std::string("profile/attempts/lc2/2026-11-06"));

  // Only the date in "now" reaches the path, and only a clock time reaches the file.
  Json hostile = call(engine, attemptRequest("lc2", "x", "2026-11-06T21:30/../../../etc/passwd"));
  CHECK_EQ(hostile["stem"].asStr(), std::string("profile/attempts/lc2/2026-11-06"));
  Json slashInTime = call(engine, attemptRequest("lc2", "x", "2026-11-06T/.:./"));
  CHECK_EQ(slashInTime["stem"].asStr(), std::string("profile/attempts/lc2/2026-11-06"));
  CHECK(slashInTime["block"].asStr().find("Paste 1 at --:-- ──") != std::string::npos);
  CHECK(call(engine, attemptRequest("lc2", "x", "2026-11-06"))["block"].asStr().find("at --:--") != std::string::npos);
}

static void testAttemptIsRefusedWhenItCannotBeCode() {
  Engine engine;
  engine.loadResources(tinyResources());
  const char* now = "2026-11-06T21:30";
  auto refused = [&](const std::string& request, const char* reason) {
    const std::string response = engine.call(request);
    CHECK(response.find("\"ok\":false") != std::string::npos);
    CHECK(response.find(reason) != std::string::npos);
  };

  refused(attemptRequest("lc99999", "x", now), "unknown question");
  refused(attemptRequest("../../etc/passwd", "x", now), "unknown question");
  refused(attemptRequest("signals-1", "x", now), "is a card");
  refused(attemptRequest("lc1", "", now), "Nothing to save");
  refused(attemptRequest("lc1", " \\n\\t\\r\\n ", now), "Nothing to save");
  refused("{\"cmd\":\"attempt\",\"id\":\"lc1\",\"now\":\"2026-11-06T21:30\"}", "Nothing to save");
  refused(attemptRequest("lc1", "a\\u0000b", now), "isn't text");
  refused("{\"cmd\":\"attempt\",\"id\":\"lc1\",\"code\":\"x\"}", "needs \\\"now\\\"");

  const std::string atLimit(ATTEMPT_MAX_BYTES, 'a');
  call(engine, attemptRequest("lc1", atLimit, now));
  refused(attemptRequest("lc1", atLimit + "a", now), "too long");
}

// ── The coach ─────────────────────────────────────────────────────────────

static const char* const COACH_NOW = "2026-11-20T21:30";

static std::string pendingRequest(const std::string& log, const std::string& attempts = "[]",
                                  const std::string& oldLog = "") {
  return std::string("{\"cmd\":\"pending\",\"now\":\"") + COACH_NOW + "\",\"log\":" + Json(log).dump() +
         ",\"old_log\":" + Json(oldLog).dump() + ",\"attempts\":" + attempts + "}";
}

static std::string judgementRequest(const std::string& judgement, const std::string& log = "",
                                    const std::string& attempts = "[]") {
  return std::string("{\"cmd\":\"judgement\",\"now\":\"") + COACH_NOW + "\",\"judgement\":" + judgement +
         ",\"log\":" + Json(log).dump() + ",\"attempts\":" + attempts + "}";
}

static std::string dropRequest(const std::string& drop, const std::string& log = "", const std::string& oldLog = "",
                               const std::string& attempts = "[]") {
  return std::string("{\"cmd\":\"drop\",\"now\":\"") + COACH_NOW + "\",\"drop\":" + drop + ",\"log\":" + Json(log).dump() +
         ",\"old_log\":" + Json(oldLog).dump() + ",\"attempts\":" + attempts + "}";
}

static std::string commitRequest(const std::string& profile) {
  return std::string("{\"cmd\":\"commit\",\"now\":\"") + COACH_NOW + "\",\"profile\":" + Json(profile).dump() + "}";
}

static Json loadJudgements(Engine& engine, const std::string& text, const std::string& old = "") {
  return call(engine, Json::object().set("cmd", "judgements").set("text", text).set("old", old).dump());
}

static void expectRefused(Engine& engine, const std::string& request, const char* reason) {
  const std::string response = engine.call(request);
  CHECK(response.find("\"ok\":false") != std::string::npos);
  CHECK(response.find(reason) != std::string::npos);
  if (response.find(reason) == std::string::npos) std::printf("      wanted \"%s\" in %s\n", reason, response.c_str());
}

static void testPendingListsGradesWithNoJudgement() {
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "r", "2026-11-02"));
  call(engine, gradeRequest("signals-1", "b", "2026-11-02"));  // a card, so not reviewed
  call(engine, gradeRequest("lc2", "b", "2026-11-03"));
  call(engine, gradeRequest("lc1", "b", "2026-11-04"));
  const std::string before = engine.saveState();

  Json all = call(engine, pendingRequest(""));
  CHECK_EQ(all["total"].asInt(), 3);
  const std::vector<Json>& items = all["items"].items();
  CHECK_EQ(static_cast<int>(items.size()), 3);
  CHECK_EQ(items[0]["problem"].asStr(), std::string("lc1"));
  CHECK_EQ(items[0]["date"].asStr(), std::string("2026-11-02"));
  CHECK_EQ(items[0]["title"].asStr(), std::string("Problem 1"));
  CHECK_EQ(items[0]["grade"].asStr(), std::string("r"));
  CHECK_EQ(items[0]["resources_patterns"].items()[0].asStr(), std::string("heap"));
  CHECK_EQ(items[0]["resources_topics"].items()[0].asStr(), std::string("heap/top-k"));
  CHECK(!items[1].has("resources_topics"));   // lc2 has no finer topic
  CHECK(items[0]["first_try"].asBool());
  CHECK_EQ(items[2]["problem"].asStr(), std::string("lc1"));
  CHECK(!items[2]["first_try"].asBool());
  CHECK_EQ(engine.saveState(), before);

  // A judged grade drops out. The same problem on another day still waits.
  const Json rest = call(engine, pendingRequest("{\"problem\":\"lc1\",\"date\":\"2026-11-02\"}\n"));
  CHECK_EQ(rest["total"].asInt(), 2);
  CHECK_EQ(rest["items"].items()[0]["problem"].asStr(), std::string("lc2"));
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
  const std::string old =
      "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"your_grade\":\"r\",\"model_grade\":\"r\",\"patterns\":[\"heap\"],"
      "\"flaws\":[\"bound-wrong\"],\"note\":\"Old.\",\"had_code\":false,\"ts\":\"2026-11-07T09:35\"}\n";
  const Json pending = call(engine, pendingRequest("", "[]", old));
  CHECK_EQ(pending["total"].asInt(), 2);
  const Json& first = pending["items"].items()[0];
  CHECK_EQ(first["earlier_judgement"]["note"].asStr(), std::string("Old."));
  CHECK_EQ(first["earlier_judgement"]["flaws"].items()[0].asStr(), std::string("bound-wrong"));
  CHECK(!first["earlier_judgement"].has("ts"));
  CHECK_EQ(first["your_gaps"].items()[0].asStr(), std::string("speed"));
  CHECK_EQ(first["your_bug"].asStr(), std::string("overflow"));
  CHECK(!pending["items"].items()[1].has("earlier_judgement"));
  // Once the new log has it, judged or dropped, it waits no more.
  CHECK_EQ(call(engine, pendingRequest("{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"dropped\":true}\n", "[]", old))["total"].asInt(), 1);
}

static void testPendingStopsAtOneBatch() {
  Engine engine;
  engine.loadResources(tinyResources());
  for (int i = 1; i <= BATCH_SIZE + 3; ++i) call(engine, gradeRequest("lc" + std::to_string(i), "b", "2026-11-02"));
  const Json pending = call(engine, pendingRequest(""));
  CHECK_EQ(pending["total"].asInt(), BATCH_SIZE + 3);
  CHECK_EQ(static_cast<int>(pending["items"].size()), BATCH_SIZE);
  CHECK_EQ(pending["items"].items().back()["problem"].asStr(), "lc" + std::to_string(BATCH_SIZE));
}

static void testAttemptsBelongToTheNextGrade() {
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "r", "2026-11-02T22:00"));
  call(engine, gradeRequest("lc1", "y", "2026-11-04T22:00"));
  const std::string attempts =
      "[\"lc1/2026-11-05T10-00.cpp\",\"lc1/2026-11-02T21-30-10.cpp\",\"lc1/2026-11-02T21-30-2.cpp\","
      "\"lc1/2026-11-02T21-30.cpp\",\"lc1/2026-11-03T01-30.cpp\",\"lc1/2026-11-04T09-00.cpp\","
      "\"lc1/2026-11-04.cpp\",\"lc1/notes.txt\",\".DS_Store\",\"gone/2026-11-02T21-30.cpp\"]";
  const Json pending = call(engine, pendingRequest("", attempts));
  const std::vector<Json>& first = pending["items"].items()[0]["attempts"].items();
  CHECK_EQ(static_cast<int>(first.size()), 4);
  CHECK_EQ(first[0].asStr(), std::string("lc1/2026-11-02T21-30.cpp"));
  CHECK_EQ(first[1].asStr(), std::string("lc1/2026-11-02T21-30-2.cpp"));
  CHECK_EQ(first[2].asStr(), std::string("lc1/2026-11-02T21-30-10.cpp"));
  CHECK_EQ(first[3].asStr(), std::string("lc1/2026-11-03T01-30.cpp"));  // 1:30 AM is still the night of the 2nd
  // A day's file, named by the date alone, goes with that day's grade, ahead of a timed file from the same day.
  const std::vector<Json>& second = pending["items"].items()[1]["attempts"].items();
  CHECK_EQ(static_cast<int>(second.size()), 2);
  CHECK_EQ(second[0].asStr(), std::string("lc1/2026-11-04.cpp"));
  CHECK_EQ(second[1].asStr(), std::string("lc1/2026-11-04T09-00.cpp"));
}

static void testJudgementBecomesALogLine() {
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "r", "2026-11-02"));
  call(engine, gradeRequest("lc2", "y", "2026-11-03"));
  const std::string before = engine.saveState();
  const std::string attempts = "[\"lc1/2026-11-02T21-30.cpp\"]";
  const std::string good =
      "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"model_grade\":\"y\",\"recognised\":\"yes\",\"key_step\":\"own\","
      "\"topics\":[\"heap/top-k\"],\"flaws\":[\"off-by-one\"],\"note\":\"Right idea, wrong bound.\","
      "\"traced\":\"[1] returns 0, should be 1\",\"by\":\"test\"}";

  const Json saved = call(engine, judgementRequest(good, "", attempts));
  const Json& line = saved["line"];
  CHECK_EQ(line["title"].asStr(), std::string("Problem 1"));
  CHECK_EQ(line["your_grade"].asStr(), std::string("r"));
  CHECK_EQ(line["model_grade"].asStr(), std::string("y"));
  CHECK_EQ(line["recognised"].asStr(), std::string("yes"));
  CHECK_EQ(line["key_step"].asStr(), std::string("own"));
  CHECK_EQ(line["topics"].items()[0].asStr(), std::string("heap/top-k"));
  CHECK_EQ(line["flaws"].items()[0].asStr(), std::string("off-by-one"));
  CHECK_EQ(line["traced"].asStr(), std::string("[1] returns 0, should be 1"));
  CHECK_EQ(line["ts"].asStr(), std::string(COACH_NOW));
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
  CHECK_EQ(bare["line"]["your_grade"].asStr(), std::string("y"));
  expectRefused(engine, judgementRequest("{\"problem\":\"lc2\",\"date\":\"2026-11-03\",\"flaws\":[\"overflow\"]}"), "read from code");
  call(engine, judgementRequest("{\"problem\":\"lc2\",\"date\":\"2026-11-03\",\"recognised\":\"no\",\"flaws\":[]}"));

  const std::string log = line.dump() + "\n";
  expectRefused(engine, judgementRequest(good, log, attempts), "already judged");
  expectRefused(engine, judgementRequest("{\"problem\":\"lc1\",\"date\":\"2026-11-09\"}"), "no grade");
  expectRefused(engine, judgementRequest("{\"problem\":\"lc9\",\"date\":\"2026-11-02\"}"), "no grade");
  expectRefused(engine, judgementRequest("{\"date\":\"2026-11-02\"}"), "no grade");

  auto refusedWithCode = [&](const char* fields, const char* reason) {
    const std::string judgement = std::string("{\"problem\":\"lc1\",\"date\":\"2026-11-02\",") + fields + "}";
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
  const std::string base = "\"model_grade\":\"r\",\"recognised\":\"yes\",\"key_step\":\"own\",\"note\":\"x\",";
  auto refusedField = [&](const std::string& field, const char* reason) { refusedWithCode((base + field).c_str(), reason); };
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

  std::string tooMany = "\"topics\":[";
  for (int i = 0; i <= JUDGEMENT_MAX_TOPICS; ++i) tooMany += i ? ",\"heap\"" : "\"heap\"";
  refusedField(tooMany + "]", "at most");

  // The note limit counts characters, so a note in any language gets the same room.
  std::string fits;
  for (int i = 0; i < NOTE_MAX_CHARS; ++i) fits += "\xE9\x9D\x9E";
  const std::string longNote = "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"model_grade\":\"b\",\"recognised\":\"yes\","
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
  const std::string attemptsToo = "[\"lc1/2026-11-02T21-30.cpp\",\"lc3/2026-11-04T09-00.cpp\"]";
  const std::string onYellow = "{\"problem\":\"lc3\",\"date\":\"2026-11-04\",\"recognised\":\"yes\",\"key_step\":\"hint\",\"note\":\"x\",";
  expectRefused(engine, judgementRequest(onYellow + "\"model_grade\":\"b\"}", "", attemptsToo), "traced");
  call(engine, judgementRequest(onYellow + "\"model_grade\":\"y\"}", "", attemptsToo));
  call(engine, judgementRequest(onYellow + "\"model_grade\":\"r\"}", "", attemptsToo));

  // Too long a trace is refused like too long a note.
  const std::string longTrace(NOTE_MAX_CHARS + 1, 'x');
  expectRefused(engine, judgementRequest(onYellow + "\"model_grade\":\"b\",\"traced\":\"" + longTrace + "\"}", "", attemptsToo), "traced is");
}

static void testCommitSavesOnlyTheProfile() {
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "b", "2026-11-02"));
  const std::string before = engine.saveState();
  const Json saved = call(engine, commitRequest("# Profile"));
  CHECK(!saved.has("table"));   // the coach writes no probabilities
  CHECK(!saved["dirty"].asBool());
  CHECK_EQ(engine.saveState(), before);

  // Profile size, right at the limit and one past it.
  call(engine, commitRequest(std::string(PROFILE_MAX_BYTES, 'x')));
  expectRefused(engine, commitRequest(std::string(PROFILE_MAX_BYTES + 1, 'x')), "2048");
  expectRefused(engine, commitRequest("  \n "), "empty");
}

static void testDropIsOnlyForNothingToAdd() {
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "r", "2026-11-02"));
  call(engine, gradeRequest("lc2", "b", "2026-11-03"));
  call(engine, gradeRequest("lc3", "y", "2026-11-04"));
  const std::string before = engine.saveState();
  const std::string attempts = "[\"lc1/2026-11-02T21-30.cpp\",\"lc3/2026-11-04T09-00.cpp\"]";
  const std::string old = "{\"problem\":\"lc3\",\"date\":\"2026-11-04\",\"model_grade\":\"y\",\"note\":\"Said then.\"}\n";
  auto drop = [](const char* problem, const char* date, const char* reason) {
    return std::string("{\"problem\":\"") + problem + "\",\"date\":\"" + date + "\",\"reason\":\"" + reason + "\",\"by\":\"test\"}";
  };

  // No code: nothing to read, so it can go.
  const Json bare = call(engine, dropRequest(drop("lc2", "2026-11-03", "No code, the grade says it."), "", old, attempts));
  CHECK(bare["line"]["dropped"].asBool());
  CHECK(!bare["line"]["had_code"].asBool());
  CHECK_EQ(bare["line"]["reason"].asStr(), std::string("No code, the grade says it."));
  CHECK_EQ(bare["line"]["your_grade"].asStr(), std::string("b"));
  CHECK_EQ(bare["remaining"].asInt(), 2);
  CHECK(!bare["dirty"].asBool());
  CHECK_EQ(engine.saveState(), before);

  // Code with an earlier judgement that already says it: the coach may let it go.
  call(engine, dropRequest(drop("lc3", "2026-11-04", "The earlier judgement covers it."), "", old, attempts));
  // Code nobody has judged must be judged.
  expectRefused(engine, dropRequest(drop("lc1", "2026-11-02", "x"), "", old, attempts), "needs a judgement");

  const std::string log = bare["line"].dump() + "\n";
  expectRefused(engine, dropRequest(drop("lc2", "2026-11-03", "again"), log, old, attempts), "already judged or dropped");
  expectRefused(engine, dropRequest(drop("lc2", "2026-11-09", "x"), "", old, attempts), "no grade");
  expectRefused(engine, dropRequest("{\"problem\":\"lc2\",\"date\":\"2026-11-03\",\"reason\":\" \"}", "", old, attempts), "reason");
  expectRefused(engine, dropRequest("{\"problem\":\"lc2\",\"date\":\"2026-11-03\",\"reason\":\"x\",\"note\":\"y\"}", "", old, attempts),
                "no field called note");
  // A dropped attempt waits no more.
  CHECK_EQ(call(engine, pendingRequest(log, attempts, old))["total"].asInt(), 2);
}

static std::string logLine(const char* problem, const char* date, bool firstTry, const char* grade,
                           const char* topics, const char* flaws, const char* note) {
  return std::string("{\"problem\":\"") + problem + "\",\"title\":\"Title " + problem + "\",\"date\":\"" + date +
         "\",\"first_try\":" + (firstTry ? "true" : "false") + ",\"your_grade\":\"b\",\"model_grade\":\"" + grade +
         "\",\"topics\":" + topics + ",\"flaws\":" + flaws + ",\"note\":\"" + note + "\"}\n";
}

static std::string searchRequest(const std::string& fields, const std::string& log, const std::string& oldLog = "") {
  return "{\"cmd\":\"search\"," + fields + (fields.empty() ? "" : ",") + "\"log\":" + Json(log).dump() +
         ",\"old_log\":" + Json(oldLog).dump() + "}";
}

static void testSearchRecallsPastJudgements() {
  Engine engine;
  engine.loadResources(tinyResources());
  std::string log;
  log += logLine("lc1", "2026-11-01", true, "b", "[\"heap/top-k\"]", "[]", "Clean.");
  log += logLine("lc2", "2026-11-02", true, "r", "[\"heap\"]", "[\"off-by-one\"]", "Bound was wrong.");
  log += "this line was cut short by a crash\n\n";
  log += logLine("lc3", "2026-11-03", false, "y", "[\"trie\"]", "[\"off-by-one\"]", "Bound again, on a BOUNDARY.");
  log += logLine("lc1", "2026-11-04", false, "b", "[\"heap/top-k\"]", "[]", "Solid.");

  auto problems = [](const Json& reply) {
    std::string out;
    for (const Json& row : reply["rows"].items()) out += row["problem"].asStr() + " ";
    return out;
  };

  // Newest first, and the counts cover every match. A pattern also finds its finer topics.
  const Json heap = call(engine, searchRequest("\"label\":\"heap\"", log));
  CHECK_EQ(problems(heap), std::string("lc1 lc2 lc1 "));
  CHECK_EQ(heap["total"].asInt(), 3);
  CHECK_EQ(heap["first_tries"].asInt(), 2);
  CHECK_EQ(heap["first_try_passes"].asInt(), 1);

  // A pass needs both grades. You ran the code and the model only read it.
  auto passes = [&](const char* line) { return call(engine, searchRequest("", line))["first_try_passes"].asInt(); };
  CHECK_EQ(passes("{\"first_try\":true,\"your_grade\":\"r\",\"model_grade\":\"b\"}"), 0);
  CHECK_EQ(passes("{\"first_try\":true,\"your_grade\":\"b\",\"model_grade\":\"y\"}"), 0);
  CHECK_EQ(passes("{\"first_try\":true,\"your_grade\":\"b\",\"model_grade\":\"b\"}"), 1);
  CHECK_EQ(passes("{\"first_try\":true,\"your_grade\":\"b\"}"), 1);  // no code, so yours is all there is

  CHECK_EQ(problems(call(engine, searchRequest("\"label\":\"heap/top-k\"", log))), std::string("lc1 lc1 "));
  CHECK_EQ(problems(call(engine, searchRequest("\"label\":\"heap/two\"", log))), std::string(""));  // whole names only
  CHECK_EQ(problems(call(engine, searchRequest("\"label\":\"hea\"", log))), std::string(""));
  CHECK_EQ(problems(call(engine, searchRequest("\"label\":\"off-by-one\"", log))), std::string("lc3 lc2 "));
  CHECK_EQ(problems(call(engine, searchRequest("\"problem\":\"lc1\"", log))), std::string("lc1 lc1 "));
  CHECK_EQ(problems(call(engine, searchRequest("\"text\":\"bound\"", log))), std::string("lc3 lc2 "));
  CHECK_EQ(problems(call(engine, searchRequest("\"text\":\"TITLE LC3\"", log))), std::string("lc3 "));
  CHECK_EQ(problems(call(engine, searchRequest("\"text\":\"bound\",\"label\":\"trie\"", log))), std::string("lc3 "));
  CHECK_EQ(call(engine, searchRequest("\"label\":\"nothing-here\"", log))["total"].asInt(), 0);
  CHECK_EQ(call(engine, searchRequest("", ""))["total"].asInt(), 0);
  CHECK_EQ(call(engine, searchRequest("", log))["total"].asInt(), 4);

  /*
  The judgements from before 2026-11-09 answer too, older than everything in the
  new log and marked as such. Their old labels still match. Once the new log has
  an attempt, judged or dropped, its old line stops counting.
  */
  const std::string old = "{\"problem\":\"lc6\",\"date\":\"2026-10-30\",\"patterns\":[\"trie\"],\"flaws\":[\"bound-wrong\"],\"note\":\"Old.\"}\n"
                          "{\"problem\":\"lc7\",\"date\":\"2026-10-30\",\"topics\":[\"trie\"],\"abilities\":[\"edge\"],\"note\":\"Older.\"}\n";
  const Json trie = call(engine, searchRequest("\"label\":\"trie\"", log, old));
  CHECK_EQ(problems(trie), std::string("lc3 lc7 lc6 "));
  CHECK(trie["rows"].items()[1]["earlier_shape"].asBool());
  CHECK(!trie["rows"].items()[0].has("earlier_shape"));
  CHECK_EQ(problems(call(engine, searchRequest("\"label\":\"edge\"", log, old))), std::string("lc7 "));
  CHECK_EQ(problems(call(engine, searchRequest("\"label\":\"bound-wrong\"", log, old))), std::string("lc6 "));
  const std::string carried = log + "{\"problem\":\"lc6\",\"date\":\"2026-10-30\",\"dropped\":true}\n";
  CHECK_EQ(problems(call(engine, searchRequest("\"label\":\"trie\"", carried, old))), std::string("lc3 lc7 "));

  // The cap holds, however much is asked for.
  std::string longLog;
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
  const std::vector<Json>& patterns = list["patterns"].items();
  CHECK_EQ(static_cast<int>(patterns.size()), 4);
  CHECK_EQ(patterns[1]["slug"].asStr(), std::string("heap"));
  CHECK_EQ(patterns[1]["topics"].items()[1]["slug"].asStr(), std::string("heap/two-heaps"));
  CHECK_EQ(patterns[1]["topics"].items()[1]["signal"].asStr(), std::string("A running median."));
  CHECK(!patterns[2].has("topics"));   // trie has no finer topics, so it is a topic itself

  const std::vector<Json>& flaws = list["flaws"].items();
  CHECK_EQ(static_cast<int>(flaws.size()), FLAW_COUNT);
  CHECK_EQ(flaws[0]["slug"].asStr(), std::string("off-by-one"));
  CHECK_EQ(flaws[0]["drilled_by"].asStr(), std::string("bugs"));
  CHECK_EQ(static_cast<int>(list["thinking"]["recognised"].size()), 3);
  CHECK_EQ(list["thinking"]["key_step"].items()[1].asStr(), std::string("hint"));
  CHECK(!list.has("abilities"));

  // The command that let a model attach problems by hand is gone, and so is the one that loaded tables.
  CHECK(engine.call("{\"cmd\":\"candidates\",\"text\":\"x\",\"now\":\"2026-11-02\"}").find("\"ok\":false") != std::string::npos);
  CHECK(engine.call("{\"cmd\":\"table\",\"text\":\"{}\"}").find("unknown command") != std::string::npos);
}

static void testOverviewSaysWhereThingsStand() {
  Engine engine;
  engine.loadResources(tinyResources());
  call(engine, gradeRequest("lc1", "b", "2026-11-02"));
  call(engine, gradeRequest("lc2", "b", "2026-11-03"));
  auto overview = [&](const std::string& log, const std::string& old = "") {
    return call(engine, std::string("{\"cmd\":\"overview\",\"now\":\"") + COACH_NOW + "\",\"log\":" + Json(log).dump() +
                            ",\"old_log\":" + Json(old).dump() + "}");
  };

  // The app asks where these files live, so the layout stays in one place.
  const Json rules = call(engine, "{\"cmd\":\"rules\"}");
  CHECK_EQ(rules["judgementsFile"].asStr(), std::string(JUDGEMENTS_FILE));
  CHECK_EQ(rules["oldJudgementsFile"].asStr(), std::string(OLD_JUDGEMENTS_FILE));
  CHECK_EQ(rules["lastReviewFile"].asStr(), std::string(LAST_REVIEW_FILE));
  CHECK(!rules.has("labelsFile"));

  const std::string old = "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"model_grade\":\"b\"}\n";
  const Json first = overview("", old);
  CHECK_EQ(first["pending"].asInt(), 2);
  CHECK_EQ(first["pending_with_earlier_judgement"].asInt(), 1);
  CHECK_EQ(first["batch_size"].asInt(), BATCH_SIZE);
  CHECK_EQ(first["judged"].asInt(), 0);
  CHECK_EQ(static_cast<int>(first["flaw_counts"].size()), 0);

  // One judged and one dropped: nothing waits.
  const std::string log = "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"flaws\":[\"overflow\"]}\n"
                          "{\"problem\":\"lc2\",\"date\":\"2026-11-03\",\"dropped\":true}\n";
  const Json later = overview(log, old);
  CHECK_EQ(later["pending"].asInt(), 0);
  CHECK_EQ(later["pending_with_earlier_judgement"].asInt(), 0);
  CHECK_EQ(later["judged"].asInt(), 1);
  CHECK_EQ(later["dropped"].asInt(), 1);

  // How often each flaw and thinking label came up, most common flaw first. Old free flaws aren't flaws.
  const std::string judged = "{\"flaws\":[\"overflow\",\"off-by-one\"],\"recognised\":\"yes\",\"key_step\":\"hint\"}\n"
                             "{\"flaws\":[\"off-by-one\"],\"recognised\":\"no\",\"key_step\":\"missed\"}\n"
                             "{\"flaws\":[\"bfs-minute-count\"]}\n{\"flaws\":[\"overflow\"],\"dropped\":true}\n";
  const Json counted = overview(judged);
  const std::vector<Json::Member>& flaws = counted["flaw_counts"].members();
  CHECK_EQ(static_cast<int>(flaws.size()), 2);
  CHECK_EQ(flaws[0].first, std::string("off-by-one"));
  CHECK_EQ(flaws[0].second.asInt(), 2);
  CHECK_EQ(flaws[1].first, std::string("overflow"));
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
  const std::string before = engine.saveState();
  auto track = [&]() {
    return call(engine, "{\"cmd\":\"card\",\"id\":\"lc1\",\"now\":\"2026-11-21T09:00\"}")["card"]["track"].asStr();
  };
  CHECK_EQ(track(), std::string("retired"));

  // The coach read the code of that last check and found a bug: it keeps going, as the coach graded it.
  const Json loaded = loadJudgements(engine, "{\"problem\":\"lc1\",\"date\":\"2026-11-20\",\"model_grade\":\"y\",\"had_code\":true}\n");
  CHECK(!loaded["dirty"].asBool());
  CHECK_EQ(track(), std::string("yellow"));
  const Json steps = call(engine, "{\"cmd\":\"explain\",\"id\":\"lc1\"}")["steps"];
  CHECK_EQ(steps.items().back()["click"].asStr(), std::string("b"));     // your click stays yours
  CHECK_EQ(steps.items().back()["coach"].asStr(), std::string("y"));

  // A coach grade as kind as yours changes nothing, and neither does one on a check that wasn't the last.
  loadJudgements(engine, "{\"problem\":\"lc1\",\"date\":\"2026-11-20\",\"model_grade\":\"b\"}\n");
  CHECK_EQ(track(), std::string("retired"));
  loadJudgements(engine, "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"model_grade\":\"r\"}\n");
  CHECK_EQ(track(), std::string("retired"));

  // A judgement from before 2026-11-09 counts by its grade, until the new log judges or drops that attempt.
  const std::string old = "{\"problem\":\"lc1\",\"date\":\"2026-11-20\",\"model_grade\":\"r\",\"recognised\":\"no\"}\n";
  loadJudgements(engine, "", old);
  CHECK_EQ(track(), std::string("red"));
  loadJudgements(engine, "{\"problem\":\"lc1\",\"date\":\"2026-11-20\",\"dropped\":true}\n", old);
  CHECK_EQ(track(), std::string("retired"));

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

static std::string nowOn(int date) { return formatDate(date) + "T20:00"; }

static void testRandomSessionsKeepInvariants() {
  Rng rng(777);
  for (int run = 0; run < 12; ++run) {
    Engine engine;
    engine.loadResources(tinyResources());
    const int cap = 2 + rng.below(4);
    engine.loadState("{\"settings\":{\"codeCap\":" + std::to_string(cap) + ",\"codeNew\":1,\"cardCap\":" +
                     std::to_string(cap) + ",\"cardNew\":1}}");
    int date = parseDate("2026-11-02");
    std::set<std::string> pulled;   // every problem a pull added
    for (int step = 0; step < 90; ++step) {
      date += 1 + (rng.below(5) == 0 ? rng.below(6) : 0);  // some days get skipped
      const std::string now = nowOn(date);
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
      std::set<std::string> seen;
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
        std::set<std::string> queued;
        const Json before = Json::parse(engine.call("{\"cmd\":\"today\",\"now\":\"" + now + "\"}"));
        for (const Json& item : before["code"]["items"].items()) queued.insert(item["id"].asStr());
        const Json more = Json::parse(engine.call("{\"cmd\":\"more\",\"lane\":\"code\",\"now\":\"" + now + "\"}"));
        for (const Json& item : more["code"]["items"].items()) {
          if (!queued.count(item["id"].asStr())) pulled.insert(item["id"].asStr());
        }
      }

      // Saving and loading never changes anything.
      const std::string saved = engine.saveState();
      Engine copy;
      copy.loadResources(tinyResources());
      CHECK(copy.loadState(saved));
      CHECK_EQ(copy.saveState(), saved);
      const std::string request = "{\"cmd\":\"insights\",\"now\":\"" + now + "\"}";
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
  for (int i = 1; i <= 30; ++i) call(engine, gradeRequest("lc" + std::to_string(i), "r", nowOn(date).c_str()));

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
static std::string tinyPattern(const std::string& id) {
  static const char* const names[] = {"two-pointers", "heap", "trie", "greedy"};
  return names[std::atoi(id.c_str() + 2) % 4];
}

static Json onePick(Engine& engine, const char* now, int roll) {
  const Json picks = call(engine, std::string("{\"cmd\":\"recommend\",\"now\":\"") + now + "\",\"n\":1,\"roll\":" + std::to_string(roll) + "}");
  return picks["rows"].items().empty() ? Json() : picks["rows"].items()[0];
}

static void testEveryPatternGetsAFirstTry() {
  Engine engine;
  engine.loadResources(tinyResources());
  // Nothing tried: four picks, four patterns, each a first meeting.
  const Json picks = call(engine, "{\"cmd\":\"recommend\",\"now\":\"2026-11-02T09:00\",\"n\":4}");
  std::set<std::string> patterns;
  for (const Json& row : picks["rows"].items()) {
    CHECK_EQ(row["slot"].asStr(), std::string("new-pattern"));
    CHECK(row["why"].asStr().rfind("New pattern: ", 0) == 0);
    patterns.insert(tinyPattern(row["id"].asStr()));
  }
  CHECK_EQ(static_cast<int>(patterns.size()), 4);

  // A pattern you have tried waits while any other hasn't been.
  call(engine, gradeRequest("lc4", "r", "2026-11-02T09:00"));   // two-pointers
  for (int roll = 0; roll < 200; ++roll) {
    const Json pick = onePick(engine, "2026-11-03T09:00", roll);
    CHECK(tinyPattern(pick["id"].asStr()) != "two-pointers");
    CHECK_EQ(pick["slot"].asStr(), std::string("new-pattern"));
  }
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
    const Json picks = call(engine, "{\"cmd\":\"recommend\",\"now\":\"2026-11-03T09:00\",\"n\":4,\"roll\":" + std::to_string(roll) + "}");
    const std::vector<Json>& rows = picks["rows"].items();
    CHECK_EQ(static_cast<int>(rows.size()), 4);
    for (size_t k = 0; k < rows.size(); ++k) {
      if (rows[k]["slot"].asStr() != "new-topic") {
        CHECK_EQ(rows[k]["slot"].asStr(), std::string("practice"));
        continue;
      }
      ++topical;
      CHECK_EQ(static_cast<int>(k), 2);
      CHECK(rows[k]["id"].asStr() == "lc13" || rows[k]["id"].asStr() == "lc17");
      CHECK(rows[k]["why"].asStr().rfind("New topic: Two heaps, in Heap.", 0) == 0);
    }
  }
  CHECK(topical > 40);   // unless an earlier practice pick already landed on that topic

  // With every finer topic done, that place goes to practice too.
  call(engine, gradeRequest("lc13", "b", "2026-11-02T09:00"));
  const Json after = call(engine, "{\"cmd\":\"recommend\",\"now\":\"2026-11-03T09:00\",\"n\":4}");
  for (const Json& row : after["rows"].items()) {
    CHECK_EQ(row["slot"].asStr(), std::string("practice"));
  }
}

static void testRecentProblemsHandOnTheirPull() {
  // Every pattern missed once; heap missed `moreHeap` more times, all on day one.
  auto heapPicks = [](int moreHeap, int idleDays) {
    Engine engine;
    engine.loadResources(tinyResources());
    for (const char* id : {"lc1", "lc2", "lc3", "lc4"}) call(engine, gradeRequest(id, "r", "2026-11-02T09:00"));
    for (int k = 1; k <= moreHeap; ++k) call(engine, gradeRequest("lc" + std::to_string(1 + 4 * k), "r", "2026-11-02T09:00"));
    // Study days pass only when something is graded, so a card a day moves the clock on.
    int date = parseDate("2026-11-03");
    for (int d = 0; d < idleDays; ++d, ++date) {
      call(engine, gradeRequest("signals-" + std::to_string(d + 1), "b", nowOn(date).c_str()));
    }
    int heap = 0;
    for (int roll = 0; roll < 400; ++roll) {
      const Json pick = onePick(engine, nowOn(date).c_str(), roll);
      CHECK_EQ(pick["slot"].asStr(), std::string("practice"));
      heap += tinyPattern(pick["id"].asStr()) == "heap";
    }
    return heap;
  };
  const int fresh = heapPicks(0, 0), worn = heapPicks(3, 0);
  CHECK(fresh > 60);         // about a quarter
  CHECK(worn * 3 < fresh);   // three more lately: its pull is an eighth of theirs
  // A week of study days later the pull is back, and more, since heap went worst.
  CHECK(heapPicks(3, 8) > fresh);
}

static void testTargetCompanyNarrowsThePicks() {
  Engine engine;
  engine.loadResources(tinyResources(6));   // lc1 to lc6 are asked at Google
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"company\":\"google\"}}");
  auto picked = [&] {
    std::set<std::string> ids;
    for (int roll = 0; roll < 100; ++roll) {
      const Json picks = call(engine, "{\"cmd\":\"recommend\",\"now\":\"2026-11-02T09:00\",\"n\":4,\"roll\":" + std::to_string(roll) + "}");
      for (const Json& row : picks["rows"].items()) ids.insert(row["id"].asStr());
    }
    return ids;
  };
  const auto google = picked();
  CHECK(google.size() >= 4);
  for (const std::string& id : google) CHECK(std::atoi(id.c_str() + 2) <= 6);
  // Any company: everything is back.
  call(engine, "{\"cmd\":\"settings\",\"set\":{\"company\":\"\"}}");
  const auto any = picked();
  CHECK(std::any_of(any.begin(), any.end(), [](const std::string& id) { return std::atoi(id.c_str() + 2) > 6; }));
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
  const std::vector<Json>& needs = judged["needs"].items();
  CHECK_EQ(static_cast<int>(needs.size()), 2);
  CHECK_EQ(needs[0]["slug"].asStr(), std::string("off-by-one"));
  CHECK_EQ(needs[0]["count"].asInt(), 1);
  CHECK_EQ(needs[1]["share"].asNum(), 0.5);
  CHECK(judged["notes"].items()[0]["text"].asStr().find("Off by one, or wrong bounds, in 1 of 2 judged attempts") != std::string::npos);
}

static void testNeedsPickEveryOtherCard() {
  auto cards = [](bool judged) {
    Engine engine;
    engine.loadResources(tinyResources());
    call(engine, gradeRequest("lc1", "r", "2026-11-02T09:00"));
    if (judged) {
      loadJudgements(engine, "{\"problem\":\"lc1\",\"date\":\"2026-11-02\",\"recognised\":\"no\",\"key_step\":\"missed\",\"had_code\":true}\n");
    }
    return call(engine, "{\"cmd\":\"recommend\",\"lane\":\"cards\",\"now\":\"2026-11-03T09:00\",\"n\":6}");
  };
  const char* const followed = "of what your judged code shows";
  int drilled = 0;
  const Json judged = cards(true), plain = cards(false);
  for (const Json& row : judged["rows"].items()) {
    if (row["why"].asStr().find(followed) == std::string::npos) continue;
    ++drilled;
    CHECK(row["id"].asStr().rfind("signals-", 0) == 0);   // signals drill seeing the pattern
    CHECK(row["why"].asStr().rfind("Drills: Seeing the pattern and its key step, 100%", 0) == 0);
  }
  CHECK_EQ(drilled, 6 / FLAW_CARD_EVERY);
  // With nothing judged, no card follows a need.
  for (const Json& row : plain["rows"].items()) CHECK(row["why"].asStr().find(followed) == std::string::npos);
}

static void testSlowToWriteAimsEasier() {
  // Three heap problems asked equally often: easy, hard, and the middling one you did.
  const std::string json =
      "{\"patterns\":[{\"slug\":\"heap\",\"name\":\"Heap\"}],\"tags\":[],\"companies\":[],\"decks\":[],\"cards\":[],"
      "\"problems\":[{\"n\":1,\"t\":\"Easy one\",\"d\":1,\"r\":1100,\"f\":50,\"p\":[0]},"
      "{\"n\":2,\"t\":\"Hard one\",\"d\":3,\"r\":2300,\"f\":50,\"p\":[0]},"
      "{\"n\":3,\"t\":\"Done one\",\"d\":2,\"r\":1700,\"f\":50,\"p\":[0]}]}";
  auto easyPicks = [&](const char* flaws, std::string* why) {
    Engine engine;
    engine.loadResources(json);
    call(engine, gradeRequest("lc3", "b", "2026-11-02T09:00"));
    loadJudgements(engine, std::string("{\"problem\":\"lc3\",\"date\":\"2026-11-02\",\"recognised\":\"yes\",\"key_step\":\"own\","
                                       "\"had_code\":true,\"flaws\":") + flaws + "}\n");
    int easy = 0;
    for (int roll = 0; roll < 400; ++roll) easy += onePick(engine, "2026-11-03T09:00", roll)["id"].asStr() == "lc1";
    if (why) *why = onePick(engine, "2026-11-03T09:00", 0)["why"].asStr();
    return easy;
  };
  std::string why;
  const int plain = easyPicks("[]", nullptr), slow = easyPicks("[\"slow-to-write\"]", &why),
            other = easyPicks("[\"off-by-one\"]", nullptr);
  CHECK(slow > plain + 60);            // speed is built on problems you can finish
  CHECK(std::abs(other - plain) < 60); // other flaws leave the level alone
  CHECK(why.find("Aimed easier than usual, for: right, but slow to write.") != std::string::npos);

  // A pattern's first problem aims easier than the rest.
  Engine fresh;
  fresh.loadResources(json);
  int first = 0;
  for (int roll = 0; roll < 400; ++roll) first += onePick(fresh, "2026-11-02T09:00", roll)["id"].asStr() == "lc1";
  CHECK(first > 160);
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
  testEveryPatternGetsAFirstTry();
  testNewTopicsKeepComing();
  testRecentProblemsHandOnTheirPull();
  testTargetCompanyNarrowsThePicks();
  testSlipsNeverCountAgainstThePattern();
  testNeedsPickEveryOtherCard();
  testSlowToWriteAimsEasier();
  testBadInputDoesNotCrash();
  testJson();
  testRandomClicksKeepInvariants();
  testRandomSessionsKeepInvariants();
  testEveryDueCardGetsServedEventually();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
