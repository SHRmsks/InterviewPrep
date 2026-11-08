#!/usr/bin/env python3
"""Builds data/resources.json: every coding problem and card, with labels.

Inputs
  data/raw/lc-mini/leetcode_questions_mini.json   every LeetCode problem and its topics
  data/raw/rating/data.json                       contest ratings
  data/raw/companies/<Company>/*.csv              how often each company asks what
  data/curated.json                               your hand-picked list
  data/problem_notes.json                         key idea, pitfall and exact patterns
  data/topics.json                                finer topics under the patterns
  data/topic_labels.json                          which finer topics each problem uses
  data/decks/*.json                               trivia cards and drills

Run with: make resources

All of this is labels only. No problem statements are stored, because the app
links out to LeetCode for those.
"""

import csv
import glob
import json
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data")

# ── The label vocabulary ───────────────────────────────────────────────────

ALGORITHM_PATTERNS = [
    ("arrays-hashing", "Arrays and hashing"), ("two-pointers", "Two pointers"),
    ("sliding-window", "Sliding window"), ("prefix-sum", "Prefix sum"),
    ("binary-search", "Binary search"), ("binary-search-answer", "Binary search on the answer"),
    ("sorting", "Sorting"), ("intervals", "Intervals"), ("sweep-line", "Sweep line"),
    ("stack", "Stack"), ("monotonic-stack", "Monotonic stack"), ("monotonic-queue", "Monotonic queue"),
    ("linked-list", "Linked list"), ("tree-dfs", "Tree DFS"), ("tree-bfs", "Tree BFS"),
    ("bst", "Binary search tree"), ("trie", "Trie"), ("heap", "Heap"),
    ("graph-traversal", "Graph traversal"), ("grid", "Grid search"), ("topo-sort", "Topological sort"),
    ("union-find", "Union-find"), ("shortest-path", "Shortest path"), ("mst", "Minimum spanning tree"),
    ("backtracking", "Backtracking"), ("dp-linear", "DP, linear"), ("dp-grid", "DP on a grid"),
    ("dp-string", "DP on strings"), ("dp-knapsack", "DP, knapsack"), ("dp-interval", "DP on intervals"),
    ("dp-tree", "DP on trees"), ("dp-bitmask", "DP with bitmasks"), ("dp-state-machine", "DP, state machine"),
    ("greedy", "Greedy"), ("bit-manipulation", "Bit manipulation"), ("math", "Math"),
    ("string-matching", "String matching"), ("range-query", "Range queries"),
    ("divide-and-conquer", "Divide and conquer"), ("design", "Design a class"), ("simulation", "Simulation"),
]

TOPIC_PATTERNS = [
    ("complexity", "Complexity"), ("cpp", "C++"), ("os", "Operating systems"),
    ("concurrency", "Concurrency"), ("networking", "Networking"), ("databases", "Databases"),
    ("system-design", "System design"),
]

# Order here is the order decks appear in the apps.
DECK_ORDER = ["signals", "templates", "stl", "bugs", "edges", "complexity", "cpp", "os",
              "concurrency", "networking", "databases", "system-design"]

# Companies carried into the resources, as (folder name, slug).
COMPANIES = [
    ("Google", "google"), ("Meta", "meta"), ("Amazon", "amazon"), ("Microsoft", "microsoft"),
    ("Apple", "apple"), ("Bloomberg", "bloomberg"), ("Uber", "uber"), ("TikTok", "tiktok"),
    ("ByteDance", "bytedance"), ("LinkedIn", "linkedin"), ("Nvidia", "nvidia"), ("Oracle", "oracle"),
    ("Adobe", "adobe"), ("Goldman Sachs", "goldman-sachs"), ("Airbnb", "airbnb"), ("Citadel", "citadel"),
]

# ── From LeetCode topics to patterns ───────────────────────────────────────
# Tried top to bottom, most specific first. A problem keeps the first three
# patterns that match, and the first one is its main pattern.


def has(tags, *names):
    return any(n in tags for n in names)


