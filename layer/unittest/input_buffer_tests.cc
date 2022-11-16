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

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <vector>

#include "gtest/gtest.h"
#include "layer/support/debug_logging.h"
#include "layer/support/input_buffer.h"

namespace {

using namespace performancelayers;
namespace fs = std::filesystem;

// Helper struct to create temporary files and fill them with test data.
struct TmpFile {
  TmpFile(const char* filename) {
    fs::path tmp_dir = fs::temp_directory_path();
    path = tmp_dir / filename;
    // If the file already exists, this will automatically truncate it.
    file = fopen(path.c_str(), "wb");
    assert(file);
  }
  ~TmpFile() {
    if (file) fclose(file);
  }

  void AppendData(absl::Span<const uint8_t> data) {
    assert(file);
    if (data.empty()) return;

    size_t bytes_written = fwrite(data.data(), 1, data.size(), file);
    (void)bytes_written;
    assert(bytes_written == data.size());
    fflush(file);
  }

  fs::path path = "";
  FILE* file = nullptr;
};

TEST(InputBuffer, Placeholder) { ASSERT_TRUE(true); }

TEST(InputBuffer, FileReadNotFound) {
  auto buffer_or_err =
      InputBuffer::Create("/definitely/nothing/here/perofrmancelayers.bin",
                          InputBuffer::ImplementationKind::kFileRead);
  EXPECT_FALSE(buffer_or_err.ok());
  SPL_LOG(INFO) << buffer_or_err.status();
}

TEST(InputBuffer, FileReadEmptyFile) {
  TmpFile tmp("cache.bin");
  auto buffer_or_err = InputBuffer::Create(
      tmp.path.c_str(), InputBuffer::ImplementationKind::kFileRead);
  ASSERT_TRUE(buffer_or_err.ok());
  EXPECT_EQ(buffer_or_err->GetBufferSize(), 0);
  EXPECT_EQ(buffer_or_err->GetBuffer().size(), 0);

  // Add some data to the underlying file. Since InputBuffer doesn't re-scan
  // file contents, it should still report an empty buffer.
  tmp.AppendData(std::vector<uint8_t>(42));
  ASSERT_TRUE(buffer_or_err.ok());
  EXPECT_EQ(buffer_or_err->GetBufferSize(), 0);
  EXPECT_EQ(buffer_or_err->GetBuffer().size(), 0);
}

TEST(InputBuffer, FileReadNonEmptyFile) {
  TmpFile tmp("cache.bin");
  constexpr size_t data_size = 42;
  std::vector<uint8_t> write_data(data_size);
  std::iota(write_data.begin(), write_data.end(), uint8_t(0));
  tmp.AppendData(write_data);

  auto buffer_or_err = InputBuffer::Create(
      tmp.path.c_str(), InputBuffer::ImplementationKind::kFileRead);
  ASSERT_TRUE(buffer_or_err.ok());
  ASSERT_EQ(buffer_or_err->GetBufferSize(), data_size);
  ASSERT_EQ(buffer_or_err->GetBuffer().size(), data_size);
  for (size_t i = 0; i != data_size; ++i)
    EXPECT_EQ(buffer_or_err->GetBuffer()[i], write_data[i]);
}

#if defined(__unix__)
// Memory mapped input buffers are currently only implemented on unix.
TEST(InputBuffer, MemMapNotFound) {
  auto buffer_or_err =
      InputBuffer::Create("/definitely/nothing/here/perofrmancelayers.bin",
                          InputBuffer::ImplementationKind::kMemMapped);
  EXPECT_FALSE(buffer_or_err.ok());
  SPL_LOG(INFO) << buffer_or_err.status();
}

TEST(InputBuffer, MemMapEmptyFile) {
  TmpFile tmp("cache.bin");
  auto buffer_or_err = InputBuffer::Create(
      tmp.path.c_str(), InputBuffer::ImplementationKind::kMemMapped);
  ASSERT_TRUE(buffer_or_err.ok());
  EXPECT_EQ(buffer_or_err->GetBufferSize(), 0);
  EXPECT_EQ(buffer_or_err->GetBuffer().size(), 0);

  // Add some data to the underlying file. Since InputBuffer doesn't re-scan
  // file contents, it should still report an empty buffer.
  tmp.AppendData(std::vector<uint8_t>(27));
  ASSERT_TRUE(buffer_or_err.ok());
  EXPECT_EQ(buffer_or_err->GetBufferSize(), 0);
  EXPECT_EQ(buffer_or_err->GetBuffer().size(), 0);
}

TEST(InputBuffer, MemMapNonEmptyFile) {
  TmpFile tmp("cache.bin");
  constexpr size_t data_size = 21;
  std::vector<uint8_t> write_data(data_size);
  std::iota(write_data.begin(), write_data.end(), uint8_t(0));
  tmp.AppendData(write_data);

  auto buffer_or_err = InputBuffer::Create(
      tmp.path.c_str(), InputBuffer::ImplementationKind::kMemMapped);
  ASSERT_TRUE(buffer_or_err.ok());
  ASSERT_EQ(buffer_or_err->GetBufferSize(), data_size);
  ASSERT_EQ(buffer_or_err->GetBuffer().size(), data_size);
  for (size_t i = 0; i != data_size; ++i)
    EXPECT_EQ(buffer_or_err->GetBuffer()[i], write_data[i]);
}
#endif  // defined(__unix__)

TEST(InputBuffer, DefaultImplNonEmptyFile) {
  TmpFile tmp("cache.bin");
  constexpr size_t data_size = 36;
  std::vector<uint8_t> write_data(data_size);
  tmp.AppendData(write_data);

  auto buffer_or_err = InputBuffer::Create(tmp.path.c_str());
  ASSERT_TRUE(buffer_or_err.ok());
  ASSERT_EQ(buffer_or_err->GetBufferSize(), data_size);
}

}  // namespace
