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
#ifndef STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_LOG_OUTPUT_H_
#define STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_LOG_OUTPUT_H_

#include <cassert>
#include <string>
#include <vector>

namespace performancelayers {
// An abstraction over the output. It writes the incoming string to the output.
class LogOutput {
 public:
  virtual ~LogOutput() = default;

  virtual void Flush() = 0;

  virtual void LogLine(std::string_view line) = 0;
};

// Implements LogOutput for a file. If the given filename is `nullptr`, it
// writes to the standard output. After each write, the logs are persisted to
// the file by calling `fflush`. Since only one line is written to the output by
// each `LogLine()` call, it doens't need to aquire a mutex.
class FileOutput : public LogOutput {
 public:
  FileOutput(const char *filename);

  ~FileOutput() {
    if (out_ && out_ != stderr) {
      fclose(out_);
    }
  }

  void Flush() override {
    assert(out_);
    fflush(out_);
  }

  void LogLine(std::string_view line) override;

 private:
  FILE *out_ = nullptr;
};

// This class is used for testing. It writes the data to a string
// instead of a file. The data can be read using the `GetLog()` method.
class StringOutput : public LogOutput {
 public:
  StringOutput() = default;

  void Flush() override {}

  void LogLine(std::string_view line) override {
    assert(line.find('\n') == std::string_view::npos &&
           "Expected single line.");
    out_.emplace_back(std::string(line));
  }

  const std::vector<std::string> &GetLog() { return out_; }

 private:
  std::vector<std::string> out_;
};
}  // namespace performancelayers

#endif  // STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_LOG_OUTPUT_H_
