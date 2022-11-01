// Copyright 2022 Google LLC
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

#ifndef STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_CSV_LOGGING_H_
#define STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_CSV_LOGGING_H_

#include <cassert>
#include <string>

#include "event_logging.h"
#include "layer_utils.h"

namespace performancelayers {
std::string ValueToCSVString(const std::string &value);

std::string ValueToCSVString(const int64_t value);

std::string ValueToCSVString(const std::vector<int64_t> &values);

// Converts a `DurationClock::duration` to its nanoseconds representation.
std::string ValueToCSVString(DurationClock::duration value);

// Takes an `Event` instance as an input and generates a csv string containing
// `event`'s name and attribute values. The duration values will be logged in
// nanoseconds.
// TODO(miladhakimi): Differentiate hashes and other integers. Hashes
// should be displayed in hex.
std::string EventToCSVString(Event &event);

// CSVLogger logs the events in the CSV format to the output given in its
// constructor.
// There is no need to add '\n' at the end of the csv_header in the constructor.
// This is handled by the implementation. `filename` can be nullptr. In this
// case, the output will be written to stderr.
// The only valid methods after calling `EndLog()` is `EndLog()`.
class CSVLogger : public EventLogger {
 public:
  CSVLogger(const char *csv_header, const char *filename);

  void AddEvent(Event *event) override {
    assert(out_);
    std::string event_str = EventToCSVString(*event);
    WriteLnAndFlush(out_, event_str);
  }

  // Writes the CSV header given in the constructor to the output.
  void StartLog() override {
    assert(out_);
    WriteLnAndFlush(out_, header_);
  }

  void EndLog() override {
    if (out_ && out_ != stderr) {
      fclose(out_);
      out_ = nullptr;
    }
  }

  void Flush() override {
    assert(out_);
    fflush(out_);
  }

 private:
  FILE *out_ = nullptr;
  const char *header_ = nullptr;
};

}  // namespace performancelayers

#endif  // STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_CSV_LOGGING_H_
