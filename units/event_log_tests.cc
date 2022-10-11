#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

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
// TODO(miladhakimi): Differentiate addresses and other integers. Addresses
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
}  // namespace
