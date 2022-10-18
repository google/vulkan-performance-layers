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

#include <inttypes.h>

#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <type_traits>

#include "debug_logging.h"
#include "layer_data.h"
#include "layer_utils.h"
#include "log_scanner.h"

namespace {
// ----------------------------------------------------------------------------
// Layer book-keeping information
// ----------------------------------------------------------------------------

constexpr uint32_t kFrameTimeLayerVersion = 1;
constexpr char kLayerName[] = "VK_LAYER_STADIA_frame_time";
constexpr char kLayerDescription[] = "Stadia Frame Time Measuring Layer";

constexpr char kLogFilenameEnvVar[] = "VK_FRAME_TIME_LOG";
constexpr char kExitAfterFrameEnvVar[] = "VK_FRAME_TIME_EXIT_AFTER_FRAME";
constexpr char kFinishFileEnvVar[] = "VK_FRAME_TIME_FINISH_FILE";
constexpr char kBenchmarkWatchFileEnvVar[] =
    "VK_FRAME_TIME_BENCHMARK_WATCH_FILE";
constexpr char kBenchmarkStartStringEnvVar[] =
    "VK_FRAME_TIME_BENCHMARK_START_STRING";

const char* StrOrEmpty(const char* str_or_null) {
  return str_or_null ? str_or_null : "";
}

class FrameTimeLayerData : public performancelayers::LayerData {
 public:
  FrameTimeLayerData(char* log_filename, uint64_t exit_frame_num_or_invalid,
                     const char* benchmark_watch_filename,
                     const char* benchmark_start_string)
      : LayerData(log_filename, "Frame Time (ns),Benchmark State"),
        exit_frame_num_or_invalid_(exit_frame_num_or_invalid),
        benchmark_start_pattern_(StrOrEmpty(benchmark_start_string)) {
    LogEventOnly("frame_time_layer_init");
    if (!benchmark_watch_filename || strlen(benchmark_watch_filename) == 0)
      return;

    benchmark_log_scanner_ =
        performancelayers::LogScanner::FromFilename(benchmark_watch_filename);
    if (benchmark_log_scanner_)
      benchmark_log_scanner_->RegisterWatchedPattern(benchmark_start_pattern_);
  }

  ~FrameTimeLayerData() override;

  static constexpr uint64_t kInvalidFrameNum = ~uint64_t(0);

  // Returns the next frame number.
  uint64_t IncrementFrameNum() { return ++current_frame_num_; }
  uint64_t GetExitFrameNum() const { return exit_frame_num_or_invalid_; }

  // Returns true if the benchmark gameplay start has been detected.
  // If benchmark start detection is not configured (through env vars),
  // assumes that the benchmarks begins with the first frame.
  bool HasBenchmarkStarted();

 private:
  const uint64_t exit_frame_num_or_invalid_;
  uint64_t current_frame_num_ = 0;

