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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

#include "layer/support/debug_logging.h"
#include "layer/support/layer_utils.h"
#include "runtime_layer_data.h"

#if defined(__ANDROID__)
// https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md#layer-interface-version-0
#define EXPOSE_LAYER_INTERFACE_VERSION_0
#endif

namespace {
// ----------------------------------------------------------------------------
// Layer book-keeping information
// ----------------------------------------------------------------------------

#define LAYER_NAME "VK_LAYER_STADIA_pipeline_runtime"

constexpr uint32_t kRuntimeLayerVersion = 1;
constexpr char kLayerName[] = LAYER_NAME;
constexpr char kLayerDescription[] =
    "Stadia Pipeline Pipeline Runtime Measuring Layer";
constexpr char kLogFilenameEnvVar[] = "VK_RUNTIME_LOG";

performancelayers::RuntimeLayerData* GetLayerData() {
  // Don't use new -- make the destructor run when the layer gets unloaded.
  static performancelayers::RuntimeLayerData layer_data =
      performancelayers::RuntimeLayerData(getenv(kLogFilenameEnvVar));
  return &layer_data;
}

// Use this macro to define all vulkan functions intercepted by the layer.
#define SPL_RUNTIME_LAYER_FUNC(RETURN_TYPE_, FUNC_NAME_, FUNC_ARGS_)   \
  SPL_INTERCEPTED_VULKAN_FUNC(RETURN_TYPE_, RuntimeLayer_, FUNC_NAME_, \
                              FUNC_ARGS_)

//////////////////////////////////////////////////////////////////////////////
//  Implementation of the instance functions we want to override.
//////////////////////////////////////////////////////////////////////////////

// Override for vkDestroyInstance.  Deletes the entry for |instance| from the
// layer data.
SPL_RUNTIME_LAYER_FUNC(void, DestroyInstance,
                       (VkInstance instance,
                        const VkAllocationCallbacks* allocator)) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextInstanceProcAddr(
      instance, &VkLayerInstanceDispatchTable::DestroyInstance);
  layer_data->RemoveInstance(instance);
  next_proc(instance, allocator);
}

// Override for vkCreateInstance.  Creates the dispatch table for this instance
// and add it to the layer data.
SPL_RUNTIME_LAYER_FUNC(VkResult, CreateInstance,
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

// Override for vkCreateComputePipelines.  Measures the time it takes to compile
// the pipeline, and logs the result.
SPL_RUNTIME_LAYER_FUNC(VkResult, CreateComputePipelines,
                       (VkDevice device, VkPipelineCache pipeline_cache,
                        uint32_t create_info_count,
                        const VkComputePipelineCreateInfo* create_infos,
                        const VkAllocationCallbacks* alloc_callbacks,
                        VkPipeline* pipelines)) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CreateComputePipelines);

  assert(create_info_count > 0 &&
         "Specification says create_info_count must be > 0.");

  auto result = next_proc(device, pipeline_cache, create_info_count,
                          create_infos, alloc_callbacks, pipelines);

  for (uint32_t i = 0; i < create_info_count; i++) {
    layer_data->HashComputePipeline(pipelines[i], create_infos[i]);
  }
  return result;
}

// Override for vkCreateGraphicsPipelines.  Measures the time it takes to
// compile the pipeline, and logs the result.
SPL_RUNTIME_LAYER_FUNC(VkResult, CreateGraphicsPipelines,
                       (VkDevice device, VkPipelineCache pipeline_cache,
                        uint32_t create_info_count,
                        const VkGraphicsPipelineCreateInfo* create_infos,
                        const VkAllocationCallbacks* alloc_callbacks,
                        VkPipeline* pipelines)) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CreateGraphicsPipelines);

  assert(create_info_count > 0 &&
         "Specification says create_info_count must be > 0.");

  auto result = next_proc(device, pipeline_cache, create_info_count,
                          create_infos, alloc_callbacks, pipelines);

  for (uint32_t i = 0; i < create_info_count; i++) {
    layer_data->HashGraphicsPipeline(pipelines[i], create_infos[i]);
  }
  return result;
}

// Override for vkCmdBindPipeline.  Records the pipeline as the last pipeline
// bound for the command buffer.
SPL_RUNTIME_LAYER_FUNC(void, CmdBindPipeline,
                       (VkCommandBuffer command_buffer,
                        VkPipelineBindPoint pipeline_bind_point,
                        VkPipeline pipeline)) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      command_buffer, &VkLayerDispatchTable::CmdBindPipeline);
  next_proc(command_buffer, pipeline_bind_point, pipeline);

  layer_data->BindPipeline(command_buffer, pipeline);
}

