# InterviewPrep

Spaced repetition for coding interviews, with a recommender that keeps pushing
problems you have never seen. One C++ core runs a command-line tool, a website
and a native Mac app.

```
core/            All the rules, in C++. Start at core/config.hpp.
cli/             The command-line tool, and the MCP server a model connects to.
tests/           Every rule, stated as a test.
web/             The website: plain HTML, CSS and JavaScript over core.wasm.
mac/             The Mac app: SwiftUI over the same core.
data/decks/      Trivia and drill cards, one JSON file per deck.
data/curated.json          Your hand-picked problem list.
data/problem_notes.json    Key ideas and exact pattern labels for ~350 problems.
scripts/         Resources builder, packaging, notarizing.
```

## Commands

```bash
make test      # the rule tests under memory and undefined-behavior checks, then the MCP server
make sim       # runs made-up learners through the queue, to see what a rule change does
make cli       # build/interviewprep
make resources # data/resources.json, from the labels and decks
make web       # dist/interviewprep.html, one self-contained file
make mac       # dist/InterviewPrep.app, after checking this Mac has what it needs
make mac-check # only that check
make dmg       # dist/InterviewPrep.dmg
```

`make web` needs the WebAssembly compiler once: `scripts/get_toolchain.sh`.
`make resources` needs the LeetCode labels once: `scripts/get_data.sh`.

`make mac` needs Xcode, and nothing else has to be downloaded. The Command Line
Tools alone are not enough: the app is built for both Apple Silicon and Intel,
and `swift build` hands that to Xcode's own build system. `Package.swift` asks for
Swift 5.9 and the macOS 14 SDK, which is Xcode 15 or newer. The Swift package has
no outside dependencies, and `data/resources.json` comes with the repository; only
when it is older than the files it is built from does `make mac` rebuild it, and
that takes the LeetCode labels above. `make mac` checks all of this first and says
how to get whatever is missing.

## Changing a rule

Everything tunable is a table or a number in `core/config.hpp`. Change it, then:

```bash
make test                          # shows what you changed
./build/interviewprep rules            # prints the live table
./build/interviewprep simulate r,b,y,b,b,b,b   # walks a card through clicks
make web mac                       # rebuilds both apps from the same file
```

Your progress is a log of clicks. Card state is recomputed from that log every
time, so changing a rule never needs a migration: old clicks are re-read under
the new rule.

### When problems and cards expire

Coding problems follow the schedule chosen on 2026-11-08. Each check comes due a
fixed number of days after the last one, plus a few random extra days:

| First try      | Checks                        | Then |
|----------------|-------------------------------|------|
| Clean (Blue)   | after 10 to 20 days           | gone |
| Hint (Yellow)  | after 5 to 10 days            | gone |
| Missed (Red)   | after 1 to 4, then 5 to 10 days | gone |

On any check, a Red starts the missed routine over and a Yellow starts the hint
routine over. The extra days are drawn from the problem and how many times you've
graded it, so replaying the log always gives the same dates. Problems you learned
on the same day still come back on different days.

Cards keep their fixed gaps:

| Track  | Checks needed            | Then |
|--------|--------------------------|------|
| Red    | Blue at 1, 3 and 7 days  | first real Blue |
| Yellow | Blue at 3 and 7 days     | first real Blue |
| Blue   | Blue at 7 and 15 days    | gone for good |

Each gap counts from the day the previous check was actually done.

### What a click does

```
Problems     click B     click Y     click R
New          ToBlue      ToYellow    ToRed
Red          Advance     ToYellow    ToRed
Yellow       Advance     ToYellow    ToRed
Blue         Advance     ToYellow    ToRed
Retired      Stay        ToYellow    ToRed

Cards        click B     click Y     click R
New          ToBlue      ToYellow    ToRed
Red          Advance     Repeat      ToRed
Yellow       Advance     ToRed       ToRed
Blue         Advance     ToYellow    ToRed
Retired      Stay        ToYellow    ToRed
```

The coach can only add checks. On the check that would retire a problem, a coach
grade stricter than yours keeps it going, as if you had clicked that grade. To
change a behavior, change one cell of `PROBLEM_RULES` or `CARD_RULES` in
`core/config.hpp`.

