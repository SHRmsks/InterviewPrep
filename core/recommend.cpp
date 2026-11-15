#include "recommend.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

using namespace std;

namespace interviewprep {

// ── Rng ───────────────────────────────────────────────────────────────────

uint64_t Rng::next() {
  uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

double Rng::uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

int Rng::below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<uint64_t>(n)); }

double Rng::normal() {  // Box-Muller
  const double u = max(uniform(), 1e-12);
  const double v = uniform();
  return sqrt(-2.0 * log(u)) * cos(6.283185307179586 * v);
}

double Rng::gamma(double shape) {  // Marsaglia-Tsang
  if (shape < 1.0) {
    return gamma(shape + 1.0) * pow(max(uniform(), 1e-12), 1.0 / shape);
  }
  const double d = shape - 1.0 / 3.0;
  const double c = 1.0 / sqrt(9.0 * d);
  while (true) {
    const double x = normal();
    double v = 1.0 + c * x;
    if (v <= 0) continue;
    v = v * v * v;
    const double u = max(uniform(), 1e-12);
    if (log(u) < 0.5 * x * x + d - d * v + d * log(v)) return d * v;
  }
}

double Rng::beta(double a, double b) {
  const double x = gamma(a);
  const double y = gamma(b);
  return x / (x + y);
}

const char* slotName(Slot slot) {
  switch (slot) {
    case Slot::Sibling: return "sibling";
    case Slot::Weak: return "weak";
    case Slot::Wildcard: return "wildcard";
    case Slot::NewPattern: return "new-pattern";
    case Slot::NewTopic: return "new-topic";
    case Slot::Practice: return "practice";
  }
  return "wildcard";
}

const char* Pick::badge() const {
  const bool dealt = slot == Slot::Sibling || slot == Slot::Weak || slot == Slot::Wildcard;
  return explore && dealt ? "explore" : slotName(slot);
}

const char* needLabel(int need) {
  if (need == SEEING_NEED) return "Seeing which pattern it is";
  if (need == KEY_STEP_NEED) return "Finding the key step";
  return need >= 0 && need < FLAW_COUNT ? FLAWS[need].label : "";
}

double exploreShare(int firstDate, int date, int interviewDate) {
  if (interviewDate <= firstDate) return EXPLORE_END;
  const double progress = min(1.0, max(0.0, static_cast<double>(date - firstDate) / (interviewDate - firstDate)));
  return EXPLORE_END + (EXPLORE_START - EXPLORE_END) * (1.0 + cos(3.141592653589793 * progress)) / 2.0;
}

bool exploresAt(long slot, double share) {
  return floor((slot + 1) * share) > floor(slot * share);
}

namespace {

string percent(double share) {
  return to_string(static_cast<int>(round(share * 100))) + "%";
}

class Picker {
 public:
  Picker(RecommendContext& context, Rng& rng)
      : c_(context), rng_(rng), resources_(*context.resources), questions_(resources_.questions()),
        inPlay_(resources_.patterns().size(), 0), pickedToday_(resources_.patterns().size(), 0),
        waiting_(resources_.patterns().size(), 0), asked_(resources_.patterns().size(), 0.0),
        topicAsked_(resources_.topics().size(), 0.0), topicTried_(resources_.topics().size(), 0),
        topicWaiting_(resources_.topics().size(), 0) {
    for (const Seed& seed : c_.seeds) {
      for (int p : questions_[seed.question].patterns) inPlay_[p] = 1;
    }
    for (int i = 0; i < static_cast<int>(questions_.size()); ++i) {
      const Question& q = questions_[i];
      if (q.kind != Kind::Problem) {
        cards_.push_back(i);
        continue;
      }
      const double asked = frequency(i);
      if (!q.patterns.empty()) asked_[q.patterns[0]] += asked;
      for (int t : q.topics) {
        topicAsked_[t] += asked;
        if ((*c_.states)[i].attempts > 0) topicTried_[t] = 1;
      }
    }
    // A new face still waiting in today's queue holds its place, so asking for more never repeats its pattern.
    for (int i : c_.waiting) hold(questions_[i]);
  }

