#include "recommend.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

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
  const double u = std::max(uniform(), 1e-12);
  const double v = uniform();
  return std::sqrt(-2.0 * std::log(u)) * std::cos(6.283185307179586 * v);
}

double Rng::gamma(double shape) {  // Marsaglia-Tsang
  if (shape < 1.0) {
    return gamma(shape + 1.0) * std::pow(std::max(uniform(), 1e-12), 1.0 / shape);
  }
  const double d = shape - 1.0 / 3.0;
  const double c = 1.0 / std::sqrt(9.0 * d);
  while (true) {
    const double x = normal();
    double v = 1.0 + c * x;
    if (v <= 0) continue;
    v = v * v * v;
    const double u = std::max(uniform(), 1e-12);
    if (std::log(u) < 0.5 * x * x + d - d * v + d * std::log(v)) return d * v;
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
  if (need == THINKING_NEED) return "Seeing the pattern and its key step";
  return need >= 0 && need < FLAW_COUNT ? FLAWS[need].label : "";
}

namespace {

std::string percent(double share) {
  return std::to_string(static_cast<int>(std::round(share * 100))) + "%";
}

std::string lowerFirst(std::string text) {
  if (!text.empty() && text[0] >= 'A' && text[0] <= 'Z') text[0] = static_cast<char>(text[0] - 'A' + 'a');
  return text;
}

class Picker {
 public:
  Picker(RecommendContext& context, Rng& rng)
      : c_(context), rng_(rng), resources_(*context.resources), questions_(resources_.questions()),
        inPlay_(resources_.patterns().size(), 0), pickedToday_(resources_.patterns().size(), 0),
        mainToday_(resources_.patterns().size(), 0), asked_(resources_.patterns().size(), 0.0),
        recentNew_(resources_.patterns().size(), 0), topicAsked_(resources_.topics().size(), 0.0),
        topicTried_(resources_.topics().size(), 0), needShare_(context.profile->needShare()) {
    for (const Seed& seed : c_.seeds) {
      for (int p : questions_[seed.question].patterns) inPlay_[p] = 1;
    }
    for (int i = 0; i < static_cast<int>(questions_.size()); ++i) {
      const Question& q = questions_[i];
      if (q.kind != Kind::Problem) continue;
      const double asked = frequency(i);
      if (!q.patterns.empty()) asked_[q.patterns[0]] += asked;
      for (int t : q.topics) {
        topicAsked_[t] += asked;
        if ((*c_.states)[i].attempts > 0) topicTried_[t] = 1;
      }
    }
    for (size_t p = 0; p < recentNew_.size(); ++p) recentNew_[p] = c_.profile->patterns[p].recentNew;
  }

  bool pick(Lane lane, Slot slot, Pick* out) {
    switch (slot) {
      case Slot::Sibling: return lane == Lane::Code ? problemSibling(out) : cardSibling(out);
      case Slot::Weak: return lane == Lane::Code ? problemWeak(out) : cardWeak(out);
      case Slot::Wildcard: return lane == Lane::Code ? problemWildcard(out) : cardWildcard(out);
      default: break;  // the queue's own kinds are never dealt: see fromQueue
    }
    return false;
  }

  /*
  A new coding problem for the day's queue, by section 7 of config.hpp: a
  pattern never tried while there is one, then one in NEW_GROUND_EVERY from a
  finer topic never done, and the rest by pull. `position` is this pick's place
  among the day's new problems.
  */
  bool fromQueue(int position, Pick* out) {
    if (newPattern(out)) return true;
    if ((c_.slotOffset + position) % NEW_GROUND_EVERY == 0 && newTopic(out)) return true;
    return practice(out) || newTopic(out);
  }

  bool hasNeeds() const {
    return std::any_of(needShare_.begin(), needShare_.end(), [](double s) { return s > 0; });
  }

  /*
  Draws a need in proportion to how often it shows, then a random unseen card
  from the deck that drills it, leaning toward easier cards and toward the
  patterns already in today's queue. A need with no cards left gives up its
  share.
  */
  bool fromNeeds(Pick* out) {
    std::vector<double> shares(needShare_.begin(), needShare_.end());
    while (true) {
      const int need = drawShare(shares);
      if (need < 0) return false;
      const int deck = resources_.deckIndex(needDeck(need));
      std::vector<std::pair<int, double>> pool;
      if (deck >= 0) {
        for (int i : resources_.decks()[deck].cards) {
          if (!eligibleCard(i)) continue;
          const bool related = std::any_of(questions_[i].patterns.begin(), questions_[i].patterns.end(),
                                           [&](int p) { return inPlay_[p] != 0; });
          pool.emplace_back(i, cardLevelScore(questions_[i]) * (related ? 2.0 : 1.0));
        }
      }
      const int pick = drawWeighted(pool);
      if (pick >= 0) {
        *out = {pick, Slot::Weak, std::string("Drills: ") + needLabel(need) + ", " + percent(needShare_[need]) +
                                      " of what your judged code shows"};
        return true;
      }
      shares[need] = 0;
    }
  }

  void accept(const Pick& pick) {
    (*c_.taken)[pick.question] = 1;
    const Question& q = questions_[pick.question];
    for (int p : q.patterns) {
      inPlay_[p] = 1;
      pickedToday_[p] = 1;
    }
    if (q.kind == Kind::Problem) {
      if (!q.patterns.empty()) {
        mainToday_[q.patterns[0]] = 1;
        recentNew_[q.patterns[0]] += 1;
      }
      for (int t : q.topics) topicTried_[t] = 1;
    }
  }

 private:
  RecommendContext& c_;
  Rng& rng_;
  const Resources& resources_;
  const std::vector<Question>& questions_;
  std::vector<char> inPlay_;            // patterns anything in today's queue touches
  std::vector<char> pickedToday_;       // patterns already given a new face today
  std::vector<char> mainToday_;         // patterns that were the main one of a new problem today
  std::vector<double> asked_;           // how much of what gets asked has this main pattern
  std::vector<int> recentNew_;          // first tries per main pattern lately, today's picks included
  std::vector<double> topicAsked_;      // the same, per finer topic
  std::vector<char> topicTried_;        // a problem of this topic has been graded or picked
  std::array<double, NEED_COUNT> needShare_;
  size_t nextSeed_ = 0;

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

  bool anyEligible(const std::vector<int>& pool) const {
    return std::any_of(pool.begin(), pool.end(), [&](int i) { return eligibleProblem(i); });
  }

  bool hasEligibleProblem(int pattern) const { return anyEligible(resources_.patterns()[pattern].problems); }

  double mastery(int pattern) const { return c_.profile->patterns[pattern].mastery(); }

  bool untried(int pattern) const { return c_.profile->patterns[pattern].coldAttempts == 0 && !mainToday_[pattern]; }

  // ── Scoring ─────────────────────────────────────────────────────────────

  // How often it gets asked: by your target company alone when you have one.
  double frequency(int i) const { return resources_.frequency(i, c_.company); }

  // `shift` moves the target in rating points, for a flaw that asks for it.
  static double levelFit(const Question& q, double mastery, double shift = 0) {
    const double target = LEVEL_BASE + mastery * LEVEL_SPAN + shift;
    const double distance = (q.rating - target) / LEVEL_WIDTH;
    return std::exp(-0.5 * distance * distance);
  }

  double problemScore(int i, double m) {
    const Question& q = questions_[i];
    return W_FREQUENCY * frequency(i) + W_CURATED * (q.tier > 0 ? 1.0 : 0.0) + W_LEVEL_FIT * levelFit(q, m) +
           W_NOISE * rng_.uniform();
  }

  // How likely a problem is to be drawn once its pattern or topic is chosen.
  double problemWeight(int i, double mastery, double shift) const {
    const Question& q = questions_[i];
    return (PROBLEM_WEIGHT_FLOOR + frequency(i)) * (PROBLEM_WEIGHT_FLOOR + levelFit(q, mastery, shift)) *
           (q.tier > 0 ? CURATED_BOOST : 1.0);
  }

  // An index in proportion to the shares, skipping zeros, or -1 when all are zero.
  int drawShare(const std::vector<double>& shares) {
    double total = 0;
    for (double share : shares) total += std::max(0.0, share);
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
  int drawWeighted(const std::vector<std::pair<int, double>>& pool) {
    std::vector<double> weights;
    weights.reserve(pool.size());
    for (const auto& entry : pool) weights.push_back(entry.second);
    const int k = drawShare(weights);
    return k < 0 ? -1 : pool[k].first;
  }

  static double cardLevelScore(const Question& q) {
    return q.level == 1 ? 1.0 : q.level == 2 ? 0.6 : 0.2;
  }

  // ── The queue's new problems ────────────────────────────────────────────

  /*
  The flaw a new problem should build, drawn in proportion to how often each
  need shows, as a shift of the difficulty target. Most leave it alone.
  Returns -1 when nothing moved.
  */
  int aimFor(double* shift) {
    *shift = 0;
    const int need = drawShare(std::vector<double>(needShare_.begin(), needShare_.end()));
    if (need < 0 || need >= FLAW_COUNT || FLAWS[need].level == 0) return -1;
    *shift = FLAWS[need].level;
    return need;
  }

  /*
  Draws from a pool of problems, weighted by how often each is asked and its
  level. A pattern's first problem aims as if you knew none of it.
  */
  bool drawProblem(const std::vector<int>& candidates, int pattern, bool firstMeeting, std::string why, Pick* out) {
    double shift = 0;
    const int aim = aimFor(&shift);
    const double m = firstMeeting ? 0.0 : mastery(pattern);
    std::vector<std::pair<int, double>> pool;
    for (int i : candidates) {
      if (eligibleProblem(i)) pool.emplace_back(i, problemWeight(i, m, shift));
    }
    const int pick = drawWeighted(pool);
    if (pick < 0) return false;
    if (aim >= 0) {
      why += std::string(shift < 0 ? " Aimed easier than usual, for: " : " Aimed harder than usual, for: ") +
             lowerFirst(FLAWS[aim].label) + ".";
    }
    *out = {pick, Slot::Practice, why, false};
    return true;
  }

  // Every pattern gets one first try before anything is ranked.
  bool newPattern(Pick* out) {
    const auto& patterns = resources_.patterns();
    std::vector<double> weights(patterns.size(), 0.0);
    int left = 0;
    for (int p = 0; p < static_cast<int>(patterns.size()); ++p) {
      if (!patterns[p].algorithm || !untried(p) || !hasEligibleProblem(p)) continue;
      weights[p] = asked_[p] + 1e-9;
      ++left;
    }
    while (true) {
      const int chosen = drawShare(weights);
      if (chosen < 0) return false;
      const std::string why = "New pattern: " + patterns[chosen].name + ", one of " + std::to_string(left) +
                              " you haven't tried. Every pattern gets a first try before the rest.";
      if (drawProblem(patterns[chosen].problems, chosen, true, why, out)) {
        out->slot = Slot::NewPattern;
        out->explore = true;
        return true;
      }
      weights[chosen] = 0;
    }
  }

  // A finer topic with no graded or picked problem yet.
  bool newTopic(Pick* out) {
    const auto& topics = resources_.topics();
    const auto& patterns = resources_.patterns();
    std::vector<double> weights(topics.size(), 0.0);
    for (int t = 0; t < static_cast<int>(topics.size()); ++t) {
      const int p = topics[t].pattern;
      if (topicTried_[t] || p < 0 || p >= static_cast<int>(patterns.size()) || !patterns[p].algorithm) continue;
      if (!anyEligible(topics[t].problems)) continue;
      weights[t] = topicAsked_[t] + 1e-9;
    }
    while (true) {
      const int chosen = drawShare(weights);
      if (chosen < 0) return false;
      const Topic& topic = topics[chosen];
      const std::string why = "New topic: " + topic.name + ", in " + patterns[topic.pattern].name + ". One new problem in " +
                              std::to_string(NEW_GROUND_EVERY) + " goes to a topic you haven't done.";
      if (drawProblem(topic.problems, topic.pattern, false, why, out)) {
        out->slot = Slot::NewTopic;
        out->explore = true;
        return true;
      }
      weights[chosen] = 0;
    }
  }

  static std::string thisWeek(int count) {
    if (count == 0) return "none of it in the last " + std::to_string(RECENT_DAYS) + " study days";
    return std::to_string(count) + (count == 1 ? " new one" : " new ones") + " in the last " +
           std::to_string(RECENT_DAYS) + " study days";
  }

  /*
  Pull: how much it gets asked, times how far you are from it, halved for each
  new problem of it lately.
  */
  bool practice(Pick* out) {
    const auto& patterns = resources_.patterns();
    std::vector<double> pull(patterns.size(), 0.0);
    double total = 0;
    for (int p = 0; p < static_cast<int>(patterns.size()); ++p) {
      if (!patterns[p].algorithm || !hasEligibleProblem(p)) continue;
      pull[p] = asked_[p] * (1.0 - mastery(p)) * std::pow(PULL_KEPT_PER_RECENT_PROBLEM, recentNew_[p]);
      total += pull[p];
    }
    while (true) {
      const int chosen = drawShare(pull);
      if (chosen < 0) return false;
      const std::string why = patterns[chosen].name + ": " + percent(pull[chosen] / total) +
                              " of today's pull. Thinking score " + percent(mastery(chosen)) + ", " +
                              thisWeek(recentNew_[chosen]) + ".";
      if (drawProblem(patterns[chosen].problems, chosen, false, why, out)) {
        out->slot = Slot::Practice;
        return true;
      }
      pull[chosen] = 0;
    }
  }

  // ── Mock interviews ─────────────────────────────────────────────────────

  bool problemSibling(Pick* out) {
    while (nextSeed_ < c_.seeds.size()) {
      const int seed = c_.seeds[nextSeed_++].question;
      int best = -1;
      double bestScore = -1;
      const auto& patterns = questions_[seed].patterns;
      for (size_t k = 0; k < patterns.size() && k < 2; ++k) {
        for (int i : resources_.patterns()[patterns[k]].problems) {
          if (!eligibleProblem(i)) continue;
          const double score = W_SIMILARITY * resources_.similarity(seed, i) + problemScore(i, mastery(patterns[k]));
          if (score > bestScore) { bestScore = score; best = i; }
        }
      }
      if (best >= 0) {
        *out = {best, Slot::Sibling, "Same pattern as " + questions_[seed].title};
        return true;
      }
    }
    return false;
  }

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
      const double score = problemScore(i, mastery(p));
      if (score > bestScore) { bestScore = score; best = i; }
    }
    if (best < 0) return false;
    const PatternStats& stats = c_.profile->patterns[p];
    const std::string& name = resources_.patterns()[p].name;
    const bool never = stats.coldAttempts == 0;
    std::string why = "New ground: " + name;
    if (!never) {
      why = "Weak spot: " + name + " (" + std::to_string(stats.coldPasses) + " of " +
            std::to_string(stats.coldAttempts) + " clean on new faces)";
    }
    *out = {best, Slot::Weak, why, never};
    return true;
  }

  bool problemWildcard(Pick* out) {
    /*
    Choose a pattern nothing today points to, then a good problem inside it.
    Gap fit is left out on purpose: a real interview does not tailor itself.
    */
    std::vector<int> open;
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

  bool cardWeak(Pick* out) {
    // Draw a need in proportion to how often it shows, then a card from its deck.
    std::vector<double> shares;
    for (double s : needShare_) shares.push_back(s + 0.05);
    const int need = std::max(0, drawShare(shares));
    const int deck = resources_.deckIndex(needDeck(need));
    if (deck < 0) return false;
    int best = -1;
    double bestScore = -1;
    for (int i : resources_.decks()[deck].cards) {
      if (!eligibleCard(i)) continue;
      const Question& q = questions_[i];
      double weakness = 0;
      for (int p : q.patterns) weakness = std::max(weakness, 1.0 - mastery(p));
      const double score = 1.5 * weakness + cardLevelScore(q) + W_NOISE * rng_.uniform();
      if (score > bestScore) { bestScore = score; best = i; }
    }
    if (best < 0) return false;
    const bool evidence = hasNeeds();
    const std::string why = evidence ? std::string("Drills: ") + needLabel(need) : "From " + resources_.decks()[deck].title;
    *out = {best, Slot::Weak, why, !evidence};
    return true;
  }

  bool cardWildcard(Pick* out) {
    std::vector<int> open;
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

std::vector<Pick> recommend(Lane lane, int count, RecommendContext& context, Rng& rng) {
  std::vector<Pick> picks;
  Picker picker(context, rng);
  const int slotKinds = static_cast<int>(std::size(SLOT_ORDER));
  const bool queue = lane == Lane::Code && !context.mock;
  const bool needs = lane == Lane::Cards && picker.hasNeeds();

  for (int i = 0; i < count; ++i) {
    Pick pick;
    bool found = queue && picker.fromQueue(i, &pick);
    // Every other new card follows your needs; the rest keep every deck in play.
    if (!found && needs && (context.slotOffset + i) % FLAW_CARD_EVERY == 0) found = picker.fromNeeds(&pick);
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
    picks.push_back(std::move(pick));
  }
  return picks;
}

}  // namespace interviewprep