### Days and the bounded queue

- A calendar day counts only if you graded something on it. Skip a day and
  nothing moves. Set "Days that count" to "Every calendar day" to change that.
- Each lane holds a fixed number of items a day. Whatever does not fit stays due,
  rolls to the next day with no penalty, and moves up the line.
- "Reviews a day" (3 to start) caps how many coding checks a day's queue holds.
  The rest wait in line, misses first, and "Pull more" serves them first.
- A new problem or card stays in the queue until you grade it or press "Never
  show this one", so code you meant to paste never loses its problem. Waiting
  ones fill the day's new slots first, and fresh picks only fill what is left.
  Ones you pulled with "Pull more" can go over the daily limit, but never push
  a review out.
- Order inside a lane is Red, then Yellow, then Blue, plus one point for each day
  a card has waited, so nothing starves.
- A day starts at 4 AM, so a late session counts as the night before.

## The recommender

It runs locally on labels and your own clicks. There is no server and no model.

Each of the 3,692 algorithm problems carries labels: LeetCode topics, a pattern
from a fixed list of 41, a contest difficulty rating (real for 2,628, estimated
from acceptance rate for the rest), and how often 16 companies ask it.

The 827 most asked problems also carry one or two of 116 finer topics, such as
`sliding-window/shortest-valid` or `graph-traversal/multi-source-bfs`, listed in
`data/topics.json` with the signal that points to each. A model labeled them once
from their titles (`data/topic_labels.json`). Problems it didn't recognize stay at
pattern level.

"Asked" below means asked by your target company when you set one, and then only
that company's problems are picked. With "Any company" it means asked anywhere.
Each new coding problem is one of these:

| Badge       | What it picks | Why |
|-------------|---------------|-----|
| New pattern | While any pattern is new, 3 new problems in 5: one of those, the most asked first | Nothing is known about a pattern you have never been tested on |
| New topic   | Once none is, one new problem in four: a finer topic you have never done | New ground never stops |
| Practice    | The rest, by each pattern's pull, among the patterns you have tried | See below |

A pattern is new until it has been the main pattern of a problem you tried.
Meeting it on the side of another problem doesn't count, here or in the pull:
that never tested whether you can spot it.

A pattern's pull is how much it gets asked, times how weak you are on it, halved
for each new problem of it you solved in the last 7 study days. *Weak* doubles
for every 120 points the pattern's working-code rating (below) sits under your
overall one, and halves for every 120 above, kept between a half and four times.
So a pattern where your code keeps failing comes up more, and one you just
solved steps back for a week. A miss doesn't step it back: that would send you
away from what you need.

A new problem still waiting in your queue holds its pattern's place, and
nothing is assumed about how it will go: being picked isn't a result, only a
grade is. Until you grade or hide it, that pattern isn't picked again, so asking
for more brings other patterns and never the same one twice. How hard the
problem is, and which one, comes next.

### How hard, and which problem

Problem ratings come from LeetCode contests: a problem rated 1800 is solved by
about half the contestants rated 1800. The app gives you two ratings on the same
scale, from your first tries only (a re-solve says more about memory):

- **For the idea.** The result is the thinking: the coach's thinking credit, or
  your grade until the coach has judged the try. It sets how hard your next
  problem is.
- **For working code.** The result is whether your code worked: your grade, or
  the coach's grade of your code when that is stricter. It sets which pattern
  comes next, in the pull above.

Each pattern gets your overall rating plus a nudge from the tries where it was
the main pattern, so a pattern you have barely touched starts from your overall
rating. A try moves a rating by how far its result was from what the rating
expected, so solving a problem rated above you moves it a lot and missing that
problem only a little. Insights shows both ratings.

A new problem aims 150 above your idea rating on its pattern, where you would
find the approach yourself on about 3 first tries in 10. If Hards beat you, your
rating drops and the aim follows. It is chosen in three steps:

