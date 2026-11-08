# Deck file format

One JSON file per deck, in `data/decks/<deck>.json`. These files are the source of truth for every
non-LeetCode card in the app. `scripts/build_bank.py` validates them and merges them into the bank.

```json
{
  "deck": "signals",
  "title": "Pattern signals",
  "cards": [
    {
      "id": "signals-001",
      "type": "flash",
      "q": "Front of the card.",
      "qcode": "optional code shown on the front",
      "a": "Back of the card.",
      "code": "optional code shown on the back",
      "patterns": ["two-pointers"],
      "skill": "recog",
      "level": 2
    }
  ]
}
```

## Fields

| Field      | Required | Meaning |
|------------|----------|---------|
| `id`       | yes | `<deck>-<3 digit number>`, unique, never reused |
| `type`     | yes | `flash` = flip the card. `drill` = the user types code from memory and the app diffs it against `code` |
| `q`        | yes | Front. At most 2 sentences. Plain text; `backticks` for inline code |
| `qcode`    | no  | Code block on the front (spot-the-bug snippets) |
| `a`        | yes | Back. At most 3 sentences. Plain text; `backticks` for inline code |
| `code`     | drill: yes, flash: no | Reference code on the back. Drills are compared to it token by token |
| `patterns` | yes | 0 to 3 slugs from the pattern list below. Empty only for cards with no algorithm pattern |
| `skill`    | yes | The gap this card trains, from the skill list below |
| `level`    | yes | 1 = must know cold, 2 = standard interview depth, 3 = deep or senior |

## Skills (what a miss on this card says about the person)

| Slug      | Meaning |
|-----------|---------|
| `recog`   | Sees which pattern a problem needs |
| `idea`    | Knows the key step or invariant inside a pattern |
| `impl`    | Turns the idea into correct code |
| `edge`    | Finds bugs and edge cases |
| `syntax`  | Recalls APIs and language syntax without autocomplete |
| `explain` | States complexity and explains trade-offs out loud |

## Pattern slugs

Algorithm patterns:

`arrays-hashing`, `two-pointers`, `sliding-window`, `prefix-sum`, `binary-search`,
`binary-search-answer`, `sorting`, `intervals`, `sweep-line`, `stack`, `monotonic-stack`,
`monotonic-queue`, `linked-list`, `tree-dfs`, `tree-bfs`, `bst`, `trie`, `heap`, `graph-traversal`,
`grid`, `topo-sort`, `union-find`, `shortest-path`, `mst`, `backtracking`, `dp-linear`, `dp-grid`,
`dp-string`, `dp-knapsack`, `dp-interval`, `dp-tree`, `dp-bitmask`, `dp-state-machine`, `greedy`,
`bit-manipulation`, `math`, `string-matching`, `range-query`, `divide-and-conquer`, `design`,
`simulation`

Non-algorithm topics:

`complexity`, `cpp`, `os`, `concurrency`, `networking`, `databases`, `system-design`

## Writing rules

- Write every card yourself. Never copy a LeetCode statement, a textbook, a website or a flashcard set.
- Be correct first. If you are not sure a fact holds, leave the card out. A wrong flash card teaches a wrong fact.
- Short and concrete. No filler such as "great question", no preamble, no em-dash asides, no marketing tone.
- One idea per card. Split a card that tests two things.
- The front must not give away the answer. A `signals` card must not contain the pattern's name.
- C++ means C++17, `using namespace std;` assumed, standard library only.
- All code must compile. Verify it with `clang++ -std=c++17 -fsyntax-only` (wrapped in a throwaway harness outside `data/decks/` if the snippet is a fragment) before you finish.
- Validate your JSON: `python3 -c "import json,sys; json.load(open(sys.argv[1]))" file.json`.
