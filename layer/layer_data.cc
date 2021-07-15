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

#include "layer_data.h"

#include <cinttypes>
#include <cstdint>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "layer_utils.h"
#include "logging.h"

namespace {

constexpr char kEventLogFileEnvVar[] = "VK_PERFORMANCE_LAYERS_EVENT_LOG_FILE";

// Writes |content| to |file| and flushes it.
void WriteLnAndFlush(FILE* file, std::string_view content) {
  assert(file);
  fprintf(file, "%.*s\n", static_cast<int>(content.size()), content.data());
  fflush(file);
}

// Returns a quoted string.
template <typename StrTy>
std::string QuoteStr(StrTy&& str) {
  return absl::StrCat("\"", std::forward<StrTy>(str), "\"");
}

// Returns event log file row prefix with ','-separated |event_type| and
// |timestamp|.
std::string MakeEventLogPrefix(std::string_view event_type,
                               absl::Time timestamp = absl::Now()) {
  return performancelayers::CsvCat(event_type, absl::ToUnixNanos(timestamp));
}

// Returns the first create info of type
// VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO in the chain |create_info|.
// Returns |nullptr| if no info is found.
VkLayerInstanceCreateInfo* FindInstanceCreateInfo(
    const VkInstanceCreateInfo* create_info) {
  auto* instance_create_info = const_cast<VkLayerInstanceCreateInfo*>(
      static_cast<const VkLayerInstanceCreateInfo*>(create_info->pNext));

  while (instance_create_info &&
         (instance_create_info->sType !=
              VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
          instance_create_info->function != VK_LAYER_LINK_INFO)) {
    instance_create_info = const_cast<VkLayerInstanceCreateInfo*>(
        static_cast<const VkLayerInstanceCreateInfo*>(
            instance_create_info->pNext));
  }
  return instance_create_info;
}

// Returns the first create info of type
// VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO in the chain |create_info|.
// Returns |nullptr| if no info is found.
VkLayerDeviceCreateInfo* FindDeviceCreateInfo(
    const VkDeviceCreateInfo* create_info) {
  auto* device_create_info = const_cast<VkLayerDeviceCreateInfo*>(
      static_cast<const VkLayerDeviceCreateInfo*>(create_info->pNext));

  while (device_create_info &&
         (device_create_info->sType !=
              VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
          device_create_info->function != VK_LAYER_LINK_INFO)) {
    device_create_info = const_cast<VkLayerDeviceCreateInfo*>(
        static_cast<const VkLayerDeviceCreateInfo*>(device_create_info->pNext));
  }

  return device_create_info;
}
}  // namespace

