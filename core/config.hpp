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
Cards are for concepts: complexity, how C++ works inside, operating systems,
concurrency, networking, databases and system design. The decks below drill
coding itself, which you practise on problems (section 7), so they start
switched off. Switch one on in the app's settings and it joins the daily
queue like any other.
*/
inline constexpr const char* CODING_DECKS[] = {"signals", "templates", "stl", "bugs", "edges"};

/*
The progress file's version. A file from before version 2 dates from when
every deck started switched on, so loading one switches the coding decks off,
once. After that the file says which decks are off and nothing is added.
*/
inline constexpr int STATE_VERSION = 2;

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

Each graded attempt says two things about its patterns, and the app keeps
them apart:

   the thinking    did you see which pattern it was, and did you find the key
                   step. Once the coach has judged an attempt, only its
                   thinking labels count here; until then your own grade
                   stands in. A coding slip says nothing about whether you
                   know the pattern, so it never pulls this down.
   working code    did what you wrote work on the first try: your grade, or
                   the coach's grade of your code when that is stricter. This
                   is where a slip counts.

The thinking sets how hard a pattern's next problem is. Working code sets how
often the pattern comes up. Both are in section 7. The numbers below say how
much each attempt counts.
───────────────────────────────────────────────────────────────────────────
*/

// How much of a pass each click is worth: for working code always, and for the thinking until the coach has judged it.
inline constexpr double CREDIT_BLUE = 1.0;
inline constexpr double CREDIT_YELLOW = 0.4;
inline constexpr double CREDIT_RED = 0.0;

/*
How much of a pass each thinking label is worth. An attempt's credit is the
two multiplied, so seeing the pattern but missing the key step earns nothing.

   recognised   yes       the right pattern, or any other approach that is
                          correct and fast enough for the limits
                partial   the right family, the wrong variant: a fixed window
                          where it has to grow and shrink
                wrong     a pattern whose precondition fails here, such as a
                          window over negative numbers. The coach also names
                          the pattern they went for, as reached_for
                no        no approach, or a brute force nowhere near it
   key_step     own, hint or missed. For DP, the state and the recurrence
                are the key step. With wrong or no it is always missed.

Wrong and no both earn nothing. They differ in what you practise next (the
thinking needs, below): wrong sets the two patterns side by side, no teaches
the one.
*/
inline constexpr double CREDIT_RECOGNISED_YES = 1.0;
inline constexpr double CREDIT_RECOGNISED_PARTIAL = 0.5;
inline constexpr double CREDIT_RECOGNISED_WRONG = 0.0;
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
Flaws: slips that can happen on any topic. The coach names them from your
code when it reviews a batch: the app never asks you what went wrong, because
right after a miss you rarely know. They never count against the thinking.
Code that slipped is graded down by the coach, and that grade is what brings
its pattern up more often (section 7). The flaw's name is kept, on the
patterns where it showed up, so the app can say which slip to watch for there.

They run in the order you solve a problem: reading it, planning the steps,
writing the code, and time. When two could name one mistake, the coach names
the cause, not the symptom, and these decide the rest:

   case or step    an input or outcome the code doesn't handle is
                   missed-edge-input; a whole step of the algorithm that
                   isn't there at all, such as sorting first or a visited
                   set, is missing-piece; a step that's there but wrong, or
                   one update, bound or branch missing from code that's
                   there, is a code flaw
   C++ semantics   are library-misuse wherever they sit: a > b > c inside an
                   if is not wrong-condition
   brute force     written because the pattern wasn't seen is recognised: no,
                   not too-slow-approach
   misread         only when the code answers a different question than the
                   one asked. A rule they knew and left out is a case or a step

`deck` is the deck whose missed cards hint at the flaw before the coach has
judged any code (below), or nullptr when none does. Flaws don't choose cards:
a slip is practised on a problem. `byClock` marks one the app sets itself
from your time, so the coach never names it. `level` moves the aim of a new
coding problem, in rating points, as often as that flaw comes up on its
pattern: speed is built on problems you can finish inside the time box. The
others leave it alone.
*/
struct FlawInfo {
  const char* slug;
  const char* label;
  const char* deck;
  double level;
  bool byClock;
};

