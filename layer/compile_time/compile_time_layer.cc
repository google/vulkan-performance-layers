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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

#include "layer/support/event_logging.h"
#include "layer/support/layer_data.h"
#include "layer/support/layer_utils.h"
#include "layer/support/trace_event_logging.h"

namespace performancelayers {
namespace {
// ----------------------------------------------------------------------------
// Layer book-keeping information
// ----------------------------------------------------------------------------

constexpr uint32_t kCompileTimeLayerVersion = 1;
constexpr char kLayerName[] = "VK_LAYER_STADIA_pipeline_compile_time";
constexpr char kLayerDescription[] =
    "Stadia Pipeline Compile Time Measuring Layer";
constexpr char kLogFilenameEnvVar[] = "VK_COMPILE_TIME_LOG";
constexpr char kTraceEventCategory[] = "compile_time_layer";

class CompileTimeEvent : public Event {
 public:
  CompileTimeEvent(const char* name, const std::vector<int64_t>& hash_values,
                   Duration duration)
      : Event(name, LogLevel::kHigh),
        hash_values_("hashes", hash_values),
        duration_{"duration", duration},
        trace_attr_("trace_attr", kTraceEventCategory, "X",
                    {&duration_, &hash_values_}) {
    InitAttributes({&hash_values_, &duration_, &trace_attr_});
  }

 private:
  VectorInt64Attr hash_values_;
  DurationAttr duration_;
  TraceEventAttr trace_attr_;
};

class ShaderModuleSlackEvent : public Event {
 public:
  ShaderModuleSlackEvent(const char* name, int64_t hash_value,
                         Duration duration)
      : Event(name),
        shader_hash_("shader_hash", hash_value),
        duration_("slack", duration),
        trace_attr_("trace_attr", kTraceEventCategory, "X",
                    {&duration_, &shader_hash_}) {
    InitAttributes({&shader_hash_, &duration_, &trace_attr_});
  }

 private:
  HashAttr shader_hash_;
  DurationAttr duration_;
  TraceEventAttr trace_attr_;
};

class CreateShaderEvent : public Event {
 public:
  CreateShaderEvent(const char* name, int64_t hash_value, Duration duration)
      : Event(name),
        shader_hash_("shader_hash", hash_value),
        duration_("duration", duration),
        trace_attr_("trace_attr", kTraceEventCategory, "X",
                    {&duration_, &shader_hash_}) {
    InitAttributes({&shader_hash_, &duration_, &trace_attr_});
  }

 private:
  HashAttr shader_hash_;
  DurationAttr duration_;
  TraceEventAttr trace_attr_;
};

class CompileTimeLayerData : public LayerDataWithTraceEventLogger {
 public:
  CompileTimeLayerData(char* log_filename)
      : LayerDataWithTraceEventLogger(log_filename,
                                      "Pipeline,Compile Time (ns)") {
    LayerInitEvent event("compile_time_layer_init", kTraceEventCategory);
    LogEvent(&event);
  }

  // Used to track the slack between shader module creation and its first use
  // in pipeline creation.
  struct ShaderModuleSlack {
    std::optional<DurationClock::time_point> creation_end_time = std::nullopt;
    std::optional<DurationClock::time_point> first_use_time = std::nullopt;
  };

  // Records the time |create_end| of the |shader| creation.
  void RecordShaderModuleCreation(VkShaderModule shader,
                                  DurationClock::time_point create_end) {
    absl::MutexLock lock(&shader_module_usage_lock_);
    assert(!shader_module_to_usage_.contains(shader) &&
           "Shader already created");
    shader_module_to_usage_[shader] = {create_end, std::nullopt};
  }

  // Records shader module use in a pipeline creation. If this is the first
  // use of this shader module, adds an event with the time since shader
  // module creation.
  void RecordShaderModuleUse(VkShaderModule shader) {
    Duration first_use_slack_ns = Duration::Min();

    {
      absl::MutexLock lock(&shader_module_usage_lock_);
      assert(shader_module_to_usage_.contains(shader) &&
             "Shader creation not recorded");
      ShaderModuleSlack& usage_info = shader_module_to_usage_[shader];
      assert(usage_info.creation_end_time.has_value());

      if (!usage_info.first_use_time) {
        usage_info.first_use_time = Now();
        first_use_slack_ns =
            *usage_info.first_use_time - *usage_info.creation_end_time;
      }
    }

    if (first_use_slack_ns != Duration::Min()) {
      const uint64_t hash = GetShaderHash(shader);
      ShaderModuleSlackEvent event("shader_module_first_use_slack_ns", hash,
                                   first_use_slack_ns);
      LogEvent(&event);
    }
  }

