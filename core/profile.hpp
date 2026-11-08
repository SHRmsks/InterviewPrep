/*
profile.hpp — what your click history says about you.

Built fresh from the log, per pattern, along four separate lines of evidence:

  cold     first attempt at a problem      can you do it when you have not seen it
  review   later attempts at a problem     can you repeat it (may be memorized)
  drill    typed code from memory          can you produce it
  flash    flip cards                      can you recognize and explain it

Keeping these apart is the point. Comparing them shows gaps a single score
hides: review well above cold means memorizing, flash well above cold means
you can explain it but not code it.
*/
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "resources.hpp"
#include "config.hpp"
#include "schedule.hpp"

namespace interviewprep {

// One click, as stored in the log.
struct Event {
  std::string id;
  Click click = Click::Red;
  int date = 0;                     // calendar date
  /*
  What older versions of the app asked you after a miss. Kept so the log
  round-trips unchanged, and never read: only the coach names abilities now.
  */
  std::vector<std::string> gaps;
  std::string bug;
  int secs = 0;                     // time spent, 0 if unknown
  bool hinted = false;              // looked at a hint first
  bool mock = false;                // happened in a mock interview
};

/*
The coach's judgement of one graded attempt, from profile/judged-attempts.jsonl.
Keyed by gradeKey(), since grading again the same day replaces a grade.
*/
struct Judged {
  bool dropped = false;             // the coach left it out: the grade alone counts
  bool hasModelGrade = false;
  Click modelGrade = Click::Blue;   // what the coach would give the first paste
  double thinking = -1;             // 0 to 1, recognised x key step; -1 when not given
  std::vector<int> flaws;           // into FLAWS
  bool hadCode = false;
};

using JudgedMap = std::map<std::string, Judged>;

// "lc76@2026-11-07": one problem graded on one date.
std::string gradeKey(const std::string& id, const std::string& date);

struct Evidence {
  double pass = 0;
  double total = 0;

  void add(double credit, double weight) {
    pass += credit * weight;
    total += weight;
  }
  double rate() const { return total > 0 ? pass / total : 0; }
};

struct PatternStats {
  Evidence cold, review, drill, flash;
  int attempts = 0;
  int coldPasses = 0;               // raw counts, for display
  int coldAttempts = 0;
  int recentNew = 0;                // first tries with this as the main pattern in the last RECENT_DAYS

  // Beta(alpha, beta) belief that you get the thinking right cold.
  double alpha = 1;
  double beta = 1;
  double mastery() const { return alpha / (alpha + beta); }

  /*
  Review rate minus cold rate, when there is enough evidence on both sides.
  Above MEMORIZE_MIN_GAP, the app calls it memorizing.
  */
  bool hasMemorizeSignal() const;
  double memorizeGap() const { return review.rate() - cold.rate(); }
};

// What drill cards should work on: each flaw, then the thinking side last.
inline constexpr int NEED_COUNT = FLAW_COUNT + 1;
inline constexpr int THINKING_NEED = FLAW_COUNT;

struct Profile {
  std::vector<PatternStats> patterns;   // same order as Resources::patterns()

  /*
  Signs of each need, decayed like everything else: each judged attempt that
  showed a flaw, each judged attempt's missing thinking credit, and a little
  for each missed card from a deck that drills one.
  */
  std::array<double, NEED_COUNT> needs{};
  std::array<int, FLAW_COUNT> flawCounts{};   // raw counts, for display
  int judgedWithCode = 0;                     // raw count, for display

  // Each need's share of all the signs, 0 to 1. All zero with no history.
  std::array<double, NEED_COUNT> needShare() const;
};

// `events` must be in date order. `judged` is the coach's log, when there is one.
Profile buildProfile(const Resources& resources, const std::vector<Event>& events, const Clock& clock, int today,
                     const JudgedMap* judged = nullptr);

// The deck that drills a need, or nullptr.
const char* needDeck(int need);

}  // namespace interviewprep