  uint32_t benchmark_state_idx_ = 0;
  std::string benchmark_start_pattern_;
  std::optional<performancelayers::LogScanner> benchmark_log_scanner_;
};

FrameTimeLayerData* GetLayerData() {
  auto GetExitAfterFrameVal = [] {
    if (const char* exit_after_frame_val_str = getenv(kExitAfterFrameEnvVar)) {
      std::stringstream ss;
      ss << exit_after_frame_val_str;
      uint64_t exit_frame_val = FrameTimeLayerData::kInvalidFrameNum;
      ss >> exit_frame_val;
      return exit_frame_val;
    }
    return FrameTimeLayerData::kInvalidFrameNum;
  };

  // Don't use new -- make the destructor run when the layer gets unloaded.
  static FrameTimeLayerData layer_data(
      getenv(kLogFilenameEnvVar), GetExitAfterFrameVal(),
      getenv(kBenchmarkWatchFileEnvVar), getenv(kBenchmarkStartStringEnvVar));
  return &layer_data;
}

// If |kFinishFileEnvVar| is set, this function will create a finish file with
// under |finishCause| and time written under that location.
void CreateFinishIndicatorFile(const char* finishCause) {
  assert(finishCause);
  const char* finish_indicator_file = getenv(kFinishFileEnvVar);
  if (!finish_indicator_file) return;

  FILE* finish_file = fopen(finish_indicator_file, "w");
  if (!finish_file) return;

  // Create the application finish indicator file and write the current time
  // there. This is to aid debugging when the modification time can be lost
  // when sending the file over a wire.
  std::time_t curr_time = std::time(nullptr);
  std::tm tm = *std::localtime(&curr_time);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%c %Z");
  fprintf(finish_file, "Stadia Frame Time Layer\n%s %s\n", finishCause,
          oss.str().c_str());
  fclose(finish_file);
}

bool FrameTimeLayerData::HasBenchmarkStarted() {
  if (benchmark_state_idx_ > 0 || benchmark_start_pattern_.empty()) return true;

  if (!benchmark_log_scanner_) return true;

  if (benchmark_log_scanner_->ConsumeNewLines()) {
    benchmark_state_idx_ = 1;
    benchmark_log_scanner_.reset();
    return true;
  }

  return false;
}

FrameTimeLayerData::~FrameTimeLayerData() {
  CreateFinishIndicatorFile("APPLICATION_EXIT");
  LogEventOnly("frame_time_layer_exit", "application_exit");
}

// Use this macro to define all vulkan functions intercepted by the layer.
#define SPL_FRAME_TIME_LAYER_FUNC(RETURN_TYPE_, FUNC_NAME_, FUNC_ARGS_)  \
  SPL_INTERCEPTED_VULKAN_FUNC(RETURN_TYPE_, FrameTimeLayer_, FUNC_NAME_, \
                              FUNC_ARGS_)

//////////////////////////////////////////////////////////////////////////////
//  Implementation of the instance functions we want to override.
//////////////////////////////////////////////////////////////////////////////

SPL_FRAME_TIME_LAYER_FUNC(VkResult, QueuePresentKHR,
                          (VkQueue queue,
                           const VkPresentInfoKHR* present_info)) {
  auto* layer_data = GetLayerData();
  layer_data->LogTimeDelta("frame_present",
                           layer_data->HasBenchmarkStarted() ? "1" : "0");

  uint64_t frames_elapsed = layer_data->IncrementFrameNum();
  uint64_t exit_frame_num = layer_data->GetExitFrameNum();
  // If the layer should make Vulkan application exit after this frame.
  if (frames_elapsed == exit_frame_num) {
    SPL_LOG(INFO)
        << "Stadia Frame Time Layer: Terminating application after frame "
        << frames_elapsed;

    // _Exit will bring down the parent Vulkan application without running any
    // cleanup. Resources will be reclaimed by the operating system.
    CreateFinishIndicatorFile("FRAME_TIME_LAYER_TERMINATED");
    layer_data->LogEventOnly("frame_time_layer_exit",
                             absl::StrCat("terminated,frame:", frames_elapsed));
    std::_Exit(99);
  }

  auto next_proc = layer_data->GetNextDeviceProcAddr(
      queue, &VkLayerDispatchTable::QueuePresentKHR);
  return next_proc(queue, present_info);
}

// Override for vkDestroyInstance.  Deletes the entry for |instance| from the
// layer data.
SPL_FRAME_TIME_LAYER_FUNC(void, DestroyInstance,
                          (VkInstance instance,
                           const VkAllocationCallbacks* allocator)) {
  auto* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextInstanceProcAddr(
      instance, &VkLayerInstanceDispatchTable::DestroyInstance);
  layer_data->RemoveInstance(instance);
  next_proc(instance, allocator);
}

// Override for vkCreateInstance.  Creates the dispatch table for this instance
// and add it to the layer data.
SPL_FRAME_TIME_LAYER_FUNC(VkResult, CreateInstance,
                          (const VkInstanceCreateInfo* create_info,
                           const VkAllocationCallbacks* allocator,
                           VkInstance* instance)) {
  auto build_dispatch_table =
      [instance](PFN_vkGetInstanceProcAddr get_proc_addr) {
        // Build dispatch table for the instance functions we need to call.
        VkLayerInstanceDispatchTable dispatch_table{};

        // Get the next layer's instance of the instance functions we will
        // override.
        SPL_DISPATCH_INSTANCE_FUNC(DestroyInstance);
        SPL_DISPATCH_INSTANCE_FUNC(GetInstanceProcAddr);
        return dispatch_table;
      };

  return GetLayerData()->CreateInstance(create_info, allocator, instance,
                                        build_dispatch_table);
}

//////////////////////////////////////////////////////////////////////////////
//  Implementation of the device function we want to override.
//////////////////////////////////////////////////////////////////////////////

// Override for vkDestroyDevice.  Removes the dispatch table for the device from
// the layer data.
SPL_FRAME_TIME_LAYER_FUNC(void, DestroyDevice,
                          (VkDevice device,
                           const VkAllocationCallbacks* allocator)) {
  auto* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::DestroyDevice);
  layer_data->RemoveDevice(device);
  next_proc(device, allocator);
}

