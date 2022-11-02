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
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace performancelayers {
namespace {
// This is a simple test and only calls the methods to make sure the
// logger doesn't crash.
TEST(CommonLogger, MethodCheck) {
  CommonLogger logger(nullptr);
  VectorInt64Attr hashes("hashes", {2, 3});
  CreateGraphicsPipelinesEvent pipeline_event(
      "create_graphics_pipeline", 1, hashes, DurationClock::duration(4),
      LogLevel::kHigh);
  logger.StartLog();
  logger.AddEvent(&pipeline_event);
  logger.Flush();
  logger.EndLog();
  // Checks double `EndLog` calls.
  logger.EndLog();

  SUCCEED();
}

}  // namespace
}  // namespace performancelayers