def derive_patterns(title, tags):
    t = title.lower()
    dp = has(tags, "Dynamic Programming", "Memoization", "Knapsack Problem", "0-1 Knapsack", "Complete Knapsack",
             "Multiple Knapsack", "Mixed Knapsack", "DP on Trees", "Longest Increasing Subsequence",
             "Longest Common Subsequence")
    tree = has(tags, "Tree", "Binary Tree", "Binary Search Tree", "Lowest Common Ancestor")
    graph = has(tags, "Graph Theory", "Directed Acyclic Graph", "Bipartite Graph", "Graph Coloring",
                "Strongly Connected Component", "Biconnected Component", "Bridge (Graph)", "Articulation Point",
                "Eulerian Circuit", "Eulerian Path", "Eulerian Graph", "Semi-Eulerian Graph", "Hamiltonian Path",
                "Flow Network", "Maximum Flow", "Minimum Cut", "Matching (Graph)", "Maximum Matching",
                "Perfect Matching", "Planar Graph", "Kosaraju's Algorithm", "Tarjan's SCC Algorithm")
    search = has(tags, "Breadth-First Search", "Depth-First Search", "0-1 BFS", "Bidirectional Search", "A* Search",
                 "Heuristic Search")

    rules = [
        (has(tags, "Monotonic Queue"), "monotonic-queue"),
        (has(tags, "Monotonic Stack", "Cartesian Tree"), "monotonic-stack"),
        (has(tags, "Topological Sort"), "topo-sort"),
        (has(tags, "Union-Find"), "union-find"),
        (has(tags, "Shortest Path", "Dijkstra's Algorithm", "Bellman–Ford Algorithm", "Floyd–Warshall Algorithm",
             "0-1 BFS", "K Shortest Path", "A* Search"), "shortest-path"),
        (has(tags, "Minimum Spanning Tree", "Prim's Algorithm", "Kruskal's Algorithm", "Borůvka's Algorithm"), "mst"),
        (has(tags, "Trie", "Aho–Corasick Algorithm", "Suffix Automaton", "Suffix Tree"), "trie"),
        (has(tags, "Segment Tree", "Binary Indexed Tree", "Ordered Set", "Sqrt Decomposition", "Sparse Table",
             "Range Minimum/Maximum Query", "Persistent Data Structure", "Treap", "Splay Tree", "Li Chao Tree",
             "K-D Tree"), "range-query"),
        (has(tags, "Sweep Line"), "sweep-line"),
        ("interval" in t or "meeting" in t or "overlapping" in t, "intervals"),
        (dp and has(tags, "Bitmask"), "dp-bitmask"),
        (dp and has(tags, "DP on Trees") or (dp and tree), "dp-tree"),
        (has(tags, "Knapsack Problem", "0-1 Knapsack", "Complete Knapsack", "Multiple Knapsack", "Mixed Knapsack"),
         "dp-knapsack"),
        (dp and "stock" in t, "dp-state-machine"),
        (dp and has(tags, "Matrix"), "dp-grid"),
        (has(tags, "Longest Common Subsequence") or (dp and has(tags, "String")), "dp-string"),
        (dp, "dp-linear"),
        (has(tags, "String Matching", "Knuth–Morris–Pratt Algorithm", "Rolling Hash", "Z Algorithm", "Suffix Array",
             "Hash Function", "Manacher", "Boyer–Moore String-Search Algorithm", "Palindromic Tree",
             "Lyndon Factorization"), "string-matching"),
        (has(tags, "Sliding Window"), "sliding-window"),
        (has(tags, "Binary Search") and not has(tags, "Binary Search Tree") and "sorted" not in t and
         re.search(r"\b(minimi[sz]e|maximi[sz]e|minimum|maximum|smallest|largest|kth)\b", t), "binary-search-answer"),
        (has(tags, "Binary Search", "Ternary Search"), "binary-search"),
        (has(tags, "Two Pointers", "Floyd's Cycle Finding Algorithm"), "two-pointers"),
        (has(tags, "Prefix Sum"), "prefix-sum"),
        (has(tags, "Backtracking", "Dancing Links", "Algorithm X"), "backtracking"),
        (has(tags, "Heap", "Heap (Priority Queue)"), "heap"),
        (has(tags, "Stack", "Bracket Sequences"), "stack"),
        (has(tags, "Linked List", "Doubly-Linked List"), "linked-list"),
        (has(tags, "Binary Search Tree"), "bst"),
        (tree and has(tags, "Breadth-First Search") and not has(tags, "Depth-First Search"), "tree-bfs"),
        (tree, "tree-dfs"),
        (search and has(tags, "Matrix"), "grid"),
        (graph or search, "graph-traversal"),
        (has(tags, "Design", "Data Stream", "Iterator"), "design"),
        (has(tags, "Greedy", "Pigeonhole Principle"), "greedy"),
        (has(tags, "Bit Manipulation", "Bitmask"), "bit-manipulation"),
        (has(tags, "Divide and Conquer", "Merge Sort", "Quickselect", "Quicksort", "Meet in the Middle"),
         "divide-and-conquer"),
        (has(tags, "Simulation", "Interactive"), "simulation"),
        (has(tags, "Math", "Number Theory", "Combinatorics", "Geometry", "Game Theory", "Probability and Statistics",
             "Greatest Common Divisor", "Least Common Multiple", "Brainteaser", "Randomized", "Prime Factorization",
             "Sieve Theory", "Linear Algebra", "Minimax", "Inclusion-Exclusion Principle"), "math"),
        (has(tags, "Sorting", "Sort", "Counting Sort", "Bucket Sort", "Radix Sort"), "sorting"),
        (has(tags, "Concurrency"), "concurrency"),
    ]
    patterns = []
    for matched, pattern in rules:
        if matched and pattern not in patterns:
            patterns.append(pattern)
    # dp-tree also carries a plain tree label, and so on, but three is plenty.
    patterns = patterns[:3]
    return patterns or ["arrays-hashing"]


