#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::ElementsAre;
using ::testing::IsEmpty;

namespace {
// Set of supported value types for an attribute.
enum ValueType { String, Int64, VectorInt64 };
// Specifies the importance of an Event. Events are logged based on their level
// of importance. E.g., compile_time.csv only contains the important compile
// time events while event.log contains all the compile time events.
enum LogLevel { Low, Medium, High };

// Attribute class
//
// An event consists of a set of attributes. Each attribute has a name that
// indicates what the attribute is (e.g., timestamp, hash, etc) and a value.
// `Attribute` is the base class for an attribute. For each `ValueType`, there
// should be an implmentation of `Attribute`. Each implementation must expose an
// `id_` to indicate which ValueType it is implementing.
class Attribute {
 public:
  // Checks if the `Attribute` is an instance of the given class. Used for
  // safely casting an `Attribute` to a proper derived class.
  template <class T>
  bool isa() {
    return value_type_ == T::id_;
  }

  // Safely casts the Attribute to the given class (T). Valid typenames should
  // be derived from Attribute and have an ValueType `id_` field.
  template <class T>
  const T *cast() {
    return isa<T>() ? static_cast<T *>(this) : nullptr;
  };

  const char *GetName() const { return name_; }
  ValueType GetValueType() const { return value_type_; }

 protected:
  Attribute(const char *name, ValueType value_type)
      : name_(name), value_type_(value_type) {}

 private:
  const char *name_;
  ValueType value_type_;
};

// Implements the Attribute base class for the `ValueType`s.
// Valid typenames: {std::string, int64_t, std::vector<int64_t>}
// Example:
//    `AttributeImpl<int64_t, ValueType::Int64> timestamp("timestamp", 1234);`
template <typename T, ValueType VT>
class AttributeImpl : public Attribute {
 public:
  static constexpr ValueType id_ = VT;

  AttributeImpl(const char *name, const T &value)
      : Attribute{name, VT}, value_(value) {}

  const T &GetValue() const { return value_; }

 protected:
  T &GetValue() { return value_; }

 private:
  T value_;
};

using VectorInt64Attr =
    AttributeImpl<std::vector<int64_t>, ValueType::VectorInt64>;
using StringAttr = AttributeImpl<std::string, ValueType::String>;
using Int64Attr = AttributeImpl<int64_t, ValueType::Int64>;

// Event represents the base struct for a loggable event. It contains the
// event's name and the level of importance. The derived structs must define and
// initialize their own set of attributes.
class Event {
 public:
  Event(const char *name, LogLevel log_level)
      : name_(name), log_level_(log_level) {}

  virtual ~Event() = default;

  // Each implementation of an `Event` contains attribute(s) and
  // overrides this function to return them.
  virtual const std::vector<Attribute *> &GetAttributes() = 0;
  virtual size_t GetNumAttributes() const = 0;

  const char *GetEventName() const { return name_; }
  LogLevel GetLogLevel() const { return log_level_; }

 private:
  const char *name_;
  LogLevel log_level_;
};

// An `Event` for the CreateShaderModule function.
// Example:
// ```c++
//    const int64_t create_time_ns = create_end - create_start;
//    Int64Attr timestamp("timestamp", 1234);
//    Int64Attr shader_hash("hash", res.shader_hash);
//    std::vector<Attribute *> attributes = {&shader_hash, &timestamp};
//    CreateShaderModuleEvent("create_shader_module_ns", attributes,
//    LogLevel::High);
// ```
struct CreateShaderModuleEvent : Event {
 public:
  CreateShaderModuleEvent(const char *name, int64_t timestamp,
                          int64_t hash_value, int64_t duration,
                          LogLevel log_level)
      : Event(name, log_level),
        timestamp_{"timestamp", timestamp},
        hash_value_{"hash", hash_value},
        duration_{"duration", duration},
        attributes_{&timestamp_, &hash_value_, &duration_} {}

  const std::vector<Attribute *> &GetAttributes() override {
    return attributes_;
  }
  size_t GetNumAttributes() const override { return attributes_.size(); }

 private:
  Int64Attr timestamp_;
  Int64Attr hash_value_;
  Int64Attr duration_;
  std::vector<Attribute *> attributes_;
};

struct CreateGraphicsPipelinesEvent : Event {
 public:
  CreateGraphicsPipelinesEvent(const char *name, int64_t timestamp,
                               VectorInt64Attr &hash_values, int64_t duration,
                               LogLevel log_level)
      : Event(name, log_level),
        timestamp_{"timestamp", timestamp},
        hash_values_(hash_values),
        duration_{"duration", duration},
        attributes_{&timestamp_, &hash_values_, &duration_} {}

  const std::vector<Attribute *> &GetAttributes() override {
    return attributes_;
  }
  size_t GetNumAttributes() const override { return attributes_.size(); }

