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

#ifndef STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_COMMON_LOGGING_H_
#define STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_COMMON_LOGGING_H_

#include <cassert>
#include <cstdio>
#include <string>

#include "layer/support/event_logging.h"
#include "layer/support/layer_utils.h"
#include "log_output.h"

namespace performancelayers {
// Converts `event` to a string with the common log format. The common log
// format looks like this:
// `event_name,attribute1_name:attribute1_value,attribute2_name:attribute2_value,...`
std::string EventToCommonLogStr(Event &event);

// CommonLogger logs the events in the common log. The only valid method after
// calling `EndLog()` is `EndLog()`.
class CommonLogger : public EventLogger {
 public:
  CommonLogger(LogOutput *out) : out_(out){};

  void AddEvent(Event *event) override {
    assert(out_);
    std::string event_str = EventToCommonLogStr(*event);
    out_->LogLine(event_str);
  }

  void StartLog() override {}

  void EndLog() override {}

  void Flush() override {}

 private:
  LogOutput *out_ = nullptr;
};

}  // namespace performancelayers

#endif  // STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_COMMON_LOGGING_H_
