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

#include <cassert>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "debug_logging.h"
#include "input_buffer.h"
#include "layer_data.h"
#include "layer_utils.h"

namespace performancelayers {
namespace {
class CacheSideloadLayerData : public LayerData {
 public:
  CacheSideloadLayerData(const char* pipeline_cache_path)
      : LayerData(nullptr, ""),
        implicit_pipeline_cache_path_(pipeline_cache_path) {
    LogEventOnly("cache_sideload_layer_init");
  }

  VkPipelineCache GetImplicitDeviceCache(VkDevice) const;
  void RemoveImplicitDeviceCache(VkDevice);
  VkPipelineCache CreateImplicitDeviceCache(
      VkDevice device, const VkAllocationCallbacks* alloc_callbacks,
      absl::Span<const uint8_t> initial_data);

  std::optional<size_t> QueryPipelineCacheSize(VkDevice, VkPipelineCache cache);

  std::optional<InputBuffer> ReadImplicitCacheFile();

 private:
  mutable absl::Mutex device_to_implicit_cache_handle_lock_;
  absl::flat_hash_map<VkDevice, VkPipelineCache>
      device_to_implicit_cache_handle_
          ABSL_GUARDED_BY(device_to_implicit_cache_handle_lock_);

  const char* implicit_pipeline_cache_path_ = nullptr;
};

VkPipelineCache CacheSideloadLayerData::GetImplicitDeviceCache(
    VkDevice device) const {
  absl::MutexLock lock(&device_to_implicit_cache_handle_lock_);
  if (auto it = device_to_implicit_cache_handle_.find(device);
      it != device_to_implicit_cache_handle_.end())
    return it->second;

  return nullptr;
}

void CacheSideloadLayerData::RemoveImplicitDeviceCache(VkDevice device) {
  absl::MutexLock lock(&device_to_implicit_cache_handle_lock_);
  device_to_implicit_cache_handle_.erase(device);
}

VkPipelineCache CacheSideloadLayerData::CreateImplicitDeviceCache(
    VkDevice device, const VkAllocationCallbacks* alloc_callbacks,
    absl::Span<const uint8_t> initial_data) {
  absl::MutexLock lock(&device_to_implicit_cache_handle_lock_);
  assert(device_to_implicit_cache_handle_.count(device) == 0 &&
         "Implicit cache already created for this device.");

  const size_t initial_data_size = initial_data.size();
  VkPipelineCacheCreateInfo create_info = {};
  create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  create_info.initialDataSize = initial_data_size;
  create_info.pInitialData = initial_data.data();

  const std::string path_info =
      absl::StrCat("path: ", implicit_pipeline_cache_path_);
  const std::string initial_size_info =
      absl::StrCat("initial_data_size: ", initial_data_size);

  auto create_proc =
      GetNextDeviceProcAddr(device, &VkLayerDispatchTable::CreatePipelineCache);
  VkPipelineCache new_cache = nullptr;
  const VkResult result =
      create_proc(device, &create_info, alloc_callbacks, &new_cache);
  if (result != VK_SUCCESS) {
    SPL_LOG(ERROR) << "Failed to create implicit pipeline cache (" << path_info
                   << ", " << initial_size_info << ")";
    return nullptr;
  }

  // Insert only valid pipeline cache handles.
  device_to_implicit_cache_handle_[device] = new_cache;
  SPL_LOG(INFO) << "Created implicit pipeline cache (" << path_info << ", "
                << initial_size_info << ")";

  // Check if the pipeline cache size, as reported by the ICD, is close enough
  // to the initial data size. If the reported size is unexpectedly small, warn
  // that the initial data might have been not used by the driver.
  const auto cache_size_or_none = QueryPipelineCacheSize(device, new_cache);
  if (cache_size_or_none) {
    SPL_LOG(INFO) << "Cache size reported by the ICD: " << *cache_size_or_none
                  << " B";
    // If the reported size is less than 10% of the initial size, issue a
    // warning.
    if (*cache_size_or_none * 10 < initial_data_size) {
      SPL_LOG(WARNING) << "Cache might not have been accepted by the ICD. "
                          "Initial pipeline data size is "
                       << initial_data_size
                       << " B, but the created cache is only "
                       << *cache_size_or_none << " B large.";
    }
  }

  const std::string cache_size_info =
      absl::StrCat("cache_size: ", cache_size_or_none.value_or(0));
  LogEventOnly("create_implicit_pipeline_cache",
               CsvCat(path_info, initial_size_info, cache_size_info));

  return new_cache;
}

std::optional<size_t> CacheSideloadLayerData::QueryPipelineCacheSize(
    VkDevice device, VkPipelineCache cache) {
  assert(device);
  assert(cache);
  const auto get_pipeline_cache_data_proc = GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::GetPipelineCacheData);

