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

#ifndef STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_RUNTIME_LAYER_DATA_H_
#define STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_RUNTIME_LAYER_DATA_H_

#include <vector>

#include "event_logging.h"
#include "layer_data.h"
#include "layer_utils.h"

namespace performancelayers {
class RuntimeEvent : public Event {
 public:
  RuntimeEvent(const char* name, const std::vector<int64_t>& hash_values,
               Duration runtime, int64_t frag_shader_invocation,
               int64_t comp_shader_invocattion)
      : Event(name, LogLevel::kHigh),
        hash_values_({"pipeline", hash_values}),
        runtime_({"runtime", runtime}),
        frag_shader_invocations_(
            {"fragment_shader_invocations", frag_shader_invocation}),
        comp_shader_invocations_(
            {"compute_shader_invocations", comp_shader_invocattion}) {
    InitAttributes({&hash_values_, &runtime_, &frag_shader_invocations_,
                    &comp_shader_invocations_});
  }

 private:
  VectorInt64Attr hash_values_;
  DurationAttr runtime_;
  Int64Attr frag_shader_invocations_;
  Int64Attr comp_shader_invocations_;
};

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
    Event event("runtime_layer_init");
    LogEvent(&event);
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

  // Removes queries that contain the freed command buffers.
  void RemoveQueries(const VkCommandBuffer* cmd_buff_list,
                     uint32_t cmd_buff_count);

 private:
  mutable absl::Mutex cmd_buf_to_pipeline_lock_;
  // The map from a command buffer to its bound pipeline.
  absl::flat_hash_map<VkCommandBuffer, VkPipeline> cmd_buf_to_pipeline_
      ABSL_GUARDED_BY(cmd_buf_to_pipeline_lock_);

  mutable absl::Mutex timestamp_queries_lock_;
  // The set of query pools used for the time stamps and their corresponding
  // pipelines.
  std::vector<QueryInfo> timestamp_queries_
      ABSL_GUARDED_BY(timestamp_queries_lock_);
};

}  // namespace performancelayers
#endif  // STADIA_OPEN_SOURCE_PERFORMANCE_LAYERS_RUNTIME_LAYER_DATA_H_
