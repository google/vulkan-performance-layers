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

add_executable(layer_support_tests
    common_log_tests.cc
    csv_log_tests.cc
    event_log_tests.cc
    input_buffer_tests.cc
    log_output_tests.cc
    log_scanner_tests.cc
)

target_include_directories(layer_support_tests PRIVATE
    ${GVPL_THIRD_PARTY_DIR}/googletest/googletest/include
)

target_link_libraries(layer_support_tests PRIVATE
    performance_layers_support_lib
    gtest
    gtest_main
    gmock
    ${FILESYSTEM_LIB_NAME}
)

add_test(layer_support_tests layer_support_tests)
add_custom_target(check COMMAND layer_support_tests)
add_dependencies(check layer_support_tests)