# ── What each pattern demands ──────────────────────────────────────────────
# How hard a problem of this pattern leans on four skills, 0 to 9:
#   recog  seeing which pattern it is
#   idea   finding the key insight once you see it
#   impl   turning it into correct code
#   edge   bugs and edge cases
# This is how "you can explain it but you can't code it" turns into picks:
# an implementation gap steers toward high impl, a recognition gap toward high recog.

DEMAND = {
    "arrays-hashing": (3, 3, 3, 3), "two-pointers": (4, 4, 3, 5), "sliding-window": (5, 4, 5, 5),
    "prefix-sum": (6, 5, 3, 4), "binary-search": (4, 3, 4, 7), "binary-search-answer": (7, 5, 4, 6),
    "sorting": (2, 3, 3, 3), "intervals": (3, 4, 5, 6), "sweep-line": (6, 5, 6, 5),
    "stack": (4, 4, 5, 5), "monotonic-stack": (7, 6, 5, 5), "monotonic-queue": (7, 6, 6, 5),
    "linked-list": (2, 3, 8, 7), "tree-dfs": (3, 4, 5, 4), "tree-bfs": (2, 3, 4, 3), "bst": (3, 4, 5, 5),
    "trie": (4, 3, 7, 4), "heap": (3, 3, 5, 3), "graph-traversal": (4, 3, 6, 5), "grid": (3, 3, 6, 6),
    "topo-sort": (5, 3, 5, 4), "union-find": (5, 4, 5, 3), "shortest-path": (5, 4, 6, 4), "mst": (5, 4, 6, 3),
    "backtracking": (4, 4, 7, 6), "dp-linear": (6, 7, 4, 5), "dp-grid": (4, 5, 5, 5), "dp-string": (5, 6, 5, 5),
    "dp-knapsack": (7, 6, 5, 5), "dp-interval": (7, 8, 6, 5), "dp-tree": (6, 7, 6, 5), "dp-bitmask": (6, 7, 7, 5),
    "dp-state-machine": (6, 7, 5, 5), "greedy": (7, 8, 3, 5), "bit-manipulation": (5, 7, 3, 5), "math": (5, 8, 3, 7),
    "string-matching": (6, 6, 7, 5), "range-query": (6, 6, 9, 6), "divide-and-conquer": (6, 7, 6, 5),
    "design": (3, 4, 9, 6), "simulation": (1, 2, 9, 8), "concurrency": (4, 5, 7, 7),
}


