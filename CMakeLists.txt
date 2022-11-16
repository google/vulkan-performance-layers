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


cmake_minimum_required(VERSION 3.10)

project(stadia-performance-layers CXX)

set(CMAKE_CXX_STANDARD 17)

# Disable C++ exceptions.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")

# Enable warnings.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

if ( CMAKE_CXX_COMPILER_ID STREQUAL "Clang" )
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wthread-safety")
endif()

set(FILESYSTEM_LIB_NAME stdc++fs)

# Abseil requires PIC.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

set(GVPL_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Add external dependencies.
add_subdirectory(third_party)
set(GVPL_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

if(DEFINED VULKAN_HEADERS_INSTALL_DIR)
  if(NOT IS_ABSOLUTE "${VULKAN_HEADERS_INSTALL_DIR}")
    message(FATAL_ERROR "VULKAN_HEADERS_INSTALL_DIR must be an absolute path")
  endif()
  find_path(VulkanHeaders_INCLUDE_DIR
      NAMES vulkan/vk_layer.h
      PATHS "${VULKAN_HEADERS_INSTALL_DIR}/include"
      NO_DEFAULT_PATH)
  if(VulkanHeaders_INCLUDE_DIR MATCHES ".*_INCLUDE_DIR-NOTFOUND$")
    message(FATAL_ERROR "Vulkan Headers not found. Set VULKAN_HEADERS_INSTALL_DIR to the Vulkan-Headers installation directory.")
  endif()
else()
  message(FATAL_ERROR "VULKAN_HEADERS_INSTALL_DIR must be specified!")
endif()
message(STATUS "VulkanHeaders: ${VulkanHeaders_INCLUDE_DIR}")

if(DEFINED VULKAN_LOADER_GENERATED_DIR)
  if(NOT IS_ABSOLUTE "${VULKAN_LOADER_GENERATED_DIR}")
    message(FATAL_ERROR "VULKAN_LOADER_GENERATED_DIR must be an absolute path")
  endif()
  find_path(VulkanLoaderGenerated_INCLUDE_DIR
      NAMES vk_layer_dispatch_table.h
      PATHS "${VULKAN_LOADER_GENERATED_DIR}"
      NO_DEFAULT_PATH)
  if(VulkanLoaderGenerated_INCLUDE_DIR MATCHES ".*_INCLUDE_DIR-NOTFOUND$")
    message(FATAL_ERROR "Vulkan Loader sources not found. Set VULKAN_LOADER_GENERATED_DIR to the 'Vulkan-Loader/loader/generated' directory.")
  endif()
else()
  message(FATAL_ERROR "VULKAN_LOADER_GENERATED_DIR must be specified!")
endif()
message(STATUS "VulkanLoaderGenerated: ${VulkanLoaderGenerated_INCLUDE_DIR}")

# Support testing with `ctest`. In this project, all unit tests can also be
# executed with `cmake --build . --target check`, or `ninja check` if you
# use `ninja`.
enable_testing()

# Add layers.
add_subdirectory(layer)