 private:
  mutable absl::Mutex shader_module_usage_lock_;
  // Map from  shader module handles to their usage info.
  absl::flat_hash_map<VkShaderModule, ShaderModuleSlack> shader_module_to_usage_
      ABSL_GUARDED_BY(shader_module_usage_lock_);
};

CompileTimeLayerData* GetLayerData() {
  // Don't use new -- make the destructor run when the layer gets unloaded.
  static CompileTimeLayerData layer_data(getenv(kLogFilenameEnvVar));
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
  CompileTimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextInstanceProcAddr(
      instance, &VkLayerInstanceDispatchTable::DestroyInstance);
  layer_data->RemoveInstance(instance);
  next_proc(instance, allocator);
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
SPL_COMPILE_TIME_LAYER_FUNC(VkResult, CreateComputePipelines,
                            (VkDevice device, VkPipelineCache pipeline_cache,
                             uint32_t create_info_count,
                             const VkComputePipelineCreateInfo* create_infos,
                             const VkAllocationCallbacks* alloc_callbacks,
                             VkPipeline* pipelines)) {
  CompileTimeLayerData* layer_data = GetLayerData();

  for (uint32_t i = 0; i != create_info_count; ++i) {
    layer_data->RecordShaderModuleUse(create_infos[i].stage.module);
  }

  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CreateComputePipelines);

  assert(create_info_count > 0 &&
         "Specification says create_info_count must be > 0.");

  DurationClock::time_point start = Now();
  auto result = next_proc(device, pipeline_cache, create_info_count,
                          create_infos, alloc_callbacks, pipelines);
  DurationClock::time_point end = Now();
  Duration duration = end - start;

  LayerData::HashVector hashes;
  for (uint32_t i = 0; i < create_info_count; ++i) {
    auto h = layer_data->HashComputePipeline(pipelines[i], create_infos[i]);
    hashes.insert(hashes.end(), h.begin(), h.end());
  }
  std::vector<int64_t> pipeline_hashes(hashes.begin(), hashes.end());
  CompileTimeEvent event("create_compute_pipelines", pipeline_hashes, duration);
  layer_data->LogEvent(&event);
  return result;
}

// Override for vkCreateGraphicsPipelines.  Measures the time it takes to
// compile the pipeline, and logs the result.
SPL_COMPILE_TIME_LAYER_FUNC(VkResult, CreateGraphicsPipelines,
                            (VkDevice device, VkPipelineCache pipeline_cache,
                             uint32_t create_info_count,
                             const VkGraphicsPipelineCreateInfo* create_infos,
                             const VkAllocationCallbacks* alloc_callbacks,
                             VkPipeline* pipelines)) {
  CompileTimeLayerData* layer_data = GetLayerData();

  for (uint32_t i = 0; i != create_info_count; ++i) {
    const uint32_t stage_count = create_infos[i].stageCount;
    for (uint32_t stage_idx = 0; stage_idx != stage_count; ++stage_idx) {
      layer_data->RecordShaderModuleUse(
          create_infos[i].pStages[stage_idx].module);
    }
  }

  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CreateGraphicsPipelines);

  assert(create_info_count > 0 &&
         "Specification says create_info_count must be > 0.");

  DurationClock::time_point start = Now();
  auto result = next_proc(device, pipeline_cache, create_info_count,
                          create_infos, alloc_callbacks, pipelines);
  DurationClock::time_point end = Now();
  Duration duration = end - start;