1. **Level.** Easy, Medium or Hard, each placed at the middle rating of the
   pattern's problems a pick can reach there (the ones in a finer topic, when
   the level has any). The one nearest the aim is the most likely. How many
   problems a level has doesn't matter.
2. **Subpattern.** One of the pattern's finer topics with a problem left at that
   level, drawn by pull: how much it gets asked, one minus its working-code
   score, and halved for each new problem of it you solved lately. Problems with no finer
   topic (most of the bank, and the less asked part of it) come up only when no
   subpattern has one left at that level.
3. **Problem.** Within that, by how much it gets asked and how close its rating is
   to the aim.

Two exceptions. A pattern where you showed zero clue (a blank, or "recognised:
no" with the key step not found on your own) starts over: Easy problems until you
find a key step yourself, then one Medium, then back to the aim. And on a pattern
where you go over the time box, that share of its new problems aims 250 lower.

In a mock interview, the weak slot aims the same way. The wildcard
doesn't tailor itself, like a real interview. The numbers are under DIFFICULTY in
`core/config.hpp`.

That table is the rule with no interview date. Set one in Settings and new ground
eases off smoothly instead: from 60% of new problems on your first practice day
to 10% by the interview, on a cosine (35% halfway). New ground means a pattern you
have never tried, then a finer topic you have never done; the rest come from the
pull, among patterns you have met. The exploring picks are spaced evenly, with no
coin flip, so at 35% you get 7 in every 20. Settings shows today's share under the
date. The numbers are `EXPLORE_START` and `EXPLORE_END` in `core/config.hpp`.

Cards, and the problems in a mock interview, come from three other kinds:

| Slot     | What it picks | Why |
|----------|---------------|-----|
| Sibling  | Cards only. Unseen, on a pattern of a problem you missed lately | Ties a concept to what you just got wrong |
| Weak     | Unseen. A mock problem comes from a pattern chosen by Thompson sampling; a card is the one on your weakest pattern | Weak patterns come up often, strong ones fade |
| Wildcard | Unseen, from a pattern or deck nothing today points to | No priming, like a real interview |

Cards are for concepts: complexity, C++ internals, operating systems,
concurrency, networking, databases and system design. The five decks that drill
coding itself (pattern signals, code templates, C++ without autocomplete, spot
the bug, edge cases) start switched off, because a slip is practised on a
problem. Switch one on in Settings and it joins the daily queue like any other.

New faces show no labels until you have attempted them. Mixing patterns in the
queue is deliberate, because telling patterns apart is the skill being trained.

The Mac app's Today page shows where you stand, under the progress bars: your
two ratings, the share of new problems that goes to new ground, and the patterns
with the most pull. Details lists every pattern you have tried with its share of
the pull, both ratings and the two factors behind that share; rest the pointer
on a name for the picker's own sentence. The shares are standing ones, worked
out with nothing held, so they never give away which patterns are waiting in
your queue. `./build/interviewprep standing` prints the same.

### The personal profile

Each graded attempt says two things about its patterns, and they are kept apart.
The **thinking**: did you see which pattern it was, and did you find the key
step. Once the coach has judged an attempt, its thinking labels are the credit;
until then your own grade stands in. A coding slip never counts against the
thinking, since the same off-by-one on two topics says nothing about either.
And **working code**: did what you wrote work on the first try, by your grade or
the coach's when it is stricter. That is where a slip counts, and it brings the
pattern up more often (see above).

The thinking labels:

| Label | Value | Credit | Means |
|-------|-------|--------|-------|
| recognised | yes | 1 | The right pattern, or any other approach that's correct and fast enough |
| | partial | 0.5 | The right family, the wrong variant |
| | wrong | 0 | A pattern whose precondition fails here, such as a window over negatives. The coach names the pattern you went for |
| | no | 0 | No approach, or a brute force nowhere near it |
| key step | own, hint, missed | 1, 0.5, 0 | For DP, the state and the recurrence. Always missed with wrong or no |

Missing the pattern or the key step shows in your profile as a need, on the
patterns where it happened. A pattern where you showed zero clue starts over:
Easy problems until you find a key step yourself (see above).

The slips are named as **flaws**, in the order you solve a problem. They don't
choose cards. The coach grades the code that slipped, that grade lowers your
working-code rating on the pattern, and the pattern comes up more. The reason
shown after you attempt its next problem names the slip that shows most there,
so you know what to watch for.

| Flaw |
|------|
| Solved a different problem: the code answers another question |
| Didn't handle a case the problem allows: empty, one item, all equal, negatives, nothing reachable |
| Left out a step the approach needs: sort first, a visited set, skipping duplicates |
| Off by one, or wrong bounds |
| Wrong start value or base case |
| Updated the wrong thing, or at the wrong time |
| Wrong test or control flow: an inverted condition, `if` for `else if`, a stray `break` |
| Overflow, or the wrong integer type |
| C++ slip, library or language: containers, comparators, precedence, `a > b > c` |
| Right idea, too slow for the limits |
| Over the time box for its difficulty: it also eases that pattern's new problems |

The coach names a slip by its cause, not its symptom. The last one is never the
coach's: the app sets it from your time, with no slack, against 15, 25 or 40
minutes for Easy, Medium or Hard. Until the coach has judged code, a missed card
from a deck that drills a flaw counts as a small sign of it.

Evidence is kept in four separate lines per pattern: first attempts, reviews,
typed drills and flip cards. Comparing them finds what one score hides:

- **Reviews well above first attempts** means you are memorizing answers.
- **Cards well above first attempts** means you can explain it but not code it.

A Blue on a review counts for 0.35 of a Blue on a new face, and a grade loses half
its weight every 21 days. All of these numbers are in `core/config.hpp`. After
changing any of them, run `make sim` to see where the picks go now.

## The coach

A model can read the code you saved, judge it, and keep notes on you between
sessions. Save your code before you run it, and grade as usual. The Today screen
counts what's waiting, which costs nothing. When 10 problems are ready, press
**Review now**: it runs Claude in the background, with only the InterviewPrep tools,
and shows you what it found. Nothing goes to a model until you press it. It needs
Claude Code signed in on this Mac, and it uses your Claude plan.

If Claude Code can't sign in by itself, the button opens a new Code chat in the
Claude desktop app instead, with the request already typed. Press send there. That
chat uses the desktop app's sign-in and whichever model you pick, and it needs the
server connected as shown below. The background run comes back by itself once
Claude Code can sign in again.

You can also say "review my batch" in any Claude session, or use any other MCP
client. The app carries its own copy of the server, so set that up once:

```bash
claude mcp add --transport stdio --scope user interviewprep -- /Applications/InterviewPrep.app/Contents/Helpers/interviewprep mcp --resources /Applications/InterviewPrep.app/Contents/Resources/resources.json
```

With only the Claude desktop app installed, `claude` isn't on your PATH. Its copy is
at `~/Library/Application Support/Claude/claude-code/<version>/<build>/claude.app/Contents/MacOS/claude`.

Claude Desktop takes the same command in `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "interviewprep": {
      "command": "/Applications/InterviewPrep.app/Contents/Helpers/interviewprep",
      "args": ["mcp", "--resources", "/Applications/InterviewPrep.app/Contents/Resources/resources.json"]
    }
  }
}
```

The model writes down what it finds, and each session starts from what the last
one left:

| File | Holds | Read |
|------|-------|------|
| `profile/profile.md` | One page on your patterns, flaws and recent results | Every session |
| `profile/judged-attempts.jsonl` | One line per judged attempt, kept for good | When searched |
| `profile/attempts/` | Your code, one plain file per problem per day | Only the current batch |

It records what it saw, from closed lists, so the same thing keeps the same name
from one session to the next. It never writes a probability:

- **Topics**: the 116 finer topics, or a whole pattern when none fits.
- **Thinking**: recognised the pattern (yes, partial, wrong with the pattern you
  went for, no), and found the key step (own, after a hint, missed).
- **Flaws**: the ones above, except the clock's. Anything more specific, such as
  a base case set too late, goes in the judgement's note.

Paste every attempt. From 2026-11-09 an attempt with no code counts as a blank,
the same as no approach: you had none, or you asked for help. Before that day no
code only means nothing was saved, so the coach leaves those out.

The app decides everything else from those: how hard your next problem is,
which pattern comes next, and whether a problem is really done.

Before 2026-11-09 the model wrote two tables of probabilities
(`profile/labels.json`) and judgements in an older shape
(`profile/judgements.jsonl`). Both stay exactly as they were and are never
written again. Each of those attempts waits for a judgement in the new shape,
with the old one shown beside it, and the model can drop one with nothing to add,
such as an attempt with no code. Until it has, the old judgement's grade alone
counts, and only to keep a check going.

Limits cap what gets read, never what gets kept, so nothing needs cleaning up. A
save over a limit is refused, which makes the model compact as it goes:

| Limit | Value |
|-------|-------|
| Profile | 2 KB |
| Search | 50 lines |
| Code shown per problem | 4 KB |

What this is and isn't:

- **No model is trained on you.** The labels are a model's judgement, not
  measurements, and every rule that turns them into picks is a hand-set number
  in `core/config.hpp`. The only numbers fitted to your results are your two
  ratings.
- **It can be wrong.** You ran the code and it only read it. In live tests, models
  called buggy code correct and explained away a true Red. So a grade kinder than
  yours is refused unless it comes with an input worked through the code by hand,
  and a first try counts as a pass only when both grades are Blue.
- **It can't change your progress, only add to it.** Your click decides when
  cards expire. The model's grade sits beside yours in the log, and it has no way
  to touch `state.json`. The most its grade can do is keep a problem's checks
  going and, when it is stricter than yours, count that first try as code that
  didn't work, which brings the pattern up more often.

## Your data

- Mac app and command-line tool share
  `~/Library/Application Support/InterviewPrep/state.json`. A copy from the start of
  each session is kept beside it as `state.backup.json`. If the Mac app can't read
  `state.json` it never saves over it: the file is kept as
  `state.unreadable-<time>.json` and the app goes back to the backup.
- Until November 2026 the app was called Coldstart and this folder was
  `Coldstart`. The first run of the new app or command-line tool moves it over
  whole. Quit the old app first, or its next save starts a new `Coldstart` folder.
- A problem keeps one grade per day. Grading it again the same day replaces the
  first grade, so a misclick is easy to fix. Your pasted code is never replaced.
- All of this stays on this Mac, so copy the `InterviewPrep` folder somewhere else if
  you want a second copy.
- Your code goes in a `profile/attempts` folder beside that file, one plain file
  per problem per day, such as `profile/attempts/lc207/2026-11-06.cpp`, with every
  paste of that day in order under a `// ── Paste 2 at 21:40 ──` line. Paste once
  before you run anything, to show your dry accuracy, then again after each fix
  up to what you submit: press **Paste attempt** (⌘V) on a problem in the Mac app,
  or run `interviewprep attempt lc207 < sol.cpp`. The coach grades the first paste
  and reads the rest for what running taught you. Pastes are only ever added, never
  overwritten, and saving never touches your progress. The website doesn't save
  attempts yet.
- The coach's notes sit in the same `profile` folder. They stay on your machine,
  apart from what your MCP client reads and sends to its model.
- The website saves to your account when it runs as a Claude artifact, and to the
  browser otherwise. Open tabs and devices on the same account stay in step.
- The Mac app and the website do not sync with each other. Both can export and
  import progress in Settings, and the file format is the same, so you can move
  your progress across by hand. Pick one as your daily driver.

To work on the Mac app in Xcode, run `open Package.swift`.

## Sharing the Mac app

`make dmg` gives you a disk image, signed ad hoc. It runs on your Mac. On anyone
else's, macOS blocks it until it is notarized, which needs an Apple Developer ID
($99 a year). `scripts/notarize.sh` does the rest once you have one.

The company frequency labels come from
[liquidslr/leetcode-company-wise-problems](https://github.com/liquidslr/leetcode-company-wise-problems),
which has no license. That is fine for personal use. Before distributing the app
publicly, either get permission or drop the `Companies` table in
`scripts/build_resources.py`. LeetCode problem statements are not stored anywhere: the
app links to leetcode.com.
