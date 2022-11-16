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

#include "runtime_layer_data.h"

#include <cstdint>
#include <iomanip>
#include <vector>

#include "layer/support/debug_logging.h"
#include "layer/support/event_logging.h"
#include "layer/support/layer_utils.h"

namespace performancelayers {

bool RuntimeLayerData::GetNewQueryInfo(VkCommandBuffer cmd_buf,
                                       VkQueryPool* timestamp_query_pool,
                                       VkQueryPool* stat_query_pool) {
  VkQueryPoolCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  createInfo.pNext = nullptr;
  createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
  createInfo.queryCount = 2;

  auto create_query_pool_function =
      GetNextDeviceProcAddr(cmd_buf, &VkLayerDispatchTable::CreateQueryPool);

  VkDevice device = GetDevice(DeviceKey(cmd_buf));
  auto ret = (create_query_pool_function)(device, &createInfo, nullptr,
                                          timestamp_query_pool);
  if (ret != VK_SUCCESS) {
    return false;
  }

  createInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
  createInfo.queryCount = 1;
  createInfo.pipelineStatistics =
      VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
      VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
  ret = (create_query_pool_function)(device, &createInfo, nullptr,
                                     stat_query_pool);
  if (ret != VK_SUCCESS) {
    auto destroy_query_pool_function =
        GetNextDeviceProcAddr(device, &VkLayerDispatchTable::DestroyQueryPool);
    (destroy_query_pool_function)(device, *timestamp_query_pool, nullptr);
    return false;
  }

  absl::MutexLock lock(&timestamp_queries_lock_);
  timestamp_queries_.push_back(
      {*timestamp_query_pool, *stat_query_pool, cmd_buf, GetPipeline(cmd_buf)});

  return true;
}

void RuntimeLayerData::LogAndRemoveQueryPools() {
  absl::MutexLock lock(&timestamp_queries_lock_);
  for (auto info = timestamp_queries_.begin();
       info != timestamp_queries_.end();) {
    VkCommandBuffer cmd_buf = info->command_buffer;
    auto query_pool_results_function = GetNextDeviceProcAddr(
        cmd_buf, &VkLayerDispatchTable::GetQueryPoolResults);
    auto destroy_query_pool_function =
        GetNextDeviceProcAddr(cmd_buf, &VkLayerDispatchTable::DestroyQueryPool);

    constexpr uint64_t kInvalidValue = ~uint64_t(0);
    uint64_t query_data[2] = {kInvalidValue, kInvalidValue};
    VkDevice device = GetDevice(DeviceKey(cmd_buf));
    VkResult result = (query_pool_results_function)(
        device, info->timestamp_pool,
        /*firstQuery=*/0, /*queryCount=*/2,
        /*dataSize=*/sizeof(query_data), /*pData=*/&query_data,
        /*stride=*/sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

    // FIXME: We should check timestampValidBits and use it to mask the results
    // here. The current ICD always produces either 64 or 0 bit of results, so
    // not masking at all seems fine for now.
    const uint64_t& timestamp0 = query_data[0];
    const uint64_t& timestamp1 = query_data[1];

    const bool result_available =
        (result == VK_SUCCESS && timestamp0 != 0 && timestamp1 != 0);
    bool discard_result = false;
    constexpr uint64_t kUnreasonablyLongRuntime = 10ull * 1000 * 1000 * 1000;

    if (result != VK_SUCCESS && result != VK_NOT_READY) {
      // This query failed for some reason. Remove it from the list so we do
      // not keep checking it.  Write an error to stderr.
      SPL_LOG(ERROR) << "Timestamp query failed for "
                     << PipelineHashToString(GetPipelineHash(info->pipeline))
                     << " with error " << result;
      discard_result = true;
    } else if (result_available &&
               (timestamp0 == kInvalidValue || timestamp1 == kInvalidValue ||
                timestamp1 <= timestamp0 ||
                (timestamp1 - timestamp0 > kUnreasonablyLongRuntime))) {
      // This query did not produce valid timestamps for some reason. Remove
      // it from the list so we do not keep checking it.
      SPL_LOG(ERROR) << "Timestamp query failed for "
                     << PipelineHashToString(GetPipelineHash(info->pipeline))
                     << " producing invalid timestamps: t0=" << timestamp0
                     << ", t1=" << timestamp1;
      discard_result = true;
    }

    if (!result_available && !discard_result) {
      ++info;
      continue;
    }

    uint64_t invocations[2] = {};
    result = (query_pool_results_function)(
        device, info->stat_pool,
        /*firstQuery=*/0, /*queryCount=*/1,
        /*dataSize=*/sizeof(invocations), /*pData=*/invocations,
        /*stride=*/sizeof(invocations[0]),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    if (result != VK_SUCCESS) {
      discard_result = true;
    }

    if (!discard_result) {
      // FIXME: We should adjust the elapsed units to account for the current
      // GPU frequency. Calling vkGetPhysicalDeviceProperties here causes the
      // driver to crash, however.
      HashVector pipeline = GetPipelineHash(info->pipeline);
      std::vector<int64_t> hashes(pipeline.begin(), pipeline.end());
      RuntimeEvent event("pipeline_execution", hashes,
                         Duration::FromNanoseconds(timestamp1 - timestamp0),
                         invocations[0], invocations[1]);
      LogEvent(&event);
    }

    (destroy_query_pool_function)(device, info->timestamp_pool, nullptr);
    (destroy_query_pool_function)(device, info->stat_pool, nullptr);
    // Remove the current query from the queue.
    using std::swap;
    swap(*info, timestamp_queries_.back());
    timestamp_queries_.pop_back();
  }
}

void RuntimeLayerData::RemoveQueries(const VkCommandBuffer* cmd_buff_list,
                                     uint32_t cmd_buff_count) {
  absl::MutexLock lock(&timestamp_queries_lock_);
  for (size_t i = 0; i < cmd_buff_count; ++i) {
    for (auto it = timestamp_queries_.begin();
         it != timestamp_queries_.end();) {
      if (it->command_buffer == cmd_buff_list[i]) {
        it = timestamp_queries_.erase(it);
      } else {
        ++it;
      }
    }
  }
}

}  // namespace performancelayers
