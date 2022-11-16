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

#include "layer/support/input_buffer.h"

#include <cassert>
#include <cstdio>
#include <vector>

#include "absl/strings/str_cat.h"
#include "layer/support/debug_logging.h"

#if defined(__unix__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace performancelayers {

namespace {
// Cross-platfrom input buffer implementation that reads file contents using
// fopen/fread/fclose.
class FileInputBufferImpl : public InputBuffer::InputBufferImplBase {
 public:
  static absl::StatusOr<std::unique_ptr<InputBuffer::InputBufferImplBase>>
  Create(const std::string& path) {
    struct FileCloser {
      void operator()(FILE* file) {
        if (file) fclose(file);
      }
    };
    std::unique_ptr<FILE, FileCloser> handle(fopen(path.c_str(), "rb"));
    if (!handle) {
      return absl::UnavailableError(
          absl::StrCat("Failed to fopen file for read: ", path));
    }

    fseek(handle.get(), 0, SEEK_END);
    const auto file_size = ftell(handle.get());
    fseek(handle.get(), 0, SEEK_SET);
    if (file_size < 0) {
      return absl::UnavailableError(
          absl::StrCat("Failed to read file size: ", path));
    }

    const auto buffer_size = static_cast<size_t>(file_size);
    FileInputBufferImpl res;
    res.buffer_.resize(buffer_size);
    if (fread(res.buffer_.data(), 1, buffer_size, handle.get()) !=
        buffer_size) {
      return absl::UnavailableError(
          absl::StrCat("Failed to read file buffer contents: ", path));
    }

    return std::make_unique<FileInputBufferImpl>(std::move(res));
  }

  absl::Span<const uint8_t> GetBuffer() const override { return buffer_; }
  ~FileInputBufferImpl() override = default;

 private:
  std::vector<uint8_t> buffer_;
};

#if defined(__unix__)
// Unix memory mapped input buffer implementation.
class UnixMemMappedInputBufferImpl : public InputBuffer::InputBufferImplBase {
 public:
  static absl::StatusOr<std::unique_ptr<InputBuffer::InputBufferImplBase>>
  Create(const std::string& path) {
    UnixMemMappedInputBufferImpl res;
    res.file_descriptor_ = open(path.c_str(), O_RDONLY);
    if (res.file_descriptor_ == -1) {
      return absl::UnavailableError(
          absl::StrCat("Failed to open file for read: ", path));
    }

    struct stat s = {};
    if (fstat(res.file_descriptor_, &s) != 0) {
      return absl::UnavailableError(
          absl::StrCat("Failed to stat file: ", path));
    }
    size_t size = s.st_size;
    if (size == 0) {
      // We cannot mmap empty files, but we can return an empty buffer without
      // mmaping the underlying file.
      return std::make_unique<UnixMemMappedInputBufferImpl>(std::move(res));
    }

    // This memory is read-only, but we do not use a const pointer, because
    // munmap takes the memory to unmap through a void * pointer. No non-const
    // pointers ever leave this class.
    void* data = mmap(0, size, PROT_READ, MAP_PRIVATE, res.file_descriptor_, 0);
    if (data == MAP_FAILED) {
      return absl::UnavailableError(
          absl::StrCat("Failed to mmap file: ", path));
    }

    res.buffer_start_ = reinterpret_cast<uint8_t*>(data);
    res.buffer_size_ = size;
    SPL_LOG(INFO) << "Mmapped file " << path << " sz: " << size;
    return std::make_unique<UnixMemMappedInputBufferImpl>(std::move(res));
  }

  absl::Span<const uint8_t> GetBuffer() const override {
    assert(file_descriptor_ != -1);
    assert(buffer_start_ != MAP_FAILED);
    assert(buffer_start_ || buffer_size_ == 0);
    return absl::MakeConstSpan(buffer_start_, buffer_size_);
  }

  UnixMemMappedInputBufferImpl(UnixMemMappedInputBufferImpl&& other)
      : file_descriptor_(other.file_descriptor_),
        buffer_start_(other.buffer_start_),
        buffer_size_(other.buffer_size_) {
    other.file_descriptor_ = -1;
    other.buffer_start_ = nullptr;
    other.buffer_size_ = 0;
  }

  UnixMemMappedInputBufferImpl& operator=(
      UnixMemMappedInputBufferImpl&& other) {
    reset();
    std::swap(file_descriptor_, other.file_descriptor_);
    std::swap(buffer_start_, other.buffer_start_);
    std::swap(buffer_size_, other.buffer_size_);
    return *this;
  }

  UnixMemMappedInputBufferImpl(const UnixMemMappedInputBufferImpl&) = delete;
  UnixMemMappedInputBufferImpl& operator=(const UnixMemMappedInputBufferImpl&) =
      delete;

  ~UnixMemMappedInputBufferImpl() override { reset(); }

 private:
  UnixMemMappedInputBufferImpl() = default;

  void reset() {
    if (buffer_start_) munmap(buffer_start_, buffer_size_);
    if (file_descriptor_ != -1) close(file_descriptor_);

    file_descriptor_ = -1;
    buffer_start_ = nullptr;
    buffer_size_ = 0;
  }

  int file_descriptor_ = -1;
  // This pointer is passed to munmap and cannot be const. The API does not
  // expose it directly, and no non-const pointers ever leave the class.
  uint8_t* buffer_start_ = nullptr;
  size_t buffer_size_ = 0;
};
#endif  // defined(__unix__)

}  // namespace

absl::StatusOr<InputBuffer> InputBuffer::Create(
    const std::string& path,
    InputBuffer::ImplementationKind requested_implementation) {
  switch (requested_implementation) {
    case ImplementationKind::kFileRead: {
      auto file_input_buffer_or_err = FileInputBufferImpl::Create(path);
      if (file_input_buffer_or_err.ok())
        return InputBuffer(std::move(*file_input_buffer_or_err));

      return file_input_buffer_or_err.status();
    }
    case ImplementationKind::kMemMapped: {
#if defined(__unix__)
      auto mmap_input_buffer_or_err =
          UnixMemMappedInputBufferImpl::Create(path);
      if (mmap_input_buffer_or_err.ok())
        return InputBuffer(std::move(*mmap_input_buffer_or_err));

      return mmap_input_buffer_or_err.status();
#else   // defined(__unix__)
      return absl::UnavailableError(
          "kMemMapped InputBuffer is implemented for this platform");
#endif  // defined(__unix__)
    }
    default:
      assert(false && "Case not handled.");
      return absl::InternalError("Case not handled");
  }
}

absl::StatusOr<InputBuffer> InputBuffer::Create(const std::string& path) {
#if defined(__unix__)
  constexpr auto kPreferredPlatformImpl =
      InputBuffer::ImplementationKind::kMemMapped;
#else
  constexpr auto kPreferredPlatformImpl =
      InputBuffer::ImplementationKind::kFileRead;
#endif  // defined(__unix__)

  return Create(path, kPreferredPlatformImpl);
}

}  // namespace performancelayers