// Override for vkCmdDispatch.  Adds commands to write timestamps before and
// after the dispatch command that will be added.
SPL_RUNTIME_LAYER_FUNC(void, CmdDispatch,
                       (VkCommandBuffer command_buffer, uint32_t group_count_x,
                        uint32_t group_count_y, uint32_t group_count_z)) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      command_buffer, &VkLayerDispatchTable::CmdDispatch);

  VkQueryPool timestamp_query_pool = VK_NULL_HANDLE;
  VkQueryPool stat_query_pool = VK_NULL_HANDLE;
  if (!layer_data->GetNewQueryInfo(command_buffer, &timestamp_query_pool,
                                   &stat_query_pool)) {
    // Couldn't allocate query pool - continue as if no tracing is in place.
    next_proc(command_buffer, group_count_x, group_count_y, group_count_z);
    return;
  }
  auto write_timestamp_function = layer_data->GetNextDeviceProcAddr(
      command_buffer, &VkLayerDispatchTable::CmdWriteTimestamp);
  auto begin_query_function = layer_data->GetNextDeviceProcAddr(
      command_buffer, &VkLayerDispatchTable::CmdBeginQuery);
  auto end_query_function = layer_data->GetNextDeviceProcAddr(
      command_buffer, &VkLayerDispatchTable::CmdEndQuery);
  auto pipeline_barrier_function = layer_data->GetNextDeviceProcAddr(
      command_buffer, &VkLayerDispatchTable::CmdPipelineBarrier);

  // Ensure any previous commands have completed.
  VkMemoryBarrier full_memory_barrier = {
      VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
      VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
      VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT};
  (pipeline_barrier_function)(
      command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      /*dependencyFlags=*/0,
      /*memoryBarrierCount=*/1, &full_memory_barrier, 0, nullptr, 0, nullptr);
  (begin_query_function)(command_buffer, stat_query_pool, /*queryIndex=*/0,
                         /*flags=*/0);

  next_proc(command_buffer, group_count_x, group_count_y, group_count_z);

  // Get the timestamp when the dispatch starts.
  (write_timestamp_function)(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             timestamp_query_pool, 0);
  // Ensure the command has completed.
  (pipeline_barrier_function)(
      command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      /*dependencyFlags=*/0,
      /*memoryBarrierCount=*/1, &full_memory_barrier, 0, nullptr, 0, nullptr);
  // Get the timestamp after the dispatch ends.
  (write_timestamp_function)(command_buffer,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             timestamp_query_pool, 1);
  (end_query_function)(command_buffer, stat_query_pool, /*queryIndex=*/0);
}

template <typename TFuncPtr, typename... Args>
static void WrapCallWithTimestamp(TFuncPtr func_ptr,
                                  VkCommandBuffer command_buffer,
                                  Args&&... args) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(command_buffer, func_ptr);

  VkQueryPool timestamp_query_pool = VK_NULL_HANDLE;
  VkQueryPool stat_query_pool = VK_NULL_HANDLE;
  if (!layer_data->GetNewQueryInfo(command_buffer, &timestamp_query_pool,
                                   &stat_query_pool)) {
    // Couldn't allocate query pool - continue as if no tracing is in place.
    next_proc(command_buffer, std::forward<Args>(args)...);
    return;
  }
  auto write_timestamp_function = layer_data->GetNextDeviceProcAddr(
      command_buffer, &VkLayerDispatchTable::CmdWriteTimestamp);
  auto pipeline_barrier_function = layer_data->GetNextDeviceProcAddr(
      command_buffer, &VkLayerDispatchTable::CmdPipelineBarrier);
  auto begin_query_function = layer_data->GetNextDeviceProcAddr(
      command_buffer, &VkLayerDispatchTable::CmdBeginQuery);
  auto end_query_function = layer_data->GetNextDeviceProcAddr(
      command_buffer, &VkLayerDispatchTable::CmdEndQuery);

  // Ensure any previous commands have completed.
  VkMemoryBarrier full_memory_barrier = {
      VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
      VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
      VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT};
  (pipeline_barrier_function)(
      command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      /*dependencyFlags=*/0,
      /*memoryBarrierCount=*/1, &full_memory_barrier, 0, nullptr, 0, nullptr);
  (begin_query_function)(command_buffer, stat_query_pool, /*queryIndex=*/0,
                         /*flags=*/0);

  next_proc(command_buffer, std::forward<Args>(args)...);

  // Get the timestamp when the dispatch starts.
  (write_timestamp_function)(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             timestamp_query_pool, 0);
  // Ensure the command has completed.
  (pipeline_barrier_function)(
      command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      /*dependencyFlags=*/0,
      /*memoryBarrierCount=*/1, &full_memory_barrier, 0, nullptr, 0, nullptr);
  // Get the timestamp after the dispatch ends.
  (write_timestamp_function)(command_buffer,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             timestamp_query_pool, 1);
  (end_query_function)(command_buffer, stat_query_pool, /*queryIndex=*/0);
}

