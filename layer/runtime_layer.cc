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

#include "runtime_layer_data.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

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

constexpr uint32_t kRuntimeLayerVersion = 1;
constexpr char kLayerName[] = "VK_LAYER_STADIA_pipeline_runtime";
constexpr char kLayerDescription[] =
    "Stadia Pipeline Pipeline Runtime Measuring Layer";
constexpr char kLogFilenameEnvVar[] = "VK_RUNTIME_LOG";

performancelayers::RuntimeLayerData* GetLayerData() {
  // Don't use new -- make the destructor run when the layer gets unloaded.
  static performancelayers::RuntimeLayerData layer_data =
      performancelayers::RuntimeLayerData(getenv(kLogFilenameEnvVar));
  return &layer_data;
}

//////////////////////////////////////////////////////////////////////////////
//  Implementation of the instance functions we want to override.
//////////////////////////////////////////////////////////////////////////////

// Override for vkDestroyInstance.  Deletes the entry for |instance| from the
// layer data.
void RuntimeLayer_DestroyInstance(VkInstance instance,
                                  const VkAllocationCallbacks* allocator) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextInstanceProcAddr(
      instance, &VkLayerInstanceDispatchTable::DestroyInstance);
  (next_proc)(instance, allocator);
  layer_data->RemoveInstance(instance);
}

// Override for vkCreateInstance.  Creates the dispatch table for this instance
// and add it to the layer data.
VkResult RuntimeLayer_CreateInstance(const VkInstanceCreateInfo* create_info,
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

// Override for vkCreateComputePipelines.  Measures the time it takes to compile
// the pipeline, and logs the result.
VKAPI_ATTR VkResult RuntimeLayer_CreateComputePipelines(
    VkDevice device, VkPipelineCache pipeline_cache, uint32_t create_info_count,
    const VkComputePipelineCreateInfo* create_infos,
    const VkAllocationCallbacks* alloc_callbacks, VkPipeline* pipelines) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CreateComputePipelines);

  assert(create_info_count > 0 &&
         "Spececification says create_info_count must be > 0.");

  auto result = (next_proc)(device, pipeline_cache, create_info_count,
                            create_infos, alloc_callbacks, pipelines);

  for (uint32_t i = 0; i < create_info_count; i++) {
    layer_data->HashComputePipeline(pipelines[i], create_infos[i]);
  }
  return result;
}

// Override for vkCreateGraphicsPipelines.  Measures the time it takes to
// compile the pipeline, and logs the result.
VKAPI_ATTR VkResult RuntimeLayer_CreateGraphicsPipelines(
    VkDevice device, VkPipelineCache pipeline_cache, uint32_t create_info_count,
    const VkGraphicsPipelineCreateInfo* create_infos,
    const VkAllocationCallbacks* alloc_callbacks, VkPipeline* pipelines) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CreateGraphicsPipelines);

  assert(create_info_count > 0 &&
         "Spececification says create_info_count must be > 0.");

  auto result = (next_proc)(device, pipeline_cache, create_info_count,
                            create_infos, alloc_callbacks, pipelines);

  for (uint32_t i = 0; i < create_info_count; i++) {
    layer_data->HashGraphicsPipeline(pipelines[i], create_infos[i]);
  }
  return result;
}

// Override for vkAllocateCommandBuffers.  Records the device for the command
// buffer that was allocated.
VKAPI_ATTR VkResult RuntimeLayer_AllocateCommandBuffers(
    VkDevice device, const VkCommandBufferAllocateInfo* allocate_info,
    VkCommandBuffer* command_buffers) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::AllocateCommandBuffers);
  auto result = (next_proc)(device, allocate_info, command_buffers);

  for (uint32_t i = 0; i < allocate_info->commandBufferCount; i++) {
    layer_data->SetDevice(command_buffers[i], device);
  }
  return result;
}

// Override for vkCmdBindPipeline.  Records the pipeline as the last pipeline
// bound for the command buffer.
VKAPI_ATTR void RuntimeLayer_CmdBindPipeline(
    VkCommandBuffer command_buffer, VkPipelineBindPoint pipeline_bind_point,
    VkPipeline pipeline) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc =
      layer_data->GetNextDeviceProcAddr(layer_data->GetDevice(command_buffer),
                                        &VkLayerDispatchTable::CmdBindPipeline);
  (next_proc)(command_buffer, pipeline_bind_point, pipeline);

  layer_data->BindPipeline(command_buffer, pipeline);
}

