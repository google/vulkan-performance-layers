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

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "debug_logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "log_output.h"

using ::testing::ElementsAre;
using ::testing::TempDir;

namespace performancelayers {
namespace {
constexpr unsigned kLogsPerThread = 1337;
constexpr unsigned kThreadCount = 100;

// Used by the tests prior to logging to make sure the underlying file is empty.
void CreateEmptyFile(const std::string &filename) {
  std::ofstream(filename.c_str());
}

TEST(LogOutput, SingleThreadedLog) {
  std::string file_path = TempDir() + "/single_thread.log";
  CreateEmptyFile(file_path);

  const std::string line = "name:event_name,timestamp:1234";
  FileOutput file_out(file_path.c_str());
  file_out.LogLine(line);

  std::ifstream test_file(file_path);
  ASSERT_TRUE(test_file.is_open());
  std::string stored_line;
  getline(test_file, stored_line);
  EXPECT_EQ(stored_line, line);

  StringOutput string_out;
  string_out.LogLine(line);
  EXPECT_THAT(string_out.GetLog(), ElementsAre(line));
}

#ifndef NDEBUG
TEST(LogOutput, InvalidLog) {
  std::string file_path = TempDir() + "/invalid.log";
  CreateEmptyFile(file_path);
  FileOutput file_out(file_path.c_str());
  const std::string invalid_line =
      "name:event_name,timestamp:1234\nname:event_name,timestamp:1234";
  ASSERT_DEATH(file_out.LogLine(invalid_line), "Expected single line.");
}
#endif

// This function is executed by multiple threads. It logs the input to the file
// as many times as `kLogsPerThread`.
void LogToFileOutput(FileOutput *file_out, std::string_view line) {
  for (size_t i = 0; i < kLogsPerThread; ++i) file_out->LogLine(line);
}

TEST(LogOutput, MultiThreadedFileLog) {
  std::string file_path = TempDir() + "/multi_thread.log";
  CreateEmptyFile(file_path);

  FileOutput file_out(file_path.c_str());
  const std::string line = "name:event_name,timestamp:1234";
  std::array<std::thread, kThreadCount> threads;
  for (std::thread &thread : threads)
    thread = std::thread(LogToFileOutput, &file_out, line);

  for (std::thread &thread : threads) thread.join();

  std::ifstream test_file(file_path);
  ASSERT_TRUE(test_file.is_open());
  std::string stored_line;
  for (size_t i = 0; i < kThreadCount * kLogsPerThread; ++i) {
    getline(test_file, stored_line);
    EXPECT_EQ(stored_line, line);
  }
  getline(test_file, stored_line);
  EXPECT_EQ(stored_line, "");
}

}  // namespace
}  // namespace performancelayers
