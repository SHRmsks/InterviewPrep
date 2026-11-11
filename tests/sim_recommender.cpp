/*
sim_recommender.cpp — what do the picker's rules actually do?

Runs made-up learners through the app's daily queue on the real resources, and
reports where the new problems went and what came of it. Change a rule in
core/config.hpp, run `make sim` before and after, and compare the two tables.

The learner. Each pattern has a hidden rating for working code. The DP
patterns and monotonic stack start WEAK_BEHIND points below the rest, so there
is something for the app to find. A problem rated R gets working code on the
first try with the chance the app itself assumes,

   1 / (1 + 10^((R - rating) / 400))

and the idea alone comes IDEA_AHEAD points more easily. A made-up coach reads
each first try the way the real one does: it grades the code, and says whether
the idea was there. Every repeat of a problem adds REPEAT_HELPS to the chance.

Practice raises the hidden rating of the problem's main pattern. Nobody knows
by how much, so the table shows two guesses side by side:

   more where weak     a first try adds up to LEARNS points, in proportion to
                       how far the pattern is below CEILING
   the same anywhere   a first try adds LEARNS_ANYWHERE points, whatever the
                       pattern

A review adds a quarter of that. The app sees only the grades and the coach's
labels. The hidden ratings are there to score the outcome.

The settings are the ones in daily use: any company, 4 new problems, a queue
of 8, 3 reviews a day. No cards are graded.

Usage: sim_recommender [resources.json] [learners]
*/

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../core/engine.hpp"

using namespace std;
using namespace interviewprep;

static const int DAYS = 90;
static const int LEARNERS = 20;               // each has its own luck
static const char* const SETTINGS = R"({"settings":{"company":"","codeNew":4,"codeCap":8,"codeReviews":3}})";

static const double STRONG = 1900;            // the hidden working-code rating of most patterns
static const double WEAK_BEHIND = 450;        // how far below it the weak ones start
static const double IDEA_AHEAD = 200;         // seeing the idea is this much easier than getting the code to work
static const double REPEAT_HELPS = 0.20;      // each earlier attempt at the same problem adds this to the chance
static const double YELLOW_WHEN_MISSED = 0.3; // a miss is graded Yellow this often, Red otherwise

static const double CEILING = 2100;           // "more where weak": nothing more to learn at this rating
static const double LEARNS = 40;              // a first try on a pattern WEAK_BEHIND below STRONG adds this much
static const double LEARNS_ANYWHERE = 15;     // "the same anywhere"
static const double REVIEW_TEACHES = 0.25;    // of a first try

static const double INTERVIEW_PROBLEM = 1800; // the outcome: the chance of working code on a problem rated this

static bool isWeak(const string& pattern) {
  return pattern.rfind("dp-", 0) == 0 || pattern == "monotonic-stack";
}

static Json ask(Engine& engine, const Json& request) {
  return Json::parse(engine.call(request.dump()));
}

static double chanceOf(double problem, double rating) {
  return 1.0 / (1.0 + pow(10.0, (problem - rating) / RATING_SCALE));
}

// One pattern of the resources: its slug and how much of what gets asked has it as the main pattern.
struct PatternAsked {
  string slug;
  double asked = 0;
};

// What one learner's ninety days came to.
struct Outcome {
  int fresh = 0, reviews = 0, worstOverflow = 0;
  int lateFresh = 0, lateFreshWeak = 0;                 // new problems from day 31 on
  int earlyFresh = 0, earlyExploring = 0;               // new problems in the first 30 days
  int levels[3] = {0, 0, 0};
  int firstWeak = 0, firstWeakWorked = 0, firstRest = 0, firstRestWorked = 0;   // first tries from day 31 on
  int triedByDay30 = 0;
  double patternsAWeek = 0, mostOnePatternTook = 0;
  int leftOut = 0;                                      // patterns with no new problem from day 15 on
  double chanceWeak = 0, chanceAll = 0;                 // at the end, on an INTERVIEW_PROBLEM
  double appGap = 0, trueGap = 0;                       // working-code rating, the rest minus the weak patterns
};

