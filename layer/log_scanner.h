// Copyright 2020 Google LLC
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

#ifndef STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_LOG_SCANNER_H_
#define STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_LOG_SCANNER_H_

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"

namespace performancelayers {

// Scans files looking for registered patterns. Assumes that log files will
// be appended to, but not modified in the middle.
class LogScanner {
 public:
  // Opens |filename| and returns a LogScanner object on success,
  // or std::nullopt on failure.
  static std::optional<LogScanner> FromFilename(const std::string& filename);

  // Reads the underlying log file until the end. Checks if any of the
  // registered patterns appear within the lines read.
  // Returns true if any register pattern was found for the first time.
  bool ConsumeNewLines();

  // Registers |pattern| as a watched pattern. No-op if pattern has
  // already been registered.
  // Patterns are matched using exact (full) maching only.
  void RegisterWatchedPattern(const std::string& pattern) {
    pattern_to_line_num_.try_emplace(pattern, 0);
  }

  // Returns the first line where |pattern| was seen, or 0 if not seen yet.
  uint64_t GetFirstOccurrenceLineNum(const std::string& pattern) const;

  using PatternLineNumPair = std::pair<std::string, uint64_t>;
  // Returns all seen patterns, sorted by the line of occurrence in asc. order.
  std::vector<PatternLineNumPair> GetSeenPatterns() const;

 private:
  LogScanner(std::ifstream file) : file_(std::move(file)) {}
  std::ifstream file_;
  uint64_t current_line_num_ = 0;
  absl::flat_hash_map<std::string, uint64_t> pattern_to_line_num_;
};
}  // namespace performancelayers

#endif  // STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_LOG_SCANNER_H_
