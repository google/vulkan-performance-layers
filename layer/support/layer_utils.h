// Copyright 2020-2021 Google LLC
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

#ifndef STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_UTILS_H_
#define STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_UTILS_H_

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ratio>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "vulkan/vulkan.h"
#include "vulkan/vulkan_core.h"

// Function attributes for layer entry points. Place these in front of function
// delcarations.
#if defined(_WIN32)
#define SPL_LAYER_ENTRY_POINT extern "C" _declspec(dllexport)
#else
#define SPL_LAYER_ENTRY_POINT extern "C" __attribute__((visibility("default")))
#endif

// Helper macro for initializing dispatch table with instance functions.
// This assumes a dispatch table builder code:
// 1. Has a VkLayerInstanceDispatchTable named |dispatch_table|.
// 2. Has an instance handle pointer named |instance|.
// 3. Has a PFN_vkGetInstanceProcAddr function named |get_proc_addr|.
//
// In terms of Vulkan function pointers, this macro is type safe.
#define SPL_DISPATCH_INSTANCE_FUNC(FUNC_NAME_)                      \
  dispatch_table.FUNC_NAME_ = reinterpret_cast<PFN_vk##FUNC_NAME_>( \
      get_proc_addr(*instance, "vk" #FUNC_NAME_))

// Helper macro for initializing dispatch table with device functions.
// This assumes a dispatch table builder code:
// 1. Has a VkLayerInstanceDispatchTable named |dispatch_table|.
// 2. Has an device handle pointer named |device|.
// 3. Has a PFN_vkGetDeviceProcAddr function named |gdpa|.
//
// In terms of Vulkan function pointers, this macro is type safe.
#define SPL_DISPATCH_DEVICE_FUNC(FUNC_NAME_) \
  dispatch_table.FUNC_NAME_ =                \
      reinterpret_cast<PFN_vk##FUNC_NAME_>(gdpa(*device, "vk" #FUNC_NAME_))

namespace performancelayers {
// steady_clock, system_clock, and high_resolution_clock comparison:
// 1. steady_clock: A monotonic clock. The current value of steady_clock does
// not matter, but the guarantee that is strictly increasing is useful for
// calculating the difference between two samples.
// 2. system_clock: Is the wall-clock. Used for time/day related stuff.
// 3. high_resolution_clock: An alias for one of the above.
using TimestampClock = std::chrono::system_clock;
using DurationClock = std::chrono::steady_clock;

// Returns a system_clock::time_point as the timestamp. The system_clock acts
// like a real clock and shows the current time, while the steady_clock is a
// monotonic clock and behaves like a chronometer. This is why system_clock is
// used for the timestamp.
TimestampClock::time_point GetTimestamp();

// Returns a monotonic time_point to be used for measuring duration.
DurationClock::time_point Now();

// Converts a chrono time_point to a Unix int64 nanoseconds representation.
int64_t ToUnixNanos(TimestampClock::time_point time);

// Converts a chrono time_point to a Unix int64 milliseconds representation.
double ToUnixMillis(TimestampClock::time_point time);

// A wrapper around `DurationClock::duration` to keep track of the time unit.
// When the `Duration` is used, we know it's either created from a
// `DurationClock::duration` that has nanosecond-level precision or an int that
// represents duration in nanoseconds.
class Duration {
 public:
  static Duration FromNanoseconds(int64_t nanos) {
    return Duration(DurationClock::duration(nanos));
  }

  Duration(DurationClock::duration duration) : duration_{duration} {}

  int64_t ToNanoseconds() const {
    return std::chrono::nanoseconds(duration_).count();
  }

  double ToMilliseconds() const {
    return std::chrono::duration<double, std::milli>(duration_).count();
  }

 private:
  DurationClock::duration duration_;
};

// Represents a type-erased layer function pointer intercepting a known
// Vulkan function. Should be constructed with the type safe |Create|
// function.
// Note: This is an internal code, do not use directly. See
//       SPL_INTERCEPTED_VULKAN_FUNC below for more details.
//
// Sample use:
//   auto intercepted_func =
//     InterceptedVulkanFunc::Create<&vkDestroyDevice,
//                                   &MyLayer_DestroyDevice>("vkDestroyDevice");
//
// This will check that the two function pointers have the same type.
struct InterceptedVulkanFunc {
  template <auto VulkanFuncPtr, auto LayerFuncPtr>
  static InterceptedVulkanFunc Create(const char* vulkan_func_name) {
    static_assert(
        std::is_same_v<decltype(VulkanFuncPtr), decltype(LayerFuncPtr)>,
        "Vulkan and Layer function types do not match. Does your Layer "
        "function have the right signature?");
    assert(vulkan_func_name);

    std::string_view vulkan_func_str(vulkan_func_name);
    (void)vulkan_func_str;
    assert(!vulkan_func_str.empty());
    assert(vulkan_func_str.find("vk") == 0 &&
           "Vulkan function names must start with 'vk'.");

    return {vulkan_func_name,
            reinterpret_cast<PFN_vkVoidFunction>(LayerFuncPtr)};
  }

  const char* vulkan_function_name;
  PFN_vkVoidFunction layer_function;
};

// Helper class to automatically register intercepted Vulkan functions.
// Maintains a global map of all intercepted functions. Adds new map entries
// into this map upon constructions. Expects each function to be registered at
// most once.
//
// Layer code can check if a vulkan function has been registered by calling:
//   performancelayers::FunctionInterceptor::GetInterceptedOrNull(vk_name)
class FunctionInterceptor {
 public:
  FunctionInterceptor(InterceptedVulkanFunc intercepted_function);

  FunctionInterceptor(const FunctionInterceptor&) = delete;
  FunctionInterceptor(FunctionInterceptor&&) = delete;
  FunctionInterceptor& operator=(const FunctionInterceptor&) = delete;
  FunctionInterceptor& operator=(FunctionInterceptor&&) = delete;

  static PFN_vkVoidFunction GetInterceptedOrNull(
      std::string_view vk_function_name);

 private:
  using FunctionNameToPtr =
      absl::flat_hash_map<std::string_view, PFN_vkVoidFunction>;

  static FunctionNameToPtr& GetInterceptedFunctions();
};

// Function attributes for intercepted vulkan functions. These are added
// automatically by SPL_INTERCEPTED_VULKAN_FUNC.
#define SPL_LAYER_FUNCTION_ATTRIBUTES(RETURN_TYPE_) \
  VKAPI_ATTR RETURN_TYPE_ VKAPI_CALL

#define SPL_INTERNAL_CAT_IMPL_(X_, Y_) X_##Y_
#define SPL_INTERNAL_CAT_(X_, Y_) SPL_INTERNAL_CAT_IMPL_(X_, Y_)

// Creates a layer function declaration and automatically registers this
// functions in the layer-global map of intercepted Vulkan functions.
// This macro *must* be used to define all intercepted functions.
//
// This also makes sure that the overriden
// Vulkan function has the same signature as the layer function being defined.
//
// Sample use:
// 1. Define a layer-specific macro:
//    #define SPL_MY_LAYER_FUNC(RETURN_TYPE_, FUNC_NAME_, FUNC_ARGS_)
//      SPL_INTERCEPTED_VULKAN_FUNC(RETURN_TYPE_, MyLayer_, FUNC_NAME_,
//                                  FUNC_ARGS_)
//
// 2. Define an intercepted function:
//    SPL_FRAME_MY_LAYER_FUNC(VkResult, QueuePresentKHR,
//                            (VkQueue q, const VkPresentInfoKHR* pi)) {
//      ... function body ...
//
// This will generate a function definition and declaration that looks
// like below, and intercepts the vkQueuePresentKHR function:
//   VkResult MyLayer_QueuePresentKHR(VkQueue q, const VkPresentInfoKHR *pi) {
//     ... function body ...
//
#define SPL_INTERCEPTED_VULKAN_FUNC(RETURN_TYPE_, LAYER_PREFIX_, FUNC_NAME_,   \
                                    FUNC_ARGS_)                                \
  SPL_LAYER_FUNCTION_ATTRIBUTES(RETURN_TYPE_)                                  \
  LAYER_PREFIX_##FUNC_NAME_ FUNC_ARGS_;                                        \
  static const auto* const SPL_INTERNAL_CAT_(kSPL_internal_intercepted_func_,  \
                                             __LINE__) =                       \
      new performancelayers::FunctionInterceptor(                              \
          performancelayers::InterceptedVulkanFunc::Create<                    \
              &vk##FUNC_NAME_, &LAYER_PREFIX_##FUNC_NAME_>("vk" #FUNC_NAME_)); \
  RETURN_TYPE_ LAYER_PREFIX_##FUNC_NAME_ FUNC_ARGS_

}  // namespace performancelayers

#endif  // STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_UTILS_H_
