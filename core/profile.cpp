#include "profile.hpp"

#include <algorithm>
#include <cmath>

using namespace std;

namespace interviewprep {

string gradeKey(const string& id, const string& date) { return id + "@" + date; }

bool PatternStats::hasMemorizeSignal() const {
  return review.total >= MEMORIZE_MIN_EVIDENCE && cold.total >= MEMORIZE_MIN_EVIDENCE &&
         memorizeGap() >= MEMORIZE_MIN_GAP;
}

array<double, NEED_COUNT> Profile::needShare() const {
  array<double, NEED_COUNT> share{};
  double total = 0;
  for (double n : needs) total += n;
  if (total <= 0) return share;
  for (int i = 0; i < NEED_COUNT; ++i) share[i] = needs[i] / total;
  return share;
}

bool needHasDrills(int need) {
  if (need == SEEING_NEED || need == KEY_STEP_NEED) return true;
  return need >= 0 && need < FLAW_COUNT && FLAWS[need].deck != nullptr;
}

bool cardDrills(int need, const Question& card) {
  if (card.kind == Kind::Problem) return false;
  if (need == SEEING_NEED) {
    // A signal card with no skill on record is taken to ask which pattern it is.
    return card.deck == SIGNALS_DECK && (card.skill.empty() || card.skill == SKILL_SEES_PATTERN);
  }
  if (need == KEY_STEP_NEED) {
    return (card.deck == SIGNALS_DECK && card.skill == SKILL_KEY_STEP) || card.deck == TEMPLATES_DECK;
  }
  return needHasDrills(need) && card.deck == FLAWS[need].deck;
}

const char* needSlug(int need) {
  if (need == SEEING_NEED) return "seeing-the-pattern";
  if (need == KEY_STEP_NEED) return "finding-the-key-step";
  return need >= 0 && need < FLAW_COUNT ? FLAWS[need].slug : "";
}

static double creditFor(Click click, bool hinted) {
  switch (click) {
    case Click::Blue: return hinted ? CREDIT_YELLOW : CREDIT_BLUE;
    case Click::Yellow: return CREDIT_YELLOW;
    case Click::Red: return CREDIT_RED;
  }
  return 0;
}

// A sign of a need, on the patterns it showed up on.
static void addNeed(Profile& profile, int need, double amount, const vector<int>& where) {
  profile.needs[need] += amount;
  vector<double>& byPattern = profile.needPatterns[need];
  for (int p : where) {
    if (p >= 0 && p < static_cast<int>(byPattern.size())) byPattern[p] += amount;
  }
}

/*
A missed card that drills something is a small sign of it, shared among
everything that card drills. Other cards imply nothing: a missed TLS card says
nothing about how you code. Nor does any card imply a flaw the clock sets.
*/
static void addImplied(Profile& profile, const Question& card, double weight) {
  vector<int> drilled;
  for (int n = 0; n < NEED_COUNT; ++n) {
    if (n < FLAW_COUNT && FLAWS[n].byClock) continue;
    if (cardDrills(n, card)) drilled.push_back(n);
  }
  for (int n : drilled) addNeed(profile, n, weight / drilled.size(), card.patterns);
}

double Profile::topicScore(int topic, int pattern) const {
  const Evidence& tries = topics[topic].firstTries;
  const double lean = patterns[pattern].codeScore();
  return (tries.pass + BORROWED_TRIES * lean) / (tries.total + BORROWED_TRIES);
}

namespace {

// One first try on a rated problem, for the rating.
struct RatedTry {
  int pattern;     // its main pattern
  double rating;   // the problem's
  double result;   // 0 to 1
  double weight;   // faded by age
};

// What a fit gives: your overall rating, how sure it is, and your rating on each pattern.
struct Ratings {
  double overall = RATING_START;
  double spread = RATING_START_SPREAD;
  vector<double> byPattern;
};

/*
The most likely overall rating and pattern nudges given the tries and the two
starting beliefs (DIFFICULTY in config.hpp). Each round takes a Newton step on
the overall rating, then on every nudge. Each is one smooth hill, so they
settle within a few rounds; the rest is margin. The same fit serves both
ratings: only what counts as a try's result differs.
*/
Ratings fitRatings(const vector<RatedTry>& tries, size_t patternCount) {
  const double k = log(10.0) / RATING_SCALE;
  const double startPrecision = 1.0 / (RATING_START_SPREAD * RATING_START_SPREAD);
  const double nudgePrecision = 1.0 / (RATING_NUDGE_SPREAD * RATING_NUDGE_SPREAD);
  vector<double> nudge(patternCount, 0.0), slope(patternCount), curve(patternCount);
  auto chance = [&](double you, double problem) { return 1.0 / (1.0 + exp(-k * (you - problem))); };

  double overall = RATING_START, information = startPrecision;
  for (int round = 0; round < 60; ++round) {
    double s = -(overall - RATING_START) * startPrecision, c = -startPrecision;
    for (const RatedTry& t : tries) {
      const double p = chance(overall + nudge[t.pattern], t.rating);
      s += t.weight * k * (t.result - p);
      c -= t.weight * k * k * p * (1 - p);
    }
    overall -= s / c;
    information = -c;

    for (size_t p = 0; p < patternCount; ++p) {
      slope[p] = -nudge[p] * nudgePrecision;
      curve[p] = -nudgePrecision;
    }
    for (const RatedTry& t : tries) {
      const double p = chance(overall + nudge[t.pattern], t.rating);
      slope[t.pattern] += t.weight * k * (t.result - p);
      curve[t.pattern] -= t.weight * k * k * p * (1 - p);
    }
    for (size_t p = 0; p < patternCount; ++p) nudge[p] -= slope[p] / curve[p];
  }

  Ratings ratings;
  ratings.overall = overall;
  ratings.spread = 1.0 / sqrt(information);
  ratings.byPattern.resize(patternCount);
  for (size_t p = 0; p < patternCount; ++p) ratings.byPattern[p] = overall + nudge[p];
  return ratings;
}

}  // namespace

Profile buildProfile(const Resources& resources, const vector<Event>& events, const Clock& clock, int today,
                     const JudgedMap* judged) {
  Profile profile;
  const size_t patternCount = resources.patterns().size();
  profile.patterns.resize(patternCount);
  profile.topics.resize(resources.topics().size());
  for (vector<double>& byPattern : profile.needPatterns) byPattern.assign(patternCount, 0.0);
  vector<char> seen(resources.questions().size(), 0);
  vector<int> startOver(patternCount, 0);
  vector<RatedTry> tries, codeTries;   // each first try twice: by the thinking, and by whether the code worked
  // Timed problem tries, and those over the box, by main pattern.
  vector<double> timed(patternCount, 0.0), slow(patternCount, 0.0);

  for (const Event& e : events) {
    const int index = resources.indexOf(e.id);
    if (index < 0) continue;  // a question since removed from the resources
    const Question& q = resources.questions()[index];
    const bool cold = !seen[index];
    seen[index] = 1;

    const double age = max(0, today - clock.day(e.date));
    const double decay = pow(0.5, age / HALF_LIFE_DAYS);
    double credit = creditFor(e.click, e.hinted);
    // Whether the code worked: your grade, or the coach's below when it is stricter.
    double codeCredit = credit;
    const int main = q.patterns.empty() ? -1 : q.patterns[0];

    // The clock needs no coach: an attempt longer than its time box was slow.
    const bool overTheBox = q.kind == Kind::Problem && e.secs > timeboxMinutes(q.level) * 60;
    if (overTheBox) {
      for (int f = 0; f < FLAW_COUNT; ++f) {
        if (!FLAWS[f].byClock) continue;
        addNeed(profile, f, decay, q.patterns);
        profile.flawCounts[f] += 1;
      }
    }
    if (q.kind == Kind::Problem && e.secs > 0 && main >= 0) {
      timed[main] += decay;
      profile.patterns[main].timedTries += 1;
      if (overTheBox) {
        slow[main] += decay;
        profile.patterns[main].slowTries += 1;
      }
    }

    /*
    Once the coach has judged an attempt, its thinking labels are the credit, so
    a slip in the code no longer counts against the pattern. The slips go to the
    flaws instead, on the patterns of the topics the coach named.
    */
    const Judged* judgement = nullptr;
    if (q.kind == Kind::Problem && judged) {
      const auto found = judged->find(gradeKey(e.id, formatDate(e.date)));
      if (found != judged->end() && !found->second.dropped) {
        const Judged& j = found->second;
        judgement = &j;
        const vector<int>& where = j.patterns.empty() ? q.patterns : j.patterns;
        if (j.hasModelGrade) codeCredit = min(codeCredit, creditFor(j.modelGrade, false));
        if (j.thinking >= 0) credit = j.thinking;
        if (j.recognised >= 0 && j.recognised < 1) {
          // A wrong pick counts the pattern they went for too, so the two get drilled side by side.
          vector<int> seeing = where;
          if (j.reachedFor >= 0) seeing.push_back(j.reachedFor);
          addNeed(profile, SEEING_NEED, (1.0 - j.recognised) * decay, seeing);
        }
        // With the pattern missed there was no key step to find, so only seeing it counts.
        if (j.keyStep >= 0 && j.keyStep < 1 && !j.missedPattern) {
          addNeed(profile, KEY_STEP_NEED, (1.0 - j.keyStep) * decay, where);
        }
        /*
        Zero clue starts the pattern over at Easy; each key step found on your own climbs back a level.
        A blank the coach couldn't place counts on the main pattern alone: it says nothing about the others.
        */
        const vector<int> placed = !j.patterns.empty() ? j.patterns : main >= 0 ? vector<int>{main} : vector<int>{};
        for (int p : placed) {
          if (p < 0 || p >= static_cast<int>(patternCount)) continue;
          if (j.noApproach) startOver[p] = 1;
          else if (j.keyStepOwn && startOver[p] > 0) startOver[p] = startOver[p] == 1 ? 2 : 0;
        }
        if (j.hadCode) {
          profile.judgedWithCode += 1;
          for (int f : j.flaws) {
            if (f < 0 || f >= FLAW_COUNT || FLAWS[f].byClock) continue;
            addNeed(profile, f, decay, where);
            profile.flawCounts[f] += 1;
          }
        }
      }
    }

    if (q.kind == Kind::Problem && cold) {
      // Your ratings count first tries only.
      if (q.rating > 0 && main >= 0) {
        tries.push_back({main, static_cast<double>(q.rating), credit, decay});
        codeTries.push_back({main, static_cast<double>(q.rating), codeCredit, decay});
      }
      // The finer topics it used: the coach's when it named some, else the problem's own.
      const vector<int>& used = judgement && !judgement->topics.empty() ? judgement->topics : q.topics;
      for (int t : used) {
        if (t < 0 || t >= static_cast<int>(profile.topics.size())) continue;
        profile.topics[t].firstTries.add(codeCredit, decay);
        if (age < RECENT_DAYS) profile.topics[t].recentSolved += codeCredit;
      }
    }

    double evidenceWeight = WEIGHT_FLASH;
    if (q.kind == Kind::Problem) evidenceWeight = cold ? WEIGHT_COLD_PROBLEM : WEIGHT_REVIEW_PROBLEM;
    else if (q.kind == Kind::Drill) evidenceWeight = WEIGHT_DRILL;

    if (q.kind != Kind::Problem && e.click != Click::Blue) addImplied(profile, q, IMPLIED_FLAW_WEIGHT * decay);

    for (size_t i = 0; i < q.patterns.size(); ++i) {
      if (q.patterns[i] >= static_cast<int>(profile.patterns.size())) continue;
      PatternStats& stats = profile.patterns[q.patterns[i]];
      const double share = i == 0 ? 1.0 : 0.5;  // the first pattern is the main one
      const double weight = decay * share;

      if (q.kind == Kind::Problem) {
        (cold ? stats.cold : stats.review).add(credit, weight);
        if (cold && i == 0) {
          stats.coldAttempts += 1;
          stats.coldPasses += e.click == Click::Blue && !e.hinted;
          stats.code.add(codeCredit, decay);
          if (age < RECENT_DAYS) stats.recentSolved += codeCredit;
        }
      } else if (q.kind == Kind::Drill) {
        stats.drill.add(credit, weight);
      } else {
        stats.flash.add(credit, weight);
      }

      stats.alpha += credit * evidenceWeight * weight;
      stats.beta += (1.0 - credit) * evidenceWeight * weight;
      stats.attempts += 1;
    }
  }
  const Ratings idea = fitRatings(tries, patternCount), code = fitRatings(codeTries, patternCount);
  profile.rating = idea.overall;
  profile.ratingSpread = idea.spread;
  profile.ratedTries = static_cast<int>(tries.size());
  profile.codeRating = code.overall;
  for (const RatedTry& t : tries) profile.patterns[t.pattern].ratedTries += 1;
  for (size_t p = 0; p < patternCount; ++p) {
    PatternStats& stats = profile.patterns[p];
    stats.rating = idea.byPattern[p];
    stats.codeRating = code.byPattern[p];
    stats.startOverStep = startOver[p];
    // Only where it happened: a pattern with no slow tries is never eased.
    stats.slowShare = slow[p] / (timed[p] + BORROWED_TRIES);
  }
  return profile;
}

}  // namespace interviewprep
