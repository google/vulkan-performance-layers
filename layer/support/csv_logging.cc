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

#include "layer/support/csv_logging.h"

#include <sstream>
#include <string>

#include "layer/support/event_logging.h"
#include "layer/support/layer_utils.h"

namespace performancelayers {
std::string ValueToCSVString(const bool value) { return std::to_string(value); }

std::string ValueToCSVString(const std::string &value) { return value; }

std::string ValueToCSVString(const int64_t value) {
  return std::to_string(value);
}

std::string ValueToCSVString(const std::vector<int64_t> &values) {
  std::ostringstream csv_string;
  csv_string << "\"[" << std::hex;
  size_t e = values.size();
  for (size_t i = 0; i != e; ++i) {
    const char *delimiter = i < e - 1 ? "," : "";
    csv_string << "0x" << values[i] << delimiter;
  }
  csv_string << "]\"";
  return csv_string.str();
}

std::string ValueToCSVString(Duration value) {
  return std::to_string(value.ToNanoseconds());
}

std::string ValueToCSVString(Timestamp value) {
  return std::to_string(value.ToNanoseconds());
}

// Takes an `Event` instance as an input and generates a csv string containing
// `event`'s name and attribute values.
// TODO(miladhakimi): Differentiate hashes and other integers. Hashes
// should be displayed in hex.
std::string EventToCSVString(Event &event) {
  const std::vector<Attribute *> &attributes = event.GetAttributes();

  // Remove attributes that should be ignored.
  std::vector<Attribute *> filtered_attributes;
  std::copy_if(attributes.begin(), attributes.end(),
               std::back_inserter(filtered_attributes), [](Attribute *att) {
                 return att->GetValueType() != ValueType::kTraceEvent;
               });

  std::ostringstream csv_str;
  for (size_t i = 0, e = filtered_attributes.size(); i != e; ++i) {
    Attribute *att = filtered_attributes[i];
    switch (att->GetValueType()) {
      case ValueType::kHashAttribute: {
        csv_str << "0x" << std::hex << att->cast<HashAttr>()->GetValue();
        break;
      }
      case ValueType::kTimestamp: {
        csv_str << ValueToCSVString(att->cast<TimestampAttr>()->GetValue());
        break;
      }
      case ValueType::kDuration: {
        csv_str << ValueToCSVString(att->cast<DurationAttr>()->GetValue());
        break;
      }
      case ValueType::kBool: {
        csv_str << ValueToCSVString(att->cast<BoolAttr>()->GetValue());
        break;
      }
      case ValueType::kInt64: {
        csv_str << ValueToCSVString(att->cast<Int64Attr>()->GetValue());
        break;
      }
      case ValueType::kString: {
        csv_str << att->cast<StringAttr>()->GetValue();
        break;
      }
      case ValueType::kVectorInt64: {
        csv_str << ValueToCSVString(att->cast<VectorInt64Attr>()->GetValue());
        break;
      }
      case ValueType::kTraceEvent:
        assert(false);
        break;
    }
    if (i + 1 != e) csv_str << ",";
  }
  return csv_str.str();
}

}  // namespace performancelayers