  bool pick(Lane lane, Slot slot, Pick* out) {
    switch (slot) {
      case Slot::Sibling: return lane == Lane::Cards && cardSibling(out);   // a mock interview points nowhere
      case Slot::Weak: return lane == Lane::Code ? problemWeak(out) : cardWeak(out);
      case Slot::Wildcard: return lane == Lane::Code ? problemWildcard(out) : cardWildcard(out);
      default: break;  // the queue's own kinds are never dealt: see fromQueue
    }
    return false;
  }

  /*
  A new coding problem for the day's queue, by section 7 of config.hpp.
  `position` is this pick's place among the day's new problems.

  With no interview date: while any pattern is new, NEW_PATTERN_SHARE of the
  new problems go to one, evenly spaced, and the rest come from the pull
  among the patterns you have tried. Once none is new, one in
  NEW_GROUND_EVERY comes from a finer topic never done. With a date: today's
  share of them explore, evenly spaced, and the rest come from the pull. Each
  kind falls back on the others when it has nothing left.

  The pull draws among the patterns you have tried. A pattern with a new
  problem waiting in your queue is held: neither the pull nor exploring gives
  it again until that one is graded or hidden. Only when nothing else is left
  does a held pattern get a second problem.
  */
  bool fromQueue(int position, Pick* out) {
    if (c_.explore < 0) {
      if (anyUntried()) {
        if (exploresAt(c_.newSoFar + position, NEW_PATTERN_SHARE)) {
          return newPattern(out) || practice(out, true) || newTopic(out) || lastResort(out);
        }
        return practice(out, true) || newPattern(out) || newTopic(out) || lastResort(out);
      }
      if ((c_.slotOffset + position) % NEW_GROUND_EVERY == 0 && newTopic(out)) return true;
      return practice(out, true) || newTopic(out) || lastResort(out);
    }
    if (exploresAt(c_.newSoFar + position, c_.explore)) {
      return newPattern(out) || newTopic(out) || practice(out, true) || lastResort(out);
    }
    return practice(out, true) || newPattern(out) || newTopic(out) || lastResort(out);
  }

  void accept(const Pick& pick) {
    (*c_.taken)[pick.question] = 1;
    hold(questions_[pick.question]);
  }

  // Each pattern a new problem can come from, and its share of its group: see PatternOdds.
  vector<PatternOdds> odds() const {
    const auto& patterns = resources_.patterns();
    vector<PatternOdds> out;
    double pullTotal = 0, newTotal = 0;
    for (int p = 0; p < static_cast<int>(patterns.size()); ++p) {
      if (!patterns[p].algorithm || !hasEligibleProblem(p)) continue;
      PatternOdds o;
      o.pattern = p;
      o.isNew = c_.profile->patterns[p].coldAttempts == 0;
      if (o.isNew) {
        o.share = asked_[p] + 1e-9;   // as newPattern() draws them
        newTotal += o.share;
      } else {
        o.weak = weak(p);
        o.recent = recent(p);
        o.share = pullOf(p);
        o.why = whyPulled(p).substr(1);   // without the space that joins it to a sentence before
        pullTotal += o.share;
      }
      out.push_back(move(o));
    }
    for (PatternOdds& o : out) {
      const double total = o.isNew ? newTotal : pullTotal;
      o.share = total > 0 ? o.share / total : 0;
    }
    return out;
  }

 private:
  RecommendContext& c_;
  Rng& rng_;
  const Resources& resources_;
  const vector<Question>& questions_;
  vector<char> inPlay_;            // patterns anything in today's queue touches
  vector<char> pickedToday_;       // patterns already given a new face today
  vector<int> waiting_;            // new problems in today's queue with this main pattern, picked and not graded yet
  vector<double> asked_;           // how much of what gets asked has this main pattern
  vector<double> topicAsked_;      // how much of what gets asked uses this finer topic
  vector<char> topicTried_;        // a problem of this topic has been graded, or one is waiting
  vector<char> topicWaiting_;      // a new problem of this topic is waiting in today's queue
  vector<int> cards_;              // every question that is a card
  size_t nextSeed_ = 0;

  const vector<int>& allCards() const { return cards_; }

