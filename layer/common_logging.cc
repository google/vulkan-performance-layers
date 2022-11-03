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

#include "common_logging.h"

#include <string>

#include "csv_logging.h"
#include "debug_logging.h"
#include "event_logging.h"

namespace performancelayers {
std::string EventToCommonLogStr(Event &event) {
  const std::vector<Attribute *> &attributes = event.GetAttributes();

  std::ostringstream csv_str;
  csv_str << event.GetEventName();
  csv_str << ",";
  for (size_t i = 0, e = attributes.size(); i != e; ++i) {
    csv_str << attributes[i]->GetName() << ":";
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

CommonLogger::CommonLogger(const char *filename) {
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