def demand_string(patterns, difficulty):
    weights = [1.0, 0.5, 0.25]
    total = [0.0, 0.0, 0.0, 0.0]
    weight_sum = 0.0
    for pattern, w in zip(patterns, weights):
        vector = DEMAND.get(pattern, (4, 4, 4, 4))
        for i in range(4):
            total[i] += vector[i] * w
        weight_sum += w
    bump = {1: -1.0, 2: 0.0, 3: 1.0}[difficulty]
    digits = []
    for i in range(4):
        value = total[i] / weight_sum
        if i < 2:
            value += bump  # harder problems hide the pattern and the insight deeper
        digits.append(str(int(max(0, min(9, round(value))))))
    return "".join(digits)


# ── Loaders ────────────────────────────────────────────────────────────────

def slugify(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower().replace("'", "")).strip("-")


def read_json(*parts):
    with open(os.path.join(DATA, *parts), encoding="utf8") as f:
        return json.load(f)


def load_company_frequency():
    """Returns ({slug: {company: frequency}}, {slug: overall_score})."""
    per_problem = {}
    overall = {}
    folders = sorted(glob.glob(os.path.join(DATA, "raw", "companies", "*")))
    kept = dict(COMPANIES)
    for folder in folders:
        name = os.path.basename(folder)
        rows = {}
        # Recent data counts fully, all-time data counts half.
        for filename, scale in (("5. All.csv", 0.5), ("3. Six Months.csv", 1.0)):
            path = os.path.join(folder, filename)
            if not os.path.exists(path):
                continue
            with open(path, encoding="utf8", newline="") as f:
                for row in csv.DictReader(f):
                    link = (row.get("Link") or "").rstrip("/")
                    if not link:
                        continue
                    try:
                        frequency = float(row.get("Frequency") or 0)
                    except ValueError:
                        continue
                    slug = link.rsplit("/", 1)[-1]
                    rows[slug] = max(rows.get(slug, 0.0), frequency * scale)
        # A company with thousands of reports says more than one with ten.
        company_weight = math.log1p(len(rows))
        for slug, frequency in rows.items():
            overall[slug] = overall.get(slug, 0.0) + frequency * company_weight
            if name in kept and frequency >= 1:
                per_problem.setdefault(slug, {})[kept[name]] = round(frequency, 1)
    return per_problem, overall


def percentile_ranks(scores):
    """Turns raw scores into 0 to 100 so one giant value does not squash the rest."""
    ordered = sorted(scores.values())
    if not ordered:
        return {}
    position = {}
    for i, value in enumerate(ordered):
        position[value] = i
    n = max(1, len(ordered) - 1)
    return {slug: round(100.0 * position[value] / n, 1) for slug, value in scores.items()}


def fit_rating_model(problems, ratings):
    """A line per difficulty: rating from acceptance rate, for unrated problems."""
    samples = {1: [], 2: [], 3: []}
    for p in problems:
        number = p["number"]
        if number in ratings and p["ac"] is not None:
            samples[p["difficulty"]].append((p["ac"], ratings[number]))
    model = {}
    for difficulty, points in samples.items():
        n = len(points)
        mean_x = sum(x for x, _ in points) / n
        mean_y = sum(y for _, y in points) / n
        variance = sum((x - mean_x) ** 2 for x, _ in points)
        covariance = sum((x - mean_x) * (y - mean_y) for x, y in points)
        slope = covariance / variance if variance else 0.0
        model[difficulty] = (mean_x, mean_y, slope)
    return model


def estimate_rating(model, difficulty, ac):
    mean_x, mean_y, slope = model[difficulty]
    x = mean_x if ac is None else ac
    return mean_y + slope * (x - mean_x)


# ── Decks ──────────────────────────────────────────────────────────────────

