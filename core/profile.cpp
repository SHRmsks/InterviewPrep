#include "profile.hpp"

#include <algorithm>
#include <cmath>

namespace interviewprep {

std::string gradeKey(const std::string& id, const std::string& date) { return id + "@" + date; }

bool PatternStats::hasMemorizeSignal() const {
  return review.total >= MEMORIZE_MIN_EVIDENCE && cold.total >= MEMORIZE_MIN_EVIDENCE &&
         memorizeGap() >= MEMORIZE_MIN_GAP;
}

std::array<double, NEED_COUNT> Profile::needShare() const {
  std::array<double, NEED_COUNT> share{};
  double total = 0;
  for (double n : needs) total += n;
  if (total <= 0) return share;
  for (int i = 0; i < NEED_COUNT; ++i) share[i] = needs[i] / total;
  return share;
}

const char* needDeck(int need) {
  if (need == THINKING_NEED) return THINKING_DECK;
  return need >= 0 && need < FLAW_COUNT ? FLAWS[need].deck : nullptr;
}

static double creditFor(Click click, bool hinted) {
  switch (click) {
    case Click::Blue: return hinted ? CREDIT_YELLOW : CREDIT_BLUE;
    case Click::Yellow: return CREDIT_YELLOW;
    case Click::Red: return CREDIT_RED;
  }
  return 0;
}

/*
A missed card from a deck that drills something is a small sign of it, shared
among everything that deck drills. Other decks imply nothing: a missed TLS
card says nothing about how you code.
*/
static void addImplied(Profile& profile, const std::string& deck, double weight) {
  int drills = 0;
  for (int n = 0; n < NEED_COUNT; ++n) drills += deck == needDeck(n);
  if (drills == 0) return;
  for (int n = 0; n < NEED_COUNT; ++n) {
    if (deck == needDeck(n)) profile.needs[n] += weight / drills;
  }
}

Profile buildProfile(const Resources& resources, const std::vector<Event>& events, const Clock& clock, int today,
                     const JudgedMap* judged) {
  Profile profile;
  profile.patterns.resize(resources.patterns().size());
  std::vector<char> seen(resources.questions().size(), 0);

  for (const Event& e : events) {
    const int index = resources.indexOf(e.id);
    if (index < 0) continue;  // a question since removed from the resources
    const Question& q = resources.questions()[index];
    const bool cold = !seen[index];
    seen[index] = 1;

    const double age = std::max(0, today - clock.day(e.date));
    const double decay = std::pow(0.5, age / HALF_LIFE_DAYS);
    double credit = creditFor(e.click, e.hinted);

    /*
    Once the coach has judged an attempt, its thinking labels are the credit, so
    a slip in the code no longer counts against the pattern. The slips go to the
    flaws instead.
    */
    if (q.kind == Kind::Problem && judged) {
      const auto found = judged->find(gradeKey(e.id, formatDate(e.date)));
      if (found != judged->end() && !found->second.dropped) {
        const Judged& j = found->second;
        if (j.thinking >= 0) {
          credit = j.thinking;
          profile.needs[THINKING_NEED] += (1.0 - j.thinking) * decay;
        }
        if (j.hadCode) {
          profile.judgedWithCode += 1;
          for (int f : j.flaws) {
            if (f < 0 || f >= FLAW_COUNT) continue;
            profile.needs[f] += decay;
            profile.flawCounts[f] += 1;
          }
        }
      }
    }

    double evidenceWeight = WEIGHT_FLASH;
    if (q.kind == Kind::Problem) evidenceWeight = cold ? WEIGHT_COLD_PROBLEM : WEIGHT_REVIEW_PROBLEM;
    else if (q.kind == Kind::Drill) evidenceWeight = WEIGHT_DRILL;

    if (q.kind != Kind::Problem && e.click != Click::Blue) addImplied(profile, q.deck, IMPLIED_FLAW_WEIGHT * decay);

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
          if (age < RECENT_DAYS) stats.recentNew += 1;
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
  return profile;
}

}  // namespace interviewprep
