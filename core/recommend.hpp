/*
recommend.hpp — picks the new faces.

Runs on local labels, your own history and the coach's labels on it, by the
rules in section 7 of config.hpp. Nothing is trained.
*/
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "resources.hpp"
#include "profile.hpp"

namespace interviewprep {

enum class Lane { Code, Cards };

/*
Deterministic random numbers, so a day's picks are reproducible and the same
seed gives the same result natively and in WebAssembly.
*/
class Rng {
 public:
  explicit Rng(uint64_t seed) : state_(seed) {}
  uint64_t next();            // SplitMix64
  double uniform();           // [0, 1)
  double normal();            // standard normal
  double gamma(double shape);
  double beta(double a, double b);
  int below(int n);           // [0, n)

 private:
  uint64_t state_;
};

// A question that points at the next new face: something due, or a recent miss.
struct Seed {
  int question = -1;
};

struct Pick {
  int question = -1;
  Slot slot = Slot::Wildcard;
  std::string why;            // shown only after the attempt, it names the pattern
  bool explore = false;       // landed on a pattern with no history yet

  /*
  The badge: the slot's name, except that a Sibling, Weak or Wildcard pick
  landing on a pattern you have no history with says "explore". Calling that a
  weak spot would be untrue.
  */
  const char* badge() const;
};

struct RecommendContext {
  const Resources* resources = nullptr;
  const std::vector<CardState>* states = nullptr;
  const Profile* profile = nullptr;
  std::vector<Seed> seeds;
  std::vector<char>* taken = nullptr;  // questions out of the running; picks are added
  std::vector<char> deckOff;           // per Resources::decks() index
  bool includePremium = false;
  int company = -1;                    // Resources::companies() index, or -1 for any
  int slotOffset = 0;                  // rotates the slot order day to day
  bool mock = false;                   // a mock interview: Weak and Wildcard only, nothing tailored
};

std::vector<Pick> recommend(Lane lane, int count, RecommendContext& context, Rng& rng);

// "sibling", "weak", "wildcard", "new-pattern", "new-topic", "practice"
const char* slotName(Slot slot);

// What a need is called: a flaw's label, or the thinking side.
const char* needLabel(int need);

}  // namespace interviewprep