def load_decks(pattern_index):
    decks = []
    cards = []
    seen_ids = set()
    problems = []
    skills = {"recog", "idea", "impl", "edge", "syntax", "speed", "explain"}
    for slug in DECK_ORDER:
        path = os.path.join(DATA, "decks", slug + ".json")
        if not os.path.exists(path):
            print(f"  deck missing, skipped: {slug}")
            continue
        with open(path, encoding="utf8") as f:
            deck = json.load(f)
        decks.append({"slug": slug, "title": deck.get("title", slug)})
        for card in deck["cards"]:
            card_id = card.get("id", "?")
            if card_id in seen_ids:
                problems.append(f"{card_id}: duplicate id")
            seen_ids.add(card_id)
            for field in ("q", "a", "type", "skill"):
                if not card.get(field):
                    problems.append(f"{card_id}: missing {field}")
            if card.get("type") not in ("flash", "drill"):
                problems.append(f"{card_id}: bad type")
            if card.get("type") == "drill" and not card.get("code"):
                problems.append(f"{card_id}: drill with no code")
            if card.get("skill") not in skills:
                problems.append(f"{card_id}: bad skill {card.get('skill')}")
            indexes = []
            for pattern in card.get("patterns", []):
                if pattern not in pattern_index:
                    problems.append(f"{card_id}: unknown pattern {pattern}")
                else:
                    indexes.append(pattern_index[pattern])
            item = {"id": card_id, "type": card["type"], "deck": slug, "q": card["q"], "a": card["a"],
                    "patterns": indexes, "skill": card["skill"], "level": card.get("level", 2)}
            for optional in ("qcode", "code", "bug"):
                if card.get(optional):
                    item[optional] = card[optional]
            cards.append(item)
    return decks, cards, problems


# ── Finer topics ───────────────────────────────────────────────────────────

def load_topics(pattern_index, algorithm):
    """Finer topics, and which of them each problem uses.

    A model labeled the most asked problems once, from their titles, with one
    or two labels each, primary first. A label is a finer topic or a plain
    pattern that has none. Problems it did not recognize stay at pattern level.
    """
    errors = []
    if not os.path.exists(os.path.join(DATA, "topics.json")):
        return [], {}, {}, errors
    topics = []
    for t in read_json("topics.json")["topics"]:
        parent = t["slug"].split("/")[0]
        if parent not in algorithm:
            errors.append(f"topics.json: {t['slug']} is under an unknown pattern")
            continue
        topics.append({"slug": t["slug"], "name": t["name"], "signal": t["signal"],
                       "pattern": pattern_index[parent]})
    topic_index = {t["slug"]: i for i, t in enumerate(topics)}

    labels = {}
    if os.path.exists(os.path.join(DATA, "topic_labels.json")):
        for number, names in read_json("topic_labels.json").items():
            for name in names:
                if name not in topic_index and name not in algorithm:
                    errors.append(f"topic_labels.json: problem {number} uses unknown label {name}")
            labels[int(number)] = [n for n in names if n in topic_index or n in algorithm]
    return topics, topic_index, labels, errors


# ── Main ───────────────────────────────────────────────────────────────────