  /*
  A new face in today's queue holds its place. Its pattern and finer topics
  aren't picked again until it is graded or hidden, and nothing is assumed
  about how it will go: being picked isn't a result, only a grade is.
  */
  void hold(const Question& q) {
    for (int p : q.patterns) {
      inPlay_[p] = 1;
      pickedToday_[p] = 1;
    }
    if (q.kind != Kind::Problem) return;
    if (!q.patterns.empty()) waiting_[q.patterns[0]] += 1;
    for (int t : q.topics) {
      topicTried_[t] = 1;
      topicWaiting_[t] = 1;
    }
  }

  // New problems of this pattern you solved in the last RECENT_DAYS, each counted by how it went. Graded ones only.
  double recentSolved(int pattern) const { return c_.profile->patterns[pattern].recentSolved; }

  bool isUnseen(int i) const {
    return (*c_.states)[i].track == Track::New && !(*c_.taken)[i];
  }

  // A company you target narrows the problems to the ones it asks.
  bool eligibleProblem(int i) const {
    return questions_[i].kind == Kind::Problem && isUnseen(i) && (c_.includePremium || !questions_[i].premium) &&
           (c_.company < 0 || resources_.frequency(i, c_.company) > 0);
  }

  bool eligibleCard(int i) const {
    const Question& q = questions_[i];
    if (q.kind == Kind::Problem || !isUnseen(i)) return false;
    int deck = resources_.deckIndex(q.deck);
    return deck < 0 || deck >= static_cast<int>(c_.deckOff.size()) || !c_.deckOff[deck];
  }

  bool anyEligible(const vector<int>& pool) const {
    return any_of(pool.begin(), pool.end(), [&](int i) { return eligibleProblem(i); });
  }

  bool hasEligibleProblem(int pattern) const { return anyEligible(resources_.patterns()[pattern].problems); }

  double mastery(int pattern) const { return c_.profile->patterns[pattern].mastery(); }

  // New: never the main pattern of a problem you tried, and none of it is waiting in today's queue.
  bool untried(int pattern) const { return c_.profile->patterns[pattern].coldAttempts == 0 && waiting_[pattern] == 0; }

  bool anyUntried() const {
    const auto& patterns = resources_.patterns();
    for (int p = 0; p < static_cast<int>(patterns.size()); ++p) {
      if (patterns[p].algorithm && untried(p) && hasEligibleProblem(p)) return true;
    }
    return false;
  }

  /*
  How much being weak on a pattern multiplies its pull: doubled for every
  WEAK_DOUBLES_EVERY points its working-code rating sits below your overall
  one, halved for every one above, within the two limits.
  */
  double weak(int pattern) const {
    const double below = c_.profile->codeRating - c_.profile->patterns[pattern].codeRating;
    return min(WEAK_PULL_MOST, max(WEAK_PULL_LEAST, pow(2.0, below / WEAK_DOUBLES_EVERY)));
  }

  // How much the new problems of a pattern you solved lately step its pull back: 1/2 for each.
  double recent(int pattern) const { return pow(PULL_KEPT_PER_RECENT_PROBLEM, recentSolved(pattern)); }

  // A pattern's pull: how much it gets asked × weak × recent. The picker draws by it, and odds() shows it.
  double pullOf(int pattern) const { return asked_[pattern] * weak(pattern) * recent(pattern); }

  // The flaw that shows most on this pattern, or -1. The clock's flaw isn't a slip in the code.
  int mostSeenSlip(int pattern) const {
    int most = -1;
    double seen = 0;
    for (int f = 0; f < FLAW_COUNT; ++f) {
      if (FLAWS[f].byClock || c_.profile->needPatterns[f][pattern] <= seen) continue;
      seen = c_.profile->needPatterns[f][pattern];
      most = f;
    }
    return most;
  }

  // ── Scoring ─────────────────────────────────────────────────────────────

  // How often it gets asked: by your target company alone when you have one.
  double frequency(int i) const { return resources_.frequency(i, c_.company); }

  // Where a new problem of this pattern aims: your idea rating on it + STRETCH (DIFFICULTY in config.hpp).
  double aimOf(int pattern) const { return c_.profile->patterns[pattern].rating + STRETCH; }

  // 1 for a rating at the aim, about 0.6 one LEVEL_WIDTH away.
  static double closeness(double rating, double aim) {
    const double distance = (rating - aim) / LEVEL_WIDTH;
    return exp(-0.5 * distance * distance);
  }