// Override for vkCmdDispatch.  Adds commands to write timestamps before and
// after the dispatch command that will be added.
VKAPI_ATTR void RuntimeLayer_CmdDispatch(VkCommandBuffer command_buffer,
                                         uint32_t group_count_x,
                                         uint32_t group_count_y,
                                         uint32_t group_count_z) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  VkDevice device = layer_data->GetDevice(command_buffer);
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CmdDispatch);

  VkQueryPool timestamp_query_pool = VK_NULL_HANDLE;
  VkQueryPool stat_query_pool = VK_NULL_HANDLE;
  if (!layer_data->GetNewQueryInfo(command_buffer, &timestamp_query_pool,
                                   &stat_query_pool)) {
    // Couldn't allocate query pool - continue as if no tracing is in place.
    (next_proc)(command_buffer, group_count_x, group_count_y, group_count_z);
    return;
  }
  auto write_timestamp_function = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CmdWriteTimestamp);
  auto begin_query_function = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CmdBeginQuery);
  auto end_query_function = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CmdEndQuery);
  auto pipeline_barrier_function = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CmdPipelineBarrier);

  // Ensure any previous commands have completed.
  VkMemoryBarrier full_memory_barrier = {
      VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
      VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
      VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT};
  (pipeline_barrier_function)(
      command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, /*dependencyFlags=*/0,
      /*memoryBarrierCount=*/1, &full_memory_barrier, 0, nullptr, 0, nullptr);
  (begin_query_function)(command_buffer, stat_query_pool, /*queryIndex=*/0,
                         /*flags=*/0);

  (next_proc)(command_buffer, group_count_x, group_count_y, group_count_z);

  // Get the timestamp when the dispatch starts.
  (write_timestamp_function)(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             timestamp_query_pool, 0);
  // Ensure the command has completed.
  (pipeline_barrier_function)(
      command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, /*dependencyFlags=*/0,
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
  VkDevice device = layer_data->GetDevice(command_buffer);
  auto next_proc = layer_data->GetNextDeviceProcAddr(device, func_ptr);

  VkQueryPool timestamp_query_pool = VK_NULL_HANDLE;
  VkQueryPool stat_query_pool = VK_NULL_HANDLE;
  if (!layer_data->GetNewQueryInfo(command_buffer, &timestamp_query_pool,
                                   &stat_query_pool)) {
    // Couldn't allocate query pool - continue as if no tracing is in place.
    (next_proc)(command_buffer, std::forward<Args>(args)...);
    return;
  }
  auto write_timestamp_function = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CmdWriteTimestamp);
  auto pipeline_barrier_function = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CmdPipelineBarrier);
  auto begin_query_function = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CmdBeginQuery);
  auto end_query_function = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CmdEndQuery);

  // Ensure any previous commands have completed.
  VkMemoryBarrier full_memory_barrier = {
      VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
      VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
      VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT};
  (pipeline_barrier_function)(
      command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, /*dependencyFlags=*/0,
      /*memoryBarrierCount=*/1, &full_memory_barrier, 0, nullptr, 0, nullptr);
  (begin_query_function)(command_buffer, stat_query_pool, /*queryIndex=*/0,
                         /*flags=*/0);

  (next_proc)(command_buffer, std::forward<Args>(args)...);

  // Get the timestamp when the dispatch starts.
  (write_timestamp_function)(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             timestamp_query_pool, 0);
  // Ensure the command has completed.
  (pipeline_barrier_function)(
      command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, /*dependencyFlags=*/0,
      /*memoryBarrierCount=*/1, &full_memory_barrier, 0, nullptr, 0, nullptr);
  // Get the timestamp after the dispatch ends.
  (write_timestamp_function)(command_buffer,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             timestamp_query_pool, 1);
  (end_query_function)(command_buffer, stat_query_pool, /*queryIndex=*/0);
}

// Override for vkCmdDraw.  Adds commands to write timestamps before and
// after the draw command that will be added.
VKAPI_ATTR void RuntimeLayer_CmdDraw(VkCommandBuffer command_buffer,
                                     uint32_t vertex_count,
                                     uint32_t instance_count,
                                     uint32_t first_vertex,
                                     uint32_t first_instance) {
  WrapCallWithTimestamp(&VkLayerDispatchTable::CmdDraw, command_buffer,
                        vertex_count, instance_count, first_vertex,
                        first_instance);
}

// Override for vkCmdDrawIndexed.  Adds commands to write timestamps before and
// after the draw command that will be added.
VKAPI_ATTR void RuntimeLayer_CmdDrawIndexed(VkCommandBuffer command_buffer,
                                            uint32_t index_count,
                                            uint32_t instance_count,
                                            uint32_t first_index,
                                            int32_t vertex_offset,
                                            uint32_t first_instance) {
  WrapCallWithTimestamp(&VkLayerDispatchTable::CmdDrawIndexed, command_buffer,
                        index_count, instance_count, first_index, vertex_offset,
                        first_instance);
}

