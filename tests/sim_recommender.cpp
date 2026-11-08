/*
sim_recommender.cpp — what do the recommender's weights actually do?

Simulates a learner on the real resources and reports where the new faces
went. The learner is strong everywhere except a few weak patterns, and tags
most misses "Knew it, couldn't code it". Change a weight in core/config.hpp,
run `make sim`, and compare.
*/

#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../core/engine.hpp"

using namespace interviewprep;

static const int kDays = 90;
static const double kStrong = 0.80;   // chance of a clean first pass on a strong pattern
static const double kWeak = 0.25;     // chance on a weak pattern
static const double kLearning = 0.20; // each repeat of the same problem adds this much

static bool isWeak(const std::string& pattern) {
  return pattern.rfind("dp-", 0) == 0 || pattern == "monotonic-stack";
}

static Json ask(Engine& engine, const Json& request) {
  return Json::parse(engine.call(request.dump()));
}

static std::string nowOn(int date) { return formatDate(date) + "T20:00"; }

int main(int argc, char** argv) {
  const std::string resourcesPath = argc > 1 ? argv[1] : "data/resources.json";
  std::ifstream in(resourcesPath, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();

  Engine engine;
  std::string error;
  if (engine.loadResources(text.str(), &error) < 0) {
    std::fprintf(stderr, "Can't load %s: %s\n", resourcesPath.c_str(), error.c_str());
    return 1;
  }
  engine.loadState(R"({"settings":{"company":"google"}})");

  Rng rng(2026);
  int date = parseDate("2026-11-07");
  int picksTotal = 0, picksWeak = 0;
  std::map<std::string, int> bySlot, weakBySlot;
  std::map<std::string, int> byPattern;
  std::map<int, int> byLevel;
  std::set<std::string> seenIds;
  int duplicates = 0;
  int maxOverflow = 0, totalReviews = 0;
  std::vector<int> weakShareByMonth(4, 0), totalByMonth(4, 0);

  for (int day = 0; day < kDays; ++day, ++date) {
    const std::string now = nowOn(date);
    Json today = ask(engine, Json::object().set("cmd", "today").set("now", now));
    maxOverflow = std::max(maxOverflow, today["code"]["overflow"].asInt());

    for (const Json& item : today["code"]["items"].items()) {
      const std::string& id = item["id"].asStr();
      const std::string& slot = item["slot"].asStr();
      Json card = ask(engine, Json::object().set("cmd", "card").set("id", id).set("now", now))["card"];
      const auto& patterns = card["patterns"].items();
      const std::string main = patterns.empty() ? "" : patterns[0]["slug"].asStr();
      const bool weak = isWeak(main);

      if (slot == "review") {
        ++totalReviews;
      } else {
        if (!seenIds.insert(id).second) ++duplicates;
        ++picksTotal;
        picksWeak += weak;
        ++bySlot[slot];
        weakBySlot[slot] += weak;
        ++byPattern[main];
        ++byLevel[card["level"].asStr() == "Easy" ? 1 : card["level"].asStr() == "Hard" ? 3 : 2];
        const int month = std::min(3, day / 30);
        ++totalByMonth[month];
        weakShareByMonth[month] += weak;
      }

      const double chance = std::min(0.95, (weak ? kWeak : kStrong) + kLearning * card["attempts"].asInt());
      const bool pass = rng.uniform() < chance;
      Json grade = Json::object().set("cmd", "grade").set("id", id).set("now", now);
      if (pass) {
        grade.set("grade", "b");
      } else {
        grade.set("grade", rng.uniform() < 0.5 ? "y" : "r");
        Json gaps = Json::array();
        gaps.push(rng.uniform() < 0.7 ? "impl" : "recog");
        grade.set("gaps", gaps);
      }
      ask(engine, grade);
    }
    for (const Json& item : today["cards"]["items"].items()) {
      ask(engine, Json::object().set("cmd", "grade").set("id", item["id"].asStr()).set("now", now)
                      .set("grade", rng.uniform() < 0.8 ? "b" : "r"));
    }
  }

  // The base rate: how much of the catalog is weak patterns at all.
  Json library = ask(engine, Json::object().set("cmd", "library").set("kind", "problem").set("limit", 1).set("now", nowOn(date)));
  const int catalog = library["total"].asInt();
  int catalogWeak = 0;
  Json facets = ask(engine, Json::object().set("cmd", "facets").set("now", nowOn(date)));
  std::set<std::string> counted;
  for (const Json& p : facets["patterns"].items()) {
    if (!isWeak(p["slug"].asStr())) continue;
    for (int offset = 0;; offset += 500) {
      Json page = ask(engine, Json::object().set("cmd", "library").set("kind", "problem").set("pattern", p["slug"].asStr())
                                  .set("limit", 500).set("offset", offset).set("now", nowOn(date)));
      for (const Json& row : page["rows"].items()) counted.insert(row["id"].asStr());
      if (static_cast<int>(page["rows"].size()) < 500) break;
    }
  }
  catalogWeak = static_cast<int>(counted.size());

  std::printf("%d study days, %d new faces, %d reviews, %d duplicate picks, worst overflow %d\n",
              kDays, picksTotal, totalReviews, duplicates, maxOverflow);
  std::printf("Weak patterns are %.0f%% of the catalog and got %.0f%% of new faces\n",
              100.0 * catalogWeak / catalog, 100.0 * picksWeak / std::max(1, picksTotal));
  std::printf("\nBy month, share of new faces on weak patterns:");
  for (int m = 0; m < 3; ++m) {
    std::printf("  %.0f%%", 100.0 * weakShareByMonth[m] / std::max(1, totalByMonth[m]));
  }
  std::printf("\n\nBy slot\n");
  for (const auto& s : bySlot) {
    std::printf("  %-10s %4d   %3.0f%% on weak patterns\n", s.first.c_str(), s.second,
                100.0 * weakBySlot[s.first] / std::max(1, s.second));
  }
  std::printf("\nBy difficulty: Easy %d, Medium %d, Hard %d\n", byLevel[1], byLevel[2], byLevel[3]);

  std::vector<std::pair<int, std::string>> top;
  for (const auto& p : byPattern) top.emplace_back(-p.second, p.first);
  std::sort(top.begin(), top.end());
  std::printf("\nMost picked patterns\n");
  for (size_t i = 0; i < top.size() && i < 10; ++i) {
    std::printf("  %-24s %4d%s\n", top[i].second.c_str(), -top[i].first, isWeak(top[i].second) ? "  (weak)" : "");
  }

  Json insights = ask(engine, Json::object().set("cmd", "insights").set("now", nowOn(date)));
  std::printf("\nWhat the app concluded\n");
  for (const Json& note : insights["notes"].items()) std::printf("  - %s\n", note["text"].asStr().c_str());
  return 0;
}
