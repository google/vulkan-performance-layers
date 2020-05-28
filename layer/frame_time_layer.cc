// Copyright 2020 Google LLC
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

#include <ctime>
#include <iomanip>
#include <sstream>

#include "layer_data.h"

#undef VK_LAYER_EXPORT
#ifndef _WIN32
#define VK_LAYER_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define VK_LAYER_EXPORT extern "C" _declspec(dllexport)
#endif

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

class FrameTimeLayerData : public performancelayers::LayerData {
 public:
  FrameTimeLayerData(char* log_filename, uint64_t exit_frame_num_or_invalid)
      : LayerData(log_filename, "Frame Time (ns)"),
        exit_frame_num_or_invalid_(exit_frame_num_or_invalid) {}

  // Records the device that owns |queue|.
  void SetDevice(VkQueue queue, VkDevice device) {
    absl::MutexLock lock(&queue_to_device_lock_);
    queue_to_device_.insert_or_assign(queue, device);
  }

  // Returns the device that owns |cmd_buffer|.
  VkDevice GetDevice(VkQueue queue) const {
    absl::MutexLock lock(&queue_to_device_lock_);
    assert(queue_to_device_.count(queue) != 0);
    return queue_to_device_.at(queue);
  }

  static constexpr uint64_t kInvalidFrameNum = ~uint64_t(0);

  // Returns the next frame number.
  uint64_t IncrementFrameNum() { return ++current_frame_num_; }
  uint64_t GetExitFrameNum() const { return exit_frame_num_or_invalid_; }

 private:
  const uint64_t exit_frame_num_or_invalid_;
  uint64_t current_frame_num_ = 0;

  mutable absl::Mutex queue_to_device_lock_;
  // The map from a queue to the device that owns it.
  absl::flat_hash_map<VkQueue, VkDevice> queue_to_device_
      ABSL_GUARDED_BY(queue_to_device_lock_);
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
  static FrameTimeLayerData* layer_data = new FrameTimeLayerData(
      getenv(kLogFilenameEnvVar), GetExitAfterFrameVal());
  return layer_data;
}

//////////////////////////////////////////////////////////////////////////////
//  Implementation of the instance functions we want to override.
//////////////////////////////////////////////////////////////////////////////

VKAPI_ATTR VkResult FrameTimeLayer_QueuePresentKHR(
    VkQueue queue, const VkPresentInfoKHR* present_info) {
  auto* layer_data = GetLayerData();
  layer_data->LogTimeDelta();

  uint64_t frames_elapsed = layer_data->IncrementFrameNum();
  uint64_t exit_frame_num = layer_data->GetExitFrameNum();
  // If the layer should make Vulkan application exit after this frame.
  if (exit_frame_num != FrameTimeLayerData::kInvalidFrameNum &&
      frames_elapsed >= exit_frame_num) {
    if (const char* finish_indicator_file = getenv(kFinishFileEnvVar)) {
      // Create the application finish indicator file and write the current time
      // there. This is to aid debugging when the modification time can be lost
      // when sending the file over a wire.
      if (FILE* finish_file = fopen(finish_indicator_file, "w")) {
        std::time_t curr_time = std::time(nullptr);
        std::tm tm = *std::localtime(&curr_time);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%c %Z");
        fprintf(finish_file, "Stadia Frame Time Layer\nTemninated on %s\n",
                oss.str().c_str());
        fclose(finish_file);
      }
    }

    fprintf(
        stderr,
        "Stadia Frame Time Layer: Terminating application after frame %" PRIu64
        "\n",
        frames_elapsed);
    fflush(stderr);
    // _Exit will bring down the parent Vulkan application without running any
    // cleanup. Resources will be reclaimed by the operating system.
    // Delete to force the destructor to save the layer log files.
    delete layer_data;
    std::_Exit(99);
  }

  auto device = layer_data->GetDevice(queue);
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::QueuePresentKHR);
  return (next_proc)(queue, present_info);
}

VKAPI_ATTR void FrameTimeLayer_GetDeviceQueue(VkDevice device,
                                              uint32_t queue_family_index,
                                              uint32_t queue_index,
                                              VkQueue* queue) {
  auto* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::GetDeviceQueue);
  (next_proc)(device, queue_family_index, queue_index, queue);
  if (queue && *queue) {
    layer_data->SetDevice(*queue, device);
  }
}

VKAPI_ATTR void FrameTimeLayer_GetDeviceQueue2(
    VkDevice device, const VkDeviceQueueInfo2* queue_info, VkQueue* queue) {
  auto* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::GetDeviceQueue2);
  (next_proc)(device, queue_info, queue);
  if (queue && *queue) {
    layer_data->SetDevice(*queue, device);
  }
}

// Override for vkDestroyDevice.  Deletes the entry for |instance| from the
// layer data.
void FrameTimeLayer_DestroyInstance(VkInstance instance,
                                    const VkAllocationCallbacks* allocator) {
  auto* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextInstanceProcAddr(
      instance, &VkLayerInstanceDispatchTable::DestroyInstance);
  (next_proc)(instance, allocator);
  layer_data->RemoveInstance(instance);
}