static Outcome runLearner(const string& resources, const vector<PatternAsked>& patterns, bool moreWhereWeak, uint64_t seed) {
  Engine engine;
  engine.loadResources(resources);
  engine.loadState(SETTINGS);
  Rng rng(seed);

  map<string, double> rating;
  for (const PatternAsked& p : patterns) rating[p.slug] = isWeak(p.slug) ? STRONG - WEAK_BEHIND : STRONG;
  map<string, int> firstTries, late;
  set<string> thisWeek;
  vector<int> weeks;
  string coachLog;
  Outcome out;

  int date = parseDate("2026-11-11");
  for (int day = 0; day < DAYS; ++day, ++date) {
    const string now = formatDate(date) + "T20:00";
    const Json today = ask(engine, Json::object().set("cmd", "today").set("now", now));
    out.worstOverflow = max(out.worstOverflow, today["code"]["overflow"].asInt());

    for (const Json& item : today["code"]["items"].items()) {
      const string& id = item["id"].asStr();
      const string& slot = item["slot"].asStr();
      const Json card = ask(engine, Json::object().set("cmd", "card").set("id", id).set("now", now))["card"];
      const auto& its = card["patterns"].items();
      const string main = its.empty() ? "" : its[0]["slug"].asStr();
      const bool weak = isWeak(main);
      const int attempts = card["attempts"].asInt();
      const double problem = card["rating"].asInt();
      const string level = card["level"].asStr();

      if (slot == "review") {
        ++out.reviews;
      } else {
        ++out.fresh;
        ++out.levels[level == "Easy" ? 0 : level == "Hard" ? 2 : 1];
        if (day < 30) {
          ++out.earlyFresh;
          out.earlyExploring += slot == "new-pattern" || slot == "new-topic";
        } else {
          ++out.lateFresh;
          out.lateFreshWeak += weak;
        }
        if (day >= 14) {
          ++late[main];
          thisWeek.insert(main);
        }
      }

      // The idea first, then whether the code worked given the idea: together, the chance above.
      bool idea = true, worked = false;
      if (attempts == 0) {
        const double ideaChance = min(0.95, chanceOf(problem, rating[main] + IDEA_AHEAD));
        const double codeChance = min(ideaChance, chanceOf(problem, rating[main]));
        idea = rng.uniform() < ideaChance;
        worked = idea && rng.uniform() < codeChance / ideaChance;
        ++firstTries[main];
        if (day >= 30) {
          (weak ? out.firstWeak : out.firstRest) += 1;
          (weak ? out.firstWeakWorked : out.firstRestWorked) += worked;
        }
      } else {
        worked = rng.uniform() < min(0.95, chanceOf(problem, rating[main]) + REPEAT_HELPS * attempts);
      }
      const char* grade = worked ? "b" : rng.uniform() < YELLOW_WHEN_MISSED ? "y" : "r";
      ask(engine, Json::object().set("cmd", "grade").set("id", id).set("now", now).set("grade", grade));
      if (attempts == 0) {
        // The coach agrees with the grade, and says whether the idea was there.
        coachLog += Json::object().set("problem", id).set("date", formatDate(date)).set("model_grade", grade)
                        .set("recognised", "yes").set("key_step", idea ? "own" : "missed").set("had_code", true).dump() + "\n";
        ask(engine, Json::object().set("cmd", "judgements").set("text", coachLog));
      }

      const double learns = moreWhereWeak ? LEARNS * max(0.0, (CEILING - rating[main]) / (CEILING - STRONG + WEAK_BEHIND))
                                          : LEARNS_ANYWHERE;
      rating[main] += attempts == 0 ? learns : REVIEW_TEACHES * learns;
    }

    if (day == 29) {
      for (const PatternAsked& p : patterns) out.triedByDay30 += firstTries[p.slug] > 0;
    }
    if (day >= 14 && (day - 14) % 7 == 6) {
      weeks.push_back(static_cast<int>(thisWeek.size()));
      thisWeek.clear();
    }
  }

  int lateTotal = 0, most = 0;
  for (const PatternAsked& p : patterns) {
    lateTotal += late[p.slug];
    most = max(most, late[p.slug]);
    out.leftOut += late[p.slug] == 0;
  }
  out.mostOnePatternTook = lateTotal ? static_cast<double>(most) / lateTotal : 0;
  for (int w : weeks) out.patternsAWeek += static_cast<double>(w) / weeks.size();

  // Where the learner stands, and what the app made of it.
  map<string, double> appRating;
  const Json insights = ask(engine, Json::object().set("cmd", "insights").set("now", formatDate(date - 1) + "T21:00"));
  for (const Json& row : insights["patterns"].items()) appRating[row["slug"].asStr()] = row["codeRating"].asInt();
  double asked = 0, askedWeak = 0, appWeak = 0, appRest = 0, trueWeak = 0, trueRest = 0;
  int weakCount = 0, restCount = 0;
  for (const PatternAsked& p : patterns) {
    const double chance = chanceOf(INTERVIEW_PROBLEM, rating[p.slug]);
    asked += p.asked;
    out.chanceAll += p.asked * chance;
    const double app = appRating.count(p.slug) ? appRating[p.slug] : insights["rating"]["code"].asInt();
    if (isWeak(p.slug)) {
      askedWeak += p.asked;
      out.chanceWeak += p.asked * chance;
      appWeak += app;
      trueWeak += rating[p.slug];
      ++weakCount;
    } else {
      appRest += app;
      trueRest += rating[p.slug];
      ++restCount;
    }
  }
  out.chanceAll /= asked;
  out.chanceWeak /= askedWeak;
  out.appGap = appRest / restCount - appWeak / weakCount;
  out.trueGap = trueRest / restCount - trueWeak / weakCount;
  return out;
}