  // For the single best problem of a mock slot.
  double problemScore(int i, double aim) {
    const Question& q = questions_[i];
    return W_FREQUENCY * frequency(i) + W_CURATED * (q.tier > 0 ? 1.0 : 0.0) +
           W_LEVEL_FIT * closeness(q.rating, aim) + W_NOISE * rng_.uniform();
  }

  // How likely a problem is to be drawn once its level and subpattern are chosen.
  double problemWeight(int i, double aim) const {
    const Question& q = questions_[i];
    return (PROBLEM_WEIGHT_FLOOR + frequency(i)) * (PROBLEM_WEIGHT_FLOOR + closeness(q.rating, aim)) *
           (q.tier > 0 ? CURATED_BOOST : 1.0);
  }

  // An index in proportion to the shares, skipping zeros, or -1 when all are zero.
  int drawShare(const vector<double>& shares) {
    double total = 0;
    for (double share : shares) total += max(0.0, share);
    if (total <= 0) return -1;
    double roll = rng_.uniform() * total;
    int chosen = -1;
    for (size_t k = 0; k < shares.size(); ++k) {
      if (shares[k] <= 0) continue;
      chosen = static_cast<int>(k);
      roll -= shares[k];
      if (roll <= 0) break;
    }
    return chosen;
  }

  // A question from the pool in proportion to its weight, or -1 for an empty pool.
  int drawWeighted(const vector<pair<int, double>>& pool) {
    vector<double> weights;
    weights.reserve(pool.size());
    for (const auto& entry : pool) weights.push_back(entry.second);
    const int k = drawShare(weights);
    return k < 0 ? -1 : pool[k].first;
  }

  static double cardLevelScore(const Question& q) {
    return q.level == 1 ? 1.0 : q.level == 2 ? 0.6 : 0.2;
  }

  // ── The queue's new problems ────────────────────────────────────────────

  static constexpr const char* LEVEL_NAMES[] = {"Easy", "Medium", "Hard"};

  // The middle rating of some problems, or 0 for none.
  double middleRating(vector<int> problems) const {
    if (problems.empty()) return 0;
    const auto middle = problems.begin() + static_cast<long>(problems.size() / 2);
    nth_element(problems.begin(), middle, problems.end(),
                [&](int a, int b) { return questions_[a].rating < questions_[b].rating; });
    return questions_[*middle].rating;
  }

  // The level wanted if it has problems, else the nearest that has, the easier side first.
  static int nearestLevel(const array<vector<int>, 3>& byLevel, int wanted) {
    for (int step = 0; step < 3; ++step) {
      if (wanted - step >= 0 && !byLevel[wanted - step].empty()) return wanted - step;
      if (wanted + step < 3 && !byLevel[wanted + step].empty()) return wanted + step;
    }
    return -1;
  }

  // The problem's finer topic within this pattern, or -1 when it has none there.
  int subpatternOf(int question, int pattern) const {
    for (int t : questions_[question].topics) {
      if (resources_.topics()[t].pattern == pattern) return t;
    }
    return -1;
  }

  // The problems of one level a pick can reach: the ones in a finer topic of the pattern when any is, else all.
  vector<int> pickable(const vector<int>& problems, int pattern) const {
    vector<int> inTopic;
    for (int i : problems) {
      if (subpatternOf(i, pattern) >= 0) inTopic.push_back(i);
    }
    return inTopic.empty() ? problems : inTopic;
  }

  /*
  A subpattern's pull: asked × 1 minus its working-code score × 1/2 per new
  problem of it you solved lately. One with a problem waiting is held, like a
  pattern, and pulls nothing.
  */
  double subpatternPull(int topic, int pattern) const {
    if (topicWaiting_[topic]) return 0;
    const double solved = topic < static_cast<int>(c_.profile->topics.size()) ? c_.profile->topics[topic].recentSolved : 0.0;
    return topicAsked_[topic] * (1.0 - c_.profile->topicScore(topic, pattern)) * pow(PULL_KEPT_PER_RECENT_PROBLEM, solved);
  }

  static int slowFlaw() {
    for (int f = 0; f < FLAW_COUNT; ++f) {
      if (FLAWS[f].byClock) return f;
    }
    return -1;
  }

