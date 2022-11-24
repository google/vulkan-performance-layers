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

#include "layer/support/layer_utils.h"

#include <cassert>

namespace performancelayers {
TimestampClock::time_point GetTimestamp() { return TimestampClock::now(); }

DurationClock::time_point Now() { return DurationClock::now(); }

FunctionInterceptor::FunctionInterceptor(
    InterceptedVulkanFunc intercepted_function) {
  FunctionNameToPtr& registered_functions = GetInterceptedFunctions();
  assert(registered_functions.count(
             intercepted_function.vulkan_function_name) == 0 &&
         "Already registered");
  registered_functions[intercepted_function.vulkan_function_name] =
      intercepted_function.layer_function;
}

PFN_vkVoidFunction FunctionInterceptor::GetInterceptedOrNull(
    std::string_view vk_function_name) {
  assert(!vk_function_name.empty());
  assert(vk_function_name.find("vk") == 0 &&
         "Vulkan function names must start with 'vk'.");
  FunctionNameToPtr& registered_functions = GetInterceptedFunctions();
  if (auto it = registered_functions.find(vk_function_name);
      it != registered_functions.end())
    return it->second;
  return nullptr;
}

FunctionInterceptor::FunctionNameToPtr&
FunctionInterceptor::GetInterceptedFunctions() {
  static auto* registered_functions =
      new FunctionInterceptor::FunctionNameToPtr();
  assert(registered_functions);
  return *registered_functions;
}

}  // namespace performancelayers