 private:
  Int64Attr timestamp_;
  VectorInt64Attr hash_values_;
  Int64Attr duration_;
  std::vector<Attribute *> attributes_;
};

std::string ValueToCSVString(const std::string &value) { return value; }

std::string ValueToCSVString(const int64_t value) {
  return std::to_string(value);
}

std::string ValueToCSVString(const std::vector<int64_t> &values) {
  std::stringstream csv_string;
  csv_string << "\"[";
  size_t e = values.size();
  for (size_t i = 0; i != e; ++i) {
    const char *delimiter = i < e - 1 ? "," : "";
    csv_string << values[i] << delimiter;
  }
  csv_string << "]\"";
  return csv_string.str();
}

// Takes an `Event` instance as an input and generates a csv string containing
// `event`'s name and attribute values.
// TODO(miladhakimi): Differentiate hashes and other integers. Hashes
// should be displayed in hex.
std::string EventToCSVString(Event &event) {
  const std::vector<Attribute *> &attributes = event.GetAttributes();

  std::stringstream csv_str;
  csv_str << event.GetEventName();
  csv_str << ",";
  for (size_t i = 0, e = attributes.size(); i != e; ++i) {
    switch (attributes[i]->GetValueType()) {
      case ValueType::Int64: {
        csv_str << ValueToCSVString(
            attributes[i]->cast<Int64Attr>()->GetValue());
        break;
      }
      case ValueType::String: {
        csv_str << ValueToCSVString(
            attributes[i]->cast<StringAttr>()->GetValue());
        break;
      }
      case ValueType::VectorInt64: {
        csv_str << ValueToCSVString(
            attributes[i]->cast<VectorInt64Attr>()->GetValue());
        break;
      }
    }
    if (i + 1 != e) csv_str << ",";
  }
  return csv_str.str();
}

// EventLogger base class
//
// EventLogger provides an abstraction for the concrete loggers that log the
// events in various formats (CSV, Chrome Trace Event, etc). The `EventLogger`'s
// methods can be called from multiple threads at the same time. Hence, they are
// expected to be internally synchronized.
class EventLogger {
 public:
  virtual ~EventLogger() = default;

  // Converts an `Event` into a string and writes it to the output.
  virtual void AddEvent(Event *event) = 0;

  // This method is called exactly once before any events are added to denote
  // that the log has started. For instance, in case of a CSV log, the CSV
  // header is written when this function is called.
  virtual void StartLog() = 0;

  // This method is called exactly once to denote that the log is finished.
  virtual void EndLog() = 0;

  // Makes sure all the logs are written to the output.
  virtual void Flush() = 0;
};

// FilterLogger is a wrapper around the `EventLogger` that filters the input
// events based on a given log level.
// Example:
// ```C++
// CSVLogger logger = ...;
// FilterLogger filter(&logger, LogLevel::High);
// ```
class FilterLogger : public EventLogger {
 public:
  FilterLogger(EventLogger *logger, LogLevel log_level)
      : logger_(logger), log_level_(log_level){};

  void AddEvent(Event *event) override {
    if (event->GetLogLevel() >= log_level_) logger_->AddEvent(event);
  }

  void StartLog() override { logger_->StartLog(); }

  void EndLog() override { logger_->EndLog(); }

  void Flush() override { logger_->Flush(); }

 private:
  EventLogger *logger_ = nullptr;
  LogLevel log_level_ = LogLevel::Low;
};

// A subclass of `EventLogger` that forwards all events, start/end, and flushes
// to all its children. Example: `
// ```c++
// BroadCastLogger({&event_logger1,&event_logger2});
// ```
class BroadcastLogger : public EventLogger {
 public:
  BroadcastLogger(std::vector<EventLogger *> loggers) : loggers_(loggers) {}

  void AddEvent(Event *event) override {
    for (EventLogger *logger : loggers_) logger->AddEvent(event);
  }

  void StartLog() override {
    for (EventLogger *logger : loggers_) logger->StartLog();
  }

  void EndLog() override {
    for (EventLogger *logger : loggers_) logger->EndLog();
  }

  void Flush() override {
    for (EventLogger *logger : loggers_) logger->Flush();
  }

  const std::vector<EventLogger *> &GetLoggers() { return loggers_; }