namespace performancelayers {
LayerData::LayerData(char* log_filename, const char* header) {
  out_ = nullptr;
  if (log_filename) {
    out_ = fopen(log_filename, "w");
    if (out_ == nullptr) {
      SPL_LOG(ERROR) << "Failed to open " << log_filename
                     << ", output will be to STDERR.";
      out_ = stderr;
    }
  } else {
    out_ = stderr;
  }

  WriteLnAndFlush(out_, header);

  if (const char* event_log_file = getenv(kEventLogFileEnvVar)) {
    // The underlying log file can be written to by multiple layers from
    // multiple threads. All contentens have to be written in whole lines(s)
    // at a time to ensure there is no unintended interleaving within a single
    // line.
    event_log_ = fopen(event_log_file, "a");
  }
}

void LayerData::RemoveInstance(VkInstance instance) {
  InstanceKey key(instance);
  absl::MutexLock lock(&instance_dispatch_lock_);
  instance_dispatch_map_.erase(key);
  instance_keys_map_.erase(key);
}

void LayerData::LogLine(std::string_view event_type, std::string_view line,
                        absl::Time timestamp) const {
  WriteLnAndFlush(out_, line);
  if (event_log_)
    WriteLnAndFlush(event_log_,
                    CsvCat(MakeEventLogPrefix(event_type, timestamp), line));
}

void LayerData::Log(std::string_view event_type, const HashVector& pipeline,
                    uint64_t time) const {
  // Quote the comma-separated hash value array to always create 2 CSV cells.
  std::string pipeline_hash = QuoteStr(PipelineHashToString(pipeline));
  std::string pipeline_and_time = CsvCat(pipeline_hash, time);
  LogLine(event_type, pipeline_and_time);
}

void LayerData::Log(std::string_view event_type, const HashVector& pipeline,
                    std::string_view prefix) const {
  // Quote the comma-separated hash value array to always create 2 CSV cells.
  std::string pipeline_hash = QuoteStr(PipelineHashToString(pipeline));
  std::string pipeline_and_content = CsvCat(pipeline_hash, prefix);
  LogLine(event_type, pipeline_and_content);
}

void LayerData::LogTimeDelta(std::string_view event_type,
                             std::string_view extra_content) {
  absl::MutexLock lock(&log_time_lock_);
  auto now = absl::Now();
  if (last_log_time_ != absl::InfinitePast()) {
    int64_t logged_delta = ToInt64Nanoseconds(now - last_log_time_);
    LogLine(event_type, CsvCat(logged_delta, extra_content), now);
  }
  last_log_time_ = now;
}

void LayerData::LogEventOnly(std::string_view event_type,
                             std::string_view extra_content) const {
  if (event_log_) {
    const std::string prefix = MakeEventLogPrefix(event_type);
    WriteLnAndFlush(event_log_, extra_content.empty()
                                    ? prefix
                                    : CsvCat(prefix, extra_content));
  }
}

std::string LayerData::ShaderHashToString(uint64_t hash) {
  return absl::StrFormat("%#x", hash);
}

std::string LayerData::PipelineHashToString(const HashVector& pipeline) const {
  return absl::StrCat("[",
                      absl::StrJoin(pipeline, ",",
                                    [](std::string* out, uint64_t hash) {
                                      return absl::StrAppend(
                                          out, ShaderHashToString(hash));
                                    }),
                      "]");
}

VkResult LayerData::CreateInstance(
    const VkInstanceCreateInfo* create_info,
    const VkAllocationCallbacks* allocator, VkInstance* instance,
    std::function<VkLayerInstanceDispatchTable(PFN_vkGetInstanceProcAddr)>
        get_dispatch_table) {
  auto* instance_create_info = FindInstanceCreateInfo(create_info);
  if (instance_create_info == nullptr) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  auto get_proc_addr =
      instance_create_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;

  // Create the instance by calling the next layer's vkCreateInstance.
  instance_create_info->u.pLayerInfo =
      instance_create_info->u.pLayerInfo->pNext;
  auto create_function = reinterpret_cast<PFN_vkCreateInstance>(
      get_proc_addr(VK_NULL_HANDLE, "vkCreateInstance"));
  VkResult res = create_function(create_info, allocator, instance);
  if (res != VK_SUCCESS) {
    return res;
  }

  // Build dispatch table for the instance functions we need to call.
  VkLayerInstanceDispatchTable dispatch_table =
      get_dispatch_table(get_proc_addr);

  // Add the dispatch table to the dispatch map.
  if (!AddInstance(*instance, dispatch_table)) {
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }

  return VK_SUCCESS;
}

VkResult LayerData::CreateDevice(
    VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info,
    const VkAllocationCallbacks* allocator, VkDevice* device,
    std::function<VkLayerDispatchTable(PFN_vkGetDeviceProcAddr)>
        get_dispatch_table) {
  assert(create_info != nullptr);

  auto* device_create_info = FindDeviceCreateInfo(create_info);
  if (device_create_info == nullptr) {
    // No loader device create info.
    return VK_ERROR_INITIALIZATION_FAILED;
  }
  assert(device_create_info->u.pLayerInfo);

  PFN_vkGetInstanceProcAddr get_instance_proc_addr =
      device_create_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  PFN_vkGetDeviceProcAddr get_device_proc_addr =
      device_create_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
  VkInstance instance = GetInstance(InstanceKey(physical_device));
  assert(instance);

  // Create the device after removing the current layer.
  device_create_info->u.pLayerInfo = device_create_info->u.pLayerInfo->pNext;
  auto create_function = reinterpret_cast<PFN_vkCreateDevice>(
      get_instance_proc_addr(instance, "vkCreateDevice"));
  assert(create_function);
  VkResult result =
      create_function(physical_device, create_info, allocator, device);

  if (result != VK_SUCCESS) {
    return result;
  }

  // Build dispatch table for the device functions we need to call.
  VkLayerDispatchTable dispatch_table =
      get_dispatch_table(get_device_proc_addr);

  // Add the dispatch_table to the dispatch map.
  if (!AddDevice(*device, dispatch_table)) {
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  return VK_SUCCESS;
}

LayerData::ShaderModuleCreateResult LayerData::CreateShaderModule(
    VkDevice device, const VkShaderModuleCreateInfo* create_info,
    const VkAllocationCallbacks* allocator, VkShaderModule* shader_module) {
  auto next_proc =
      GetNextDeviceProcAddr(device, &VkLayerDispatchTable::CreateShaderModule);
  absl::Time start = absl::Now();
  VkResult result = next_proc(device, create_info, allocator, shader_module);
  absl::Time end = absl::Now();
  uint64_t hash =
      HashShader(*shader_module, create_info->pCode, create_info->codeSize);
  return {result, hash, start, end};
}
}  // namespace performancelayers
