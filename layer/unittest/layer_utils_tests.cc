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

#include "gtest/gtest.h"
#include "layer/support/layer_utils.h"

namespace performancelayers {
namespace {

TEST(LayerUtils, DurationUnits) {
  static constexpr double epsilon = 0.000001;
  auto start = Timestamp::FromNanoseconds(1'000'000'000);
  EXPECT_EQ(start.ToNanoseconds(), 1'000'000'000);
  EXPECT_NEAR(start.ToMilliseconds(), 1000.0, epsilon);

  Duration dur = Duration::FromNanoseconds(1000);
  EXPECT_EQ(dur.ToNanoseconds(), 1000);
  EXPECT_NEAR(dur.ToMilliseconds(), 0.001, epsilon);

  auto end =
      Timestamp::FromNanoseconds(start.ToNanoseconds() + dur.ToNanoseconds());
  EXPECT_EQ(end.ToNanoseconds(), 1'000'001'000);
  EXPECT_NEAR(end.ToMilliseconds(), 1000.001, epsilon);

  Timestamp newStart = end - dur;
  EXPECT_EQ(newStart.ToNanoseconds(), 1'000'000'000);
  EXPECT_NEAR(newStart.ToMilliseconds(), 1000.0, epsilon);
}

}  // namespace
}  // namespace performancelayers