// Override for vkCmdDraw.  Adds commands to write timestamps before and
// after the draw command that will be added.
SPL_RUNTIME_LAYER_FUNC(void, CmdDraw,
                       (VkCommandBuffer command_buffer, uint32_t vertex_count,
                        uint32_t instance_count, uint32_t first_vertex,
                        uint32_t first_instance)) {
  WrapCallWithTimestamp(&VkLayerDispatchTable::CmdDraw, command_buffer,
                        vertex_count, instance_count, first_vertex,
                        first_instance);
}

// Override for vkCmdDrawIndexed.  Adds commands to write timestamps before and
// after the draw command that will be added.
SPL_RUNTIME_LAYER_FUNC(void, CmdDrawIndexed,
                       (VkCommandBuffer command_buffer, uint32_t index_count,
                        uint32_t instance_count, uint32_t first_index,
                        int32_t vertex_offset, uint32_t first_instance)) {
  WrapCallWithTimestamp(&VkLayerDispatchTable::CmdDrawIndexed, command_buffer,
                        index_count, instance_count, first_index, vertex_offset,
                        first_instance);
}

// Override for vkCmdDrawIndirect.  Adds commands to write timestamps before and
// after the draw command that will be added.
SPL_RUNTIME_LAYER_FUNC(void, CmdDrawIndirect,
                       (VkCommandBuffer command_buffer, VkBuffer buffer,
                        VkDeviceSize offset, uint32_t draw_count,
                        uint32_t stride)) {
  WrapCallWithTimestamp(&VkLayerDispatchTable::CmdDrawIndirect, command_buffer,
                        buffer, offset, draw_count, stride);
}

// Override for vkCmdDrawIndexedIndirect.  Adds commands to write timestamps
// before and after the draw command that will be added.
SPL_RUNTIME_LAYER_FUNC(void, CmdDrawIndexedIndirect,
                       (VkCommandBuffer command_buffer, VkBuffer buffer,
                        VkDeviceSize offset, uint32_t draw_count,
                        uint32_t stride)) {
  WrapCallWithTimestamp(&VkLayerDispatchTable::CmdDrawIndexedIndirect,
                        command_buffer, buffer, offset, draw_count, stride);
}

// Override for vkDeviceWaitIdle. Checks for timestamps that are available after
// waiting.
SPL_RUNTIME_LAYER_FUNC(VkResult, DeviceWaitIdle, (VkDevice device)) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::DeviceWaitIdle);
  VkResult result = next_proc(device);
  layer_data->LogAndRemoveQueryPools();
  return result;
}

// Override for vkQueueWaitIdle.  Checks for timestamps that are available after
// waiting.
SPL_RUNTIME_LAYER_FUNC(VkResult, QueueWaitIdle, (VkQueue queue)) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      queue, &VkLayerDispatchTable::QueueWaitIdle);
  VkResult result = next_proc(queue);
  layer_data->LogAndRemoveQueryPools();
  return result;
}

// Override for vkCreateShaderModule.  Records the hash of the shader module in
// the layer data.
SPL_RUNTIME_LAYER_FUNC(VkResult, CreateShaderModule,
                       (VkDevice device,
                        const VkShaderModuleCreateInfo* create_info,
                        const VkAllocationCallbacks* allocator,
                        VkShaderModule* shader_module)) {
  return GetLayerData()
      ->CreateShaderModule(device, create_info, allocator, shader_module)
      .result;
}

// Override for vkDestroyShaderModule. Erases the shader module from
// the layer data.
SPL_RUNTIME_LAYER_FUNC(void, DestroyShaderModule,
                       (VkDevice device, VkShaderModule shader_module,
                        const VkAllocationCallbacks* allocator)) {
  return GetLayerData()->DestroyShaderModule(device, shader_module, allocator);
}

