# Copyright 2020-2022 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_library(performance_layers_support_lib INTERFACE)

target_sources(performance_layers_support_lib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/common_logging.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/csv_logging.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/debug_logging.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/input_buffer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/layer_data.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/layer_utils.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/log_output.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/log_scanner.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/trace_event_logging.cc
)

target_include_directories(performance_layers_support_lib INTERFACE
    ${GVPL_ROOT_DIR}
    ${VulkanHeaders_INCLUDE_DIR}
    ${VulkanLoaderGenerated_INCLUDE_DIR}
    ${GVPL_THIRD_PARTY_DIR}/farmhash/src
)

target_link_libraries(performance_layers_support_lib INTERFACE
    absl::flat_hash_map
    absl::flat_hash_set
    absl::inlined_vector
    absl::status
    absl::statusor
    absl::strings
    absl::str_format
    absl::synchronization
    farmhash
)
