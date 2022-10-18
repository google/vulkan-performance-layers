// Copyright 2020-2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "log_scanner.h"

#include <algorithm>
#include <cassert>

#include "absl/strings/match.h"
#include "debug_logging.h"

namespace performancelayers {

std::optional<LogScanner> LogScanner::FromFilename(
    const std::string& filename) {
  std::ifstream file(filename);
  if (!file.good()) {
    SPL_LOG(ERROR) << "Failed to open " << filename;
    return std::nullopt;
  }

  return LogScanner(std::move(file));
}

bool LogScanner::ConsumeNewLines() {
  if (!file_.is_open() || pattern_to_line_num_.empty()) return false;

  bool new_patterns_found = false;
  std::string buffer;
  while (std::getline(file_, buffer)) {
    ++current_line_num_;

    for (auto& pattern_to_line_pair : pattern_to_line_num_)
      if (pattern_to_line_pair.second == 0 &&
          absl::StrContains(buffer, pattern_to_line_pair.first)) {
        pattern_to_line_pair.second = current_line_num_;
        new_patterns_found = true;
      }
  }

  if (file_.eof()) file_.clear();

  return new_patterns_found;
}

uint64_t LogScanner::GetFirstOccurrenceLineNum(
    const std::string& pattern) const {
  if (auto it = pattern_to_line_num_.find(pattern);
      it != pattern_to_line_num_.end())
    return it->second;
  return 0;
}

std::vector<LogScanner::PatternLineNumPair> LogScanner::GetSeenPatterns()
    const {
  std::vector<PatternLineNumPair> seen_patterns;
  for (const auto& pattern_line_num_pair : pattern_to_line_num_)
    if (pattern_line_num_pair.second > 0)
      seen_patterns.push_back(pattern_line_num_pair);

  std::sort(seen_patterns.begin(), seen_patterns.end(),
            [](const auto& LHS, const auto& RHS) {
              return std::tie(LHS.second, LHS.first) <
                     std::tie(RHS.second, RHS.first);
            });
  return seen_patterns;
}

}  // namespace performancelayers