inline constexpr FlawInfo FLAWS[] = {
    // Reading the problem
    {"misread-problem",          "Solved a different problem: the code answers another question than the one asked",       nullptr,      0,    false},
    {"missed-edge-input",        "Didn't handle a case the problem allows: empty, one item, all equal, negatives, nothing reachable", "edges", 0, false},
    // Planning the steps
    {"missing-piece",            "Left out a step the approach needs: sort first, a visited set, skipping duplicates",     "templates",  0,    false},
    // Writing the code
    {"off-by-one",               "Off by one, or wrong bounds",                                                           "bugs",       0,    false},
    {"wrong-start-or-base-case", "Wrong start value or base case",                                                        "bugs",       0,    false},
    {"wrong-state-update",       "Updated the wrong thing, or at the wrong time: pointer direction, window counts, marking visited", "bugs", 0, false},
    {"wrong-condition",          "Wrong test or control flow: an inverted condition, if where else-if belongs, a stray break", "bugs",   0,    false},
    {"overflow",                 "Overflow, or the wrong integer type",                                                   "edges",      0,    false},
    {"library-misuse",           "C++ slip, library or language: containers, iterators, comparators, precedence, a > b > c", "stl",     0,    false},
    // Time
    {"too-slow-approach",        "Right idea, too slow for the limits: recomputing, re-scanning, the wrong container",     "complexity", 0,    false},
    {"slow-to-write",            "Over the time box for its difficulty",                                                  "templates",  -250, true},
};

inline constexpr int FLAW_COUNT = sizeof(FLAWS) / sizeof(FLAWS[0]);

/*
The thinking side has two needs of its own, counted on the patterns where
they showed up:

   seeing the pattern     recognised partial, wrong or no. A wrong pick also
                          counts the pattern they went for. A pattern missed
                          outright whose key step you have never found
                          yourself starts over: its next problem is aimed as
                          easy as a first meeting (section 7).
   finding the key step   key_step hint or missed, after seeing the pattern.

Like the flaws, they show in your profile and don't choose cards. The decks
named here are the ones whose missed cards hint at a need (below).
*/
inline constexpr const char* SIGNALS_DECK = "signals";
inline constexpr const char* TEMPLATES_DECK = "templates";
inline constexpr const char* SKILL_SEES_PATTERN = "recog";
inline constexpr const char* SKILL_KEY_STEP = "idea";

/*
Before the coach has judged any code, a missed card from one of those decks
counts as a small sign of what it drills, on that card's patterns. Other decks
imply nothing: a missed TLS card says nothing about how you code. Nor does any
card imply a flaw the clock sets.
*/
inline constexpr double IMPLIED_FLAW_WEIGHT = 0.25;

/*
───────────────────────────────────────────────────────────────────────────
7. THE NEW-PROBLEM PICKER

"Asked" below means asked by your target company when you have set one, and
then only problems that company asks are picked. With no company it means
how often a problem gets asked anywhere.

A pattern is new until it has been the main pattern of a problem you tried.
Meeting it on the side of another problem doesn't count, here or in the pull:
that never tested whether you can spot it.

   New pattern   While any pattern is new, NEW_PATTERN_SHARE of your new
                 problems go to one, drawn in proportion to how much it gets
                 asked. They are evenly spaced, not a coin flip: at 60% it is
                 3 in every 5.
   New topic     Once none is, one new problem in NEW_GROUND_EVERY comes from
                 a finer topic you have never done, so new ground never stops.
   Practice      The rest are drawn among the patterns you have met, in
                 proportion to each one's pull:

                    how much it gets asked
                    × weak: 2 for every WEAK_DOUBLES_EVERY points its
                      working-code rating sits below your overall one, 1/2
                      for every WEAK_DOUBLES_EVERY above, kept between
                      WEAK_PULL_LEAST and WEAK_PULL_MOST
                    × 1/2 for each new problem of it in the last RECENT_DAYS,
                      counted by how it went: a clean one counts 1, one that
                      needed a hint CREDIT_YELLOW, a miss 0

So a pattern where your code keeps failing comes up more, up to
WEAK_PULL_MOST times its share, and one you just solved steps back for a
week. A miss doesn't step it back: that would send you away from exactly
what you need. The limits keep one result from deciding a week, and keep
every pattern in play: none drops below WEAK_PULL_LEAST of its share.

A new problem still waiting in your queue holds its place, and nothing is
assumed about how it will go: being picked isn't a result, only a grade is.
Until you grade or hide it, its pattern isn't picked again, as new or by the
pull, so asking for more brings other patterns and never the same one twice.
Only when every pattern you could get has a problem waiting does one get a
second. Waiting problems do count toward the spacing above, which only
numbers the new problems handed out.

Multiplying these is a softmax: take logarithms and each factor is a score
that adds. WEAK_DOUBLES_EVERY is its temperature. Lower drills weak patterns
harder and covers fewer patterns in a week. `make sim` shows what a change
does to both.

With an interview date set (Settings), exploring is no longer a fixed share
while a pattern is new and then a fixed one in NEW_GROUND_EVERY. Its share of
new problems falls smoothly from your first practice day to the interview, on
a cosine:

   share = EXPLORE_END + (EXPLORE_START - EXPLORE_END) × (1 + cos(π × progress)) / 2

   progress   0      1/4    1/2    3/4    1 (the interview, and after)
   share      60%    53%    35%    17%    10%

Progress counts calendar days. An exploring pick takes a pattern that has
never been the main one of a problem you did, then a finer topic you have
never done; the rest come from the pull above, among patterns you have met.
There is no coin flip: a running count adds the day's share for each new
problem and explores each time it passes a whole number, so at 35% you get
7 in every 20, evenly spaced. When nothing new is left, an exploring pick
falls back to the pull.

How hard each new problem is, and which one it is within its pattern, comes
after: see DIFFICULTY below.

Cards and mock interviews use three other kinds, dealt in this repeating
order:

   Sibling   Cards only. Unseen, on a pattern of a problem you missed lately.
   Weak      Unseen. A mock problem comes from a pattern chosen by Thompson
             sampling on your thinking scores, so weak patterns come up often
             and strong ones fade. A card is the one on your weakest pattern.
   Wildcard  Unseen, from a pattern (for a card, a deck) nothing in today's
             queue points to. You get no priming, the way a real interview
             gives you none.

Cards come only from the decks switched on (section 5), and neither your
flaws nor your needs choose them.
───────────────────────────────────────────────────────────────────────────
*/