  /*
  A new problem from `candidates`, in the three steps of DIFFICULTY in
  config.hpp: the level nearest the aim, then a subpattern of `pattern` with a
  problem left at that level (skipped when the candidates are one finer topic
  already), then the problem. Each level sits at the middle rating of the
  problems a pick can reach there. A pattern starting over takes its Easy,
  then its Medium, problems instead.
  */
  bool drawProblem(const vector<int>& candidates, int pattern, bool bySubpattern, string why, Pick* out) {
    array<vector<int>, 3> byLevel;
    for (int i : candidates) {
      if (eligibleProblem(i)) byLevel[min(3, max(1, questions_[i].level)) - 1].push_back(i);
    }
    array<double, 3> middle{};
    for (int l = 0; l < 3; ++l) middle[l] = middleRating(bySubpattern ? pickable(byLevel[l], pattern) : byLevel[l]);

    const PatternStats& stats = c_.profile->patterns[pattern];
    const string& name = resources_.patterns()[pattern].name;
    double aim = aimOf(pattern);
    int level = -1;
    if (stats.startingOver()) {
      level = nearestLevel(byLevel, stats.startOverStep == 1 ? 0 : 1);
      if (level < 0) return false;
      aim = middle[level];
      why += stats.startOverStep == 1
                 ? string(" ") + LEVEL_NAMES[level] + ": you showed zero clue on " + name + ", so it starts over."
                 : string(" ") + LEVEL_NAMES[level] + ": you found a key step yourself after starting over. One more and " +
                       name + " is back to your aim.";
    } else {
      const int slow = slowFlaw();
      const bool eased = slow >= 0 && rng_.uniform() < stats.slowShare;
      if (eased) aim += FLAWS[slow].level;
      vector<double> odds(3, 0.0);
      for (int l = 0; l < 3; ++l) {
        if (!byLevel[l].empty()) odds[l] = closeness(middle[l], aim);
      }
      level = drawShare(odds);
      if (level < 0) return false;
      why += string(" ") + LEVEL_NAMES[level] + ", aimed at " + to_string(lround(aim)) + ": your idea rating on " + name + " " +
             to_string(lround(stats.rating)) + " + " + to_string(lround(STRETCH));
      if (eased) {
        why += ", less " + to_string(lround(-FLAWS[slow].level)) + ": over the time box on " +
               to_string(stats.slowTries) + " of your " + to_string(stats.timedTries) + " timed tries here";
      }
      why += ".";
    }

    vector<int> pool = byLevel[level];
    if (bySubpattern) {
      vector<int> topics, rest;
      vector<vector<int>> members;
      for (int i : pool) {
        const int t = subpatternOf(i, pattern);
        if (t < 0) {
          rest.push_back(i);
          continue;
        }
        const auto at = find(topics.begin(), topics.end(), t);
        if (at == topics.end()) {
          topics.push_back(t);
          members.push_back({i});
        } else {
          members[at - topics.begin()].push_back(i);
        }
      }
      if (!topics.empty()) {
        vector<double> pulls;
        for (int t : topics) pulls.push_back(subpatternPull(t, pattern));
        int k = drawShare(pulls);
        if (k < 0) {   // nothing pulls, or every one is held: by how much each gets asked
          pulls.clear();
          for (int t : topics) pulls.push_back(topicAsked_[t] + 1e-9);
          k = drawShare(pulls);
        }
        pool = members[k];
        why += " Subpattern: " + resources_.topics()[topics[k]].name + ".";
      } else {
        pool = rest;
      }
    }

    vector<pair<int, double>> weighted;
    for (int i : pool) weighted.emplace_back(i, problemWeight(i, aim));
    const int pick = drawWeighted(weighted);
    if (pick < 0) return false;
    *out = {pick, Slot::Practice, why, false};
    return true;
  }

  // A pattern that has never been the main one of a problem you tried, by how much it gets asked.
  bool newPattern(Pick* out) {
    const auto& patterns = resources_.patterns();
    vector<double> weights(patterns.size(), 0.0);
    int left = 0;
    for (int p = 0; p < static_cast<int>(patterns.size()); ++p) {
      if (!patterns[p].algorithm || !untried(p) || !hasEligibleProblem(p)) continue;
      weights[p] = asked_[p] + 1e-9;
      ++left;
    }
    while (true) {
      const int chosen = drawShare(weights);
      if (chosen < 0) return false;
      const string why = "New pattern: " + patterns[chosen].name + ". It has never been the main pattern of a problem you tried, and " +
                         to_string(left) + (left == 1 ? " pattern like that is left." : " patterns like that are left.") +
                         exploring(" " + percent(NEW_PATTERN_SHARE) + " of new problems go to one until none is left.");
      if (drawProblem(patterns[chosen].problems, chosen, true, why, out)) {
        out->slot = Slot::NewPattern;
        out->explore = true;
        return true;
      }
      weights[chosen] = 0;
    }
  }

