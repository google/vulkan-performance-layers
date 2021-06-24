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

#ifndef STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_RUNTIME_LAYER_DATA_H_
#define STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_RUNTIME_LAYER_DATA_H_

#include <vector>

#include "layer_data.h"

namespace performancelayers {

// A class that contains all of the data that is needed for the functions
// that this layer will override.
//
// The filename for the log file will be retrieved from the environment variable
// "VK_RUNTIME_LOG".  If it is unset, then stderr will be used as the
// log file.
class RuntimeLayerData : public LayerData {
 private:
  struct QueryInfo {
    VkQueryPool timestamp_pool;
    VkQueryPool stat_pool;
    VkCommandBuffer command_buffer;
    VkPipeline pipeline;
  };

 public:
  explicit RuntimeLayerData(char* log_filename)
      : LayerData(log_filename,
                  "Pipeline,Run Time (ns),Fragment Shader Invocations,Compute "
                  "Shader Invocations") {
    LogEventOnly("runtime_layer_init");
  }

  // Records the device that owns |cmd_buffer|.
  void SetDevice(VkCommandBuffer cmd_buffer, VkDevice device) {
    absl::MutexLock lock(&cmd_buf_to_device_lock_);
    cmd_buf_to_device_.insert_or_assign(cmd_buffer, device);
  }

  // Returns the device that owns |cmd_buffer|.
  VkDevice GetDevice(VkCommandBuffer cmd_buffer) const {
    absl::MutexLock lock(&cmd_buf_to_device_lock_);
    assert(cmd_buf_to_device_.count(cmd_buffer) != 0);
    return cmd_buf_to_device_.at(cmd_buffer);
  }

  void GetDeviceQueue(VkDevice device, uint32_t queue_family_index,
                      uint32_t queue_index, VkQueue* queue) {
    queue_to_device_map_.GetDeviceQueue(this, device, queue_family_index,
                                        queue_index, queue);
  }

  void GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* queue_info,
                       VkQueue* queue) {
    queue_to_device_map_.GetDeviceQueue2(this, device, queue_info, queue);
  }

  VkDevice GetDevice(VkQueue queue) {
    return queue_to_device_map_.GetDevice(queue);
  }

  // Records |pipeline| as the latest pipeline that has been bound to
  // |cmd_buffer|.
  void BindPipeline(VkCommandBuffer cmd_buffer, VkPipeline pipeline) {
    absl::MutexLock lock(&cmd_buf_to_pipeline_lock_);
    cmd_buf_to_pipeline_.insert_or_assign(cmd_buffer, pipeline);
  }

  // Returns the latest pipeline that has been bound to |cmd_buffer|.
  VkPipeline GetPipeline(VkCommandBuffer cmd_buffer) const {
    absl::MutexLock lock(&cmd_buf_to_pipeline_lock_);
    assert(cmd_buf_to_pipeline_.count(cmd_buffer) != 0);
    return cmd_buf_to_pipeline_.at(cmd_buffer);
  }

  // Allocates and returns a new query pool to be used in the command buffer
  // |cmd_buf|.
  bool GetNewQueryInfo(VkCommandBuffer cmd_buf,
                       VkQueryPool* timestamp_query_pool,
                       VkQueryPool* stat_query_pool);

  // Allocates a new fence object, and returns a handle to it.
  VkFence GetNewFence(VkDevice device);

  void LinkFence(VkFence fence, const VkCommandBuffer* cmd_buffers,
                 uint32_t cmd_buffer_count);

  // Reads the time stamp in each query pool associated with |cmd_buf|, and logs
  // the time spent running each pipeline.
  void LogAndRemoveQueryPools();

 private:
  mutable absl::Mutex cmd_buf_to_device_lock_;
  // The map from a command buffer to the device that owns it.
  absl::flat_hash_map<VkCommandBuffer, VkDevice> cmd_buf_to_device_
      ABSL_GUARDED_BY(cmd_buf_to_device_lock_);

  mutable absl::Mutex cmd_buf_to_pipeline_lock_;
  // The map from a command buffer to the device that owns it.
  absl::flat_hash_map<VkCommandBuffer, VkPipeline> cmd_buf_to_pipeline_
      ABSL_GUARDED_BY(cmd_buf_to_pipeline_lock_);

  mutable absl::Mutex timestamp_queries_lock_;
  // The set of query pools used for the time stamps and their corresponding
  // pipelines.
  std::vector<QueryInfo> timestamp_queries_
      ABSL_GUARDED_BY(timestamp_queries_lock_);

  QueueToDeviceMap queue_to_device_map_;
};

}  // namespace performancelayers
#endif  // STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_RUNTIME_LAYER_DATA_H_
