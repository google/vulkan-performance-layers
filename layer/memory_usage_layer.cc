// Copyright 2020-2022 Google LLC
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

#include "layer_data.h"
#include "layer_utils.h"

namespace {
// ----------------------------------------------------------------------------
// Layer book-keeping information
// ----------------------------------------------------------------------------

constexpr uint32_t kCompileTimeLayerVersion = 1;
constexpr char kLayerName[] = "VK_LAYER_STADIA_memory_usage";
constexpr char kLayerDescription[] =
    "Stadia Memory Usage Measuring Layer";
constexpr char kLogFilenameEnvVar[] = "VK_MEMORY_USAGE_LOG_LOG";

performancelayers::LayerData* GetLayerData() {
  // Don't use new -- make the destructor run when the layer gets unloaded.
  static performancelayers::LayerData layer_data = performancelayers::LayerData(
      getenv(kLogFilenameEnvVar), "Memory usage format TBD");
  static bool first_call = true;
  if (first_call) {
    layer_data.LogEventOnly("compile_time_layer_init");
    first_call = false;
  }

  return &layer_data;
}

// Use this macro to define all vulkan functions intercepted by the layer.
#define SPL_COMPILE_TIME_LAYER_FUNC(RETURN_TYPE_, FUNC_NAME_, FUNC_ARGS_)  \
  SPL_INTERCEPTED_VULKAN_FUNC(RETURN_TYPE_, CompileTimeLayer_, FUNC_NAME_, \
                              FUNC_ARGS_)

//////////////////////////////////////////////////////////////////////////////
//  Implementation of the instance functions we want to override.
//////////////////////////////////////////////////////////////////////////////

// Override for vkDestroyInstance.  Deletes the entry for |instance| from the
// layer data.
SPL_COMPILE_TIME_LAYER_FUNC(void, DestroyInstance,
                            (VkInstance instance,
                             const VkAllocationCallbacks* allocator)) {
  performancelayers::LayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextInstanceProcAddr(
      instance, &VkLayerInstanceDispatchTable::DestroyInstance);
  (next_proc)(instance, allocator);
  layer_data->RemoveInstance(instance);
}

// Override for vkCreateInstance.  Creates the dispatch table for this instance
// and add it to the layer data.
SPL_COMPILE_TIME_LAYER_FUNC(VkResult, CreateInstance,
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
        SPL_DISPATCH_INSTANCE_FUNC(EnumeratePhysicalDevices);
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
SPL_COMPILE_TIME_LAYER_FUNC(void, DestroyDevice,
                            (VkDevice device,
                             const VkAllocationCallbacks* allocator)) {
  performancelayers::LayerData* layer_data = GetLayerData();
  // Remove memory allocation records for the device being destroyed.
  layer_data->RecordDestroyDeviceMemory(device);
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::DestroyDevice);
  (next_proc)(device, allocator);
  layer_data->RemoveDevice(device);
}

// Override for vkCreateDevice.  Builds the dispatch table for the new device
// and add it to the layer data.
SPL_COMPILE_TIME_LAYER_FUNC(VkResult, CreateDevice,
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
    return dispatch_table;
  };

  return GetLayerData()->CreateDevice(physical_device, create_info, allocator,
                                      device, build_dispatch_table);
}

// Override for vkAllocateMemory.  Records the allocation size.
SPL_COMPILE_TIME_LAYER_FUNC(VkResult, AllocateMemory,
                            (VkDevice device,
                             const VkMemoryAllocateInfo* pAllocateInfo,
                             const VkAllocationCallbacks* pAllocator,
                             VkDeviceMemory* pMemory)) {
  performancelayers::LayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::AllocateMemory);

  auto result = (next_proc)(device, pAllocateInfo, pAllocator, pMemory);

  if (result == VK_SUCCESS) {
    // TODO: Also records failed allocations in some way?
    layer_data->RecordAllocateMemory(device, *pMemory, pAllocateInfo->allocationSize);
  }
  return result;
}

// Override for vkFreeMemory. Deletes the records.
SPL_COMPILE_TIME_LAYER_FUNC(void, FreeMemory,
                            (VkDevice device,
                             VkDeviceMemory memory,
                             const VkAllocationCallbacks* pAllocator)) {
  performancelayers::LayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::FreeMemory);

  layer_data->RecordFreeMemory(device, memory);

  (next_proc)(device, memory, pAllocator);
}

}  // namespace

// The *GetProcAddr functions are the entry points to the layers.
// They return a function pointer for the instance requested by |name|.  We
// return the functions defined in this layer for those we want to override.
// Otherwise we call the *GetProcAddr function for the next layer to get the
// function to be called.

SPL_LAYER_ENTRY_POINT SPL_COMPILE_TIME_LAYER_FUNC(PFN_vkVoidFunction,
                                                  GetDeviceProcAddr,
                                                  (VkDevice device,
                                                   const char* name)) {
  if (auto func =
          performancelayers::FunctionInterceptor::GetInterceptedOrNull(name)) {
    return func;
  }

  performancelayers::LayerData* layer_data = GetLayerData();

  PFN_vkGetDeviceProcAddr next_get_proc_addr =
      layer_data->GetNextDeviceProcAddr(
          device, &VkLayerDispatchTable::GetDeviceProcAddr);
  assert(next_get_proc_addr && next_get_proc_addr != VK_NULL_HANDLE);
  return next_get_proc_addr(device, name);
}

SPL_LAYER_ENTRY_POINT SPL_COMPILE_TIME_LAYER_FUNC(PFN_vkVoidFunction,
                                                  GetInstanceProcAddr,
                                                  (VkInstance instance,
                                                   const char* name)) {
  if (auto func =
          performancelayers::FunctionInterceptor::GetInterceptedOrNull(name)) {
    return func;
  }

  performancelayers::LayerData* layer_data = GetLayerData();

  PFN_vkGetInstanceProcAddr next_get_proc_addr =
      layer_data->GetNextInstanceProcAddr(
          instance, &VkLayerInstanceDispatchTable::GetInstanceProcAddr);
  assert(next_get_proc_addr && next_get_proc_addr != VK_NULL_HANDLE);
  return next_get_proc_addr(instance, name);
}
