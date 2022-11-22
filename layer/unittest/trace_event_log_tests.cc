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

#include <cassert>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "layer/support/event_logging.h"
#include "layer/support/layer_utils.h"

using ::testing::ElementsAre;
using ::testing::IsEmpty;

namespace performancelayers {
namespace {
std::string ValueToJsonString(bool value) { return value ? "true" : "false"; }

std::string ValueToJsonString(int64_t value) { return std::to_string(value); }

std::string ValueToJsonString(const std::vector<int64_t> &values) {
  std::ostringstream json_str;
  json_str << "[" << std::hex;
  size_t e = values.size();
  for (size_t i = 0; i != e; ++i) {
    const char *delimiter = i < e - 1 ? "," : "";
    json_str << "\"0x" << values[i] << "\"" << delimiter;
  }
  json_str << "]";
  return json_str.str();
}

// Converts the duration to milliseconds, the defualt time unit in the `Trace
// Event` format.
std::string ValueToJsonString(Duration value) {
  return std::to_string(value.ToMilliseconds());
}

// Converts the timestamp to milliseconds, the defualt time unit in the `Trace
// Event` format.
std::string ValueToJsonString(TimestampClock::time_point value) {
  return std::to_string(ToUnixMillis(value));
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
                 << "\"" << std::hex;
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

  double duration_ms = duration_attr->GetValue().ToMilliseconds();
  double end_timestamp_ms = ToUnixMillis(timestamp.GetValue());
  double start_timestamp_ms = end_timestamp_ms - duration_ms;
  json_stream << ", " << std::quoted("ts") << " : " << std::fixed
              << start_timestamp_ms << ", " << std::quoted("dur") << " : "
              << duration_ms;
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

// Converts an `Event` containing `TraceEventAttr` to a JSON string in the Trace
// Event format.
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

// An `InstantEvent` in the Trace Event format. Adds a `scope` variable
// indicating event's visibility in the trace viewer.
class InstantEvent : public Event {
 public:
  InstantEvent(const char *name, const char *category, int64_t pid, int64_t tid,
               const char *scope = "g")
      : Event(name),
        scope_("scope", scope),
        trace_event_("trace_event", category, "i", pid, tid, {&scope_}) {
    InitAttributes({&trace_event_});
  }

 private:
  StringAttr scope_;
  TraceEventAttr trace_event_;
};

// A `CompleteEvent`. Adds the `duration` to the args indicating event's length
// in the trace viewer.
class CreateShaderCompleteEvent : public Event {
 public:
  CreateShaderCompleteEvent(const char *name,
                            const std::vector<int64_t> &hash_values,
                            Duration duration, int64_t pid, int64_t tid)
      : Event(name),
        hash_values_("hashes", hash_values),
        duration_("duration", duration),
        trace_event_("trace_event", "pipeline", "X", pid, tid, {&duration_}) {
    InitAttributes({&hash_values_, &duration_, &trace_event_});
  }

 private:
  VectorInt64Attr hash_values_;
  DurationAttr duration_;
  TraceEventAttr trace_event_;
};

// An invalid event used for checking assertions in the helper functions.
class UnhandledTypeEvent : public Event {
 public:
  UnhandledTypeEvent()
      : Event("name"),
        trace_event_("trace_event", "category", "z", 123, 321, {}) {
    InitAttributes({&trace_event_});
  }

 private:
  TraceEventAttr trace_event_;
};

// An invalid `InstantEvent` used for checking assertions in the helper
// functions.
class InvalidInstantEvent : public Event {
 public:
  static InvalidInstantEvent EventWithoutArgs() {
    return InvalidInstantEvent("name", "cat", 123, 321);
  }

  static InvalidInstantEvent EventWithoutScopeArg() {
    return InvalidInstantEvent("name", "cat", 123, 321, "NOT_SCOPE", "g");
  }

  static InvalidInstantEvent EventWithWrongScopeArg() {
    return InvalidInstantEvent("name", "cat", 123, 321, "scope", "w");
  }

 private:
  InvalidInstantEvent(const char *name, const char *category, int64_t pid,
                      int64_t tid)
      : Event(name),
        scope_("scope", "q"),
        trace_event_("trace_event", category, "i", pid, tid, {}) {
    InitAttributes({&trace_event_});
  }

  InvalidInstantEvent(const char *name, const char *category, int64_t pid,
                      int64_t tid, const char *attr_name,
                      const char *attr_value)
      : Event(name),
        scope_(attr_name, attr_value),
        trace_event_("trace_event", category, "i", pid, tid, {&scope_}) {
    InitAttributes({&trace_event_});
  }

  StringAttr scope_;
  TraceEventAttr trace_event_;
};

// An invalid `CompleteEvent` used for checking assertions in the helper
// functions.
class InvalidCompleteEvent : public Event {
 public:
  static InvalidCompleteEvent EventWithoutArgs() {
    return InvalidCompleteEvent("name", 123, 321);
  }

  static InvalidCompleteEvent EventWithoutDurationArg() {
    const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
    return InvalidCompleteEvent("name", {hash_val1}, 123, 321);
  }

 private:
  InvalidCompleteEvent(const char *name,
                       const std::vector<int64_t> &hash_values, int64_t pid,
                       int64_t tid)
      : Event(name),
        hash_values_("hashes", hash_values),
        trace_event_("trace_event", "pipeline", "X", pid, tid,
                     {&hash_values_}) {
    InitAttributes({&hash_values_, &trace_event_});
  }

  InvalidCompleteEvent(const char *name, int64_t pid, int64_t tid)
      : Event(name),
        hash_values_("hashes", {}),
        trace_event_("trace_event", "pipeline", "X", pid, tid, {}) {
    InitAttributes({&hash_values_, &trace_event_});
  }

  VectorInt64Attr hash_values_;
  TraceEventAttr trace_event_;
};

TEST(TraceEvent, InstantEventCreation) {
  InstantEvent instant_event("compile_time_init", "compile_time", 123, 321);
  std::vector<Attribute *> attrs = instant_event.GetAttributes();
  const TraceEventAttr *trace_attr =
      instant_event.GetAttribute<TraceEventAttr>();
  ASSERT_TRUE(trace_attr);
  EXPECT_EQ(trace_attr->GetCategory().GetValue(), "compile_time");
  EXPECT_EQ(trace_attr->GetPhase().GetValue(), "i");
  EXPECT_EQ(trace_attr->GetPid().GetValue(), 123);
  EXPECT_EQ(trace_attr->GetTid().GetValue(), 321);

  const std::vector<Attribute *> &trace_args = trace_attr->GetArgs();
  ASSERT_TRUE(trace_args.size());
  const StringAttr *scope_attr = trace_attr->GetArg<StringAttr>("scope");
  ASSERT_TRUE(scope_attr);
  ASSERT_EQ(scope_attr->GetValue(), "g");
}

TEST(TraceEvent, CompleteEventCreation) {
  const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
  const int64_t hash_val2 = 0x67d390249c2f20ce;
  Duration duration = Duration::FromNanoseconds(100000);
  CreateShaderCompleteEvent complete_event(
      "compile_time", {hash_val1, hash_val2}, duration, 321, 123);
  std::vector<Attribute *> attrs = complete_event.GetAttributes();
  const TraceEventAttr *trace_attr =
      complete_event.GetAttribute<TraceEventAttr>();
  ASSERT_TRUE(trace_attr);
  EXPECT_EQ(trace_attr->GetCategory().GetValue(), "pipeline");
  EXPECT_EQ(trace_attr->GetPhase().GetValue(), "X");
  EXPECT_EQ(trace_attr->GetPid().GetValue(), 321);
  EXPECT_EQ(trace_attr->GetTid().GetValue(), 123);

  const DurationAttr *dur_attr = trace_attr->GetArg<DurationAttr>();
  ASSERT_TRUE(dur_attr);
}

TEST(TraceEvent, InstantEventToString) {
  InstantEvent instant_event("compile_time_init", "compile_time", 123, 321);
  std::ostringstream expected_str;
  expected_str
      << R"({ "name" : "compile_time_init", "ph" : "i", "cat" : "compile_time", "pid" : 123, "tid" : 321, "ts" : )"
      << std::fixed << ToUnixMillis(instant_event.GetCreationTime().GetValue())
      << R"(, "s" : "g", "args" : { "scope" : "g" } },)";
  EXPECT_EQ(EventToTraceEventString(instant_event), expected_str.str());
}

TEST(TraceEvent, CompleteEventToString) {
  const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
  const int64_t hash_val2 = 0x67d390249c2f20ce;
  Duration duration = Duration::FromNanoseconds(100000);
  CreateShaderCompleteEvent complete_event(
      "compile_time", {hash_val1, hash_val2}, duration, 123, 321);

  std::ostringstream expected_str;
  double start_timestamp =
      ToUnixMillis(complete_event.GetCreationTime().GetValue()) -
      duration.ToMilliseconds();
  expected_str
      << R"({ "name" : "compile_time", "ph" : "X", "cat" : "pipeline", "pid" : 123, "tid" : 321, "ts" : )"
      << std::fixed << start_timestamp << R"(, "dur" : )"
      << duration.ToMilliseconds() << R"(, "args" : { "duration" : )"
      << duration.ToMilliseconds() << " } },";
  EXPECT_EQ(EventToTraceEventString(complete_event), expected_str.str());
}

#ifndef NDEBUG
TEST(TraceEventDeathTest, InvalidEventType) {
  Event event("event without TraceEventAttr");
  EXPECT_DEATH(EventToTraceEventString(event),
               "Could not find TraceEventAttr in the event.");
  UnhandledTypeEvent unrecognized_event = {};
  ASSERT_DEATH(EventToTraceEventString(unrecognized_event),
               "Unrecognized phase.");
}

TEST(TraceEventDeathTest, InvalidInstantEvent) {
  InvalidInstantEvent no_arg_event = InvalidInstantEvent::EventWithoutArgs();
  EXPECT_DEATH(EventToTraceEventString(no_arg_event), "Scope not found.");

  InvalidInstantEvent no_scope_event =
      InvalidInstantEvent::EventWithoutScopeArg();
  EXPECT_DEATH(EventToTraceEventString(no_scope_event), "Scope not found.");

  InvalidInstantEvent wrong_scope_event =
      InvalidInstantEvent::EventWithWrongScopeArg();
  ASSERT_DEATH(EventToTraceEventString(wrong_scope_event), "Invalid scope.");
}

TEST(TraceEventDeathTest, InvalidCompleteEvent) {
  InvalidCompleteEvent no_arg_event = InvalidCompleteEvent::EventWithoutArgs();
  EXPECT_DEATH(EventToTraceEventString(no_arg_event), "Duration not found.");

  InvalidCompleteEvent no_scope_event =
      InvalidCompleteEvent::EventWithoutDurationArg();
  ASSERT_DEATH(EventToTraceEventString(no_scope_event), "Duration not found.");
}
#endif

}  // namespace
}  // namespace performancelayers
