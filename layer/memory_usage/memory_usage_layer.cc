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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "layer/support/csv_logging.h"
#include "layer/support/debug_logging.h"
#include "layer/support/event_logging.h"
#include "layer/support/layer_data.h"
#include "layer/support/layer_utils.h"

namespace performancelayers {
namespace {
// ----------------------------------------------------------------------------
// Layer book-keeping information
// ----------------------------------------------------------------------------

constexpr char kLogFilenameEnvVar[] = "VK_MEMORY_USAGE_LOG";

// An event that holds memory allocation information (current and peak
// allocated) and can be logged both in the private and common files.
class MemoryUsageEvent : public Event {
 public:
  MemoryUsageEvent(const char* name, int64_t current, int64_t peak)
      : Event(name, LogLevel::kHigh),
        current_({"current", current}),
        peak_({"peak", peak}),
        trace_attr_("trace_attr", "memory_usage", "i",
                    {&scope_, &current_, &peak_}) {
    InitAttributes({&current_, &peak_, &trace_attr_});
  }

 private:
  Int64Attr current_;
  Int64Attr peak_;
  // `Perfetto` displays the args only for instant events with thread-level
  // scope.
  StringAttr scope_{"scope", "t"};
  TraceEventAttr trace_attr_;
};

class MemoryUsageLayerData : public LayerData {
 public:
  explicit MemoryUsageLayerData(char* log_filename)
      : LayerData(log_filename, "Current (bytes), peak (bytes)") {
    LayerInitEvent event("memory_usage_layer_init", "memory_usage");
    LogEvent(&event);
  }

  void RecordAllocateMemory(VkDevice device, VkDeviceMemory memory,
                            VkDeviceSize size) {
    absl::MutexLock lock(&memory_hash_lock_);
    bool inserted;
    std::tie(std::ignore, inserted) =
        memory_hash_map_.try_emplace({device, memory}, size);
    assert(inserted);
    current_allocation_size_ += size;
    peak_allocation_size_ =
        std::max(peak_allocation_size_, current_allocation_size_);
  }

  void RecordFreeMemory(VkDevice device, VkDeviceMemory memory) {
    absl::MutexLock lock(&memory_hash_lock_);
    auto it = memory_hash_map_.find({device, memory});
    assert(it != memory_hash_map_.end());
    VkDeviceSize size = it->second;
    memory_hash_map_.erase(it);

    assert(size <= current_allocation_size_);
    current_allocation_size_ -= size;
  }

  void RecordDestroyDeviceMemory(VkDevice device) {
    absl::MutexLock lock(&memory_hash_lock_);
    VkDeviceSize size = 0;
    for (auto it = memory_hash_map_.begin(); it != memory_hash_map_.end();
         ++it) {
      if (it->first.first == device) {
        size += it->second;
        // Note that absl::flat_hash_map does not invalidate iterators on erase.
        memory_hash_map_.erase(it);
      }
    }
    assert(size <= current_allocation_size_);
    current_allocation_size_ -= size;
  }

  uint64_t GetPeakAllocationSize() const {
    absl::MutexLock ReaderLock(&memory_hash_lock_);
    return peak_allocation_size_;
  }

  uint64_t GetCurrentAllocationSize() const {
    absl::MutexLock ReaderLock(&memory_hash_lock_);
    return current_allocation_size_;
  }

 private:
  mutable absl::Mutex memory_hash_lock_;
  // The map from device, memory tuple to its allocation size. TODO: Should be a
  // two-level map, so that per-device data can be purged easily on
  // DestroyDevice.
  absl::flat_hash_map<std::pair<VkDevice, VkDeviceMemory>, VkDeviceSize>
      memory_hash_map_ ABSL_GUARDED_BY(memory_hash_lock_);

