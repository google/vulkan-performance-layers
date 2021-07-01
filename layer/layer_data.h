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

#ifndef STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_LAYER_DATA_H_
#define STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_LAYER_DATA_H_

#include <cstdint>
#include <cstdio>
#include <functional>
#include <set>
#include <tuple>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "farmhash.h"
#include "vulkan/vk_layer.h"
#include "vulkan/vulkan.h"
#include "vulkan/vulkan_core.h"

// clang-format: do not reorder the include below.
#include "vk_layer_dispatch_table.h"

namespace performancelayers {

// Joins all |args| with the ',' CSV separator.
template <typename... Args>
std::string CsvCat(Args&&... args) {
  return absl::StrJoin(std::forward_as_tuple(std::forward<Args>(args)...), ",");
}

// Base class for Vulkan dispatchable handle wrappers. Exposes the underlying
// key to derived classes and implements basic operations like key comparisons
// and hash.
template <typename ConcreteKeyT>
class DispatchableKeyBase {
 public:
  bool operator==(const ConcreteKeyT& other) const {
    return key_ == other.key_;
  }
  bool operator!=(const ConcreteKeyT& other) const {
    return key_ != other.key_;
  }
  size_t hash() const { return std::hash<void*>{}(key_); }

 protected:
  using BaseT = DispatchableKeyBase;
  explicit DispatchableKeyBase(void** raw_handle) {
    if (raw_handle) key_ = *raw_handle;
  }
  void* key_ = nullptr;
};

// InstanceKey provides the underlying instance key for dispatchable instance
// handles: VkInstance and VkPhysicalDevice. Typically used with
// `GetNextInstanceProcAddr`. Can be used as a hash map key.
class InstanceKey : public DispatchableKeyBase<InstanceKey> {
 public:
  InstanceKey() : BaseT(nullptr) {}

  explicit InstanceKey(VkInstance instance)
      : BaseT(reinterpret_cast<void**>(instance)) {}
  explicit InstanceKey(VkPhysicalDevice gpu)
      : BaseT(reinterpret_cast<void**>(gpu)) {}

  // Copyable and moveable.
  InstanceKey(const InstanceKey&) = default;
  InstanceKey(InstanceKey&&) = default;
  InstanceKey& operator=(const InstanceKey&) = default;
  InstanceKey& operator=(InstanceKey&&) = default;
};

// DeviceKey provides the underlying device key for dispatchable device
// handles: VkDevice, VkQueue, and VkCommandBuffer. Typically used with
// `GetNextDeviceProcAddr`. Can be used as a hash map key.
class DeviceKey : public DispatchableKeyBase<DeviceKey> {
 public:
  DeviceKey() : BaseT(nullptr) {}

  explicit DeviceKey(VkDevice device)
      : BaseT(reinterpret_cast<void**>(device)) {}
  explicit DeviceKey(VkQueue queue) : BaseT(reinterpret_cast<void**>(queue)) {}
  explicit DeviceKey(VkCommandBuffer cmd_buffer)
      : BaseT(reinterpret_cast<void**>(cmd_buffer)) {}

  // Copyable and moveable.
  DeviceKey(const DeviceKey&) = default;
  DeviceKey(DeviceKey&&) = default;
  DeviceKey& operator=(const DeviceKey&) = default;
  DeviceKey& operator=(DeviceKey&&) = default;
};

}  // namespace performancelayers

