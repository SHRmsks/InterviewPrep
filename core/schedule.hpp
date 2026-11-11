/*
schedule.hpp — the state machine, the clock and the queue order.

Nothing here knows about questions or JSON. It is pure logic over clicks and
day numbers, which is why tests/test_engine.cpp can check every rule directly.
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config.hpp"

using namespace std;

namespace interviewprep {

/*
── Calendar helpers ──────────────────────────────────────────────────────
Calendar dates are stored as days since 1970-01-01.
*/

int dateFromCivil(int year, int month, int day);
string formatDate(int date);                     // "2026-11-06"
int parseDate(const string& text);               // -1 if malformed
/*
Accepts "YYYY-MM-DD" or "YYYY-MM-DDTHH:MM". With a time, hours before
DAY_ROLLOVER_HOUR belong to the previous date.
*/
int dateFromNow(const string& now);

// ── One card ──────────────────────────────────────────────────────────────

struct CardState {
  Track track = Track::New;
  int step = 0;           // which of this track's checks comes next
  int due = 0;            // day number of the next check, 0 for New and Retired
  int attempts = 0;       // how many clicks this card has had
  int lastDay = 0;        // day number of the last click
  int lastDate = 0;       // calendar date of the last click
  Click lastClick = Click::Red;
};

/*
The rules one kind of question follows: the click table and each track's
checks, from sections 2 and 3 of config.hpp.
*/
struct Schedule {
  const Move (*rules)[3];
  const Gap* checks[5];   // per Track; New and Retired have none
  int sizes[5];
  bool blueAfterLast;     // past its last check, Red or Yellow moves on to Blue's checks
};

extern const Schedule PROBLEM_SCHEDULE;
extern const Schedule CARD_SCHEDULE;

int checkCount(const Schedule& schedule, Track track);           // how many checks the track has
Gap checkGap(const Schedule& schedule, Track track, int step);   // the gap before check `step`

// A fixed number from a question's id, so its extra days never change.
uint64_t seedOf(const string& id);

/*
The whole policy: the state after clicking `click` on day number `day`. The
seed draws the extra days of a gap with a spread.
*/
CardState applyClick(CardState state, Click click, int day, const Schedule& schedule = CARD_SCHEDULE,
                     uint64_t seed = 0);

const char* trackName(Track track);     // "new", "red", "yellow", "blue", "retired"
const char* clickName(Click click);     // "b", "y", "r"
bool parseClick(const string& text, Click* out);
const char* moveName(Move move);

/*
── The clock ─────────────────────────────────────────────────────────────
Turns calendar dates into the day numbers that due dates are measured in.
*/

class Clock {
 public:
  explicit Clock(ClockMode mode = DEFAULT_CLOCK) : mode_(mode) {}

  void markActive(int date);        // this date had at least one click
  int day(int date) const;          // the day number of a calendar date
  ClockMode mode() const { return mode_; }
  int activeDays() const { return static_cast<int>(active_.size()); }

 private:
  ClockMode mode_;
  vector<int> active_;         // sorted, distinct
};

// ── Queue order ───────────────────────────────────────────────────────────

bool isDue(const CardState& state, int today);

// Higher goes first: track priority plus AGING_PER_DAY for each day waited.
int queuePriority(const CardState& state, int today);

/*
True when card `a` should be served before card `b`. Ties break on the
older due day, so the order is stable and fair from one day to the next.
*/
bool servedBefore(const CardState& a, const CardState& b, int today);

}  // namespace interviewprep
