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

#ifndef STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_INPUT_BUFFER_H_
#define STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_INPUT_BUFFER_H_

#include <cassert>
#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace performancelayers {

// Represents a memory buffer, created with a path to the (on-disk) resource
// to open and read.
// Moveable but not copyable.
class InputBuffer {
 public:
  enum class ImplementationKind { kFileRead, kMemMapped };

  // Creates an input buffer by accessing the |path| file using a default
  // platform-preferred implementation.
  static absl::StatusOr<InputBuffer> Create(const std::string& path);

  // Creates an input buffer by accessing the |path| file using the speficied
  // |requested_implementation|. Returns an |absl::UnavailableError| if the
  // requested implementation is not supported for the current platform.
  static absl::StatusOr<InputBuffer> Create(
      const std::string& path, ImplementationKind requested_implementation);

  InputBuffer(InputBuffer&&) = default;
  InputBuffer& operator=(InputBuffer&&) = default;

  InputBuffer() = delete;
  InputBuffer(const InputBuffer&) = delete;
  InputBuffer& operator=(const InputBuffer&) = delete;

  absl::Span<const uint8_t> GetBuffer() const {
    assert(concrete_impl_);
    return concrete_impl_->GetBuffer();
  }

  size_t GetBufferSize() const { return GetBuffer().size(); }

  class InputBufferImplBase {
   public:
    virtual ~InputBufferImplBase() = default;
    virtual absl::Span<const uint8_t> GetBuffer() const = 0;
  };

 private:
  InputBuffer(std::unique_ptr<InputBufferImplBase> impl)
      : concrete_impl_(std::move(impl)) {
    assert(concrete_impl_);
  }
  std::unique_ptr<InputBufferImplBase> concrete_impl_ = nullptr;
};

}  // namespace performancelayers

#endif  // STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_INPUT_BUFFER_H_