  // A finer topic with no problem graded or waiting yet, in a pattern that isn't held.
  bool newTopic(Pick* out) {
    const auto& topics = resources_.topics();
    const auto& patterns = resources_.patterns();
    vector<double> weights(topics.size(), 0.0);
    for (int t = 0; t < static_cast<int>(topics.size()); ++t) {
      const int p = topics[t].pattern;
      if (topicTried_[t] || p < 0 || p >= static_cast<int>(patterns.size()) || !patterns[p].algorithm) continue;
      if (waiting_[p] > 0 || !anyEligible(topics[t].problems)) continue;
      weights[t] = topicAsked_[t] + 1e-9;
    }
    while (true) {
      const int chosen = drawShare(weights);
      if (chosen < 0) return false;
      const Topic& topic = topics[chosen];
      const string why = "New topic: " + topic.name + ", in " + patterns[topic.pattern].name + "." +
                         exploring(" One new problem in " + to_string(NEW_GROUND_EVERY) + " goes to a topic you haven't done.");
      if (drawProblem(topic.problems, topic.pattern, false, why, out)) {
        out->slot = Slot::NewTopic;
        out->explore = true;
        return true;
      }
      weights[chosen] = 0;
    }
  }

  /*
  Why an exploring pick came: the curve's share today, or with no interview
  date, the fixed rule it would otherwise state.
  */
  string exploring(const string& fixedRule) const {
    if (c_.explore < 0) return fixedRule;
    return " " + percent(c_.explore) + " of today's new problems go to new ground, falling to " + percent(EXPLORE_END) +
           " by your interview.";
  }

  // "2.3 times" or "half", for a pull's multiplier.
  static string times(double factor) {
    const long tenths = lround(factor * 10);
    if (tenths == 5) return "half";
    return to_string(tenths / 10) + "." + to_string(tenths % 10) + " times";
  }

  /*
  What sets a pattern's pull, in words: how its working-code rating compares
  with your overall one, what is stepping it back, and the slip to watch for.
  */
  string whyPulled(int pattern) const {
    const PatternStats& stats = c_.profile->patterns[pattern];
    const long here = lround(stats.codeRating), overall = lround(c_.profile->codeRating);
    const double factor = weak(pattern);
    string why;
    if (stats.coldAttempts == 0) {
      why = " It hasn't been the main pattern of a problem you tried yet.";
    } else if (lround(factor * 10) == 10) {
      why = " Your working-code rating here, " + to_string(here) + ", is about your overall " + to_string(overall) + ".";
    } else {
      why = " Your working-code rating here is " + to_string(here) + ", " +
            to_string(here < overall ? overall - here : here - overall) + (here < overall ? " below" : " above") +
            " your overall " + to_string(overall) + ", so it comes up " + times(factor) + " as often.";
    }
    if (recentSolved(pattern) > 0) {
      why += " It is stepping back for the new problems of it you solved in the last " + to_string(RECENT_DAYS) +
             " study days.";
    }
    const int slip = mostSeenSlip(pattern);
    if (slip >= 0) why += string(" The slip that shows most here: ") + FLAWS[slip].label + ".";
    return why;
  }

