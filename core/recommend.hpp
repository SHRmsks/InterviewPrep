/*
recommend.hpp — picks the new faces.

Runs on local labels, your own history and the coach's labels on it, by the
rules in section 7 of config.hpp. The rules are set by hand. The only numbers
fitted to you are your two ratings (profile.hpp), and the picker reads both.
*/
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "resources.hpp"
#include "profile.hpp"

using namespace std;

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
  string why;            // shown only after the attempt, it names the pattern
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
  const vector<CardState>* states = nullptr;
  const Profile* profile = nullptr;
  vector<Seed> seeds;
  vector<char>* taken = nullptr;  // questions out of the running; picks are added
  vector<char> deckOff;           // per Resources::decks() index
  bool includePremium = false;
  int company = -1;                    // Resources::companies() index, or -1 for any
  int slotOffset = 0;                  // rotates the slot order day to day
  bool mock = false;                   // a mock interview: Weak and Wildcard only, nothing tailored
  double explore = -1;                 // today's share of new problems that explore; -1 with no interview date
  /*
  Problems graded so far plus the new ones waiting, so exploring picks stay
  evenly spaced across days and from one pull to the next.
  */
  int newSoFar = 0;
  /*
  New faces already in today's queue and not graded yet. Each holds its place:
  its pattern and finer topics aren't picked again until it is graded or
  hidden. Being picked is not a result, so nothing else about it counts.
  */
  vector<int> waiting;
};

vector<Pick> recommend(Lane lane, int count, RecommendContext& context, Rng& rng);

/*
How new problems are shared among the patterns, for showing and never for
picking. A pattern you have tried gets its share of the pull, and one you
haven't gets its share of the new-pattern picks, so each of the two groups
adds up to 1.

The hold of a problem waiting in today's queue is left out on purpose. This
says how often a pattern comes up, so it gives away nothing about which
patterns are sitting in your queue, where a pattern stays hidden until you
have tried the problem.
*/
struct PatternOdds {
  int pattern = -1;
  bool isNew = false;    // never the main pattern of a problem you tried
  double share = 0;      // of its group: the pull, or the new-pattern picks
  double weak = 1;       // the pull's factor for working code
  double recent = 1;     // the pull's factor for new problems of it you solved lately
  string why;            // what sets the pull, in the picker's own words. Empty for a new pattern.
};

vector<PatternOdds> patternOdds(RecommendContext context);

/*
The share of new problems that explore, on the cosine from your first practice
day to the interview (section 7 of config.hpp). Calendar dates.
*/
double exploreShare(int firstDate, int date, int interviewDate);

/*
Whether new problem number `slot` (counting every new problem you have had)
explores at this share: a running count adds the share for each one, and each
time it passes a whole number that one explores. No coin flip, evenly spaced.
*/
bool exploresAt(long slot, double share);

// "sibling", "weak", "wildcard", "new-pattern", "new-topic", "practice"
const char* slotName(Slot slot);

// What a need is called: a flaw's label, or the thinking side.
const char* needLabel(int need);

}  // namespace interviewprep
