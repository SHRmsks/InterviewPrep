#include "schedule.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace interviewprep {

/*
── Calendar helpers ──────────────────────────────────────────────────────
Howard Hinnant's civil-date algorithms. They need no time zone database, so
they give the same answer natively and in WebAssembly.
*/

int dateFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

std::string formatDate(int date) {
  int z = date + 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int y = static_cast<int>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp + (mp < 10 ? 3 : -9);
  y += m <= 2;
  char buf[16];
  std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", y, m, d);
  return buf;
}

static bool digits(const std::string& s, size_t from, size_t count, int* out) {
  if (from + count > s.size()) return false;
  int v = 0;
  for (size_t i = from; i < from + count; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (s[i] - '0');
  }
  *out = v;
  return true;
}

int parseDate(const std::string& s) {
  int y = 0, m = 0, d = 0;
  if (s.size() < 10 || s[4] != '-' || s[7] != '-') return -1;
  if (!digits(s, 0, 4, &y) || !digits(s, 5, 2, &m) || !digits(s, 8, 2, &d)) return -1;
  if (m < 1 || m > 12 || d < 1 || d > 31) return -1;
  return dateFromCivil(y, m, d);
}

int dateFromNow(const std::string& now) {
  int date = parseDate(now);
  if (date < 0) return -1;
  int hour = 0;
  if (now.size() >= 13 && now[10] == 'T' && digits(now, 11, 2, &hour) && hour < DAY_ROLLOVER_HOUR) {
    date -= 1;
  }
  return date;
}

// ── One card ──────────────────────────────────────────────────────────────

const Schedule PROBLEM_SCHEDULE = {
    PROBLEM_RULES,
    {nullptr, PROBLEM_RED_CHECKS, PROBLEM_YELLOW_CHECKS, PROBLEM_BLUE_CHECKS, nullptr},
    {0, static_cast<int>(std::size(PROBLEM_RED_CHECKS)), static_cast<int>(std::size(PROBLEM_YELLOW_CHECKS)),
     static_cast<int>(std::size(PROBLEM_BLUE_CHECKS)), 0},
    false,
};

const Schedule CARD_SCHEDULE = {
    CARD_RULES,
    {nullptr, CARD_RED_CHECKS, CARD_YELLOW_CHECKS, CARD_BLUE_CHECKS, nullptr},
    {0, static_cast<int>(std::size(CARD_RED_CHECKS)), static_cast<int>(std::size(CARD_YELLOW_CHECKS)),
     static_cast<int>(std::size(CARD_BLUE_CHECKS)), 0},
    true,
};

int checkCount(const Schedule& schedule, Track track) { return schedule.sizes[static_cast<int>(track)]; }

Gap checkGap(const Schedule& schedule, Track track, int step) {
  const int t = static_cast<int>(track);
  if (step < 0 || step >= schedule.sizes[t]) return {0, 0};
  return schedule.checks[t][step];
}

// FNV-1a, which gives the same number natively and in WebAssembly.
uint64_t seedOf(const std::string& id) {
  uint64_t hash = 0xCBF29CE484222325ull;
  for (unsigned char c : id) {
    hash ^= c;
    hash *= 0x100000001B3ull;
  }
  return hash;
}

/*
0 to `spread` extra days, drawn from the question and how many grades it has
had, so each check gets its own draw and replaying the log gives it again.
*/
static int extraDays(uint64_t seed, int attempts, int spread) {
  if (spread <= 0) return 0;
  uint64_t z = seed + 0x9E3779B97F4A7C15ull * static_cast<uint64_t>(attempts + 1);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  z ^= z >> 31;
  return static_cast<int>(z % static_cast<uint64_t>(spread + 1));
}

CardState applyClick(CardState s, Click click, int day, const Schedule& schedule, uint64_t seed) {
  switch (schedule.rules[static_cast<int>(s.track)][static_cast<int>(click)]) {
    case Move::Advance:
      s.step += 1;
      if (s.step >= checkCount(schedule, s.track)) {
        /*
        Past its last check a problem is gone. A card's Red and Yellow earn their
        first real Blue, and a card's Blue has done its checks, so it retires.
        */
        s.track = schedule.blueAfterLast && s.track != Track::Blue ? Track::Blue : Track::Retired;
        s.step = 0;
      }
      break;
    case Move::Repeat:
      break;
    case Move::ToRed:
      s.track = Track::Red;
      s.step = 0;
      break;
    case Move::ToYellow:
      s.track = Track::Yellow;
      s.step = 0;
      break;
    case Move::ToBlue:
      s.track = Track::Blue;
      s.step = 0;
      break;
    case Move::Stay:
      break;
  }
  s.attempts += 1;
  s.lastDay = day;
  s.lastClick = click;
  /*
  The gap counts from `day`, the day this was really done, not from s.due.
  That is what makes a late review harmless.
  */
  if (s.track == Track::Retired) {
    s.due = 0;
  } else {
    const Gap gap = checkGap(schedule, s.track, s.step);
    s.due = day + gap.base + extraDays(seed, s.attempts, gap.spread);
  }
  return s;
}

const char* trackName(Track track) {
  switch (track) {
    case Track::New: return "new";
    case Track::Red: return "red";
    case Track::Yellow: return "yellow";
    case Track::Blue: return "blue";
    case Track::Retired: return "retired";
  }
  return "new";
}

const char* clickName(Click click) {
  switch (click) {
    case Click::Blue: return "b";
    case Click::Yellow: return "y";
    case Click::Red: return "r";
  }
  return "r";
}

bool parseClick(const std::string& text, Click* out) {
  if (text == "b" || text == "B" || text == "blue") { *out = Click::Blue; return true; }
  if (text == "y" || text == "Y" || text == "yellow") { *out = Click::Yellow; return true; }
  if (text == "r" || text == "R" || text == "red") { *out = Click::Red; return true; }
  return false;
}

const char* moveName(Move move) {
  switch (move) {
    case Move::Advance: return "Advance";
    case Move::Repeat: return "Repeat";
    case Move::ToRed: return "ToRed";
    case Move::ToYellow: return "ToYellow";
    case Move::ToBlue: return "ToBlue";
    case Move::Stay: return "Stay";
  }
  return "";
}

// ── The clock ─────────────────────────────────────────────────────────────

void Clock::markActive(int date) {
  auto it = std::lower_bound(active_.begin(), active_.end(), date);
  if (it == active_.end() || *it != date) active_.insert(it, date);
}

int Clock::day(int date) const {
  if (mode_ == ClockMode::CalendarDays) return date;
  /*
  The number of study days before this date, plus one. A date with no clicks
  never takes a number, so a skipped day simply does not exist. Today gets
  the next free number whether or not it has clicks yet.
  */
  auto it = std::lower_bound(active_.begin(), active_.end(), date);
  return static_cast<int>(it - active_.begin()) + 1;
}

// ── Queue order ───────────────────────────────────────────────────────────

bool isDue(const CardState& s, int today) {
  if (s.track == Track::New || s.track == Track::Retired) return false;
  return s.due <= today;
}

int queuePriority(const CardState& s, int today) {
  int base = PRIORITY_BLUE;
  if (s.track == Track::Red) base = PRIORITY_RED;
  else if (s.track == Track::Yellow) base = PRIORITY_YELLOW;
  return base + AGING_PER_DAY * std::max(0, today - s.due);
}

bool servedBefore(const CardState& a, const CardState& b, int today) {
  int pa = queuePriority(a, today), pb = queuePriority(b, today);
  if (pa != pb) return pa > pb;
  return a.due < b.due;
}

}  // namespace interviewprep
