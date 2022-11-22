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
#include "layer/support/layer_utils.h"

using ::testing::ElementsAre;
using ::testing::IsEmpty;

namespace performancelayers {
namespace {
// An `InstantEvent`. Adds a `scope` variable to the args indicating event's
// visibility in the trace viewer.
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

TEST(TraceEvent, InstantEventCreation) {
  InstantEvent instant_event("compile_time_init", "compile_time", 123, 321);
  std::vector<Attribute *> attrs = instant_event.GetAttributes();
  auto it = std::find_if(attrs.begin(), attrs.end(), [](Attribute *attr) {
    return attr->isa<TraceEventAttr>();
  });

  ASSERT_TRUE(it != attrs.end());
  const TraceEventAttr *trace_attr = (*it)->cast<TraceEventAttr>();
  EXPECT_EQ(trace_attr->GetCategory().GetValue(), "compile_time");
  EXPECT_EQ(trace_attr->GetPhase().GetValue(), "i");
  EXPECT_EQ(trace_attr->GetPid().GetValue(), 123);
  EXPECT_EQ(trace_attr->GetTid().GetValue(), 321);

  const std::vector<Attribute *> &trace_args = trace_attr->GetArgs();
  ASSERT_TRUE(trace_args.size());
  auto scope_it =
      std::find_if(trace_args.begin(), trace_args.end(), [](Attribute *arg) {
        return arg->isa<StringAttr>() &&
               arg->GetName() == std::string_view("scope");
      });
  ASSERT_TRUE(scope_it != trace_args.end());
  ASSERT_EQ((*scope_it)->cast<StringAttr>()->GetValue(), "g");
}

TEST(TraceEvent, CompleteEventCreation) {
  const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
  const int64_t hash_val2 = 0x67d390249c2f20ce;
  Duration duration = Duration::FromNanoseconds(100000);
  CreateShaderCompleteEvent complete_event(
      "compile_time", {hash_val1, hash_val2}, duration, 321, 123);
  std::vector<Attribute *> attrs = complete_event.GetAttributes();
  auto it = std::find_if(attrs.begin(), attrs.end(), [](Attribute *attr) {
    return attr->isa<TraceEventAttr>();
  });

  ASSERT_TRUE(it != attrs.end());
  const TraceEventAttr *trace_attr = (*it)->cast<TraceEventAttr>();
  EXPECT_EQ(trace_attr->GetCategory().GetValue(), "pipeline");
  EXPECT_EQ(trace_attr->GetPhase().GetValue(), "X");
  EXPECT_EQ(trace_attr->GetPid().GetValue(), 321);
  EXPECT_EQ(trace_attr->GetTid().GetValue(), 123);

  const std::vector<Attribute *> &trace_args = trace_attr->GetArgs();
  ASSERT_TRUE(trace_args.size());
  auto dur_it =
      std::find_if(trace_args.begin(), trace_args.end(),
                   [](Attribute *arg) { return arg->isa<DurationAttr>(); });
  ASSERT_TRUE(dur_it != trace_args.end());
  EXPECT_EQ((*dur_it)->cast<DurationAttr>()->GetValue().ToNanoseconds(),
            duration.ToNanoseconds());
}

}  // namespace
}  // namespace performancelayers