// Make dispatchable keys hashable.
namespace std {
template <>
struct hash<performancelayers::InstanceKey> {
  size_t operator()(const performancelayers::InstanceKey& key) const {
    return key.hash();
  }
};
template <>
struct hash<performancelayers::DeviceKey> {
  size_t operator()(const performancelayers::DeviceKey& key) const {
    return key.hash();
  }
};
}  // namespace std

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
      absl::flat_hash_map<InstanceKey, VkLayerInstanceDispatchTable>;
  using DeviceDispatchMap =
      absl::flat_hash_map<DeviceKey, VkLayerDispatchTable>;

  LayerData(char* log_filename, const char* header);

  virtual ~LayerData() {
    if (out_ != stderr) {
      fclose(out_);
    }
  }

  // Records the dispatch table and instance key that is associated with
  // |instance|.
  bool AddInstance(VkInstance instance,
                   const VkLayerInstanceDispatchTable& dispatch_table) {
    InstanceKey key(instance);

    absl::MutexLock lock(&instance_dispatch_lock_);
    const bool inserted =
        instance_dispatch_map_.insert({key, dispatch_table}).second;
    if (inserted) {
      instance_keys_map_[key] = instance;
    }
    return inserted;
  }

  // Removes the dispatch table and physical devices associated with |instance|.
  void RemoveInstance(VkInstance instance);

  // Returns the instance associated with |instance_key|, or a null handle if
  // there is none.
  VkInstance GetInstance(InstanceKey instance_key) const {
    absl::MutexLock lock(&instance_dispatch_lock_);
    if (auto it = instance_keys_map_.find(instance_key);
        it != instance_keys_map_.end()) {
      return it->second;
    }
    return VK_NULL_HANDLE;
  }

  // Records the dispatch table and device key associated with
  // |device|.
  bool AddDevice(VkDevice device, const VkLayerDispatchTable& dispatch_table) {
    DeviceKey key(device);
    absl::MutexLock dispatch_table_lock(&device_dispatch_lock_);
    const bool inserted =
        device_dispatch_map_.insert({key, dispatch_table}).second;
    if (inserted) {
      device_keys_map_[key] = device;
    }
    return inserted;
  }

  // Removes the dispatch table associated with |device|.
  void RemoveDevice(VkDevice device) {
    DeviceKey key(device);
    absl::MutexLock lock(&device_dispatch_lock_);
    device_dispatch_map_.erase(key);
    device_keys_map_.erase(key);
  }

  // Returns the device associated with |device_key|, or a null handle if
  // there is none.
  VkDevice GetDevice(DeviceKey device_key) const {
    absl::MutexLock lock(&device_dispatch_lock_);
    if (auto it = device_keys_map_.find(device_key);
        it != device_keys_map_.end()) {
      return it->second;
    }
    return VK_NULL_HANDLE;
  }

  // Returns the function pointer for the function |funct_ptr| for the next
  // layer in the instance. |instance_handle| must be a VkInstance or
  // VkPhysicalDevice. This is can be used only with functions declared in the
  // dispatch table. See https://renderdoc.org/vulkan-layer-guide.html.
  template <typename DispatchableInstanceHandleT, typename TFuncPtr>
  auto GetNextInstanceProcAddr(DispatchableInstanceHandleT instance_handle,
                               TFuncPtr func_ptr) const {
    absl::MutexLock lock(&instance_dispatch_lock_);
    auto instance_dispatch_iter =
        instance_dispatch_map_.find(InstanceKey(instance_handle));
    assert(instance_dispatch_iter != instance_dispatch_map_.end());
    auto proc_addr = instance_dispatch_iter->second.*func_ptr;
    assert(proc_addr);
    return proc_addr;
  }

  // Returns the function pointer for the function |funct_ptr| for the next
  // layer in the device. |device_handle| must be one of: VkDevice, VkQueue, or
  // VkCommandBuffer. This is can be used only with functions declared in the
  // dispatch table. See https://renderdoc.org/vulkan-layer-guide.html.
  template <typename DispatchableDeviceHandleT, typename TFuncPtr>
  auto GetNextDeviceProcAddr(DispatchableDeviceHandleT device_handle,
                             TFuncPtr func_ptr) const {
    absl::MutexLock lock(&device_dispatch_lock_);
    auto device_dispatch_iter =
        device_dispatch_map_.find(DeviceKey(device_handle));
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

  // Logs one line to the log file, and to the event log file, if enabled.
  void LogLine(const char* event_type, const std::string& line,
               absl::Time timestamp = absl::Now()) const;

  // Logs the compile time |time| for |pipeline| to the log file.
  // |event_type| is used as the key in the event log file, if enabled.
  // |pipeline| is any series of integers that represent the pipeline.
  // We are using the hash of each shader that is part of the pipeline.
  void Log(const char* event_type, const std::vector<uint64_t>& pipeline,
           uint64_t time) const;

  // Logs an arbitrary string prefixed by the given pipeline.
  void Log(const char* event_type, const std::vector<uint64_t>& pipeline,
           const std::string& str) const;

  // Logs the time since the last call to LogTimeDelta.
  void LogTimeDelta(const char* event_type,
                    const std::string& extra_content = "");

  // Logs an arbitrary |extra_content| to the event log file.
  // Doesn't write to the layer log file.
  void LogEventOnly(const char* event_type,
                    const std::string& extra_content = "") const;

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
  // A map from an InstanceKey to its VkInstance.
  absl::flat_hash_map<InstanceKey, VkInstance> instance_keys_map_;
  ABSL_GUARDED_BY(instance_dispatch_lock_)

  mutable absl::Mutex device_dispatch_lock_;
  // A map from a VkDevice to its VkLayerDispatchTable.
  DeviceDispatchMap device_dispatch_map_ ABSL_GUARDED_BY(device_dispatch_lock_);
  // A map from a DeviceKey to its VkDevice.
  absl::flat_hash_map<DeviceKey, VkDevice> device_keys_map_;
  ABSL_GUARDED_BY(device_dispatch_lock_)

  mutable absl::Mutex shader_hash_lock_;
  // The map from a shader module to the result of its hash.
  absl::flat_hash_map<VkShaderModule, uint64_t> shader_hash_map_
      ABSL_GUARDED_BY(shader_hash_lock_);

  mutable absl::Mutex pipeline_hash_lock_;
  // The map from a pipeline to the result of its hash.
  absl::flat_hash_map<VkPipeline, std::vector<uint64_t>> pipeline_hash_map_
      ABSL_GUARDED_BY(pipeline_hash_lock_);

  // A pointer to the log file to use.
  FILE* out_;
  mutable absl::Mutex log_time_lock_;
  // The last time LogTimeDelta was called.
  absl::Time last_log_time_ ABSL_GUARDED_BY(log_time_lock_) =
      absl::InfinitePast();

  // Event log file appended to by multiple layers, or nullptr.
  FILE* event_log_ = nullptr;
};
}  // namespace performancelayers

#endif  // STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_LAYER_DATA_H_
