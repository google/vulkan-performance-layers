// Copyright 2021 Google LLC
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

#include "layer/support/debug_logging.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace performancelayers {

static const char* GetBasename(const char* filename) {
  std::string_view path = filename;
  if (size_t last_slash_pos = path.find_last_of("/\\");
      (last_slash_pos != std::string_view::npos) &&
      (last_slash_pos + 1 < path.size())) {
    return filename + last_slash_pos + 1;
  }
  return filename;
}

void MessageLogger::PrintMessage() {
  const char* prefix = "";
  switch (kind_) {
    case LogMessageKind::INFO:
      prefix = "INFO";
      break;
    case LogMessageKind::WARNING:
      prefix = "WARNING";
      break;
    case LogMessageKind::ERROR:
      prefix = "ERROR";
      break;
    default:
      assert(false);
  }

  fprintf(stderr, "[%s %s:%zu] %s\n", prefix, GetBasename(file_), line_,
          message_.str().c_str());
  fflush(stderr);
}

}  // namespace performancelayers