  // Query pipeline cache data size. Because we do not use the API to write out
  // the data blog, this will be an upper bound.
  size_t cache_size_upper_bound = 0;
  const VkResult result = get_pipeline_cache_data_proc(
      device, cache, &cache_size_upper_bound, nullptr);
  if (result != VK_SUCCESS) {
    SPL_LOG(ERROR) << "Failed to query pipeline cache size";
    return std::nullopt;
  }
  return cache_size_upper_bound;
}

std::optional<InputBuffer> CacheSideloadLayerData::ReadImplicitCacheFile() {
  if (!implicit_pipeline_cache_path_ ||
      strlen(implicit_pipeline_cache_path_) == 0) {
    SPL_LOG(WARNING) << "Invalid implicit pipeline cache file path";
    return std::nullopt;
  }

  auto cache_file_or_status =
      performancelayers::InputBuffer::Create(implicit_pipeline_cache_path_);
  if (!cache_file_or_status.ok()) {
    SPL_LOG(ERROR) << "Failed to read implicit pipeline cache: "
                   << cache_file_or_status.status();
    return std::nullopt;
  }
  return std::move(*cache_file_or_status);
}

}  // namespace
}  // namespace performancelayers

namespace {

// ----------------------------------------------------------------------------
// Layer book-keeping information
// ----------------------------------------------------------------------------
constexpr uint32_t kCacheSideloadLayerVersion = 1;

constexpr char kLayerName[] = "VK_LAYER_STADIA_pipeline_cache_sideload";
constexpr char kLayerDescription[] = "Stadia Pipeline Cache Sideloading Layer";
constexpr char kImplicitCacheFilenameEnvVar[] =
    "VK_PIPELINE_CACHE_SIDELOAD_FILE";

performancelayers::CacheSideloadLayerData* GetLayerData() {
  // Don't use new -- make the destructor run when the layer gets unloaded.
  static performancelayers::CacheSideloadLayerData layer_data =
      performancelayers::CacheSideloadLayerData(
          getenv(kImplicitCacheFilenameEnvVar));
  return &layer_data;
}

// Use this macro to define all vulkan functions intercepted by the layer.
#define SPL_CACHE_SIDELOAD_LAYER_FUNC(RETURN_TYPE_, FUNC_NAME_, FUNC_ARGS_)  \
  SPL_INTERCEPTED_VULKAN_FUNC(RETURN_TYPE_, CacheSideloadLayer_, FUNC_NAME_, \
                              FUNC_ARGS_)

//////////////////////////////////////////////////////////////////////////////
//  Implementation of the instance functions we want to override.
//////////////////////////////////////////////////////////////////////////////

// Override for vkDestroyInstance. Deletes the entry for |instance| from the
// layer data.
SPL_CACHE_SIDELOAD_LAYER_FUNC(void, DestroyInstance,
                              (VkInstance instance,
                               const VkAllocationCallbacks* allocator)) {
  performancelayers::CacheSideloadLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextInstanceProcAddr(
      instance, &VkLayerInstanceDispatchTable::DestroyInstance);
  layer_data->RemoveInstance(instance);
  next_proc(instance, allocator);
}

// Override for vkCreateInstance. Creates the dispatch table for this instance
// and add it to the layer data.
SPL_CACHE_SIDELOAD_LAYER_FUNC(VkResult, CreateInstance,
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

// Override for vkCreateComputePipelines. Provides implicit device pipeline
// cache when the application does not provide a pipeline cache object.
SPL_CACHE_SIDELOAD_LAYER_FUNC(VkResult, CreateComputePipelines,
                              (VkDevice device, VkPipelineCache pipeline_cache,
                               uint32_t create_info_count,
                               const VkComputePipelineCreateInfo* create_infos,
                               const VkAllocationCallbacks* alloc_callbacks,
                               VkPipeline* pipelines)) {
  assert(create_info_count > 0 &&
         "Specification says create_info_count must be > 0.");
  performancelayers::CacheSideloadLayerData* layer_data = GetLayerData();

  auto actual_cache = pipeline_cache
                          ? pipeline_cache
                          : layer_data->GetImplicitDeviceCache(device);

  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CreateComputePipelines);
  return next_proc(device, actual_cache, create_info_count, create_infos,
                   alloc_callbacks, pipelines);
}

// Override for vkCreateGraphicsPipelines. Provides implicit device pipeline
// cache when the application does not provide a pipeline cache object.
SPL_CACHE_SIDELOAD_LAYER_FUNC(VkResult, CreateGraphicsPipelines,
                              (VkDevice device, VkPipelineCache pipeline_cache,
                               uint32_t create_info_count,
                               const VkGraphicsPipelineCreateInfo* create_infos,
                               const VkAllocationCallbacks* alloc_callbacks,
                               VkPipeline* pipelines)) {
  assert(create_info_count > 0 &&
         "Specification says create_info_count must be > 0.");
  performancelayers::CacheSideloadLayerData* layer_data = GetLayerData();

  auto actual_cache = pipeline_cache
                          ? pipeline_cache
                          : layer_data->GetImplicitDeviceCache(device);

  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CreateGraphicsPipelines);
  return next_proc(device, actual_cache, create_info_count, create_infos,
                   alloc_callbacks, pipelines);
}

