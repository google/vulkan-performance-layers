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

#ifndef STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_DEBUG_LOGGING_H_
#define STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_DEBUG_LOGGING_H_

#include <sstream>

namespace performancelayers {
enum class LogMessageKind { INFO, WARNING, ERROR };

// Helper class for printing log messages. Exposes a common logging macro,
// SPL_LOG, independent of the underlying logging library. Sample use:
//   SPL_LOG(WARNING) << "Cannot load file: " << my_file_path;
//
// Note that there is no need to add a newline character at the end -- this is
// handled by the implementation.
class MessageLogger {
 public:
  MessageLogger(LogMessageKind kind, const char* file, size_t line)
      : kind_(kind), file_(file), line_(line) {}
  ~MessageLogger() { PrintMessage(); }

  MessageLogger(const MessageLogger&) = delete;
  MessageLogger& operator=(const MessageLogger&) = delete;
  MessageLogger(MessageLogger&&) = delete;
  MessageLogger& operator=(MessageLogger&&) = delete;

  template <typename Arg>
  MessageLogger& operator<<(Arg&& arg) {
    message_ << std::forward<Arg>(arg);
    return *this;
  }

 private:
  void PrintMessage();
  LogMessageKind kind_;
  const char* file_;
  size_t line_;
  std::ostringstream message_;
};
}  // namespace performancelayers

// Macro for printing logs. There are 3 log message kinds available: INFO,
// WARNING, and ERROR.
#define SPL_LOG(LOG_LEVEL_)         \
  performancelayers::MessageLogger( \
      performancelayers::LogMessageKind::LOG_LEVEL_, __FILE__, __LINE__)

#endif  // STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_DEBUG_LOGGING_H_