// Override for vkDestroyDevice.  Removes the dispatch table for the device from
// the layer data.
SPL_RUNTIME_LAYER_FUNC(void, DestroyDevice,
                       (VkDevice device,
                        const VkAllocationCallbacks* allocator)) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::DestroyDevice);
  layer_data->RemoveDevice(device);
  next_proc(device, allocator);
}

// Override for vkCreateDevice.  Builds the dispatch table for the new device
// and add it to the layer data.
SPL_RUNTIME_LAYER_FUNC(VkResult, CreateDevice,
                       (VkPhysicalDevice physical_device,
                        const VkDeviceCreateInfo* create_info,
                        const VkAllocationCallbacks* allocator,
                        VkDevice* device)) {
  auto build_dispatch_table = [device](PFN_vkGetDeviceProcAddr gdpa) {
    VkLayerDispatchTable dispatch_table{};

    // Get the next layer's instance of the device functions we will override.
    SPL_DISPATCH_DEVICE_FUNC(CmdBindPipeline);
    SPL_DISPATCH_DEVICE_FUNC(CmdDispatch);
    SPL_DISPATCH_DEVICE_FUNC(CmdDraw);
    SPL_DISPATCH_DEVICE_FUNC(CmdDrawIndexed);
    SPL_DISPATCH_DEVICE_FUNC(CmdDrawIndexedIndirect);
    SPL_DISPATCH_DEVICE_FUNC(CmdDrawIndirect);
    SPL_DISPATCH_DEVICE_FUNC(CreateComputePipelines);
    SPL_DISPATCH_DEVICE_FUNC(CreateGraphicsPipelines);
    SPL_DISPATCH_DEVICE_FUNC(CreateShaderModule);
    SPL_DISPATCH_DEVICE_FUNC(DestroyDevice);
    SPL_DISPATCH_DEVICE_FUNC(DestroyShaderModule);
    SPL_DISPATCH_DEVICE_FUNC(DeviceWaitIdle);
    SPL_DISPATCH_DEVICE_FUNC(FreeCommandBuffers);
    SPL_DISPATCH_DEVICE_FUNC(GetDeviceProcAddr);
    SPL_DISPATCH_DEVICE_FUNC(QueueWaitIdle);
    // Get the next layer's instance of the device functions we will use. We do
    // not call these Vulkan functions directly to avoid re-entering the Vulkan
    // loader and confusing it.
    SPL_DISPATCH_DEVICE_FUNC(CmdBeginQuery);
    SPL_DISPATCH_DEVICE_FUNC(CmdEndQuery);
    SPL_DISPATCH_DEVICE_FUNC(CmdPipelineBarrier);
    SPL_DISPATCH_DEVICE_FUNC(CmdResetQueryPool);
    SPL_DISPATCH_DEVICE_FUNC(CmdWriteTimestamp);
    SPL_DISPATCH_DEVICE_FUNC(CreateQueryPool);
    SPL_DISPATCH_DEVICE_FUNC(DestroyQueryPool);
    SPL_DISPATCH_DEVICE_FUNC(GetQueryPoolResults);

    return dispatch_table;
  };

  return GetLayerData()->CreateDevice(physical_device, create_info, allocator,
                                      device, build_dispatch_table);
}

SPL_RUNTIME_LAYER_FUNC(VkResult, EnumerateInstanceLayerProperties,
                       (uint32_t * property_count,
                        VkLayerProperties* properties)) {
  if (property_count) *property_count = 1;

  if (properties) {
    strncpy(properties->layerName, kLayerName, sizeof(properties->layerName));
    strncpy(properties->description, kLayerDescription,
            sizeof(properties->description));
    properties->implementationVersion = kRuntimeLayerVersion;
    properties->specVersion = VK_API_VERSION_1_0;
  }

  return VK_SUCCESS;
}

SPL_RUNTIME_LAYER_FUNC(VkResult, EnumerateDeviceLayerProperties,
                       (VkPhysicalDevice /* physical_device */,
                        uint32_t* property_count,
                        VkLayerProperties* properties)) {
  return RuntimeLayer_EnumerateInstanceLayerProperties(property_count,
                                                       properties);
}

// Override for vkFreeCommandBuffer. Deletes the queries containing the freed
// command buffers.
SPL_RUNTIME_LAYER_FUNC(void, FreeCommandBuffers,
                       (VkDevice device, VkCommandPool commandPool,
                        uint32_t commandBufferCount,
                        const VkCommandBuffer* pCommandBuffers)) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::FreeCommandBuffers);
  layer_data->RemoveQueries(pCommandBuffers, commandBufferCount);
  next_proc(device, commandPool, commandBufferCount, pCommandBuffers);
}

}  // namespace

