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

#include "layer/support/trace_event_logging.h"

#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "layer/support/event_logging.h"
#include "layer/support/layer_utils.h"

namespace performancelayers {
namespace {
constexpr size_t kMaxBufferSize = 128;

std::string DoubleToString(double value) {
  std::string str;
  // Start with the small-string size.
  str.resize(str.capacity());
  // Grow until we have enough space to store the number.
  while (str.size() < kMaxBufferSize) {
    auto [end, err] = std::to_chars(str.data(), str.data() + str.size(), value,
                                    std::chars_format::fixed);
    if (err == std::errc()) {
      return str.data();
    }
    // Not enough space.
    str.resize(str.capacity() * 2);
  }
  assert(false && "Not enough space");
  return str;
}

std::string ValueToJsonString(bool value) { return value ? "true" : "false"; }

std::string ValueToJsonString(int64_t value) { return std::to_string(value); }

std::string ValueToJsonString(const std::vector<int64_t> &values) {
  std::ostringstream json_str;
  json_str << "[" << std::hex;
  size_t e = values.size();
  for (size_t i = 0; i != e; ++i) {
    const char *delimiter = i < e - 1 ? ", " : "";
    json_str << "\"0x" << values[i] << "\"" << delimiter;
  }
  json_str << "]";
  return json_str.str();
}

// Converts the duration to milliseconds, the defualt time unit in the `Trace
// Event` format.
std::string ValueToJsonString(Duration value) {
  return DoubleToString(value.ToMilliseconds());
}

// Converts the timestamp to milliseconds, the defualt time unit in the `Trace
// Event` format.
std::string ValueToJsonString(Timestamp value) {
  return DoubleToString(value.ToMilliseconds());
}

// Appends all given attributes, including the type-specific attribute, to the
// stream.
void TraceArgsToJsonString(const std::vector<Attribute *> &args,
                           std::ostringstream &json_str) {
  json_str << ", " << std::quoted("args") << " : { ";
  for (size_t i = 0, e = args.size(); i < e; ++i) {
    json_str << std::quoted(args[i]->GetName()) << " : ";
    switch (args[i]->GetValueType()) {
      case kHashAttribute: {
        json_str << "\"0x" << std::hex << args[i]->cast<HashAttr>()->GetValue()
                 << "\"";
        break;
      }
      case ValueType::kTimestamp: {
        json_str << ValueToJsonString(
            args[i]->cast<TimestampAttr>()->GetValue());
        break;
      }
      case ValueType::kDuration: {
        json_str << ValueToJsonString(
            args[i]->cast<DurationAttr>()->GetValue());
        break;
      }
      case ValueType::kBool: {
        json_str << ValueToJsonString(args[i]->cast<BoolAttr>()->GetValue());
        break;
      }
      case ValueType::kInt64: {
        json_str << ValueToJsonString(args[i]->cast<Int64Attr>()->GetValue());
        break;
      }
      case ValueType::kString: {
        json_str << std::quoted(args[i]->cast<StringAttr>()->GetValue());
        break;
      }
      case ValueType::kVectorInt64: {
        json_str << ValueToJsonString(
            args[i]->cast<VectorInt64Attr>()->GetValue());
        break;
      }
      case ValueType::kTraceEvent:
        break;
    }
    if (i + 1 != e) json_str << ", ";
  }
  json_str << " }";
}

// Appends the start timestamp and the duration of a `TraceEventAttr` to the
// given stream.
void AppendCompleteEvent(TimestampAttr timestamp,
                         const TraceEventAttr *trace_event,
                         std::ostringstream &json_stream) {
  const DurationAttr *duration_attr = trace_event->GetArg<DurationAttr>();
  assert(duration_attr && "Duration not found.");

  Duration duration = duration_attr->GetValue();
  Timestamp start_timestamp = timestamp.GetValue() - duration;
  json_stream << ", " << std::quoted("ts") << " : "
              << ValueToJsonString(start_timestamp) << ", "
              << std::quoted("dur") << " : " << ValueToJsonString(duration);
}

// Appends the timestamp and the scope of a `TraceEventAttr` to the given
// stream.
void AppendInstantEvent(TimestampAttr timestamp,
                        const TraceEventAttr *trace_event,
                        std::ostringstream &json_stream) {
  const StringAttr *scope_attr = trace_event->GetArg<StringAttr>("scope");
  assert(scope_attr && "Scope not found.");

  std::string_view scope = scope_attr->GetValue();
  assert((scope == "g" || scope == "p" || scope == "t") &&
         "Invalid scope. Scope must be \"g\", \"p\", or \"t\".");

  json_stream << ", " << std::quoted("ts") << " : "
              << ValueToJsonString(timestamp.GetValue()) << ", "
              << std::quoted("s") << " : " << std::quoted(scope);
}

}  // namespace

std::string EventToTraceEventString(Event &event) {
  const TraceEventAttr *trace_attr = event.GetAttribute<TraceEventAttr>();
  assert(trace_attr && "Could not find TraceEventAttr in the event.");

  std::ostringstream json_stream;
  std::string_view phase_str = trace_attr->GetPhase().GetValue();

  json_stream << "{ " << std::quoted("name") << " : "
              << std::quoted(event.GetEventName()) << ", " << std::quoted("ph")
              << " : " << std::quoted(phase_str) << ", " << std::quoted("cat")
              << " : " << std::quoted(trace_attr->GetCategory().GetValue())
              << ", " << std::quoted("pid") << " : "
              << trace_attr->GetPid().GetValue() << ", " << std::quoted("tid")
              << " : " << trace_attr->GetTid().GetValue();
  if (phase_str == "X") {
    AppendCompleteEvent(event.GetCreationTime(), trace_attr, json_stream);
  } else if (phase_str == "i") {
    AppendInstantEvent(event.GetCreationTime(), trace_attr, json_stream);
  } else {
    assert(false &&
           "Unrecognized phase.\nPhase should be either \"X\" or \"i\".");
  }

  TraceArgsToJsonString(trace_attr->GetArgs(), json_stream);

  json_stream << " },";
  return json_stream.str();
}

}  // namespace performancelayers