// Override for vkCreatePipelineCache. Merges the implicit device pipeline
// cache into the newly created application cache. Reports merge failures as
// creation failures.
SPL_CACHE_SIDELOAD_LAYER_FUNC(VkResult, CreatePipelineCache,
                              (VkDevice device,
                               const VkPipelineCacheCreateInfo* create_info,
                               const VkAllocationCallbacks* alloc_callbacks,
                               VkPipelineCache* pipeline_cache)) {
  performancelayers::CacheSideloadLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::CreatePipelineCache);

  const VkResult create_result =
      next_proc(device, create_info, alloc_callbacks, pipeline_cache);

  if (create_result == VK_SUCCESS) {
    assert(*pipeline_cache);
    if (VkPipelineCache implicit_cache =
            layer_data->GetImplicitDeviceCache(device)) {
      auto merge_cache_proc = layer_data->GetNextDeviceProcAddr(
          device, &VkLayerDispatchTable::MergePipelineCaches);
      const VkResult merge_result =
          merge_cache_proc(device, *pipeline_cache, 1, &implicit_cache);

      const std::string merge_result_str = absl::StrCat(
          "result: ", (merge_result == VK_SUCCESS) ? "success" : "failure");
      SPL_LOG(INFO) << "Application pipeline cache merge with implicit cache ("
                    << merge_result_str << ")";
      layer_data->LogEventOnly("merge_implicit_pipeline_cache",
                               merge_result_str);

      return merge_result;
    }
  }

  return create_result;
}

