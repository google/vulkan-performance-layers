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

#ifndef STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_LAYER_DATA_H_
#define STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_LAYER_DATA_H_

#include <cstdint>
#include <cstdio>
#include <functional>
#include <set>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "farmhash.h"
#include "vulkan/vk_layer.h"
#include "vulkan/vulkan.h"
#include "vulkan/vulkan_core.h"

// clang-format: do not reorder the include below.
#include "vk_layer_dispatch_table.h"

namespace performancelayers {

// A class that contains all of the data that is needed for the functions
// that this layer will override.
//
// The filename for the log file will be retrieved from the environment variable
// "VK_COMPILE_TIME_LOG".  If it is unset, then stderr will be used as the
// log file.
class LayerData {
 public:
  using InstanceDispatchMap =
      absl::flat_hash_map<VkInstance, VkLayerInstanceDispatchTable>;
  using DeviceDispatchMap = absl::flat_hash_map<VkDevice, VkLayerDispatchTable>;

  LayerData(char* log_filename, const char* header);

  virtual ~LayerData() {
    if (out_ != stderr) {
      fclose(out_);
    }
  }

  // Records the dispatch table that is associated with |instance|.
  bool AddInstance(VkInstance instance,
                   const VkLayerInstanceDispatchTable& dispatch_table) {
    absl::MutexLock lock(&instance_dispatch_lock_);
    return instance_dispatch_map_.insert({instance, dispatch_table}).second;
  }

  // Removes the dispatch table associated with |instance|.
  void RemoveInstance(VkInstance instance) {
    absl::MutexLock lock(&instance_dispatch_lock_);
    instance_dispatch_map_.erase(instance);
  }

  // Records the dispatch table and get_device_proc_addr associated with
  // |device|.
  bool AddDevice(VkDevice device, const VkLayerDispatchTable& dispatch_table) {
    absl::MutexLock dispatch_table_lock(&device_dispatch_lock_);
    return device_dispatch_map_.insert({device, dispatch_table}).second;
  }

  // Removes the dispatch table associated with |device|.
  void RemoveDevice(VkDevice device) {
    absl::MutexLock lock(&device_dispatch_lock_);
    device_dispatch_map_.erase(device);
  }

  // Returns the function pointer for the function |funct_ptr| for the next
  // layer in |instance|. This is can be used only with functions declared in
  // the dispatch table. See https://renderdoc.org/vulkan-layer-guide.html.
  template <typename TFuncPtr>
  auto GetNextInstanceProcAddr(VkInstance instance, TFuncPtr func_ptr) const {
    absl::MutexLock lock(&instance_dispatch_lock_);
    auto instance_dispatch_iter = instance_dispatch_map_.find(instance);
    assert(instance_dispatch_iter != instance_dispatch_map_.end());
    auto proc_addr = instance_dispatch_iter->second.*func_ptr;
    assert(proc_addr);
    return proc_addr;
  }

  // Returns the function pointer for the function |funct_ptr| for the next
  // layer in |device|. This is can be used only with functions declared in the
  // dispatch table. See https://renderdoc.org/vulkan-layer-guide.html.
  template <typename TFuncPtr>
  auto GetNextDeviceProcAddr(VkDevice device, TFuncPtr func_ptr) const {
    absl::MutexLock lock(&device_dispatch_lock_);
    auto device_dispatch_iter = device_dispatch_map_.find(device);
    assert(device_dispatch_iter != device_dispatch_map_.end());
    auto proc_addr = device_dispatch_iter->second.*func_ptr;
    assert(proc_addr);
    return proc_addr;
  }

  // Records the hash of |code|, whose size is |size|, and associates it with
  // |shader_module|. This must be called before you can call |GetShaderHash|
  // with |shader_module|.
  void HashShader(VkShaderModule shader_module, const uint32_t* code,
                  uint32_t size) {
    const char* c = reinterpret_cast<const char*>(code);
    uint64_t hash_value = util::Fingerprint64(c, size);
    absl::MutexLock lock(&shader_hash_lock_);
    shader_hash_map_.insert_or_assign(shader_module, hash_value);
  }

  // Return the hash associated with |shader_module|.
  uint64_t GetShaderHash(VkShaderModule shader_module) const {
    absl::MutexLock lock(&shader_hash_lock_);
    assert(shader_hash_map_.count(shader_module) != 0);
    return shader_hash_map_.at(shader_module);
  }

