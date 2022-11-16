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

#include "layer/support/common_logging.h"

#include <string>

#include "layer/support/csv_logging.h"
#include "layer/support/debug_logging.h"
#include "layer/support/event_logging.h"

namespace performancelayers {
std::string EventToCommonLogStr(Event &event) {
  const std::vector<Attribute *> &attributes = event.GetAttributes();

  std::ostringstream csv_str;
  csv_str << event.GetEventName() << "," << event.GetCreationTime().GetName()
          << ":" << ValueToCSVString(event.GetCreationTime().GetValue());
  for (Attribute *attribute : attributes) {
    csv_str << "," << attribute->GetName() << ":";
    switch (attribute->GetValueType()) {
      case ValueType::kHashAttribute: {
        csv_str << "0x" << std::hex << attribute->cast<HashAttr>()->GetValue();
        break;
      }
      case ValueType::kTimestamp: {
        csv_str << ValueToCSVString(
            attribute->cast<TimestampAttr>()->GetValue());
        break;
      }
      case ValueType::kDuration: {
        csv_str << ValueToCSVString(
            attribute->cast<DurationAttr>()->GetValue());
        break;
      }
      case ValueType::kBool: {
        csv_str << ValueToCSVString(attribute->cast<BoolAttr>()->GetValue());
        break;
      }
      case ValueType::kInt64: {
        csv_str << ValueToCSVString(attribute->cast<Int64Attr>()->GetValue());
        break;
      }
      case ValueType::kString: {
        csv_str << ValueToCSVString(attribute->cast<StringAttr>()->GetValue());
        break;
      }
      case ValueType::kVectorInt64: {
        csv_str << ValueToCSVString(
            attribute->cast<VectorInt64Attr>()->GetValue());
        break;
      }
    }
  }
  return csv_str.str();
}

}  // namespace performancelayers