// Override for vkCreateDevice.  Builds the dispatch table for the new device
// and add it to the layer data.
SPL_FRAME_TIME_LAYER_FUNC(VkResult, CreateDevice,
                          (VkPhysicalDevice physical_device,
                           const VkDeviceCreateInfo* create_info,
                           const VkAllocationCallbacks* allocator,
                           VkDevice* device)) {
  auto build_dispatch_table = [device](PFN_vkGetDeviceProcAddr gdpa) {
    VkLayerDispatchTable dispatch_table{};

    // Get the next layer's instance of the device functions we will override.
    SPL_DISPATCH_DEVICE_FUNC(DestroyDevice);
    SPL_DISPATCH_DEVICE_FUNC(GetDeviceProcAddr);
    SPL_DISPATCH_DEVICE_FUNC(QueuePresentKHR);
    return dispatch_table;
  };

  return GetLayerData()->CreateDevice(physical_device, create_info, allocator,
                                      device, build_dispatch_table);
}

SPL_FRAME_TIME_LAYER_FUNC(VkResult, EnumerateInstanceLayerProperties,
                          (uint32_t * property_count,
                           VkLayerProperties* properties)) {
  if (property_count) *property_count = 1;

  if (properties) {
    strncpy(properties->layerName, kLayerName, sizeof(properties->layerName));
    strncpy(properties->description, kLayerDescription,
            sizeof(properties->description));
    properties->implementationVersion = kFrameTimeLayerVersion;
    properties->specVersion = VK_API_VERSION_1_0;
  }

  return VK_SUCCESS;
}

SPL_FRAME_TIME_LAYER_FUNC(VkResult, EnumerateDeviceLayerProperties,
                          (VkPhysicalDevice /* physical_device */,
                           uint32_t* property_count,
                           VkLayerProperties* properties)) {
  return FrameTimeLayer_EnumerateInstanceLayerProperties(property_count,
                                                         properties);
}

}  // namespace

// The *GetProcAddr functions are the entry points to the layers.
// They return a function pointer for the instance requested by |name|.  We
// return the functions defined in this layer for those we want to override.
// Otherwise we call the *GetProcAddr function for the next layer to get the
// function to be called.

SPL_LAYER_ENTRY_POINT SPL_FRAME_TIME_LAYER_FUNC(PFN_vkVoidFunction,
                                                GetDeviceProcAddr,
                                                (VkDevice device,
                                                 const char* name)) {
  if (auto func =
          performancelayers::FunctionInterceptor::GetInterceptedOrNull(name)) {
    return func;
  }

  auto* layer_data = GetLayerData();

  PFN_vkGetDeviceProcAddr next_get_proc_addr =
      layer_data->GetNextDeviceProcAddr(
          device, &VkLayerDispatchTable::GetDeviceProcAddr);
  assert(next_get_proc_addr && next_get_proc_addr != VK_NULL_HANDLE);
  return next_get_proc_addr(device, name);
}

SPL_LAYER_ENTRY_POINT SPL_FRAME_TIME_LAYER_FUNC(PFN_vkVoidFunction,
                                                GetInstanceProcAddr,
                                                (VkInstance instance,
                                                 const char* name)) {
  if (auto func =
          performancelayers::FunctionInterceptor::GetInterceptedOrNull(name)) {
    return func;
  }

  auto* layer_data = GetLayerData();

  PFN_vkGetInstanceProcAddr next_get_proc_addr =
      layer_data->GetNextInstanceProcAddr(
          instance, &VkLayerInstanceDispatchTable::GetInstanceProcAddr);
  assert(next_get_proc_addr && next_get_proc_addr != VK_NULL_HANDLE);
  return next_get_proc_addr(instance, name);
}
