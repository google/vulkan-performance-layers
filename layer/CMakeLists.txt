# Copyright 2022 Google LLC
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

# Common utilities
add_subdirectory(support)

set(GVPL_LIB_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/layer")
set(GVPL_MANIFEST_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/manifest")

# Installs layer files for the given layer target name.
function(gvpl_install_layer TARGET_NAME)
  install(TARGETS ${TARGET_NAME}
          DESTINATION ${GVPL_LIB_INSTALL_DIR})

  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
          DESTINATION ${GVPL_MANIFEST_INSTALL_DIR}
          FILES_MATCHING PATTERN "*.json")
endfunction()

# Defines a new performance layer shared library and configures basic target
# properties. Optional arguments are passed as target sources.
function(gvpl_define_layer LAYER_TARGET_NAME)
  add_library(${LAYER_TARGET_NAME} SHARED)
  target_sources(${LAYER_TARGET_NAME} PRIVATE ${ARGN})
  target_link_libraries(${LAYER_TARGET_NAME} PRIVATE performance_layers_support_lib)
  gvpl_install_layer(${LAYER_TARGET_NAME})
endfunction()

# Layers
add_subdirectory(cache_sideload)
add_subdirectory(compile_time)
add_subdirectory(frame_time)
add_subdirectory(memory_usage)
add_subdirectory(runtime)

# Tests
add_subdirectory(unittest)