// The *GetProcAddr functions are the entry points to the layers.
// They return a function pointer for the instance requested by |name|.  We
// return the functions defined in this layer for those we want to override.
// Otherwise we call the *GetProcAddr function for the next layer to get the
// function to be called.

SPL_LAYER_ENTRY_POINT SPL_RUNTIME_LAYER_FUNC(PFN_vkVoidFunction,
                                             GetDeviceProcAddr,
                                             (VkDevice device,
                                              const char* name)) {
  if (auto func =
          performancelayers::FunctionInterceptor::GetInterceptedOrNull(name)) {
    return func;
  }

  performancelayers::RuntimeLayerData* layer_data = GetLayerData();

  PFN_vkGetDeviceProcAddr next_get_proc_addr =
      layer_data->GetNextDeviceProcAddr(
          device, &VkLayerDispatchTable::GetDeviceProcAddr);
  assert(next_get_proc_addr && next_get_proc_addr != VK_NULL_HANDLE);
  return next_get_proc_addr(device, name);
}

SPL_LAYER_ENTRY_POINT SPL_RUNTIME_LAYER_FUNC(PFN_vkVoidFunction,
                                             GetInstanceProcAddr,
                                             (VkInstance instance,
                                              const char* name)) {
  if (auto func =
          performancelayers::FunctionInterceptor::GetInterceptedOrNull(name)) {
    return func;
  }

  performancelayers::RuntimeLayerData* layer_data = GetLayerData();

  PFN_vkGetInstanceProcAddr next_get_proc_addr =
      layer_data->GetNextInstanceProcAddr(
          instance, &VkLayerInstanceDispatchTable::GetInstanceProcAddr);
  assert(next_get_proc_addr && next_get_proc_addr != VK_NULL_HANDLE);
  return next_get_proc_addr(instance, name);
}

#if defined(EXPOSE_LAYER_INTERFACE_VERSION_0)

#define LAYER_NAME_FUNCTION_CONCAT(layername, func) layername##func
#define LAYER_NAME_FUNCTION(func) \
  LAYER_NAME_FUNCTION_CONCAT(VK_LAYER_STADIA_pipeline_runtime, func)

// Exposes the layer interface version 0's GetInstanceProcAddr
SPL_LAYER_ENTRY_POINT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
LAYER_NAME_FUNCTION(GetInstanceProcAddr)(VkInstance instance,
                                         const char* funcName) {
  return RuntimeLayer_GetInstanceProcAddr(instance, funcName);
}

// Exposes the layer interface version 0's GetDeviceProcAddr
SPL_LAYER_ENTRY_POINT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
LAYER_NAME_FUNCTION(GetDeviceProcAddr)(VkDevice dev, const char* funcName) {
  return RuntimeLayer_GetDeviceProcAddr(dev, funcName);
}

// Enumerates all layers in the library.
SPL_LAYER_ENTRY_POINT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(uint32_t* pPropertyCount,
                                   VkLayerProperties* pProperties) {
  return RuntimeLayer_EnumerateInstanceLayerProperties(pPropertyCount,
                                                       pProperties);
}

// Enumerates instance extensions of a given layer in the library.
SPL_LAYER_ENTRY_POINT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceExtensionProperties(const char* /*pLayerName*/,
                                       uint32_t* pPropertyCount,
                                       VkExtensionProperties* /*pProperties*/) {
  *pPropertyCount = 0;
  return VK_SUCCESS;
}

// Enumerates all layers in the library that participate in device function
// interception.
SPL_LAYER_ENTRY_POINT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                 uint32_t* pPropertyCount,
                                 VkLayerProperties* pProperties) {
  return RuntimeLayer_EnumerateDeviceLayerProperties(
      physicalDevice, pPropertyCount, pProperties);
}

// Enumerates device extensions of a given layer in the library.
SPL_LAYER_ENTRY_POINT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceExtensionProperties(VkPhysicalDevice /*physicalDevice*/,
                                     const char* /*pLayerName*/,
                                     uint32_t* pPropertyCount,
                                     VkExtensionProperties* /*pProperties*/) {
  *pPropertyCount = 0;
  return VK_SUCCESS;
}

#undef LAYER_NAME_FUNCTION

#endif  // EXPOSE_LAYER_INTERFACE_VERSION_0