  /*
  Pull: how much it gets asked, times how weak you are on it by working code,
  halved for each new problem of it you solved lately. A pattern with a
  problem waiting in your queue is held and not drawn. `triedOnly` leaves out
  patterns that have never been the main one of a problem you graded: those
  are exploring's to give. `evenIfHeld` is for when nothing else is left: then
  the patterns with the fewest problems waiting are drawn from.
  */
  bool practice(Pick* out, bool triedOnly, bool evenIfHeld = false) {
    const auto& patterns = resources_.patterns();
    vector<double> pull(patterns.size(), 0.0);
    int fewest = -1;
    for (int p = 0; p < static_cast<int>(patterns.size()); ++p) {
      if (!patterns[p].algorithm || !hasEligibleProblem(p)) continue;
      if (triedOnly && c_.profile->patterns[p].coldAttempts == 0) continue;
      if (!evenIfHeld && waiting_[p] > 0) continue;
      pull[p] = pullOf(p);
      fewest = fewest < 0 ? waiting_[p] : min(fewest, waiting_[p]);
    }
    double total = 0;
    for (int p = 0; p < static_cast<int>(patterns.size()); ++p) {
      if (waiting_[p] > fewest) pull[p] = 0;
      total += pull[p];
    }
    while (true) {
      const int chosen = drawShare(pull);
      if (chosen < 0) return false;
      string why = patterns[chosen].name + ": " + percent(pull[chosen] / total) + " of today's pull." + whyPulled(chosen);
      if (waiting_[chosen] > 0) why += " Every pattern you could get already has a problem waiting, so this is a second one here.";
      if (drawProblem(patterns[chosen].problems, chosen, true, why, out)) {
        out->slot = Slot::Practice;
        return true;
      }
      pull[chosen] = 0;
    }
  }

  // When nothing else is left: any pattern that isn't held, and only then one that is.
  bool lastResort(Pick* out) { return practice(out, false) || practice(out, false, true); }

  // ── Mock interviews ─────────────────────────────────────────────────────

  /*
  Thompson sampling. Draw a plausible mastery for each pattern from its Beta
  belief, weight by how much the pattern matters, and take the neediest.
  */
  int samplePattern() {
    int best = -1;
    double bestNeed = -1;
    const auto& patterns = resources_.patterns();
    for (int p = 0; p < static_cast<int>(patterns.size()); ++p) {
      if (!patterns[p].algorithm || !hasEligibleProblem(p)) continue;
      const PatternStats& stats = c_.profile->patterns[p];
      double need = (1.0 - rng_.beta(stats.alpha, stats.beta)) * patterns[p].importance;
      if (pickedToday_[p]) need *= 0.2;   // spread the picks of one call
      if (need > bestNeed) { bestNeed = need; best = p; }
    }
    return best;
  }

  bool problemWeak(Pick* out) {
    const int p = samplePattern();
    if (p < 0) return false;
    int best = -1;
    double bestScore = -1;
    for (int i : resources_.patterns()[p].problems) {
      if (!eligibleProblem(i)) continue;
      const double score = problemScore(i, aimOf(p));
      if (score > bestScore) { bestScore = score; best = i; }
    }
    if (best < 0) return false;
    const PatternStats& stats = c_.profile->patterns[p];
    const string& name = resources_.patterns()[p].name;
    const bool never = stats.coldAttempts == 0;
    string why = "New ground: " + name;
    if (!never) {
      why = "Weak spot: " + name + " (" + to_string(stats.coldPasses) + " of " +
            to_string(stats.coldAttempts) + " clean on new faces)";
    }
    *out = {best, Slot::Weak, why, never};
    return true;
  }

  bool problemWildcard(Pick* out) {
    /*
    Choose a pattern nothing today points to, then a good problem inside it.
    Gap fit is left out on purpose: a real interview does not tailor itself.
    */
    vector<int> open;
    double total = 0;
    const auto& patterns = resources_.patterns();
    for (int p = 0; p < static_cast<int>(patterns.size()); ++p) {
      if (patterns[p].algorithm && !inPlay_[p] && hasEligibleProblem(p)) {
        open.push_back(p);
        total += patterns[p].importance;
      }
    }
    if (open.empty()) return false;
    double roll = rng_.uniform() * total;
    int chosen = open.back();
    for (int p : open) {
      roll -= patterns[p].importance;
      if (roll <= 0) { chosen = p; break; }
    }
    int best = -1;
    double bestScore = -1;
    for (int i : patterns[chosen].problems) {
      if (!eligibleProblem(i)) continue;
      const Question& q = questions_[i];
      const double score = W_FREQUENCY * frequency(i) + (q.level == 2 ? 0.5 : 0.0) + 2 * W_NOISE * rng_.uniform();
      if (score > bestScore) { bestScore = score; best = i; }
    }
    if (best < 0) return false;
    *out = {best, Slot::Wildcard, "Wildcard: " + patterns[chosen].name + ". Nothing in today's queue points here."};
    return true;
  }