 private:
  std::vector<EventLogger *> loggers_;
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
  const int64_t timestamp_val = 1601314732230797664;
  const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
  const int64_t hash_val2 = 0x67d390249c2f20ce;
  const Int64Attr timestamp("timestamp", timestamp_val);
  const StringAttr state("state", "1");
  const VectorInt64Attr pipeline("pipeline", {hash_val1, hash_val2});
  EXPECT_EQ(timestamp.GetName(), "timestamp");
  EXPECT_EQ(timestamp.GetValue(), timestamp_val);
  EXPECT_EQ(state.GetName(), "state");
  EXPECT_EQ(state.GetValue(), "1");
  EXPECT_EQ(pipeline.GetName(), "pipeline");
  ASSERT_EQ(pipeline.GetValue().size(), 2);
  EXPECT_EQ(pipeline.GetValue()[0], hash_val1);
  EXPECT_EQ(pipeline.GetValue()[1], hash_val2);
}

TEST(Event, CreateShaderModuleEventCreation) {
  const int64_t timestamp_val = 1601314732230797664;
  const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
  const int64_t duration = 926318;
  CreateShaderModuleEvent compile_event("compile_time", timestamp_val,
                                        hash_val1, duration, LogLevel::Low);
  EXPECT_EQ(compile_event.GetNumAttributes(), 3);
}

TEST(Event, ShaderModuleEventCSVStringGeneration) {
  const int64_t timestamp_val = 1601314732230797664;
  const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
  const int64_t duration = 926318;
  std::stringstream expected_str;
  expected_str << "compile_time," << timestamp_val << "," << hash_val1 << ","
               << duration;
  CreateShaderModuleEvent compile_event("compile_time", timestamp_val,
                                        hash_val1, duration, LogLevel::Low);
  ASSERT_EQ(compile_event.GetNumAttributes(), 3);

  EXPECT_EQ(EventToCSVString(compile_event), expected_str.str());
}

TEST(Event, GraphicsPipelinesEventCSVStringGeneration) {
  const int64_t timestamp_val = 1601314732230797664;
  const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
  const int64_t hash_val2 = 0x67d390249c2f20ce;
  const int64_t duration = 926318;

  VectorInt64Attr hashes("hashes", {hash_val1, hash_val2});
  CreateGraphicsPipelinesEvent pipeline_event("create_graphics_pipeline",
                                              timestamp_val, hashes, duration,
                                              LogLevel::Low);
  EXPECT_EQ(pipeline_event.GetNumAttributes(), 3);
}

TEST(Event, CreateGraphicsPipelinesEventCreation) {
  const int64_t timestamp_val = 1601314732230797664;
  const int64_t hash_val1 = 0x67d6fd0aaa78a6d8;
  const int64_t hash_val2 = 0x67d390249c2f20ce;
  const int64_t duration = 926318;
  std::stringstream expected_str;
  expected_str << "create_graphics_pipeline," << timestamp_val << ",\"["
               << hash_val1 << "," << hash_val2 << "]\"," << duration;
  VectorInt64Attr hashes("hashes", {hash_val1, hash_val2});
  CreateGraphicsPipelinesEvent pipeline_event("create_graphics_pipeline",
                                              timestamp_val, hashes, duration,
                                              LogLevel::Low);
  ASSERT_EQ(pipeline_event.GetNumAttributes(), 3);

  EXPECT_EQ(EventToCSVString(pipeline_event), expected_str.str());
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
  CreateGraphicsPipelinesEvent pipeline_event("create_graphics_pipeline", 1,
                                              hashes, 4, LogLevel::High);
  CreateShaderModuleEvent compile_event("compile_time", 1, 2, 3, LogLevel::Low);
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
  CreateGraphicsPipelinesEvent pipeline_event("create_graphics_pipeline", 1,
                                              hashes, 4, LogLevel::High);
  CreateShaderModuleEvent compile_event("compile_time", 1, 2, 3, LogLevel::Low);
  TestLogger test_logger;
  FilterLogger filter(&test_logger, LogLevel::High);
  filter.AddEvent(&pipeline_event);
  filter.AddEvent(&compile_event);
  EXPECT_THAT(test_logger.GetEvents(), ElementsAre(&pipeline_event));
}

TEST(EventLogger, BroadcastLoggerCreation) {
  TestLogger test_logger1, test_logger2, test_logger3;
  FilterLogger filter(&test_logger1, LogLevel::High);
  BroadcastLogger broadcast1({&filter, &test_logger2});
  BroadcastLogger broadcast2({&broadcast1, &test_logger3});
  ASSERT_THAT(broadcast1.GetLoggers(), ElementsAre(&filter, &test_logger2));
  ASSERT_THAT(broadcast2.GetLoggers(), ElementsAre(&broadcast1, &test_logger3));
}

TEST(EventLogger, BroadcastLoggerFunctionCalls) {
  VectorInt64Attr hashes("hashes", {2, 3});
  CreateGraphicsPipelinesEvent pipeline_event("create_graphics_pipeline", 1,
                                              hashes, 4, LogLevel::High);
  CreateShaderModuleEvent compile_event("compile_time", 1, 2, 3, LogLevel::Low);
  TestLogger test_logger1, test_logger2, test_logger3;
  FilterLogger filter(&test_logger1, LogLevel::High);
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
