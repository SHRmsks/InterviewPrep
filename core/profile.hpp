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

Those four count the thinking. A first try is also kept by whether the code
worked, for the pattern it was mainly about: that is what the picker's pull
reads (section 7 of config.hpp).
*/
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "resources.hpp"
#include "config.hpp"
#include "schedule.hpp"

using namespace std;

namespace interviewprep {

// One click, as stored in the log.
struct Event {
  string id;
  Click click = Click::Red;
  int date = 0;                     // calendar date
  /*
  What older versions of the app asked you after a miss. Kept so the log
  round-trips unchanged, and never read: only the coach names abilities now.
  */
  vector<string> gaps;
  string bug;
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
  double recognised = -1;           // the credit of each thinking label; -1 when not given
  double keyStep = -1;
  double thinking = -1;             // 0 to 1, recognised x key step; -1 when neither is given
  bool missedPattern = false;       // recognised wrong or no: there was no key step to find
  bool noApproach = false;          // recognised no
  bool keyStepOwn = false;          // found the key step alone
  vector<int> patterns;             // where it happened: the patterns of the topics it names
  vector<int> topics;               // the finer topics it names, into Resources::topics
  int reachedFor = -1;              // recognised wrong: the pattern they went for instead
  vector<int> flaws;                // into FLAWS
  bool hadCode = false;
};

using JudgedMap = map<string, Judged>;

// "lc76@2026-11-07": one problem graded on one date.
string gradeKey(const string& id, const string& date);

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
  Evidence code;                    // first tries with this as the main pattern, by whether the code worked
  int attempts = 0;
  int coldPasses = 0;               // raw counts, for display
  int coldAttempts = 0;             // first tries with this as the main pattern
  double recentSolved = 0;          // those in the last RECENT_DAYS, each counted by how it went

  // Beta(alpha, beta) belief that you get the thinking right cold.
  double alpha = 1;
  double beta = 1;
  double mastery() const { return alpha / (alpha + beta); }

  /*
  After a zero-clue attempt (a blank, or recognised: no, with the key step not
  found on your own) the pattern starts over: 1 while its new problems are
  Easy, 2 for a Medium once you find a key step yourself, back to 0 with the
  next. Its templates come first among the drills meanwhile.
  */
  int startOverStep = 0;
  bool startingOver() const { return startOverStep > 0; }

  // Your ratings here: your overall rating plus this pattern's nudge (DIFFICULTY in config.hpp).
  double rating = RATING_START;         // for the idea: sets how hard its next problem is
  double codeRating = RATING_START;     // for working code: sets how often it comes up
  // How its first tries went by working code, starting from BORROWED_TRIES at an even chance.
  double codeScore() const { return (code.pass + BORROWED_TRIES * 0.5) / (code.total + BORROWED_TRIES); }
  int ratedTries = 0;               // first tries with this as the main pattern
  double slowShare = 0;             // timed tries here over the box, softened while there are few
  int timedTries = 0, slowTries = 0;   // raw counts, for display

  /*
  Review rate minus cold rate, when there is enough evidence on both sides.
  Above MEMORIZE_MIN_GAP, the app calls it memorizing.
  */
  bool hasMemorizeSignal() const;
  double memorizeGap() const { return review.rate() - cold.rate(); }
};

// What your attempts show: each flaw, then the two thinking needs.
inline constexpr int SEEING_NEED = FLAW_COUNT;         // seeing which pattern it is
inline constexpr int KEY_STEP_NEED = FLAW_COUNT + 1;   // finding the key step
inline constexpr int NEED_COUNT = FLAW_COUNT + 2;

// A finer topic: how your first tries that used it went, for the subpattern step of a pick.
struct TopicStats {
  Evidence firstTries;              // whether the code worked, decayed
  double recentSolved = 0;          // first tries using it in the last RECENT_DAYS, each counted by how it went
};

struct Profile {
  vector<PatternStats> patterns;   // same order as Resources::patterns()
  vector<TopicStats> topics;       // same order as Resources::topics()

  // Your overall rating for the idea on the problems' contest scale, give or take ratingSpread.
  double rating = RATING_START;
  double ratingSpread = RATING_START_SPREAD;
  int ratedTries = 0;
  // The same for working code: each first try counted by the stricter of your grade and the coach's.
  double codeRating = RATING_START;

  // A finer topic's working-code score, leaning on its pattern's until it has tries of its own.
  double topicScore(int topic, int pattern) const;

  /*
  Signs of each need, decayed like everything else: each judged attempt that
  showed a flaw, each judged attempt's missing thinking credit, each attempt
  over its time box, and a little for each missed card from a deck that drills
  one.
  */
  array<double, NEED_COUNT> needs{};
  // The same signs, by the pattern they showed up on, so drills go there.
  array<vector<double>, NEED_COUNT> needPatterns;
  array<int, FLAW_COUNT> flawCounts{};   // raw counts, for display
  int judgedWithCode = 0;                // raw count, for display

  // Each need's share of all the signs, 0 to 1. All zero with no history.
  array<double, NEED_COUNT> needShare() const;
};

// `events` must be in date order. `judged` is the coach's log, when there is one.
Profile buildProfile(const Resources& resources, const vector<Event>& events, const Clock& clock, int today,
                     const JudgedMap* judged = nullptr);

// Whether any card drills a need. A flaw that only shows in your rates has none.
bool needHasDrills(int need);

// Whether a card is one that drills a need: its deck, and for thinking, its skill.
bool cardDrills(int need, const Question& card);

// "off-by-one", ..., "seeing-the-pattern", "finding-the-key-step"
const char* needSlug(int need);

}  // namespace interviewprep