// Override for vkCmdDrawIndirect.  Adds commands to write timestamps before and
// after the draw command that will be added.
VKAPI_ATTR void RuntimeLayer_CmdDrawIndirect(VkCommandBuffer command_buffer,
                                             VkBuffer buffer,
                                             VkDeviceSize offset,
                                             uint32_t draw_count,
                                             uint32_t stride) {
  WrapCallWithTimestamp(&VkLayerDispatchTable::CmdDrawIndirect, command_buffer,
                        buffer, offset, draw_count, stride);
}

// Override for vkCmdDrawIndexedIndirect.  Adds commands to write timestamps
// before and after the draw command that will be added.
VKAPI_ATTR void RuntimeLayer_CmdDrawIndexedIndirect(
    VkCommandBuffer command_buffer, VkBuffer buffer, VkDeviceSize offset,
    uint32_t draw_count, uint32_t stride) {
  WrapCallWithTimestamp(&VkLayerDispatchTable::CmdDrawIndexedIndirect,
                        command_buffer, buffer, offset, draw_count, stride);
}

// Override for vkDeviceWaitIdle. Checks for timestamps that are available after
// waiting.
VKAPI_ATTR VkResult RuntimeLayer_DeviceWaitIdle(VkDevice device) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::DeviceWaitIdle);
  VkResult result = (next_proc)(device);
  layer_data->LogAndRemoveQueryPools();
  return result;
}

// Override for vkQueueWaitIdle.  Checks for timestamps that are available after
// waiting.
VKAPI_ATTR VkResult RuntimeLayer_QueueWaitIdle(VkQueue queue) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      layer_data->GetDevice(queue), &VkLayerDispatchTable::QueueWaitIdle);
  VkResult result = (next_proc)(queue);
  layer_data->LogAndRemoveQueryPools();
  return result;
}

// Override for vkGetDeviceQueue.  Records the device that owns the queue.
// after the draw command that will be added.
VKAPI_ATTR void RuntimeLayer_GetDeviceQueue(VkDevice device,
                                            uint32_t queue_family_index,
                                            uint32_t queue_index,
                                            VkQueue* queue) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::GetDeviceQueue);
  (next_proc)(device, queue_family_index, queue_index, queue);

  layer_data->SetDevice(*queue, device);
}

// Override for vkCreateShaderModule.  Records the hash of the shader module in
// the layer data.
VKAPI_ATTR VkResult RuntimeLayer_CreateShaderModule(
    VkDevice device, const VkShaderModuleCreateInfo* create_info,
    const VkAllocationCallbacks* allocator, VkShaderModule* shader_module) {
  return GetLayerData()->CreateShaderModule(device, create_info, allocator,
                                            shader_module);
}

// Override for vkDestroyDevice.  Removes the dispatch table for the device from
// the layer data.
void RuntimeLayer_DestroyDevice(VkDevice device,
                                const VkAllocationCallbacks* allocator) {
  performancelayers::RuntimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::DestroyDevice);
  (next_proc)(device, allocator);
  layer_data->RemoveDevice(device);
}

