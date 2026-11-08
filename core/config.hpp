/*
═══════════════════════════════════════════════════════════════════════════
config.hpp — THE FILE YOU EDIT

Every rule the app follows is a table or a number in this file. Change a
value, then:

    make test             the rule tests tell you what you changed
    ./build/interviewprep rules              prints the live rule table
    ./build/interviewprep simulate r,b,b,b   walks a card through the rules
    make web mac          rebuilds both apps from this same file

Your grades are stored as a plain log of clicks and every card's state is
recomputed from that log, so changing a rule here never loses data. Old
clicks are simply re-read under the new rule.
═══════════════════════════════════════════════════════════════════════════
*/
#pragma once

namespace interviewprep {
/*
───────────────────────────────────────────────────────────────────────────
1. TRACKS AND CLICKS
───────────────────────────────────────────────────────────────────────────
*/

enum class Track { New, Red, Yellow, Blue, Retired };
enum class Click { Blue, Yellow, Red };

/*
───────────────────────────────────────────────────────────────────────────
2. EXPIRES

Each check comes due `base` days after the previous one was actually done,
plus 0 to `spread` more. A gap always counts from the day the check was
really done, never from its due date.

The extra days are drawn from the question and how many times it has been
graded, so the same log always gives the same dates. Problems learned on the
same day still come back on different days.

Coding problems, the schedule chosen on 2026-11-08:

   Clean first try   one check after 10 to 20 days, then gone
   Needed a hint     one check after 5 to 10 days, then gone
   Missed            checks after 1 to 4 and 5 to 10 days, then gone

Cards keep the fixed gaps they always had.
───────────────────────────────────────────────────────────────────────────
*/

struct Gap {
  int base;
  int spread;
};

inline constexpr Gap PROBLEM_RED_CHECKS[]    = {{1, 3}, {5, 5}};
inline constexpr Gap PROBLEM_YELLOW_CHECKS[] = {{5, 5}};
inline constexpr Gap PROBLEM_BLUE_CHECKS[]   = {{10, 10}};

inline constexpr Gap CARD_RED_CHECKS[]    = {{1, 0}, {3, 0}, {7, 0}};   // three Blues → first real Blue
inline constexpr Gap CARD_YELLOW_CHECKS[] = {{3, 0}, {7, 0}};           // two Blues   → first real Blue
inline constexpr Gap CARD_BLUE_CHECKS[]   = {{7, 0}, {15, 0}};          // two Blues   → retired for good

/*
───────────────────────────────────────────────────────────────────────────
3. WHAT A CLICK DOES

   Advance   the next check of this track. Past the last one a problem is
             gone; a card's Red or Yellow becomes a first real Blue, and a
             card's Blue retires.
   Repeat    same check again after the same gap
   ToRed     back to the first Red check
   ToYellow  back to the first Yellow check
   ToBlue    first real Blue
   Stay      nothing changes

For a problem, any check you can't do at all starts the missed routine over,
and one that needs a hint starts the hint routine over.

The coach can only add checks. On the check that would retire a problem, a
coach grade stricter than yours keeps it going, as if you had clicked that
grade: it may have found a bug you didn't. It can never retire anything.

To change a behavior, change one cell.
───────────────────────────────────────────────────────────────────────────
*/

enum class Move { Advance, Repeat, ToRed, ToYellow, ToBlue, Stay };

//                                             click Blue     click Yellow     click Red
inline constexpr Move PROBLEM_RULES[5][3] = {
    /* New     */ {Move::ToBlue,  Move::ToYellow, Move::ToRed},
    /* Red     */ {Move::Advance, Move::ToYellow, Move::ToRed},
    /* Yellow  */ {Move::Advance, Move::ToYellow, Move::ToRed},
    /* Blue    */ {Move::Advance, Move::ToYellow, Move::ToRed},
    /* Retired */ {Move::Stay,    Move::ToYellow, Move::ToRed},
};

inline constexpr Move CARD_RULES[5][3] = {
    /* New     */ {Move::ToBlue,  Move::ToYellow, Move::ToRed},
    /* Red     */ {Move::Advance, Move::Repeat,   Move::ToRed},
    /* Yellow  */ {Move::Advance, Move::ToRed,    Move::ToRed},
    /* Blue    */ {Move::Advance, Move::ToYellow, Move::ToRed},
    /* Retired */ {Move::Stay,    Move::ToYellow, Move::ToRed},
};

/*
───────────────────────────────────────────────────────────────────────────
4. THE CLOCK

   StudyDays     A calendar day counts only if you graded at least one card
                 on it. Skip a day and nothing moves, as if it never existed.
   CalendarDays  Every calendar day counts. Missed cards still roll forward
                 with no penalty, but they pile up while you are away.

A "day" starts at this hour, so a 1 AM session belongs to the night before.
───────────────────────────────────────────────────────────────────────────
*/

enum class ClockMode { StudyDays, CalendarDays };

inline constexpr ClockMode DEFAULT_CLOCK = ClockMode::StudyDays;
inline constexpr int DAY_ROLLOVER_HOUR = 4;

/*
───────────────────────────────────────────────────────────────────────────
5. THE BOUNDED DAILY QUEUE

Two lanes, because a coding problem costs 30 minutes and a card costs 20
seconds. Each lane holds at most CAP items a day. Whatever does not fit
rolls to the next day with no penalty and keeps its place in line.

NEW is how many new faces you get on a normal day. NEW_MIN is the floor:
even when reviews overflow, that many slots stay reserved for new faces,
so reviews can never fully crowd out cold practice.

REVIEWS is the most coding checks a day's queue holds. Checks beyond it wait,
misses first, and keep their place in line. You can still pull them in by
asking for more. Cards have no such limit.

A new face stays in the queue until you grade it or hide it, from one day
to the next and through a change of settings. The ones waiting take the
day's NEW slots first, so fresh picks only fill what is left and skipped
days never pile up. Any beyond NEW are ones you pulled yourself: they stay
on top of CAP and never push a review out.

CAP, NEW and REVIEWS can be changed in the app's settings. These are the
defaults.
───────────────────────────────────────────────────────────────────────────
*/

inline constexpr int CODE_CAP = 5;
inline constexpr int CODE_NEW = 2;
inline constexpr int CODE_NEW_MIN = 1;
inline constexpr int CODE_REVIEWS = 3;

inline constexpr int CARD_CAP = 24;
inline constexpr int CARD_NEW = 8;
inline constexpr int CARD_NEW_MIN = 3;

/*
Order inside the queue. Higher goes first. Every day a card waits adds AGING
points, so a long-waiting Blue eventually passes a fresh Red and nothing
starves. Same idea as priority aging in an OS scheduler.
*/
inline constexpr int PRIORITY_RED = 2;
inline constexpr int PRIORITY_YELLOW = 1;
inline constexpr int PRIORITY_BLUE = 0;
inline constexpr int AGING_PER_DAY = 1;

/*
───────────────────────────────────────────────────────────────────────────
6. THE PERSONAL PROFILE

Each graded attempt adds evidence to its patterns about the thinking: did you
see which pattern it was, and did you find the key step. These numbers say
how much.

Once the coach has judged an attempt, only its thinking labels count here.
A coding slip says nothing about whether you know the pattern, so it goes to
your flaws instead (below) and never pulls a pattern down. Until then, your
own grade stands in.
───────────────────────────────────────────────────────────────────────────
*/

// How much of a pass each click is worth, while the coach hasn't judged it.
inline constexpr double CREDIT_BLUE = 1.0;
inline constexpr double CREDIT_YELLOW = 0.4;
inline constexpr double CREDIT_RED = 0.0;

/*
How much of a pass each thinking label is worth. An attempt's credit is the
two multiplied, so seeing the pattern but missing the key step earns nothing.
*/
inline constexpr double CREDIT_RECOGNISED_YES = 1.0;
inline constexpr double CREDIT_RECOGNISED_PARTIAL = 0.5;
inline constexpr double CREDIT_RECOGNISED_NO = 0.0;
inline constexpr double CREDIT_KEY_STEP_OWN = 1.0;
inline constexpr double CREDIT_KEY_STEP_HINT = 0.5;
inline constexpr double CREDIT_KEY_STEP_MISSED = 0.0;

/*
How much each kind of attempt proves. A Blue on a problem you have already
seen can be memorized, so it counts for much less than a Blue on a new face.
*/
inline constexpr double WEIGHT_COLD_PROBLEM = 1.0;
inline constexpr double WEIGHT_REVIEW_PROBLEM = 0.35;
inline constexpr double WEIGHT_DRILL = 0.30;
inline constexpr double WEIGHT_FLASH = 0.15;

/*
A grade loses half its weight after this many days, so the profile tracks
who you are now and not who you were last month.
*/
inline constexpr double HALF_LIFE_DAYS = 21.0;

/*
A pattern needs this much evidence on both sides before the app claims you
are memorizing it (high pass rate on reviews, low on new faces).
*/
inline constexpr double MEMORIZE_MIN_EVIDENCE = 3.0;
inline constexpr double MEMORIZE_MIN_GAP = 0.25;

/*
Flaws: coding slips that can happen on any topic. Only the coach names these,
from your code, when it reviews a batch: the app never asks you what went
wrong, because right after a miss you rarely know. They never count against
a pattern. They pick the drill cards you get, each from the deck that drills
it.

`level` moves the difficulty target of a new coding problem, in rating
points, as often as that flaw comes up: speed is built on problems you can
finish inside the time box. The others leave the target alone.
*/
struct FlawInfo {
  const char* slug;
  const char* label;
  const char* deck;
  double level;
};

inline constexpr FlawInfo FLAWS[] = {
    {"off-by-one",               "Off by one, or wrong bounds",                           "bugs",       0},
    {"wrong-state-update",       "Wrong update of pointers, window, visited or state",    "bugs",       0},
    {"wrong-start-or-base-case", "Wrong start value or base case",                        "bugs",       0},
    {"missed-edge-input",        "Missed an edge input: empty, one item, duplicates",     "edges",      0},
    {"overflow",                 "Overflow, or the wrong integer type",                   "edges",      0},
    {"library-misuse",           "C++ library misuse: containers, iterators, comparators", "stl",       0},
    {"too-slow-approach",        "An approach too slow for the limits",                   "complexity", 0},
    {"slow-to-write",            "Right, but slow to write",                              "templates",  -250},
};

inline constexpr int FLAW_COUNT = sizeof(FLAWS) / sizeof(FLAWS[0]);

/*
The thinking side has a deck too: pattern signals train seeing which pattern a
problem is. Drill cards come from it as often as judged attempts miss the
pattern or the key step.
*/
inline constexpr const char* THINKING_DECK = "signals";

/*
Before the coach has judged any code, a missed card from one of those decks
counts as a small sign of what it drills. Other decks imply nothing: a missed
TLS card says nothing about how you code.
*/
inline constexpr double IMPLIED_FLAW_WEIGHT = 0.25;

/*
───────────────────────────────────────────────────────────────────────────
7. THE NEW-PROBLEM PICKER

"Asked" below means asked by your target company when you have set one, and
then only problems that company asks are picked. With no company it means
how often a problem gets asked anywhere.

   New pattern   Until every pattern has had a first try, each new problem
                 comes from one that hasn't, drawn in proportion to how much
                 it gets asked. Nothing is known about a pattern you have
                 never tried, so nothing else can rank it yet.
   New topic     After that, one new problem in NEW_GROUND_EVERY comes from
                 a finer topic you have never done, so new ground never stops.
   Practice      The rest are drawn in proportion to each pattern's pull:

                    how much it gets asked
                    × how far you are from it: 1 minus your thinking score
                    × 1/2 for each new problem of it in the last RECENT_DAYS

The last factor is what moves you on: a pattern you just did hands most of
its pull to the others, and gets it back a week later.

A pattern's first problem aims easier than the rest, so the first meeting
teaches the idea instead of burying it.

Cards and mock interviews use three other kinds, dealt in this repeating
order:

   Sibling   Unseen, same pattern as something due or failed recently.
   Weak      Unseen, from a pattern chosen by Thompson sampling on your
             profile. Weak patterns come up often, strong ones fade.
   Wildcard  Unseen, from a pattern nothing in today's queue points to.
             You get no priming, the way a real interview gives you none.

Every other new card follows your flaws instead. See section 10.
───────────────────────────────────────────────────────────────────────────
*/

enum class Slot { Sibling, Weak, Wildcard, NewPattern, NewTopic, Practice };

inline constexpr Slot SLOT_ORDER[] = {Slot::Sibling, Slot::Weak, Slot::Wildcard};

inline constexpr int RECENT_DAYS = 7;
inline constexpr double PULL_KEPT_PER_RECENT_PROBLEM = 0.5;
inline constexpr int NEW_GROUND_EVERY = 4;

// Of each day's new cards, one in this many follows your flaws.
inline constexpr int FLAW_CARD_EVERY = 2;

/*
A new problem is random within its pattern, weighted by how much it gets
asked and how well it suits your level. The floor keeps a rarely asked or
off-level problem possible, just less likely.
*/
inline constexpr double PROBLEM_WEIGHT_FLOOR = 0.2;
inline constexpr double CURATED_BOOST = 1.5;

// How a candidate is scored in the three kinds above. Weights are relative.
inline constexpr double W_SIMILARITY = 3.0;    // closeness to the seed (Sibling only)
inline constexpr double W_FREQUENCY = 1.2;     // how often it gets asked
inline constexpr double W_CURATED = 0.8;       // on your hand-picked list
inline constexpr double W_LEVEL_FIT = 1.0;     // hard enough for this pattern, not crushing
inline constexpr double W_NOISE = 0.6;         // so picks never become predictable

/*
Difficulty target. With mastery m in [0, 1], the app aims for a problem rated
LEVEL_BASE + m * LEVEL_SPAN, and a problem LEVEL_WIDTH away scores about 60%.
*/
inline constexpr double LEVEL_BASE = 1350.0;
inline constexpr double LEVEL_SPAN = 800.0;
inline constexpr double LEVEL_WIDTH = 280.0;

/*
───────────────────────────────────────────────────────────────────────────
8. MOCK INTERVIEW
───────────────────────────────────────────────────────────────────────────
*/

inline constexpr int MOCK_PROBLEMS = 2;
inline constexpr int MOCK_MINUTES = 45;

// Time box per difficulty, in minutes. The coach weighs your time against it.
inline constexpr int TIMEBOX_EASY = 15;
inline constexpr int TIMEBOX_MEDIUM = 25;
inline constexpr int TIMEBOX_HARD = 40;

/*
───────────────────────────────────────────────────────────────────────────
9. ATTEMPTS

Your code, so it can be judged as you wrote it. Attempts are plain files
beside the progress file, one per problem per day, with every paste of that
day in order:

     profile/attempts/lc207/2026-11-06.cpp

The first paste is what you wrote before running anything, which shows your
dry accuracy. The later ones are your fixes after running, up to what you
submitted. Each starts with a line like the one below, and a paste identical
to the one before it adds nothing. The day is the study day, so a paste at
1 AM joins the night before.

     // ── Paste 2 at 21:40 ──

Saving one never touches your progress. A paste over ATTEMPT_MAX_BYTES is
refused, because that is far more likely a wrong clipboard than a solution.
───────────────────────────────────────────────────────────────────────────
*/

inline constexpr const char* ATTEMPT_FOLDER = "profile/attempts";
inline constexpr const char* ATTEMPT_EXTENSION = ".cpp";
inline constexpr int ATTEMPT_MAX_BYTES = 64 * 1024;
inline constexpr const char* PASTE_HEADER = "// ── Paste ";   // then "<n> at HH:MM ──"

/*
───────────────────────────────────────────────────────────────────────────
10. THE COACH

A model reviews your attempts through `interviewprep mcp` and keeps these beside
them:

     profile/profile.md              one page, rewritten every batch
     profile/judged-attempts.jsonl   one line per judged attempt, kept for good

It records what it saw, from closed lists, so the same thing keeps the same
name from one session to the next. It never writes a probability:

     topics     what the problem really uses: finer topics such as
                sliding-window/shortest-valid, or a plain pattern
     thinking   recognised: yes, partial or no. Did you see which pattern
                it was?
                key_step: own, hint or missed. Did you find the key step
                yourself, after a hint, or not at all?
     flaws      the coding slips in section 6, from any topic

The app decides everything from these. Thinking counts toward the problem's
patterns and nothing else (section 6), and decides which pattern comes next
(section 7). Flaws count toward your flaw rates and nothing else, and decide
which drill cards you get. Anything more specific goes in the note.

Before 2026-11-09 the model wrote probability tables (profile/labels.json) and
judgements in an older shape (profile/judgements.jsonl). Both stay exactly as
they were and are never written again. Each of those attempts waits for a
judgement in the new shape like any other. The model can drop an old one with
nothing to add, such as one with no code, and its grade alone counts for it.

Disk is cheap and a model's attention is not, so these limits cap what gets
read, never what gets kept. A commit over a limit is refused, which makes
the model compact as it goes, so nothing ever needs a cleanup job.

Nothing here is trained. The labels are the model's judgement, not
measurements. Your own click decides when cards expire, except that the coach
can add a check (section 3).
───────────────────────────────────────────────────────────────────────────
*/

inline constexpr const char* PROFILE_FILE = "profile/profile.md";
inline constexpr const char* JUDGEMENTS_FILE = "profile/judged-attempts.jsonl";
inline constexpr const char* OLD_JUDGEMENTS_FILE = "profile/judgements.jsonl";   // before 2026-11-09, never written
inline constexpr const char* LAST_REVIEW_FILE = "profile/last-review.md";

// Graded problems in one review.
inline constexpr int BATCH_SIZE = 10;

/*
Every session reads the profile, so this sets the floor on what a session
costs. The rest is read only when asked for.
*/
inline constexpr int PROFILE_MAX_BYTES = 2 * 1024;
inline constexpr int SEARCH_DEFAULT_LINES = 20;
inline constexpr int SEARCH_MAX_LINES = 50;

/*
Code shown per problem in a batch. Ten of these stay under the size at which
Claude Code moves a tool result out of the conversation and into a file.
*/
inline constexpr int ATTEMPT_READ_MAX_BYTES = 4 * 1024;

// Topics named in one judgement, and the room for its notes.
inline constexpr int JUDGEMENT_MAX_TOPICS = 3;
inline constexpr int NOTE_MAX_CHARS = 280;

}  // namespace interviewprep