  // Computes and caches the hash of the compute pipeline |pipeline| that was
  // created using |create_info|.
  std::vector<uint64_t> HashComputePipeline(
      VkPipeline pipeline, const VkComputePipelineCreateInfo& create_info) {
    std::vector<uint64_t> hashes;
    hashes.push_back(GetShaderHash(create_info.stage.module));

    absl::MutexLock lock(&pipeline_hash_lock_);
    pipeline_hash_map_.insert_or_assign(pipeline, hashes);
    return hashes;
  }

  // Computes and caches the hash of the graphics pipeline |pipeline| that was
  // created using |create_info|.
  std::vector<uint64_t> HashGraphicsPipeline(
      VkPipeline pipeline, const VkGraphicsPipelineCreateInfo& create_info) {
    std::vector<uint64_t> hashes;
    for (uint32_t j = 0; j < create_info.stageCount; ++j) {
      uint64_t h = GetShaderHash(create_info.pStages[j].module);
      hashes.push_back(h);
    }

    absl::MutexLock lock(&pipeline_hash_lock_);
    pipeline_hash_map_.insert_or_assign(pipeline, hashes);
    return hashes;
  }

  // Returns the cached hash of the pipeline |pipeline|.
  const std::vector<uint64_t>& GetPipelineHash(VkPipeline pipeline) const {
    absl::MutexLock lock(&pipeline_hash_lock_);
    assert(pipeline_hash_map_.count(pipeline) != 0);
    return pipeline_hash_map_.at(pipeline);
  }

  // Logs the compile time |time| for |pipeline| to the log file.  |pipeline| is
  // any series of integers that represent the pipeline.  We are using the hash
  // of each shader that is part of the pipeline.
  void Log(const std::vector<uint64_t>& pipeline, uint64_t time) const;

  // Logs an arbitrary string prefixed by the given pipeline.
  void Log(const std::vector<uint64_t>& pipeline, const std::string& str) const;

  // Logs the time since the last call to LogTimeDelta.
  void LogTimeDelta(const char* extra_content = "");

  // Returns a string identifier of |pipeline|.
  std::string PipelineHashToString(const std::vector<uint64_t>& pipeline) const;

  // Sets the dispatch table for |device| to the one returned by
  // |get_dispatch_table|, and calls |CreateDevice| for the next layer.
  VkResult CreateDevice(
      VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info,
      const VkAllocationCallbacks* allocator, VkDevice* device,
      std::function<VkLayerDispatchTable(PFN_vkGetDeviceProcAddr)>
          get_dispatch_table);

  // Sets the dispatch table for |instance| to the one returned by
  // |get_dispatch_table|, and calls |CreateInstance| for the next layer.
  VkResult CreateInstance(
      const VkInstanceCreateInfo* create_info,
      const VkAllocationCallbacks* allocator, VkInstance* instance,
      std::function<VkLayerInstanceDispatchTable(PFN_vkGetInstanceProcAddr)>
          get_dispatch_table);

  // Builds the shader module by calling |CreateShaderModule| for the next
  // layer, and records the hash of the resulting shader module.
  VKAPI_ATTR VkResult CreateShaderModule(
      VkDevice device, const VkShaderModuleCreateInfo* create_info,
      const VkAllocationCallbacks* allocator, VkShaderModule* shader_module);

 private:
  mutable absl::Mutex instance_dispatch_lock_;
  // A map from a VkInstance to its VkLayerInstanceDispatchTable.
  InstanceDispatchMap instance_dispatch_map_
      ABSL_GUARDED_BY(instance_dispatch_lock_);

  mutable absl::Mutex device_dispatch_lock_;
  // A map from a VkDevice to its VkLayerDispatchTable.
  DeviceDispatchMap device_dispatch_map_ ABSL_GUARDED_BY(device_dispatch_lock_);

  mutable absl::Mutex shader_hash_lock_;
  // The map from a shader module to the result of its hash.
  absl::flat_hash_map<VkShaderModule, uint64_t> shader_hash_map_
      ABSL_GUARDED_BY(shader_hash_lock_);

  mutable absl::Mutex pipeline_hash_lock_;
  // The map from a pipeline to the result of its hash.
  absl::flat_hash_map<VkPipeline, std::vector<uint64_t>> pipeline_hash_map_
      ABSL_GUARDED_BY(pipeline_hash_lock_);

  mutable absl::Mutex log_lock_;
  // A pointer to the log file to use.
  FILE* out_ ABSL_GUARDED_BY(log_lock_);
  // The last time LogTimeDelta was called.
  absl::Time last_log_time_ ABSL_GUARDED_BY(log_lock_) = absl::InfinitePast();
};

}  // namespace performancelayers

#endif  // STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_LAYER_DATA_H_
