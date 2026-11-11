#include "resources.hpp"

#include <algorithm>
#include <cmath>

#include "json.hpp"

using namespace std;

namespace interviewprep {

int Resources::load(string_view text, string* error) {
  string parseError;
  Json root = Json::parse(text, &parseError);
  if (root.isNull()) {
    if (error) *error = "resources are not valid JSON: " + parseError;
    return -1;
  }

  *this = Resources();

  for (const Json& p : root["patterns"].items()) {
    Pattern pattern;
    pattern.slug = p["slug"].asStr();
    pattern.name = p["name"].asStr();
    pattern.algorithm = p["algo"].asBool(true);
    patterns_.push_back(move(pattern));
  }
  for (const Json& t : root["topics"].items()) {
    Topic topic;
    topic.slug = t["slug"].asStr();
    topic.name = t["name"].asStr();
    topic.signal = t["signal"].asStr();
    topic.pattern = t["pattern"].asInt(-1);
    if (topic.pattern < 0 || topic.pattern >= static_cast<int>(patterns_.size())) continue;
    patterns_[topic.pattern].hasTopics = true;
    topics_.push_back(move(topic));
  }
  for (const Json& t : root["tags"].items()) tags_.push_back(t.asStr());
  for (const Json& c : root["companies"].items()) companies_.push_back({c["slug"].asStr(), c["name"].asStr()});
  for (const Json& d : root["decks"].items()) {
    Deck deck;
    deck.slug = d["slug"].asStr();
    deck.title = d["title"].asStr();
    decks_.push_back(move(deck));
  }

  // Problems use short keys because there are thousands of them.
  for (const Json& j : root["problems"].items()) {
    Question q;
    q.kind = Kind::Problem;
    q.deck = "leetcode";
    q.number = j["n"].asInt();
    q.id = "lc" + to_string(q.number);
    q.title = j["t"].asStr();
    q.slug = j["s"].asStr();
    q.level = j["d"].asInt(2);
    q.rating = j["r"].asInt();
    q.freq = j["f"].asNum();
    q.premium = j["x"].asInt() != 0;
    q.tier = j["k"].asInt();
    for (const Json& v : j["p"].items()) q.patterns.push_back(v.asInt());
    for (const Json& v : j["u"].items()) {
      const int topic = v.asInt(-1);
      if (topic >= 0 && topic < static_cast<int>(topics_.size())) q.topics.push_back(topic);
    }
    for (const Json& v : j["g"].items()) q.tags.push_back(v.asInt());
    sort(q.tags.begin(), q.tags.end());
    for (const Json& v : j["c"].items()) {
      const auto& pair = v.items();
      if (pair.size() == 2) q.companies.emplace_back(pair[0].asInt(), static_cast<float>(pair[1].asNum()));
    }
    const string& demand = j["m"].asStr();
    for (size_t k = 0; k < demand.size() && k < DEMAND_COUNT; ++k) {
      q.demand[k] = static_cast<float>(demand[k] - '0') / 9.0f;
    }
    if (j.has("o")) {
      const auto& note = j["o"].items();  // [idea, pitfall, time, space]
      if (note.size() == 4) {
        q.idea = note[0].asStr();
        q.pitfall = note[1].asStr();
        q.time = note[2].asStr();
        q.space = note[3].asStr();
      }
    }
    questions_.push_back(move(q));
  }

  for (const Json& j : root["cards"].items()) {
    Question q;
    q.id = j["id"].asStr();
    q.kind = j["type"].asStr() == "drill" ? Kind::Drill : Kind::Flash;
    q.deck = j["deck"].asStr();
    q.title = j["q"].asStr();
    q.qcode = j["qcode"].asStr();
    q.answer = j["a"].asStr();
    q.code = j["code"].asStr();
    q.bug = j["bug"].asStr();
    q.skill = j["skill"].asStr();
    q.level = j["level"].asInt(2);
    for (const Json& v : j["patterns"].items()) q.patterns.push_back(v.asInt());
    questions_.push_back(move(q));
  }

  // Everything after this point may index patterns_ by these values.
  const int patternCount = static_cast<int>(patterns_.size());
  for (Question& q : questions_) {
    q.patterns.erase(remove_if(q.patterns.begin(), q.patterns.end(),
                                    [&](int p) { return p < 0 || p >= patternCount; }),
                     q.patterns.end());
  }

  byId_.reserve(questions_.size());
  double maxShare = 0;
  vector<double> share(patterns_.size(), 0.0);
  for (int i = 0; i < static_cast<int>(questions_.size()); ++i) {
    Question& q = questions_[i];
    byId_[q.id] = i;
    if (q.kind == Kind::Problem) {
      if (!q.patterns.empty()) {
        patterns_[q.patterns[0]].problems.push_back(i);
        share[q.patterns[0]] += q.freq;
      }
      for (int p : q.patterns) patterns_[p].anyProblems.push_back(i);
      for (int t : q.topics) topics_[t].problems.push_back(i);
    } else {
      for (int p : q.patterns) patterns_[p].cards.push_back(i);
      int d = deckIndex(q.deck);
      if (d >= 0) decks_[d].cards.push_back(i);
    }
  }

  /*
  Square root so that a very common pattern matters more than a rare one
  without drowning it out.
  */
  for (double s : share) maxShare = max(maxShare, s);
  if (maxShare > 0) {
    for (size_t p = 0; p < patterns_.size(); ++p) {
      patterns_[p].importance = 0.3 + 0.7 * sqrt(share[p] / maxShare);
    }
  }

  buildFeatures();
  return static_cast<int>(questions_.size());
}

void Resources::buildFeatures() {
  // Labels live in one id space: tags first, then patterns.
  const int tagCount = static_cast<int>(tags_.size());
  const int featureCount = tagCount + static_cast<int>(patterns_.size());
  vector<int> docFreq(featureCount, 0);
  auto forEachFeature = [&](const Question& q, auto&& fn) {
    for (int t : q.tags) fn(t);
    for (int p : q.patterns) fn(tagCount + p);
  };

  for (const Question& q : questions_) {
    forEachFeature(q, [&](int f) { if (f < featureCount) ++docFreq[f]; });
  }

  const double n = static_cast<double>(questions_.size()) + 1.0;
  features_.resize(questions_.size());
  norms_.assign(questions_.size(), 0.f);
  for (size_t i = 0; i < questions_.size(); ++i) {
    auto& list = features_[i];
    forEachFeature(questions_[i], [&](int f) {
      if (f >= featureCount) return;
      double idf = log(n / (1.0 + docFreq[f]));
      // A pattern is a stronger statement than a topic tag.
      if (f >= tagCount) idf *= 1.5;
      list.push_back({f, static_cast<float>(idf)});
    });
    sort(list.begin(), list.end(), [](const Feature& a, const Feature& b) { return a.id < b.id; });
    double sum = 0;
    for (const Feature& f : list) sum += f.weight * f.weight;
    norms_[i] = static_cast<float>(sqrt(sum));
  }
}

double Resources::similarity(int a, int b) const {
  const auto& x = features_[a];
  const auto& y = features_[b];
  if (norms_[a] == 0 || norms_[b] == 0) return 0;
  double dot = 0;
  size_t i = 0, j = 0;
  while (i < x.size() && j < y.size()) {
    if (x[i].id < y[j].id) ++i;
    else if (x[i].id > y[j].id) ++j;
    else { dot += x[i].weight * y[j].weight; ++i; ++j; }
  }
  return dot / (norms_[a] * norms_[b]);
}

double Resources::frequency(int question, int company) const {
  const Question& q = questions_[question];
  if (company < 0) return q.freq / 100.0;
  for (const auto& c : q.companies) {
    if (c.first == company) return c.second / 100.0;
  }
  return 0;
}

int Resources::indexOf(const string& id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? -1 : it->second;
}

int Resources::patternIndex(const string& slug) const {
  for (size_t i = 0; i < patterns_.size(); ++i) {
    if (patterns_[i].slug == slug) return static_cast<int>(i);
  }
  return -1;
}

int Resources::topicIndex(const string& slug) const {
  for (size_t i = 0; i < topics_.size(); ++i) {
    if (topics_[i].slug == slug) return static_cast<int>(i);
  }
  return -1;
}

int Resources::deckIndex(const string& slug) const {
  for (size_t i = 0; i < decks_.size(); ++i) {
    if (decks_[i].slug == slug) return static_cast<int>(i);
  }
  return -1;
}

int Resources::companyIndex(const string& slug) const {
  for (size_t i = 0; i < companies_.size(); ++i) {
    if (companies_[i].slug == slug) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace interviewprep