int main(int argc, char** argv) {
  const string resourcesPath = argc > 1 ? argv[1] : "data/resources.json";
  const int learners = argc > 2 ? max(1, atoi(argv[2])) : LEARNERS;
  ifstream in(resourcesPath, ios::binary);
  stringstream text;
  text << in.rdbuf();
  const string resources = text.str();

  string error;
  {
    Engine check;
    if (check.loadResources(resources, &error) < 0) {
      fprintf(stderr, "Can't load %s: %s\n", resourcesPath.c_str(), error.c_str());
      return 1;
    }
  }

  // The algorithm patterns, and how much of what gets asked each one is.
  const Json root = Json::parse(resources);
  vector<PatternAsked> all;
  for (const Json& p : root["patterns"].items()) all.push_back({p["slug"].asStr(), 0});
  for (const Json& q : root["problems"].items()) {
    const auto& its = q["p"].items();
    if (!its.empty()) all[static_cast<size_t>(its[0].asInt())].asked += q["f"].asNum() / 100.0;
  }
  vector<PatternAsked> patterns;
  double asked = 0, askedWeak = 0;
  int weakCount = 0;
  for (size_t i = 0; i < all.size(); ++i) {
    if (!root["patterns"].items()[i]["algo"].asBool(true)) continue;
    patterns.push_back(all[i]);
    asked += all[i].asked;
    if (isWeak(all[i].slug)) {
      askedWeak += all[i].asked;
      ++weakCount;
    }
  }

  // Both guesses about learning, the same learners under each.
  vector<Outcome> runs[2];
  for (int guess = 0; guess < 2; ++guess) {
    for (int k = 0; k < learners; ++k) runs[guess].push_back(runLearner(resources, patterns, guess == 0, 2026 + k));
  }
  auto mean = [&](int guess, double (*value)(const Outcome&)) {
    double sum = 0;
    for (const Outcome& o : runs[guess]) sum += value(o);
    return sum / runs[guess].size();
  };
  auto row = [&](const char* label, const char* format, double (*value)(const Outcome&)) {
    char a[32], b[32];
    snprintf(a, sizeof a, format, mean(0, value));
    snprintf(b, sizeof b, format, mean(1, value));
    printf("  %-58s %12s %12s\n", label, a, b);
  };

  printf("%d days, %d made-up learners under each guess about learning.\n", DAYS, learners);
  printf("The weak patterns are %d of %d, and %.0f%% of what gets asked.\n\n", weakCount, static_cast<int>(patterns.size()),
         100.0 * askedWeak / asked);
  printf("  %-58s %12s %12s\n", "", "more where", "the same");
  printf("  %-58s %12s %12s\n", "A first try teaches", "weak", "anywhere");

  printf("\nWhere the new problems went\n");
  row("on the weak patterns, from day 31", "%.0f%%", [](const Outcome& o) { return 100.0 * o.lateFreshWeak / max(1, o.lateFresh); });
  row("to a new pattern or subpattern, days 1 to 30", "%.0f%%", [](const Outcome& o) { return 100.0 * o.earlyExploring / max(1, o.earlyFresh); });
  row("Easy", "%.0f%%", [](const Outcome& o) { return 100.0 * o.levels[0] / max(1, o.fresh); });
  row("Medium", "%.0f%%", [](const Outcome& o) { return 100.0 * o.levels[1] / max(1, o.fresh); });
  row("Hard", "%.0f%%", [](const Outcome& o) { return 100.0 * o.levels[2] / max(1, o.fresh); });

  printf("\nWhat came of it\n");
  row("working code on an 1800 problem at the end, weak patterns", "%.1f%%", [](const Outcome& o) { return 100.0 * o.chanceWeak; });
  row("the same, every pattern by how much it gets asked", "%.1f%%", [](const Outcome& o) { return 100.0 * o.chanceAll; });
  row("first tries with working code from day 31, weak patterns", "%.0f%%", [](const Outcome& o) { return 100.0 * o.firstWeakWorked / max(1, o.firstWeak); });
  row("first tries with working code from day 31, the rest", "%.0f%%", [](const Outcome& o) { return 100.0 * o.firstRestWorked / max(1, o.firstRest); });

  printf("\nWhat the app worked out: working-code rating, the rest minus the weak patterns\n");
  row("by the app", "%.0f", [](const Outcome& o) { return o.appGap; });
  row("in truth, by then", "%.0f", [](const Outcome& o) { return o.trueGap; });

  printf("\nCoverage\n");
  row("patterns given a first try by day 30", "%.1f", [](const Outcome& o) { return static_cast<double>(o.triedByDay30); });
  row("different patterns in a week of new problems, from day 15", "%.1f", [](const Outcome& o) { return o.patternsAWeek; });
  row("the most one pattern took, from day 15", "%.0f%%", [](const Outcome& o) { return 100.0 * o.mostOnePatternTook; });
  row("patterns with no new problem from day 15", "%.1f", [](const Outcome& o) { return static_cast<double>(o.leftOut); });

  printf("\nThe queue\n");
  row("new problems a day", "%.1f", [](const Outcome& o) { return static_cast<double>(o.fresh) / DAYS; });
  row("reviews a day", "%.1f", [](const Outcome& o) { return static_cast<double>(o.reviews) / DAYS; });
  row("most reviews left waiting on one day", "%.1f", [](const Outcome& o) { return static_cast<double>(o.worstOverflow); });
  return 0;
}
