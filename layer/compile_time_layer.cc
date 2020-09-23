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

#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "layer_data.h"

#undef VK_LAYER_EXPORT
#ifndef _WIN32
#define VK_LAYER_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define VK_LAYER_EXPORT extern "C" _declspec(dllexport)
#endif

#define VK_LAYER_PROC(ReturnType) VKAPI_ATTR ReturnType VKAPI_CALL

namespace {
// ----------------------------------------------------------------------------
// Layer book-keeping information
// ----------------------------------------------------------------------------

constexpr uint32_t kCompileTimeLayerVersion = 1;
constexpr char kLayerName[] = "VK_LAYER_STADIA_pipeline_compile_time";
constexpr char kLayerDescription[] =
    "Stadia Pipeline Compile Time Measuring Layer";
constexpr char kLogFilenameEnvVar[] = "VK_COMPILE_TIME_LOG";

performancelayers::LayerData* GetLayerData() {
  // Don't use new -- make the destructor run when the layer gets unloaded.
  static performancelayers::LayerData layer_data = performancelayers::LayerData(
      getenv(kLogFilenameEnvVar), "Pipeline,Compile Time (ns)");
  return &layer_data;
}

//////////////////////////////////////////////////////////////////////////////
//  Implementation of the instance functions we want to override.
//////////////////////////////////////////////////////////////////////////////

// Override for vkDestroyDevice.  Deletes the entry for |instance| from the
// layer data.
VK_LAYER_PROC(void)
CompileTimeLayer_DestroyInstance(VkInstance instance,
                                 const VkAllocationCallbacks* allocator) {
  performancelayers::LayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextInstanceProcAddr(
      instance, &VkLayerInstanceDispatchTable::DestroyInstance);
  (next_proc)(instance, allocator);
  layer_data->RemoveInstance(instance);
}

// Override for vkCreateInstance.  Creates the dispatch table for this instance
// and add it to the layer data.
VK_LAYER_PROC(VkResult)
CompileTimeLayer_CreateInstance(const VkInstanceCreateInfo* create_info,
                                const VkAllocationCallbacks* allocator,
                                VkInstance* instance) {
  auto build_dispatch_table =
      [instance](PFN_vkGetInstanceProcAddr get_proc_addr) {
        // Build dispatch table for the instance functions we need to call.
        VkLayerInstanceDispatchTable dispatch_table{};

    // Get the next layer's instance of the instance functions we will override.
#define ASSIGN_PROC(name) \
  dispatch_table.name =   \
      reinterpret_cast<PFN_vk##name>(get_proc_addr(*instance, "vk" #name))
        ASSIGN_PROC(DestroyInstance);
        ASSIGN_PROC(EnumeratePhysicalDevices);
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

// Override for vkCreateComputePipelines.  Measures the time it takes to compile
// the pipeline, and logs the result.
VK_LAYER_PROC(VkResult)
CompileTimeLayer_CreateComputePipelines(
    VkDevice device, VkPipelineCache pipeline_cache, uint32_t create_info_count,
    const VkComputePipelineCreateInfo* create_infos,
    const VkAllocationCallbacks* alloc_callbacks, VkPipeline* pipelines) {
  performancelayers::LayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CreateComputePipelines);

  assert(create_info_count > 0 &&
         "Specification says create_info_count must be > 0.");

  absl::Time start = absl::Now();
  auto result = (next_proc)(device, pipeline_cache, create_info_count,
                            create_infos, alloc_callbacks, pipelines);
  absl::Time end = absl::Now();
  uint64_t duration = ToInt64Nanoseconds(end - start);

  std::vector<uint64_t> hashes;
  for (uint32_t i = 0; i < create_info_count; i++) {
    std::vector<uint64_t> h =
        layer_data->HashComputePipeline(pipelines[i], create_infos[i]);
    hashes.insert(hashes.end(), h.begin(), h.end());
  }
  layer_data->Log(hashes, duration);
  return result;
}

// Override for vkCreateGraphicsPipelines.  Measures the time it takes to
// compile the pipeline, and logs the result.
VK_LAYER_PROC(VkResult)
CompileTimeLayer_CreateGraphicsPipelines(
    VkDevice device, VkPipelineCache pipeline_cache, uint32_t create_info_count,
    const VkGraphicsPipelineCreateInfo* create_infos,
    const VkAllocationCallbacks* alloc_callbacks, VkPipeline* pipelines) {
  performancelayers::LayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CreateGraphicsPipelines);

  assert(create_info_count > 0 &&
         "Specification says create_info_count must be > 0.");

  auto start = absl::Now();
  auto result = (next_proc)(device, pipeline_cache, create_info_count,
                            create_infos, alloc_callbacks, pipelines);
  auto end = absl::Now();
  uint64_t duration = ToInt64Nanoseconds(end - start);

  std::vector<uint64_t> hashes;
  for (uint32_t i = 0; i < create_info_count; i++) {
    std::vector<uint64_t> h =
        layer_data->HashGraphicsPipeline(pipelines[i], create_infos[i]);
    hashes.insert(hashes.end(), h.begin(), h.end());
  }
  layer_data->Log(hashes, duration);
  return result;
}

// Override for vkCreateShaderModule.  Records the hash of the shader module in
// the layer data.
VK_LAYER_PROC(VkResult)
CompileTimeLayer_CreateShaderModule(VkDevice device,
                                    const VkShaderModuleCreateInfo* create_info,
                                    const VkAllocationCallbacks* allocator,
                                    VkShaderModule* shader_module) {
  return GetLayerData()->CreateShaderModule(device, create_info, allocator,
                                            shader_module);
}

// Override fro vkEnumeratePhysicalDevices.  Maps physical devices to their
// instances. This mapping is used in the vkCreateDevice override.
VK_LAYER_PROC(VkResult)
CompileTimeLayer_EnumeratePhysicalDevices(VkInstance instance,
                                          uint32_t* pPhysicalDeviceCount,
                                          VkPhysicalDevice* pPhysicalDevices) {
  return GetLayerData()->EnumeratePhysicalDevices(
      instance, pPhysicalDeviceCount, pPhysicalDevices);
}

// Override for vkDestroyDevice.  Removes the dispatch table for the device from
// the layer data.
VK_LAYER_PROC(void)
CompileTimeLayer_DestroyDevice(VkDevice device,
                               const VkAllocationCallbacks* allocator) {
  performancelayers::LayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::DestroyDevice);
  (next_proc)(device, allocator);
  layer_data->RemoveDevice(device);
}

