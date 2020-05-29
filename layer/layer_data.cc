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

#include "layer_data.h"

#include <inttypes.h>
#include <cstdint>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/mutex.h"
#include "vulkan/vulkan_core.h"

namespace {

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
      fprintf(stderr, "Failed to open %s, output will be to STDERR.\n",
              log_filename);
      out_ = stderr;
    }
  } else {
    out_ = stderr;
  }
  fprintf(out_, "%s\n", header);
  fflush(out_);
}

void LayerData::Log(const std::vector<uint64_t>& pipeline,
                    uint64_t time) const {
  absl::MutexLock lock(&log_lock_);
  // Quote the comma-separated hash value array to always create 2 CSV cells.
  fprintf(out_, "\"%s\",%" PRIu64 "\n", PipelineHashToString(pipeline).c_str(),
          time);
  fflush(out_);
}

void LayerData::LogTimeDelta() {
  absl::MutexLock lock(&log_lock_);
  auto now = absl::Now();
  if (last_log_time_ != absl::InfinitePast()) {
    const int64_t delta = ToInt64Nanoseconds(now - last_log_time_);
    fprintf(out_, "%" PRId64 "\n", delta);
    fflush(out_);
  }
  last_log_time_ = now;
}

std::string LayerData::PipelineHashToString(
    const std::vector<uint64_t>& pipeline) const {
  return absl::StrCat("[",
                      absl::StrJoin(pipeline, ",",
                                    [](std::string* out, uint64_t num) {
                                      return absl::StrAppend(
                                          out, absl::StrFormat("%#x", num));
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

  PFN_vkGetInstanceProcAddr get_instance_proc_addr =
      device_create_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  PFN_vkGetDeviceProcAddr get_device_proc_addr =
      device_create_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;

  // Create the device after removing the current layer.
  device_create_info->u.pLayerInfo = device_create_info->u.pLayerInfo->pNext;
  auto create_function = reinterpret_cast<PFN_vkCreateDevice>(
      get_instance_proc_addr(VK_NULL_HANDLE, "vkCreateDevice"));
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

VKAPI_ATTR VkResult LayerData::CreateShaderModule(
    VkDevice device, const VkShaderModuleCreateInfo* create_info,
    const VkAllocationCallbacks* allocator, VkShaderModule* shader_module) {
  auto next_proc =
      GetNextDeviceProcAddr(device, &VkLayerDispatchTable::CreateShaderModule);
  auto result = (next_proc)(device, create_info, allocator, shader_module);
  HashShader(*shader_module, create_info->pCode, create_info->codeSize);
  return result;
}
}  // namespace performancelayers
