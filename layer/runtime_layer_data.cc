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

#if defined(STADIA_PERFORMANCE_LAYERS_NO_GLOG)
#include <iostream>
#define LOG(KIND) std::cerr << "[" #KIND "]"
#else
#include "base/logging.h"
#endif

namespace performancelayers {

VkQueryPool RuntimeLayerData::GetNewTimeStampQueryPool(
    VkCommandBuffer cmd_buf) {
  VkQueryPoolCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  createInfo.pNext = nullptr;
  createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
  createInfo.queryCount = 2;

  VkDevice device = GetDevice(cmd_buf);
  VkQueryPool timestamp_query_pool;
  auto create_query_pool_function =
      GetNextDeviceProcAddr(device, &VkLayerDispatchTable::CreateQueryPool);
  auto reset_query_pool_function =
      GetNextDeviceProcAddr(device, &VkLayerDispatchTable::CmdResetQueryPool);

  (create_query_pool_function)(device, &createInfo, nullptr,
                               &timestamp_query_pool);
  (reset_query_pool_function)(cmd_buf, timestamp_query_pool, 0,
                              createInfo.queryCount);

  absl::MutexLock lock(&timestamp_queries_lock_);
  timestamp_queries_.push_back(
      {timestamp_query_pool, cmd_buf, GetPipeline(cmd_buf)});
  return timestamp_query_pool;
}

void RuntimeLayerData::LogAndRemoveQueryPools() {
  absl::MutexLock lock(&timestamp_queries_lock_);
  for (auto info = timestamp_queries_.begin();
       info != timestamp_queries_.end();) {
    VkCommandBuffer cmd_buf = info->command_buffer;
    VkDevice device = GetDevice(cmd_buf);
    auto query_pool_results_function = GetNextDeviceProcAddr(
        device, &VkLayerDispatchTable::GetQueryPoolResults);
    auto destroy_query_pool_function =
        GetNextDeviceProcAddr(device, &VkLayerDispatchTable::DestroyQueryPool);

    constexpr uint64_t kInvalidValue = ~uint64_t(0);
    uint64_t query_data[2] = {kInvalidValue, kInvalidValue};
    VkResult result = (query_pool_results_function)(
        device, info->query_pool,
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
      LOG(ERROR) << "Timestamp query failed for "
                 << PipelineHashToString(GetPipelineHash(info->pipeline))
                 << " with error " << result;
      discard_result = true;
    } else if (result_available &&
               (timestamp0 == kInvalidValue || timestamp1 == kInvalidValue ||
                timestamp1 <= timestamp0 ||
                (timestamp1 - timestamp0 > kUnreasonablyLongRuntime))) {
      // This query did not produce valid timestamps for some reason. Remove
      // it from the list so we do not keep checking it.
      LOG(ERROR) << "Timestamp query failed for "
                 << PipelineHashToString(GetPipelineHash(info->pipeline))
                 << " producing invalid timestamps: t0=" << timestamp0
                 << ", t1=" << timestamp1;
      discard_result = true;
    }

    if (!result_available && !discard_result) {
      ++info;
      continue;
    }

    if (!discard_result) {
      // FIXME: We should adjust the elapsed units to account for the current
      // GPU frequency. Calling vkGetPhysicalDeviceProperties here causes the
      // driver to crash, however.
      Log(GetPipelineHash(info->pipeline), timestamp1 - timestamp0);
    }

    (destroy_query_pool_function)(device, info->query_pool, nullptr);
    // Remove the current query from the queue.
    using std::swap;
    swap(*info, timestamp_queries_.back());
    timestamp_queries_.pop_back();
  }
}

}  // namespace performancelayers