enum class Slot { Sibling, Weak, Wildcard, NewPattern, NewTopic, Practice };

inline constexpr Slot SLOT_ORDER[] = {Slot::Sibling, Slot::Weak, Slot::Wildcard};

inline constexpr int RECENT_DAYS = 7;
inline constexpr double PULL_KEPT_PER_RECENT_PROBLEM = 0.5;
inline constexpr int NEW_GROUND_EVERY = 4;   // with no interview date, once no pattern is new

// With no interview date: the share of new problems that go to a new pattern while any is left.
inline constexpr double NEW_PATTERN_SHARE = 0.6;

// How much being weak on a pattern multiplies its pull, and how far that can go either way.
inline constexpr double WEAK_DOUBLES_EVERY = 120.0;   // rating points below your overall working-code rating
inline constexpr double WEAK_PULL_MOST = 4.0;
inline constexpr double WEAK_PULL_LEAST = 0.5;

// With an interview date: the share of new problems that explore, at the start and by the interview.
inline constexpr double EXPLORE_START = 0.6;
inline constexpr double EXPLORE_END = 0.1;

/*
A new problem is random within its pattern, weighted by how much it gets
asked and how well it suits your level. The floor keeps a rarely asked or
off-level problem possible, just less likely.
*/
inline constexpr double PROBLEM_WEIGHT_FLOOR = 0.2;
inline constexpr double CURATED_BOOST = 1.5;

// How a mock interview's problem is scored once its pattern is chosen. Weights are relative.
inline constexpr double W_FREQUENCY = 1.2;     // how often it gets asked
inline constexpr double W_CURATED = 0.8;       // on your hand-picked list
inline constexpr double W_LEVEL_FIT = 1.0;     // close to the aim (DIFFICULTY below)
inline constexpr double W_NOISE = 0.6;         // so picks never become predictable

/*
DIFFICULTY

Problem ratings come from LeetCode contests: a problem rated R is solved by
about half the contestants rated R. The app rates you on the same scale, from
your first tries (a re-solve says more about memory than about level):

   chance = 1 / (1 + 10^((problem rating - your rating) / 400))

You have two ratings, fitted the same way to the two results of each first
try (section 6):

   for the idea       the result is the thinking: the coach's labels once it
                      has judged the try, your grade until then. It sets how
                      hard your next problem is, below.
   for working code   the result is whether your code worked: your grade, or
                      the coach's when it is stricter. It sets which pattern
                      comes next, in the pull above.

Your rating on a pattern is your overall rating plus a nudge from that
pattern's tries alone, so a pattern you have barely touched starts from your
overall rating, not from nothing. Only tries where it was the main pattern
count. Both are the most likely values given your tries and two starting
beliefs: overall about RATING_START, give or take RATING_START_SPREAD, and
each pattern within about RATING_NUDGE_SPREAD of it. Every graded try moves
them by how far its result was from what the rating expected: solving a
problem rated above you moves it a lot, missing that problem only a little.
Older tries fade on the half-life above.

A new problem aims at your idea rating on its pattern + STRETCH, where you
would find the approach yourself on about 3 first tries in 10. It is chosen
in three steps:

   level        Easy, Medium or Hard. Each sits at the middle rating of the
                pattern's problems a pick can reach there (the ones in a
                finer topic, when the level has any), and the level whose
                middle is nearest the aim is the most likely: one LEVEL_WIDTH
                away keeps about 60% of the chance. How many problems a level
                has doesn't matter.
   subpattern   one of the pattern's finer topics with a problem left at that
                level, drawn by pull: how much it gets asked × 1 minus its
                working-code score × 1/2 for each new problem of it lately,
                counted by how it went. A finer topic's score leans on its
                pattern's until it has tries of its own. Problems with no
                finer topic come up only when no finer topic has one left at
                that level.
   problem      in that subpattern and level, by how much it gets asked and
                how close its rating is to the aim.

Starting over. On a pattern where you showed zero clue (a blank, or
recognised: no, with the key step never found on your own), new problems drop
to its Easy ones. Finding a key step yourself moves it up to Medium, and the
next one back to the aim. A blank the coach couldn't place in a finer topic
counts on the problem's main pattern alone.

Slow. On a pattern where you go over the time box, about that share of its
new problems aims lower by the slow-to-write flaw's `level`. Only that
pattern: one you have never been slow on is never eased.
*/
inline constexpr double RATING_SCALE = 400.0;           // the Elo scale contest ratings use
inline constexpr double RATING_START = 1500.0;
inline constexpr double RATING_START_SPREAD = 300.0;
inline constexpr double RATING_NUDGE_SPREAD = 300.0;
inline constexpr double STRETCH = 150.0;
inline constexpr double LEVEL_WIDTH = 280.0;

