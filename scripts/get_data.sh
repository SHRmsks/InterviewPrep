#!/bin/bash
# Refreshes the LeetCode labels into data/raw/. Run this when you want new
# problems or fresher company frequencies, then run `make resources`.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p data/raw/lc-mini data/raw/rating data/raw/companies
curl -fsSL -o data/raw/lc-mini/leetcode_questions_mini.json \
  https://raw.githubusercontent.com/noworneverev/leetcode-api/main/data/leetcode_questions_mini.json
curl -fsSL -o data/raw/rating/data.json \
  https://raw.githubusercontent.com/zerotrac/leetcode_problem_rating/main/data.json
curl -fsSL -o data/raw/companies/repo.tar.gz \
  https://codeload.github.com/liquidslr/leetcode-company-wise-problems/tar.gz/refs/heads/main
tar -xzf data/raw/companies/repo.tar.gz -C data/raw/companies --strip-components=1 --include='*.csv'
rm data/raw/companies/repo.tar.gz
echo "Done. Now run: make resources"