// Override for vkCreateDevice.  Builds the dispatch table for the new device
// and add it to the layer data.
VkResult RuntimeLayer_CreateDevice(VkPhysicalDevice physical_device,
                                   const VkDeviceCreateInfo* create_info,
                                   const VkAllocationCallbacks* allocator,
                                   VkDevice* device) {
  auto build_dispatch_table = [device](PFN_vkGetDeviceProcAddr gdpa) {
    VkLayerDispatchTable dispatch_table;

#define ASSIGN_PROC(name) \
  dispatch_table.name =   \
      reinterpret_cast<PFN_vk##name>(gdpa(*device, "vk" #name))
    // Get the next layer's instance of the device functions we will override.
    ASSIGN_PROC(GetDeviceProcAddr);
    ASSIGN_PROC(DestroyDevice);
    ASSIGN_PROC(DeviceWaitIdle);
    ASSIGN_PROC(CreateComputePipelines);
    ASSIGN_PROC(CreateGraphicsPipelines);
    ASSIGN_PROC(CreateShaderModule);
    ASSIGN_PROC(AllocateCommandBuffers);
    ASSIGN_PROC(CmdBindPipeline);
    ASSIGN_PROC(CmdDispatch);
    ASSIGN_PROC(CmdDraw);
    ASSIGN_PROC(CmdDrawIndexed);
    ASSIGN_PROC(CmdDrawIndirect);
    ASSIGN_PROC(CmdDrawIndexedIndirect);
    ASSIGN_PROC(QueueWaitIdle);
    ASSIGN_PROC(GetDeviceQueue);
    // Get the next layer's instance of the device functions we will use. We do
    // not call these Vulkan functions directly to avoid re-entering the Vulkan
    // loader and confusing it.
    ASSIGN_PROC(CmdResetQueryPool);
    ASSIGN_PROC(CmdWriteTimestamp);
    ASSIGN_PROC(CmdBeginQuery);
    ASSIGN_PROC(CmdEndQuery);
    ASSIGN_PROC(CmdPipelineBarrier);
    ASSIGN_PROC(CreateQueryPool);
    ASSIGN_PROC(DestroyQueryPool);
    ASSIGN_PROC(GetQueryPoolResults);
#undef ASSIGN_PROC
    return dispatch_table;
  };

  return GetLayerData()->CreateDevice(physical_device, create_info, allocator,
                                      device, build_dispatch_table);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
RuntimeLayer_EnumerateInstanceLayerProperties(uint32_t* property_count,
                                              VkLayerProperties* properties) {
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

VK_LAYER_EXPORT VkResult VKAPI_CALL RuntimeLayer_EnumerateDeviceLayerProperties(
    VkPhysicalDevice /* physical_device */, uint32_t* property_count,
    VkLayerProperties* properties) {
  return RuntimeLayer_EnumerateInstanceLayerProperties(property_count,
                                                       properties);
}

}  // namespace

// The *GetProcAddr functions are the entry points to the layers.
// They return a function pointer for the instance requested by |name|.  We
// return the functions defined in this layer for those we want to override.
// Otherwise we call the *GetProcAddr function for the next layer to get the
// function to be called.

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL
RuntimeLayer_GetDeviceProcAddr(VkDevice device, const char* name) {
  // Device functions we intercept.
#define CHECK_FUNC(func_name)                                               \
  if (!strcmp(name, "vk" #func_name)) {                                     \
    return reinterpret_cast<PFN_vkVoidFunction>(&RuntimeLayer_##func_name); \
  }
  CHECK_FUNC(DestroyDevice);
  CHECK_FUNC(DeviceWaitIdle);
  CHECK_FUNC(CreateComputePipelines);
  CHECK_FUNC(CreateGraphicsPipelines);
  CHECK_FUNC(CreateShaderModule);
  CHECK_FUNC(EnumerateDeviceLayerProperties);
  CHECK_FUNC(AllocateCommandBuffers);
  CHECK_FUNC(CmdBindPipeline);
  CHECK_FUNC(CmdDispatch);
  CHECK_FUNC(CmdDraw);
  CHECK_FUNC(CmdDrawIndexed);
  CHECK_FUNC(CmdDrawIndirect);
  CHECK_FUNC(CmdDrawIndexedIndirect);
  CHECK_FUNC(QueueWaitIdle);
  CHECK_FUNC(GetDeviceQueue);
#undef CHECK_FUNC

  performancelayers::RuntimeLayerData* layer_data = GetLayerData();

  PFN_vkGetDeviceProcAddr next_get_proc_addr;
  next_get_proc_addr = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::GetDeviceProcAddr);
  assert(next_get_proc_addr && next_get_proc_addr != VK_NULL_HANDLE);
  return next_get_proc_addr(device, name);
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL
RuntimeLayer_GetInstanceProcAddr(VkInstance instance, const char* name) {
#define CHECK_FUNC(func_name)                                               \
  if (!strcmp(name, "vk" #func_name)) {                                     \
    return reinterpret_cast<PFN_vkVoidFunction>(&RuntimeLayer_##func_name); \
  }
  CHECK_FUNC(GetInstanceProcAddr);
  CHECK_FUNC(EnumerateInstanceLayerProperties);
  CHECK_FUNC(DestroyInstance);
  CHECK_FUNC(CreateInstance);
  CHECK_FUNC(CreateDevice);
  CHECK_FUNC(EnumerateDeviceLayerProperties);
#undef CHECK_FUNC

  performancelayers::RuntimeLayerData* layer_data = GetLayerData();

  PFN_vkGetInstanceProcAddr next_get_proc_addr;
  next_get_proc_addr = layer_data->GetNextInstanceProcAddr(
      instance, &VkLayerInstanceDispatchTable::GetInstanceProcAddr);
  assert(next_get_proc_addr && next_get_proc_addr != VK_NULL_HANDLE);
  return next_get_proc_addr(instance, name);
}