/*
A finer topic's score borrows this many tries' worth of its pattern's score
until it has tries of its own, and a pattern's working-code score starts from
this many tries at an even chance. A pattern's share of slow tries counts
this many extra on-time ones, so a single slow try doesn't ease all of it.
*/
inline constexpr double BORROWED_TRIES = 2.0;

/*
───────────────────────────────────────────────────────────────────────────
8. MOCK INTERVIEW
───────────────────────────────────────────────────────────────────────────
*/

inline constexpr int MOCK_PROBLEMS = 2;
inline constexpr int MOCK_MINUTES = 45;

/*
Time box per difficulty, in minutes. An attempt that takes longer counts as
slow-to-write (section 6). There is no slack: one second past the box is over.
*/
inline constexpr int TIMEBOX_EASY = 15;
inline constexpr int TIMEBOX_MEDIUM = 25;
inline constexpr int TIMEBOX_HARD = 40;

inline constexpr int timeboxMinutes(int level) {
  return level == 1 ? TIMEBOX_EASY : level == 3 ? TIMEBOX_HARD : TIMEBOX_MEDIUM;
}

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
     thinking   recognised: yes, partial, wrong or no. Did you see which
                pattern it was? With wrong, reached_for names the pattern
                you went for instead.
                key_step: own, hint or missed. Did you find the key step
                yourself, after a hint, or not at all?
     flaws      the slips in section 6, from any topic, except the one the
                clock sets

The app decides everything from these. Thinking counts toward the problem's
patterns (section 6) and sets how hard their next problem is (section 7). The
coach's grade of your code counts toward your working-code rating when it is
stricter than yours, and that decides which pattern comes next. Flaws count
toward your flaw rates, on the topics the judgement names, so the app can say
which slip to watch for there. Anything more specific goes in the note.

An attempt with no code means two different things, split by the day you
started pasting every attempt (BLANK_WITHOUT_CODE_FROM). Before it, nothing
was saved, so the coach drops the attempt and your grade alone counts. From
it on, no paste means a blank: no approach, or you asked for help. The coach
judges it so: model grade r, recognised no, key step missed, no flaws.

Before 2026-11-09 the model wrote probability tables (profile/labels.json) and
judgements in an older shape (profile/judgements.jsonl). Both stay exactly as
they were and are never written again. Each of those attempts waits for a
judgement in the new shape like any other. The model can drop an old one with
nothing to add, such as one with no code, and its grade alone counts for it.

Disk is cheap and a model's attention is not, so these limits cap what gets
read, never what gets kept. A commit over a limit is refused, which makes
the model compact as it goes, so nothing ever needs a cleanup job.

The labels are the model's judgement, not measurements, and no model is
trained on them. The rules are set by hand in this file; the only numbers
fitted to you are your two ratings (section 7). Your own click decides when
cards expire, except that the coach can add a check (section 3).
───────────────────────────────────────────────────────────────────────────
*/

inline constexpr const char* PROFILE_FILE = "profile/profile.md";
inline constexpr const char* JUDGEMENTS_FILE = "profile/judged-attempts.jsonl";
inline constexpr const char* OLD_JUDGEMENTS_FILE = "profile/judgements.jsonl";   // before 2026-11-09, never written
inline constexpr const char* LAST_REVIEW_FILE = "profile/last-review.md";

// From this day an attempt with no code is a blank, not an empty save.
inline constexpr const char* BLANK_WITHOUT_CODE_FROM = "2026-11-09";

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
