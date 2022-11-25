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

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "layer/support/event_logging.h"

using ::testing::ElementsAre;
using ::testing::IsEmpty;

namespace performancelayers {
namespace {
class TestEvent : public Event {
 public:
  TestEvent(const char *name, int64_t hash_value, Duration duration,
            LogLevel log_level)
      : Event(name, log_level),
        hash_value_{"hash", hash_value},
        duration_{"duration", duration} {
    InitAttributes({&duration_, &hash_value_});
  }

 private:
  Int64Attr hash_value_;
  DurationAttr duration_;
};

// A logger for testing `EventLogger`.
//
// Mocks the behavior of the virtual methods and provides getters to check if
// it works as expected.
class TestLogger : public EventLogger {
 public:
  void AddEvent(Event *event) override { events_.push_back(event); }

  void StartLog() override { log_started_ = true; }

  void EndLog() override { log_finished_ = true; }

  void Flush() override { ++flush_count_; }

  const std::vector<Event *> &GetEvents() { return events_; }

  bool IsStarted() const { return log_started_; }

  bool IsFinished() const { return log_finished_; }

  size_t GetFlushCount() const { return flush_count_; }

 private:
  std::vector<Event *> events_;

  bool log_started_ = false;
  bool log_finished_ = false;
  size_t flush_count_ = 0;
};

TEST(Event, AttributeCreation) {
  const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
  const int64_t hash_val2 = 0x67d390249c2f20ce;
  const StringAttr state("state", "1");
  const VectorInt64Attr pipeline("pipeline", {hash_val1, hash_val2});
  EXPECT_EQ(state.GetName(), "state");
  EXPECT_EQ(state.GetValue(), "1");
  EXPECT_EQ(pipeline.GetName(), "pipeline");
  ASSERT_EQ(pipeline.GetValue().size(), 2);
  EXPECT_EQ(pipeline.GetValue()[0], hash_val1);
  EXPECT_EQ(pipeline.GetValue()[1], hash_val2);
}

TEST(Event, TestEventCreation) {
  const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
  Duration duration = Duration::FromNanoseconds(1);
  TestEvent compile_event("compile_time", hash_val1, duration, LogLevel::kLow);
  EXPECT_EQ(compile_event.GetNumAttributes(), 2);
}

TEST(Event, ShaderModuleEventCreation) {
  const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
  Duration duration = Duration::FromNanoseconds(926318);
  TestEvent compile_event("compile_time", hash_val1, duration, LogLevel::kLow);
  ASSERT_EQ(compile_event.GetNumAttributes(), 2);
}

TEST(Event, GraphicsPipelinesEventCreation) {
  const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
  const int64_t hash_val2 = 0x67d390249c2f20ce;
  Duration duration = Duration::FromNanoseconds(926318);

  VectorInt64Attr hashes("hashes", {hash_val1, hash_val2});
  CreateGraphicsPipelinesEvent pipeline_event("create_graphics_pipeline",
                                              hashes, duration, LogLevel::kLow);
  ASSERT_EQ(pipeline_event.GetNumAttributes(), 2);
}

TEST(Event, CreateGraphicsPipelinesEventCreation) {
  const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
  const int64_t hash_val2 = 0x67d390249c2f20ce;
  Duration duration = Duration::FromNanoseconds(926318);
  VectorInt64Attr hashes("hashes", {hash_val1, hash_val2});
  CreateGraphicsPipelinesEvent pipeline_event("create_graphics_pipeline",
                                              hashes, duration, LogLevel::kLow);
  ASSERT_EQ(pipeline_event.GetNumAttributes(), 2);
}

TEST(EventLogger, TestLoggerCreation) {
  TestLogger test_logger;
  EXPECT_FALSE(test_logger.IsStarted());
  EXPECT_EQ(0, test_logger.GetFlushCount());
  EXPECT_FALSE(test_logger.IsFinished());
  EXPECT_THAT(test_logger.GetEvents(), IsEmpty());
}

TEST(EventLogger, TestLoggerFunctionCalls) {
  VectorInt64Attr hashes("hashes", {2, 3});
  CreateGraphicsPipelinesEvent pipeline_event(
      "create_graphics_pipeline", hashes, Duration::FromNanoseconds(4),
      LogLevel::kHigh);
  TestEvent compile_event("compile_time", 2, Duration::FromNanoseconds(3),
                          LogLevel::kLow);
  TestLogger test_logger;

  test_logger.AddEvent(&pipeline_event);
  test_logger.AddEvent(&compile_event);
  EXPECT_THAT(test_logger.GetEvents(),
              ElementsAre(&pipeline_event, &compile_event));

  test_logger.StartLog();
  EXPECT_TRUE(test_logger.IsStarted());

  test_logger.Flush();
  EXPECT_EQ(test_logger.GetFlushCount(), 1);

  test_logger.EndLog();
  EXPECT_TRUE(test_logger.IsFinished());
}

TEST(EventLogger, FilterLoggerInsert) {
  VectorInt64Attr hashes("hashes", {2, 3});
  CreateGraphicsPipelinesEvent pipeline_event(
      "create_graphics_pipeline", hashes, Duration::FromNanoseconds(4),
      LogLevel::kHigh);
  TestEvent compile_event("compile_time", 2, Duration::FromNanoseconds(3),
                          LogLevel::kLow);
  TestLogger test_logger;
  FilterLogger filter(&test_logger, LogLevel::kHigh);
  filter.AddEvent(&pipeline_event);
  filter.AddEvent(&compile_event);
  EXPECT_THAT(test_logger.GetEvents(), ElementsAre(&pipeline_event));
}

TEST(EventLogger, BroadcastLoggerCreation) {
  TestLogger test_logger1, test_logger2, test_logger3;
  FilterLogger filter(&test_logger1, LogLevel::kHigh);
  BroadcastLogger broadcast1({&filter, &test_logger2});
  BroadcastLogger broadcast2({&broadcast1, &test_logger3});
  ASSERT_THAT(broadcast1.GetLoggers(), ElementsAre(&filter, &test_logger2));
  ASSERT_THAT(broadcast2.GetLoggers(), ElementsAre(&broadcast1, &test_logger3));
}

TEST(EventLogger, BroadcastLoggerFunctionCalls) {
  VectorInt64Attr hashes("hashes", {2, 3});
  CreateGraphicsPipelinesEvent pipeline_event(
      "create_graphics_pipeline", hashes, Duration::FromNanoseconds(4),
      LogLevel::kHigh);
  TestEvent compile_event("compile_time", 2, Duration::FromNanoseconds(3),
                          LogLevel::kLow);
  TestLogger test_logger1, test_logger2, test_logger3;
  FilterLogger filter(&test_logger1, LogLevel::kHigh);
  BroadcastLogger broadcast1({&filter, &test_logger2});
  BroadcastLogger broadcast2({&broadcast1, &test_logger3});
  broadcast1.AddEvent(&pipeline_event);
  EXPECT_THAT(test_logger1.GetEvents(), ElementsAre(&pipeline_event));
  EXPECT_THAT(test_logger2.GetEvents(), ElementsAre(&pipeline_event));

  broadcast2.AddEvent(&compile_event);
  EXPECT_THAT(test_logger1.GetEvents(), ElementsAre(&pipeline_event));
  EXPECT_THAT(test_logger2.GetEvents(),
              ElementsAre(&pipeline_event, &compile_event));
  EXPECT_THAT(test_logger3.GetEvents(), ElementsAre(&compile_event));

  broadcast1.StartLog();
  EXPECT_TRUE(test_logger1.IsStarted());
  EXPECT_TRUE(test_logger2.IsStarted());
  broadcast2.StartLog();
  EXPECT_TRUE(test_logger3.IsStarted());

  broadcast1.Flush();
  EXPECT_EQ(test_logger1.GetFlushCount(), 1);
  EXPECT_EQ(test_logger2.GetFlushCount(), 1);
  EXPECT_EQ(test_logger3.GetFlushCount(), 0);

  broadcast2.Flush();
  EXPECT_EQ(test_logger1.GetFlushCount(), 2);
  EXPECT_EQ(test_logger2.GetFlushCount(), 2);
  EXPECT_EQ(test_logger3.GetFlushCount(), 1);

  broadcast1.EndLog();
  EXPECT_TRUE(test_logger1.IsFinished());
  EXPECT_TRUE(test_logger2.IsFinished());
  EXPECT_FALSE(test_logger3.IsFinished());
  broadcast2.EndLog();
  EXPECT_TRUE(test_logger3.IsFinished());
}

}  // namespace
}  // namespace performancelayers