  VkDeviceSize current_allocation_size_ ABSL_GUARDED_BY(memory_hash_lock_) = 0;
  VkDeviceSize peak_allocation_size_ ABSL_GUARDED_BY(memory_hash_lock_) = 0;
};

MemoryUsageLayerData* GetLayerData() {
  // Don't use new -- make the destructor run when the layer gets unloaded.
  static MemoryUsageLayerData layer_data(getenv(kLogFilenameEnvVar));
  return &layer_data;
}

// Use this macro to define all vulkan functions intercepted by the layer.
#define SPL_MEMORY_USAGE_LAYER_FUNC(RETURN_TYPE_, FUNC_NAME_, FUNC_ARGS_)  \
  SPL_INTERCEPTED_VULKAN_FUNC(RETURN_TYPE_, MemoryUsageLayer_, FUNC_NAME_, \
                              FUNC_ARGS_)

//////////////////////////////////////////////////////////////////////////////
//  Implementation of the instance functions we want to override.
//////////////////////////////////////////////////////////////////////////////

// Override for vkCreateDevice.  Builds the dispatch table for the new device
// and add it to the layer data.
SPL_MEMORY_USAGE_LAYER_FUNC(VkResult, CreateDevice,
                            (VkPhysicalDevice physical_device,
                             const VkDeviceCreateInfo* create_info,
                             const VkAllocationCallbacks* allocator,
                             VkDevice* device)) {
  auto build_dispatch_table = [device](PFN_vkGetDeviceProcAddr gdpa) {
    VkLayerDispatchTable dispatch_table{};

    // Get the next layer's instance of the device functions we will override.
    SPL_DISPATCH_DEVICE_FUNC(GetDeviceProcAddr);
    SPL_DISPATCH_DEVICE_FUNC(DestroyDevice);
    SPL_DISPATCH_DEVICE_FUNC(AllocateMemory);
    SPL_DISPATCH_DEVICE_FUNC(FreeMemory);
    SPL_DISPATCH_DEVICE_FUNC(QueuePresentKHR);
    return dispatch_table;
  };
  return GetLayerData()->CreateDevice(physical_device, create_info, allocator,
                                      device, build_dispatch_table);
}

// Override for vkDestroyInstance.  Deletes the entry for |instance| from the
// layer data.
SPL_MEMORY_USAGE_LAYER_FUNC(void, DestroyInstance,
                            (VkInstance instance,
                             const VkAllocationCallbacks* allocator)) {
  LayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextInstanceProcAddr(
      instance, &VkLayerInstanceDispatchTable::DestroyInstance);
  layer_data->RemoveInstance(instance);
  next_proc(instance, allocator);
}

// Override for vkCreateInstance.  Creates the dispatch table for this instance
// and add it to the layer data.
SPL_MEMORY_USAGE_LAYER_FUNC(VkResult, CreateInstance,
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
SPL_MEMORY_USAGE_LAYER_FUNC(void, DestroyDevice,
                            (VkDevice device,
                             const VkAllocationCallbacks* allocator)) {
  MemoryUsageLayerData* layer_data = GetLayerData();
  // Remove memory allocation records for the device being destroyed.
  layer_data->RecordDestroyDeviceMemory(device);

  MemoryUsageEvent event("memory_usage_destroy_device",
                         layer_data->GetCurrentAllocationSize(),
                         layer_data->GetPeakAllocationSize());
  layer_data->LogEvent(&event);

  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::DestroyDevice);
  layer_data->RemoveDevice(device);
  next_proc(device, allocator);
}

// Override for vkQueuePresentKHR. Used to log memory usage once per frame.
SPL_MEMORY_USAGE_LAYER_FUNC(VkResult, QueuePresentKHR,
                            (VkQueue queue,
                             const VkPresentInfoKHR* present_info)) {
  MemoryUsageLayerData* layer_data = GetLayerData();
  MemoryUsageEvent event("memory_usage_present",
                         layer_data->GetCurrentAllocationSize(),
                         layer_data->GetPeakAllocationSize());
  layer_data->LogEvent(&event);
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      queue, &VkLayerDispatchTable::QueuePresentKHR);
  return next_proc(queue, present_info);
}

// Override for vkAllocateMemory.  Records the allocation size.
SPL_MEMORY_USAGE_LAYER_FUNC(VkResult, AllocateMemory,
                            (VkDevice device,
                             const VkMemoryAllocateInfo* pAllocateInfo,
                             const VkAllocationCallbacks* pAllocator,
                             VkDeviceMemory* pMemory)) {
  MemoryUsageLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::AllocateMemory);

  auto result = next_proc(device, pAllocateInfo, pAllocator, pMemory);

  if (result == VK_SUCCESS) {
    // TODO: Also records failed allocations in some way?
    layer_data->RecordAllocateMemory(device, *pMemory,
                                     pAllocateInfo->allocationSize);
  }
  return result;
}

// Override for vkFreeMemory. Deletes the records.
SPL_MEMORY_USAGE_LAYER_FUNC(void, FreeMemory,
                            (VkDevice device, VkDeviceMemory memory,
                             const VkAllocationCallbacks* pAllocator)) {
  MemoryUsageLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::FreeMemory);

  layer_data->RecordFreeMemory(device, memory);

  next_proc(device, memory, pAllocator);
}

}  // namespace

// The *GetProcAddr functions are the entry points to the layers.
// They return a function pointer for the instance requested by |name|.  We
// return the functions defined in this layer for those we want to override.
// Otherwise we call the *GetProcAddr function for the next layer to get the
// function to be called.

SPL_LAYER_ENTRY_POINT SPL_MEMORY_USAGE_LAYER_FUNC(PFN_vkVoidFunction,
                                                  GetDeviceProcAddr,
                                                  (VkDevice device,
                                                   const char* name)) {
  if (auto func = FunctionInterceptor::GetInterceptedOrNull(name)) {
    return func;
  }

  MemoryUsageLayerData* layer_data = GetLayerData();

  PFN_vkGetDeviceProcAddr next_get_proc_addr =
      layer_data->GetNextDeviceProcAddr(
          device, &VkLayerDispatchTable::GetDeviceProcAddr);
  assert(next_get_proc_addr && next_get_proc_addr != VK_NULL_HANDLE);
  return next_get_proc_addr(device, name);
}

SPL_LAYER_ENTRY_POINT SPL_MEMORY_USAGE_LAYER_FUNC(PFN_vkVoidFunction,
                                                  GetInstanceProcAddr,
                                                  (VkInstance instance,
                                                   const char* name)) {
  if (auto func = FunctionInterceptor::GetInterceptedOrNull(name)) {
    return func;
  }

  MemoryUsageLayerData* layer_data = GetLayerData();

  PFN_vkGetInstanceProcAddr next_get_proc_addr =
      layer_data->GetNextInstanceProcAddr(
          instance, &VkLayerInstanceDispatchTable::GetInstanceProcAddr);
  assert(next_get_proc_addr && next_get_proc_addr != VK_NULL_HANDLE);
  return next_get_proc_addr(instance, name);
}

}  // namespace performancelayers
