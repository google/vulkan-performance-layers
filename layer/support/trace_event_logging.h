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

#ifndef STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_TRACE_EVENT_LOGGING_H_
#define STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_TRACE_EVENT_LOGGING_H_

#include <string>
#include <vector>

#include "layer/support/event_logging.h"
#include "layer/support/log_output.h"

namespace performancelayers {
// Converts an `Event` containing `TraceEventAttr` to a JSON string in the Trace
// Event format.
std::string EventToTraceEventString(Event &event);

// Logs the events in the Trace Event format to the output given in its
// constructor. The only valid method after calling `EndLog()` is `EndLog()`.
class TraceEventLogger : public EventLogger {
 public:
  TraceEventLogger(LogOutput *out) : out_(out) { assert(out); }

  void AddEvent(Event *event) override {
    if (!event->GetAttribute<TraceEventAttr>()) return;
    std::string event_str = EventToTraceEventString(*event);
    out_->LogLine(event_str);
  }

  // Writes '[', indicating the start point of the JSON array.
  void StartLog() override { out_->LogLine("["); }

  // The ']' at the end of the JSON array is optional in the Trace Event format.
  // We exploit this and do not write ']' at the end of the log to allow
  // multiple layers write into the same file.
  void EndLog() override {}

  void Flush() override { out_->Flush(); }

 private:
  LogOutput *out_ = nullptr;
};

}  // namespace performancelayers

#endif  // STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_TRACE_EVENT_LOGGING_H_