// Override for vkGetPipelineCacheData. Checks that application does not
// request pipeline cache data for implicit device pipeline caches.
SPL_CACHE_SIDELOAD_LAYER_FUNC(VkResult, GetPipelineCacheData,
                              (VkDevice device, VkPipelineCache cache,
                               size_t* data_size, void* data_out)) {
  assert(data_size &&
         "According to the spec, data size must be a valid pointer.");
  assert(cache &&
         "According to the spec, pipeline cache must be a valid handle.");

  performancelayers::CacheSideloadLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::GetPipelineCacheData);

  if (cache == layer_data->GetImplicitDeviceCache(device)) {
    SPL_LOG(ERROR)
        << "Application unexpectedly passed a handle to an implicit "
           "pipeline cache managed by the Pipeline Cache Sideload layer";
    *data_size = 0;
    return VK_INCOMPLETE;
  }

  return next_proc(device, cache, data_size, data_out);
}

// Override for vkDestroyPipelineCache. Checks that application does destroy
// an implicit device pipeline cache.
SPL_CACHE_SIDELOAD_LAYER_FUNC(void, DestroyPipelineCache,
                              (VkDevice device, VkPipelineCache cache,
                               const VkAllocationCallbacks* allocator)) {
  assert(cache &&
         "According to the spec, pipeline cache must be a valid handle.");

  performancelayers::CacheSideloadLayerData* layer_data = GetLayerData();
  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::DestroyPipelineCache);

  if (cache == layer_data->GetImplicitDeviceCache(device)) {
    SPL_LOG(ERROR)
        << "Application unexpectedly passed a handle to an implicit "
           "pipeline cache managed by the Pipeline Cache Sideload layer";
    return;
  }

  return next_proc(device, cache, allocator);
}

// Override for vkDestroyDevice. Removes the dispatch table for the device from
// the layer data.
SPL_CACHE_SIDELOAD_LAYER_FUNC(void, DestroyDevice,
                              (VkDevice device,
                               const VkAllocationCallbacks* allocator)) {
  performancelayers::CacheSideloadLayerData* layer_data = GetLayerData();

  // Destroy all layer objects created for this device.
  if (VkPipelineCache cache = layer_data->GetImplicitDeviceCache(device)) {
    layer_data->RemoveImplicitDeviceCache(device);
    auto destroy_pc_proc = layer_data->GetNextDeviceProcAddr(
        device, &VkLayerDispatchTable::DestroyPipelineCache);
    destroy_pc_proc(device, cache, allocator);
  }

  auto next_proc = layer_data->GetNextDeviceProcAddr(
      device, &VkLayerDispatchTable::DestroyDevice);
  layer_data->RemoveDevice(device);
  next_proc(device, allocator);
}

// Override for vkCreateDevice. Builds the dispatch table for the new device
// and add it to the layer data. Creates an implicit layer-managed pipeline
// for each device. This cache is pre-populated with the implicit pipeline
// cache file.
SPL_CACHE_SIDELOAD_LAYER_FUNC(VkResult, CreateDevice,
                              (VkPhysicalDevice physical_device,
                               const VkDeviceCreateInfo* create_info,
                               const VkAllocationCallbacks* allocator,
                               VkDevice* device)) {
  auto build_dispatch_table = [device](PFN_vkGetDeviceProcAddr gdpa) {
    VkLayerDispatchTable dispatch_table{};

    // Get the next layer's instance of the device functions we will override.
    SPL_DISPATCH_DEVICE_FUNC(GetDeviceProcAddr);
    SPL_DISPATCH_DEVICE_FUNC(DestroyDevice);
    SPL_DISPATCH_DEVICE_FUNC(DestroyPipelineCache);
    SPL_DISPATCH_DEVICE_FUNC(CreateComputePipelines);
    SPL_DISPATCH_DEVICE_FUNC(CreateGraphicsPipelines);
    SPL_DISPATCH_DEVICE_FUNC(CreatePipelineCache);
    SPL_DISPATCH_DEVICE_FUNC(GetPipelineCacheData);
    // Get the next layer's instance of the device functions we will use. We do
    // not call these Vulkan functions directly to avoid re-entering the Vulkan
    // loader and confusing it.
    SPL_DISPATCH_DEVICE_FUNC(MergePipelineCaches);

    return dispatch_table;
  };

  performancelayers::CacheSideloadLayerData* layer_data = GetLayerData();
  const VkResult create_device_result = layer_data->CreateDevice(
      physical_device, create_info, allocator, device, build_dispatch_table);
  if (create_device_result == VK_SUCCESS) {
    assert(*device && "Device not created?");
    if (auto cache_blob_or_none = layer_data->ReadImplicitCacheFile()) {
      auto implicit_cache_handle = layer_data->CreateImplicitDeviceCache(
          *device, allocator, cache_blob_or_none->GetBuffer());
      (void)implicit_cache_handle;
    }
  }

  return create_device_result;
}