// Override for vkCreateInstance.  Creates the dispatch table for this instance
// and add it to the layer data.
VkResult FrameTimeLayer_CreateInstance(const VkInstanceCreateInfo* create_info,
                                       const VkAllocationCallbacks* allocator,
                                       VkInstance* instance) {
  auto build_dispatch_table =
      [instance](PFN_vkGetInstanceProcAddr get_proc_addr) {
        // Build dispatch table for the instance functions we need to call.
        VkLayerInstanceDispatchTable dispatch_table;

    // Get the next layer's instance of the instance functions we will override.
#define ASSIGN_PROC(name) \
  dispatch_table.name =   \
      reinterpret_cast<PFN_vk##name>(get_proc_addr(*instance, "vk" #name))
        ASSIGN_PROC(DestroyInstance);
        ASSIGN_PROC(GetInstanceProcAddr);
#undef ASSIGN_PROC
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
void FrameTimeLayer_DestroyDevice(VkDevice device,
                                  const VkAllocationCallbacks* allocator) {
  auto* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::DestroyDevice);
  (next_proc)(device, allocator);
  layer_data->RemoveDevice(device);
}

// Override for vkCreateDevice.  Builds the dispatch table for the new device
// and add it to the layer data.
VkResult FrameTimeLayer_CreateDevice(VkPhysicalDevice physical_device,
                                     const VkDeviceCreateInfo* create_info,
                                     const VkAllocationCallbacks* allocator,
                                     VkDevice* device) {
  auto build_dispatch_table = [device](PFN_vkGetDeviceProcAddr gdpa) {
    VkLayerDispatchTable dispatch_table;

    // Get the next layer's instance of the device functions we will override.
#define ASSIGN_PROC(name) \
  dispatch_table.name =   \
      reinterpret_cast<PFN_vk##name>(gdpa(*device, "vk" #name))
    ASSIGN_PROC(GetDeviceProcAddr);
    ASSIGN_PROC(DestroyDevice);
    ASSIGN_PROC(QueuePresentKHR);
    ASSIGN_PROC(GetDeviceQueue);
    ASSIGN_PROC(GetDeviceQueue2);
#undef ASSIGN_PROC
    return dispatch_table;
  };

  return GetLayerData()->CreateDevice(physical_device, create_info, allocator,
                                      device, build_dispatch_table);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
FrameTimeLayer_EnumerateInstanceLayerProperties(uint32_t* property_count,
                                                VkLayerProperties* properties) {
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

VK_LAYER_EXPORT VkResult VKAPI_CALL
FrameTimeLayer_EnumerateDeviceLayerProperties(
    VkPhysicalDevice /* physical_device */, uint32_t* property_count,
    VkLayerProperties* properties) {
  return FrameTimeLayer_EnumerateInstanceLayerProperties(property_count,
                                                         properties);
}

}  // namespace

// The *GetProcAddr functions are the entry points to the layers.
// They return a function pointer for the instance requested by |name|.  We
// return the functions defined in this layer for those we want to override.
// Otherwise we call the *GetProcAddr function for the next layer to get the
// function to be called.

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL
FrameTimeLayer_GetDeviceProcAddr(VkDevice device, const char* name) {
  // Device functions we intercept.

#define CHECK_FUNC(func_name)                                                 \
  if (!strcmp(name, "vk" #func_name)) {                                       \
    return reinterpret_cast<PFN_vkVoidFunction>(&FrameTimeLayer_##func_name); \
  }
  CHECK_FUNC(DestroyDevice);
  CHECK_FUNC(QueuePresentKHR);
  CHECK_FUNC(GetDeviceQueue);
  CHECK_FUNC(GetDeviceQueue2);
  CHECK_FUNC(EnumerateDeviceLayerProperties);
#undef CHECK_FUNC

  auto* layer_data = GetLayerData();

  PFN_vkGetDeviceProcAddr next_get_proc_addr;
  next_get_proc_addr = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::GetDeviceProcAddr);
  assert(next_get_proc_addr && next_get_proc_addr != VK_NULL_HANDLE);
  return next_get_proc_addr(device, name);
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL
FrameTimeLayer_GetInstanceProcAddr(VkInstance instance, const char* name) {
#define CHECK_FUNC(func_name)                                                 \
  if (!strcmp(name, "vk" #func_name)) {                                       \
    return reinterpret_cast<PFN_vkVoidFunction>(&FrameTimeLayer_##func_name); \
  }
  CHECK_FUNC(GetInstanceProcAddr);
  CHECK_FUNC(EnumerateInstanceLayerProperties);
  CHECK_FUNC(DestroyInstance);
  CHECK_FUNC(CreateInstance);
  CHECK_FUNC(CreateDevice);
  CHECK_FUNC(EnumerateDeviceLayerProperties);
#undef CHECK_FUNC

  auto* layer_data = GetLayerData();

  PFN_vkGetInstanceProcAddr next_get_proc_addr;
  next_get_proc_addr = layer_data->GetNextInstanceProcAddr(
      instance, &VkLayerInstanceDispatchTable::GetInstanceProcAddr);
  assert(next_get_proc_addr && next_get_proc_addr != VK_NULL_HANDLE);
  return next_get_proc_addr(instance, name);
}