  LayerData::HashVector hashes;
  for (uint32_t i = 0; i < create_info_count; ++i) {
    auto h = layer_data->HashGraphicsPipeline(pipelines[i], create_infos[i]);
    hashes.insert(hashes.end(), h.begin(), h.end());
  }
  std::vector<int64_t> pipeline_hashes(hashes.begin(), hashes.end());
  CompileTimeEvent event("create_graphics_pipelines", pipeline_hashes,
                         duration);
  layer_data->LogEvent(&event);
  return result;
}

// Override for vkCreateShaderModule.  Records the hash of the shader module in
// the layer data.
SPL_COMPILE_TIME_LAYER_FUNC(VkResult, CreateShaderModule,
                            (VkDevice device,
                             const VkShaderModuleCreateInfo* create_info,
                             const VkAllocationCallbacks* allocator,
                             VkShaderModule* shader_module)) {
  CompileTimeLayerData* layer_data = GetLayerData();
  const CompileTimeLayerData::ShaderModuleCreateResult res =
      layer_data->CreateShaderModule(device, create_info, allocator,
                                     shader_module);

  if (res.result == VK_SUCCESS) {
    layer_data->RecordShaderModuleCreation(*shader_module, res.create_end);
    CreateShaderEvent event("create_shader_module_ns", res.shader_hash,
                            res.create_end - res.create_start);
    layer_data->LogEvent(&event);
  }
  return res.result;
}

// Override for vkDestroyShaderModule. Erases the shader module from the layer
// data.
SPL_COMPILE_TIME_LAYER_FUNC(void, DestroyShaderModule,
                            (VkDevice device, VkShaderModule shader_module,
                             const VkAllocationCallbacks* allocator)) {
  return GetLayerData()->DestroyShaderModule(device, shader_module, allocator);
}

// Override for vkDestroyDevice. Removes the dispatch table for the device from
// the layer data.
SPL_COMPILE_TIME_LAYER_FUNC(void, DestroyDevice,
                            (VkDevice device,
                             const VkAllocationCallbacks* allocator)) {
  CompileTimeLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::DestroyDevice);
  layer_data->RemoveDevice(device);
  next_proc(device, allocator);
}

// Override for vkCreateDevice. Builds the dispatch table for the new device
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
    SPL_DISPATCH_DEVICE_FUNC(CreateComputePipelines);
    SPL_DISPATCH_DEVICE_FUNC(CreateGraphicsPipelines);
    SPL_DISPATCH_DEVICE_FUNC(CreateShaderModule);
    SPL_DISPATCH_DEVICE_FUNC(DestroyShaderModule);

    return dispatch_table;
  };

  return GetLayerData()->CreateDevice(physical_device, create_info, allocator,
                                      device, build_dispatch_table);
}

SPL_COMPILE_TIME_LAYER_FUNC(VkResult, EnumerateInstanceLayerProperties,
                            (uint32_t * property_count,
                             VkLayerProperties* properties)) {
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

SPL_COMPILE_TIME_LAYER_FUNC(VkResult, EnumerateDeviceLayerProperties,
                            (VkPhysicalDevice /* physical_device */,
                             uint32_t* property_count,
                             VkLayerProperties* properties)) {
  return CompileTimeLayer_EnumerateInstanceLayerProperties(property_count,
                                                           properties);
}

}  // namespace

// The *GetProcAddr functions are the entry points to the layers.
// They return a function pointer for the instance requested by |name|.  We
// return the functions defined in this layer for those we want to override.
// Otherwise we call the *GetProcAddr function for the next layer to get the
// function to be called.

SPL_LAYER_ENTRY_POINT
SPL_COMPILE_TIME_LAYER_FUNC(PFN_vkVoidFunction, GetDeviceProcAddr,
                            (VkDevice device, const char* name)) {
  if (auto func = FunctionInterceptor::GetInterceptedOrNull(name)) {
    return func;
  }

  CompileTimeLayerData* layer_data = GetLayerData();

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
  if (auto func = FunctionInterceptor::GetInterceptedOrNull(name)) {
    return func;
  }

  CompileTimeLayerData* layer_data = GetLayerData();

  PFN_vkGetInstanceProcAddr next_get_proc_addr =
      layer_data->GetNextInstanceProcAddr(
          instance, &VkLayerInstanceDispatchTable::GetInstanceProcAddr);
  assert(next_get_proc_addr && next_get_proc_addr != VK_NULL_HANDLE);
  return next_get_proc_addr(instance, name);
}
}  // namespace performancelayers
