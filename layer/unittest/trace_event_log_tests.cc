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

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "layer/support/event_logging.h"
#include "layer/support/log_output.h"
#include "layer/support/trace_event_logging.h"

using ::testing::ElementsAre;
using ::testing::MatchesRegex;

namespace performancelayers {
namespace {
constexpr int64_t kTestEventTimestamp = 1401;

// An `InstantEvent` in the Trace Event format. Adds a `scope` variable
// indicating event's visibility in the trace viewer.
class InstantEvent : public Event {
 public:
  InstantEvent(const char *name, int64_t timestamp, const char *category,
               int64_t pid, int64_t tid, const char *scope = "g")
      : Event(name, timestamp),
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
  CreateShaderCompleteEvent(const char *name, int64_t timestamp,
                            const std::vector<int64_t> &hash_values,
                            Duration duration, int64_t pid, int64_t tid)
      : Event(name, timestamp),
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
  InstantEvent instant_event("compile_time_init", kTestEventTimestamp,
                             "compile_time", 123, 321);
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
  CreateShaderCompleteEvent complete_event("compile_time", kTestEventTimestamp,
                                           {hash_val1, hash_val2}, duration,
                                           321, 123);
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
  InstantEvent instant_event("compile_time_init", kTestEventTimestamp,
                             "compile_time", 123, 321);
  std::string_view expected_str =
      R"(\{ "name" : "compile_time_init", "ph" : "i", "cat" : "compile_time", "pid" : 123, "tid" : 321, "ts" : ([0-9\.]+), "s" : "g", "args" : \{ "scope" : "g" \} \},)";
  EXPECT_THAT(EventToTraceEventString(instant_event),
              MatchesRegex(expected_str));
}

TEST(TraceEvent, CompleteEventToString) {
  const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
  const int64_t hash_val2 = 0x67d390249c2f20ce;
  Duration duration = Duration::FromNanoseconds(1000);
  CreateShaderCompleteEvent complete_event("compile_time", kTestEventTimestamp,
                                           {hash_val1, hash_val2}, duration,
                                           123, 321);

  std::string_view expected_str =
      R"(\{ "name" : "compile_time", "ph" : "X", "cat" : "pipeline", "pid" : 123, "tid" : 321, "ts" : ([0-9\.]+), "dur" : 0.001000, "args" : \{ "duration" : 0.001000 \} \},)";
  EXPECT_THAT(EventToTraceEventString(complete_event),
              MatchesRegex(expected_str));
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

TEST(TraceEventLogger, MethodCheck) {
  StringOutput out = {};
  TraceEventLogger logger(&out);

  InstantEvent instant_event("compile_time_init", kTestEventTimestamp,
                             "compile_time", 123, 321);

  logger.StartLog();
  logger.AddEvent(&instant_event);
  logger.Flush();
  logger.EndLog();
  // Checks double `EndLog` calls.
  logger.EndLog();
  std::string_view expected_str =
      R"(\{ "name" : "compile_time_init", "ph" : "i", "cat" : "compile_time", "pid" : 123, "tid" : 321, "ts" : ([0-9\.]+), "s" : "g", "args" : \{ "scope" : "g" \} \},)";
  EXPECT_THAT(out.GetLog(), ElementsAre("[", MatchesRegex(expected_str)));
}

TEST(TraceEventLogger, LogDifferentTypes) {
  const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
  const int64_t hash_val2 = 0x67d390249c2f20ce;
  Duration duration = Duration::FromNanoseconds(1000);
  CreateShaderCompleteEvent complete_event("compile_time", kTestEventTimestamp,
                                           {hash_val1, hash_val2}, duration,
                                           321, 123);
  InstantEvent instant_event("compile_time_init", kTestEventTimestamp,
                             "compile_time", 123, 321);

  StringOutput out = {};
  TraceEventLogger logger(&out);

  logger.StartLog();
  logger.AddEvent(&instant_event);
  logger.Flush();
  logger.AddEvent(&complete_event);
  logger.EndLog();

  std::string_view instant_expected_str =
      R"(\{ "name" : "compile_time_init", "ph" : "i", "cat" : "compile_time", "pid" : 123, "tid" : 321, "ts" : ([0-9\.]+), "s" : "g", "args" : \{ "scope" : "g" \} \},)";

  std::string_view complete_expected_str =
      R"(\{ "name" : "compile_time", "ph" : "X", "cat" : "pipeline", "pid" : 321, "tid" : 123, "ts" : ([0-9\.]+), "dur" : 0.001000, "args" : \{ "duration" : 0.001000 \} \},)";
  EXPECT_THAT(out.GetLog(), ElementsAre("[", MatchesRegex(instant_expected_str),
                                        MatchesRegex(complete_expected_str)));
}

}  // namespace
}  // namespace performancelayers
