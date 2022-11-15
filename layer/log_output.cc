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

#include "log_output.h"

#include "debug_logging.h"

namespace performancelayers {
FileOutput::FileOutput(const char *filename) {
  if (!filename) {
    out_ = stderr;
    return;
  }

  // Since multiple layers open the same file and write to it at the same
  // time, it's opened in the append mode.
  out_ = fopen(filename, "a");
  if (!out_) {
    SPL_LOG(ERROR) << "Failed to open " << filename
                   << ". Using stderr as the alternative output.";
    out_ = stderr;
  }
}

void FileOutput::LogLine(std::string_view line) {
  assert(out_);
  assert(line.find('\n') == std::string_view::npos && "Expected single line.");
  const int line_len = line.length();
  fprintf(out_, "%.*s\n", line_len, line.data());
  Flush();
}

}  // namespace performancelayers