// Override for vkEnumerateInstanceLayerProperties.
SPL_CACHE_SIDELOAD_LAYER_FUNC(VkResult, EnumerateInstanceLayerProperties,
                              (uint32_t * property_count,
                               VkLayerProperties* properties)) {
  if (property_count) *property_count = 1;

  if (properties) {
    strncpy(properties->layerName, kLayerName, sizeof(properties->layerName));
    strncpy(properties->description, kLayerDescription,
            sizeof(properties->description));
    properties->implementationVersion = kCacheSideloadLayerVersion;
    properties->specVersion = VK_API_VERSION_1_0;
  }

  return VK_SUCCESS;
}

// Override for vkEnumerateDeviceLayerProperties.
SPL_CACHE_SIDELOAD_LAYER_FUNC(VkResult, EnumerateDeviceLayerProperties,
                              (VkPhysicalDevice /* physical_device */,
                               uint32_t* property_count,
                               VkLayerProperties* properties)) {
  return CacheSideloadLayer_EnumerateInstanceLayerProperties(property_count,
                                                             properties);
}

}  // namespace

// The *GetProcAddr functions are the entry points to the layers.
// They return a function pointer for the instance requested by |name|.
// We return the functions defined in this layer for those we want to override.
// Otherwise we call the *GetProcAddr function for the next layer to get the
// function to be called.

SPL_LAYER_ENTRY_POINT SPL_CACHE_SIDELOAD_LAYER_FUNC(PFN_vkVoidFunction,
                                                    GetDeviceProcAddr,
                                                    (VkDevice device,
                                                     const char* name)) {
  if (auto func =
          performancelayers::FunctionInterceptor::GetInterceptedOrNull(name)) {
    return func;
  }

  performancelayers::CacheSideloadLayerData* layer_data = GetLayerData();

  PFN_vkGetDeviceProcAddr next_get_proc_addr =
      layer_data->GetNextDeviceProcAddr(
          device, &VkLayerDispatchTable::GetDeviceProcAddr);
  assert(next_get_proc_addr);
  return next_get_proc_addr(device, name);
}

SPL_LAYER_ENTRY_POINT SPL_CACHE_SIDELOAD_LAYER_FUNC(PFN_vkVoidFunction,
                                                    GetInstanceProcAddr,
                                                    (VkInstance instance,
                                                     const char* name)) {
  if (auto func =
          performancelayers::FunctionInterceptor::GetInterceptedOrNull(name)) {
    return func;
  }

  performancelayers::CacheSideloadLayerData* layer_data = GetLayerData();

  PFN_vkGetInstanceProcAddr next_get_proc_addr =
      layer_data->GetNextInstanceProcAddr(
          instance, &VkLayerInstanceDispatchTable::GetInstanceProcAddr);
  assert(next_get_proc_addr);
  return next_get_proc_addr(instance, name);
}