  // ── Cards ───────────────────────────────────────────────────────────────

  bool cardSibling(Pick* out) {
    // Turns a recent miss into practice on the same patterns.
    while (nextSeed_ < c_.seeds.size()) {
      const Seed seed = c_.seeds[nextSeed_++];
      const Question& from = questions_[seed.question];
      int best = -1;
      double bestScore = -1;
      for (int p : from.patterns) {
        for (int i : resources_.patterns()[p].cards) {
          if (!eligibleCard(i)) continue;
          const Question& q = questions_[i];
          const double score = cardLevelScore(q) + W_NOISE * rng_.uniform();
          if (score > bestScore) { bestScore = score; best = i; }
        }
      }
      if (best >= 0) {
        *out = {best, Slot::Sibling, "Related to " + from.title};
        return true;
      }
    }
    return false;
  }

  // The card on your weakest pattern, among the decks switched on. Easier cards first, as everywhere.
  bool cardWeak(Pick* out) {
    int best = -1, weakest = -1;
    double bestScore = -1;
    for (int i : allCards()) {
      if (!eligibleCard(i)) continue;
      const Question& q = questions_[i];
      double weakness = 0;
      int at = -1;
      for (int p : q.patterns) {
        if (at < 0 || 1.0 - mastery(p) > weakness) {
          weakness = 1.0 - mastery(p);
          at = p;
        }
      }
      const double score = 1.5 * weakness + cardLevelScore(q) + W_NOISE * rng_.uniform();
      if (score > bestScore) {
        bestScore = score;
        best = i;
        weakest = at;
      }
    }
    if (best < 0) return false;
    const int deck = resources_.deckIndex(questions_[best].deck);
    const string from = deck >= 0 ? resources_.decks()[deck].title : questions_[best].deck;
    // A pattern with no history yet isn't a weak spot, so it isn't called one.
    const bool fresh = weakest < 0 || c_.profile->patterns[weakest].attempts == 0;
    string why = "From " + from;
    if (weakest >= 0) why = string(fresh ? "New ground: " : "Weak spot: ") + resources_.patterns()[weakest].name + ", from " + from;
    *out = {best, Slot::Weak, why, fresh};
    return true;
  }

  bool cardWildcard(Pick* out) {
    vector<int> open;
    for (int d = 0; d < static_cast<int>(resources_.decks().size()); ++d) {
      for (int i : resources_.decks()[d].cards) {
        if (eligibleCard(i)) { open.push_back(d); break; }
      }
    }
    if (open.empty()) return false;
    const int deck = open[rng_.below(static_cast<int>(open.size()))];
    int best = -1;
    double bestScore = -1;
    for (int i : resources_.decks()[deck].cards) {
      if (!eligibleCard(i)) continue;
      const double score = cardLevelScore(questions_[i]) + 2 * rng_.uniform();
      if (score > bestScore) { bestScore = score; best = i; }
    }
    if (best < 0) return false;
    *out = {best, Slot::Wildcard, "Wildcard from " + resources_.decks()[deck].title};
    return true;
  }
};

}  // namespace

vector<Pick> recommend(Lane lane, int count, RecommendContext& context, Rng& rng) {
  vector<Pick> picks;
  Picker picker(context, rng);
  const int slotKinds = static_cast<int>(size(SLOT_ORDER));
  const bool queue = lane == Lane::Code && !context.mock;

  for (int i = 0; i < count; ++i) {
    Pick pick;
    bool found = queue && picker.fromQueue(i, &pick);
    /*
    A slot that cannot be filled (no seeds yet, say) falls through to the
    next kind, so a brand new user still gets a full queue.
    */
    for (int attempt = 0; attempt < slotKinds && !found; ++attempt) {
      const Slot slot = SLOT_ORDER[(context.slotOffset + i + attempt) % slotKinds];
      found = picker.pick(lane, slot, &pick);
    }
    if (!found) break;
    picker.accept(pick);
    picks.push_back(move(pick));
  }
  return picks;
}

vector<PatternOdds> patternOdds(RecommendContext context) {
  context.waiting.clear();   // standing odds, with nothing held: see PatternOdds
  Rng unused(0);             // nothing is drawn
  return Picker(context, unused).odds();
}

}  // namespace interviewprep
