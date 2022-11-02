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

#include "csv_logging.h"

#include <sstream>
#include <string>

#include "debug_logging.h"
#include "event_logging.h"
#include "layer_utils.h"

namespace performancelayers {
std::string ValueToCSVString(const bool value) { return std::to_string(value); }

std::string ValueToCSVString(const std::string &value) { return value; }

std::string ValueToCSVString(const int64_t value) {
  return std::to_string(value);
}

std::string ValueToCSVString(const std::vector<int64_t> &values) {
  std::ostringstream csv_string;
  csv_string << "\"[";
  size_t e = values.size();
  for (size_t i = 0; i != e; ++i) {
    const char *delimiter = i < e - 1 ? "," : "";
    csv_string << "0x" << std::hex << values[i] << delimiter;
  }
  csv_string << "]\"";
  return csv_string.str();
}

std::string ValueToCSVString(DurationClock::duration value) {
  return std::to_string(ToInt64Nanoseconds(value));
}

std::string ValueToCSVString(TimestampClock::time_point value) {
  return std::to_string(ToUnixNanos(value));
}

// Takes an `Event` instance as an input and generates a csv string containing
// `event`'s name and attribute values.
// TODO(miladhakimi): Differentiate hashes and other integers. Hashes
// should be displayed in hex.
std::string EventToCSVString(Event &event) {
  const std::vector<Attribute *> &attributes = event.GetAttributes();

  std::ostringstream csv_str;
  for (size_t i = 0, e = attributes.size(); i != e; ++i) {
    switch (attributes[i]->GetValueType()) {
      case ValueType::kTimestamp: {
        csv_str << ValueToCSVString(
            attributes[i]->cast<TimestampAttr>()->GetValue());
        break;
      }
      case ValueType::kDuration: {
        csv_str << ValueToCSVString(
            attributes[i]->cast<DurationAttr>()->GetValue());
        break;
      }
      case ValueType::kBool: {
        csv_str << ValueToCSVString(
            attributes[i]->cast<BoolAttr>()->GetValue());
        break;
      }
      case ValueType::kInt64: {
        csv_str << ValueToCSVString(
            attributes[i]->cast<Int64Attr>()->GetValue());
        break;
      }
      case ValueType::kString: {
        csv_str << ValueToCSVString(
            attributes[i]->cast<StringAttr>()->GetValue());
        break;
      }
      case ValueType::kVectorInt64: {
        csv_str << ValueToCSVString(
            attributes[i]->cast<VectorInt64Attr>()->GetValue());
        break;
      }
    }
    if (i + 1 != e) csv_str << ",";
  }
  return csv_str.str();
}

CSVLogger::CSVLogger(const char *csv_header, const char *filename)
    : header_(csv_header) {
  if (filename) {
    out_ = fopen(filename, "w");
    if (!out_) {
      SPL_LOG(ERROR) << "Failed to open " << filename
                     << ". Using stderr as the alternative output.";
      out_ = stderr;
    }
  } else {
    out_ = stderr;
  }
}

}  // namespace performancelayers