def main():
    all_patterns = ALGORITHM_PATTERNS + TOPIC_PATTERNS
    pattern_index = {slug: i for i, (slug, _) in enumerate(all_patterns)}
    algorithm = {slug for slug, _ in ALGORITHM_PATTERNS}

    catalog = read_json("raw", "lc-mini", "leetcode_questions_mini.json")
    ratings = {int(r["ID"]): r["Rating"] for r in read_json("raw", "rating", "data.json")}
    curated = {c["id"]: c for c in read_json("curated.json")}
    for c in list(curated.values()):   # a Premium problem's free twin joins the list
        if c.get("twin"):
            curated.setdefault(c["twin"], {"tier": c["tier"]})
    notes_path = os.path.join(DATA, "problem_notes.json")
    notes = read_json("problem_notes.json") if os.path.exists(notes_path) else {}
    topics, topic_index, topic_labels, topic_errors = load_topics(pattern_index, algorithm)
    company_frequency, overall = load_company_frequency()
    frequency = percentile_ranks(overall)

    rows = []
    for item in catalog:
        if item["categoryTitle"] not in ("Algorithms", "Concurrency"):
            continue  # SQL, shell, pandas and JavaScript exercises are a different skill
        stats = json.loads(item["stats"]) if isinstance(item.get("stats"), str) else {}
        ac_text = (stats.get("acRate") or "").rstrip("%")
        rows.append({
            "number": int(item["questionFrontendId"]),
            "title": item["title"],
            "slug": item["url"].rstrip("/").rsplit("/", 1)[-1],
            "difficulty": {"Easy": 1, "Medium": 2, "Hard": 3}[item["difficulty"]],
            "ac": float(ac_text) if ac_text else None,
            "premium": bool(item["isPaidOnly"]),
            "topics": [t["name"] for t in item["topicTags"]],
        })

    model = fit_rating_model(rows, ratings)

    tag_names = sorted({slugify(t) for row in rows for t in row["topics"]})
    tag_index = {t: i for i, t in enumerate(tag_names)}
    company_index = {slug: i for i, (_, slug) in enumerate(COMPANIES)}

    problems = []
    unknown_notes = []
    for row in sorted(rows, key=lambda r: r["number"]):
        number = row["number"]
        note = notes.get(str(number))
        patterns = derive_patterns(row["title"], set(row["topics"]))
        exact = []
        if note:
            exact = [p for p in note.get("patterns", []) if p in algorithm]
            unknown_notes.extend(p for p in note.get("patterns", []) if p not in algorithm)
        labels = topic_labels.get(number, [])
        # Hand labels go first, then the patterns above the finer topics a model
        # named, which beat patterns guessed from tags. Derived ones stay behind
        # them for similarity.
        lead = exact + [label.split("/")[0] for label in labels]
        patterns = list(dict.fromkeys(lead + patterns))[:3]

        rating = ratings.get(number)
        if rating is None:
            rating = estimate_rating(model, row["difficulty"], row["ac"])

        record = {
            "n": number,
            "t": row["title"],
            "s": row["slug"],
            "d": row["difficulty"],
            "r": int(round(rating)),
            "f": frequency.get(row["slug"], 0),
            "p": [pattern_index[p] for p in patterns],
            "g": [tag_index[slugify(t)] for t in row["topics"]],
            "m": demand_string(patterns, row["difficulty"]),
        }
        if row["premium"]:
            record["x"] = 1
        if number in curated:
            record["k"] = curated[number]["tier"]
        finer = [topic_index[label] for label in labels if label in topic_index]
        if finer:
            record["u"] = finer
        companies = company_frequency.get(row["slug"], {})
        if companies:
            record["c"] = [[company_index[c], v] for c, v in sorted(companies.items(), key=lambda kv: company_index[kv[0]])]
        if note:
            record["o"] = [note.get("idea", ""), note.get("pitfall", ""), note.get("time", ""), note.get("space", "")]
        problems.append(record)

    decks, cards, deck_problems = load_decks(pattern_index)
    missing = set(curated) - {p["n"] for p in problems}
    errors = list(deck_problems) + topic_errors
    if unknown_notes:
        errors.append(f"problem_notes.json uses unknown patterns: {sorted(set(unknown_notes))}")
    if missing:
        errors.append(f"curated problems not in the catalog: {sorted(missing)}")
    if errors:
        print("Resources have problems:")
        for e in errors[:40]:
            print("  " + e)
        sys.exit(1)

    resources = {
        "patterns": [{"slug": s, "name": n, "algo": s in algorithm} for s, n in all_patterns],
        "topics": topics,
        "tags": tag_names,
        "companies": [{"slug": slug, "name": name} for name, slug in COMPANIES],
        "decks": decks,
        "problems": problems,
        "cards": cards,
    }
    out = os.path.join(DATA, "resources.json")
    with open(out, "w", encoding="utf8") as f:
        json.dump(resources, f, ensure_ascii=False, separators=(",", ":"))

    size = os.path.getsize(out) / 1e6
    rated = sum(1 for p in problems if p["n"] in ratings)
    noted = sum(1 for p in problems if "o" in p)
    finer = sum(1 for p in problems if "u" in p)
    print(f"{len(problems)} problems ({rated} with a real contest rating, {noted} with notes, "
          f"{finer} with finer topics from {len(topics)}), "
          f"{len(cards)} cards in {len(decks)} decks, {size:.2f} MB")


if __name__ == "__main__":
    main()