// Override for vkCreateDevice.  Builds the dispatch table for the new device
// and add it to the layer data.
VK_LAYER_PROC(VkResult)
CompileTimeLayer_CreateDevice(VkPhysicalDevice physical_device,
                              const VkDeviceCreateInfo* create_info,
                              const VkAllocationCallbacks* allocator,
                              VkDevice* device) {
  auto build_dispatch_table = [device](PFN_vkGetDeviceProcAddr gdpa) {
    VkLayerDispatchTable dispatch_table{};

    // Get the next layer's instance of the device functions we will override.
#define ASSIGN_PROC(name) \
  dispatch_table.name =   \
      reinterpret_cast<PFN_vk##name>(gdpa(*device, "vk" #name))
    ASSIGN_PROC(GetDeviceProcAddr);
    ASSIGN_PROC(DestroyDevice);
    ASSIGN_PROC(CreateComputePipelines);
    ASSIGN_PROC(CreateGraphicsPipelines);
    ASSIGN_PROC(CreateShaderModule);
#undef ASSIGN_PROC
    return dispatch_table;
  };

  return GetLayerData()->CreateDevice(physical_device, create_info, allocator,
                                      device, build_dispatch_table);
}

VK_LAYER_PROC(VkResult)
CompileTimeLayer_EnumerateInstanceLayerProperties(
    uint32_t* property_count, VkLayerProperties* properties) {
  if (property_count) *property_count = 1;

  if (properties) {
    strncpy(properties->layerName, kLayerName, sizeof(properties->layerName));
    strncpy(properties->description, kLayerDescription,
            sizeof(properties->description));
    properties->implementationVersion = kCompileTimeLayerVersion;
    properties->specVersion = VK_API_VERSION_1_0;
  }

  return VK_SUCCESS;
}

VK_LAYER_PROC(VkResult)
CompileTimeLayer_EnumerateDeviceLayerProperties(
    VkPhysicalDevice /* physical_device */, uint32_t* property_count,
    VkLayerProperties* properties) {
  return CompileTimeLayer_EnumerateInstanceLayerProperties(property_count,
                                                           properties);
}

}  // namespace

// The *GetProcAddr functions are the entry points to the layers.
// They return a function pointer for the instance requested by |name|.  We
// return the functions defined in this layer for those we want to override.
// Otherwise we call the *GetProcAddr function for the next layer to get the
// function to be called.

VK_LAYER_EXPORT VK_LAYER_PROC(PFN_vkVoidFunction)
    CompileTimeLayer_GetDeviceProcAddr(VkDevice device, const char* name) {
  // Device functions we intercept.

#define CHECK_FUNC(func_name)                    \
  if (!strcmp(name, "vk" #func_name)) {          \
    return reinterpret_cast<PFN_vkVoidFunction>( \
        &CompileTimeLayer_##func_name);          \
  }
  CHECK_FUNC(CreateComputePipelines);
  CHECK_FUNC(CreateGraphicsPipelines);
  CHECK_FUNC(CreateShaderModule);
  CHECK_FUNC(DestroyDevice);
  CHECK_FUNC(EnumerateDeviceLayerProperties);
  CHECK_FUNC(GetDeviceProcAddr);
#undef CHECK_FUNC

  performancelayers::LayerData* layer_data = GetLayerData();

  PFN_vkGetDeviceProcAddr next_get_proc_addr;
  next_get_proc_addr = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::GetDeviceProcAddr);
  assert(next_get_proc_addr && next_get_proc_addr != VK_NULL_HANDLE);
  return next_get_proc_addr(device, name);
}

VK_LAYER_EXPORT VK_LAYER_PROC(PFN_vkVoidFunction)
    CompileTimeLayer_GetInstanceProcAddr(VkInstance instance,
                                         const char* name) {
#define CHECK_FUNC(func_name)                    \
  if (!strcmp(name, "vk" #func_name)) {          \
    return reinterpret_cast<PFN_vkVoidFunction>( \
        &CompileTimeLayer_##func_name);          \
  }
  CHECK_FUNC(CreateDevice);
  CHECK_FUNC(CreateInstance);
  CHECK_FUNC(DestroyInstance);
  CHECK_FUNC(EnumerateDeviceLayerProperties);
  CHECK_FUNC(EnumerateInstanceLayerProperties);
  CHECK_FUNC(EnumeratePhysicalDevices);
  CHECK_FUNC(GetDeviceProcAddr);
  CHECK_FUNC(GetInstanceProcAddr);
#undef CHECK_FUNC

  performancelayers::LayerData* layer_data = GetLayerData();

  PFN_vkGetInstanceProcAddr next_get_proc_addr;
  next_get_proc_addr = layer_data->GetNextInstanceProcAddr(
      instance, &VkLayerInstanceDispatchTable::GetInstanceProcAddr);
  assert(next_get_proc_addr && next_get_proc_addr != VK_NULL_HANDLE);
  return next_get_proc_addr(instance, name);
}
