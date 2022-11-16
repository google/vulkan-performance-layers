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


#
# Dockerfile for Stadia Performance Layers Continuous Integration.
# Sample invocation:
#    docker build . --file docker/build.Dockerfile               \
#                   --build-arg CONFIG=Release                   \
#                   --build-arg COMPILER=clang                   \
#                   --build-arg GENERATOR=Ninja

FROM ubuntu:22.04

SHELL ["/bin/bash", "-c"]

ARG CONFIG
ARG COMPILER
ARG GENERATOR

# Install required packages.
RUN export DEBIAN_FRONTEND=noninteractive && export TZ=America/New_York \
    && apt-get update \
    && apt-get install -yqq --no-install-recommends \
       build-essential cmake ninja-build \
       gcc g++ python3 \
       llvm-14 llvm-14-dev clang-14 clang-tidy-14 lld-14 \
       xvfb mesa-vulkan-drivers \
       git wget openssh-client gpg gpg-agent ca-certificates \
    && wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc | apt-key add - \
    && wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.3.216-bionic.list https://packages.lunarg.com/vulkan/1.3.216/lunarg-vulkan-1.3.216-bionic.list \
    && apt-get update \
    && apt-get install -yqq --no-install-recommends \
       vulkan-sdk libvulkan-dev libvulkan1 \
    && rm -rf /var/lib/apt/lists/* \
    && for tool in clang clang++ clang-tidy llvm-cov llvm-profdata llvm-symbolizer lld ld.lld FileCheck ; do \
         update-alternatives --install /usr/bin/"$tool" "$tool" /usr/bin/"$tool"-14 10 ; \
        done \
    && update-alternatives --install /usr/bin/ld ld /usr/bin/ld.lld 10

# Get all dependencies.
WORKDIR /dependencies
RUN git clone https://github.com/KhronosGroup/Vulkan-Headers.git \
    && git clone https://github.com/KhronosGroup/Vulkan-Loader.git \
    && mkdir vulkan-headers-build \
    && cd /dependencies/vulkan-headers-build \
    && cmake ../Vulkan-Headers \
         -G "$GENERATOR" \
         -DCMAKE_BUILD_TYPE="$CONFIG" \
         -DCMAKE_INSTALL_PREFIX=run \
    && cmake --build . \
    && cmake --build . --target install

COPY . /performance-layers

# Build performance layers.
WORKDIR /build
RUN  CXX_COMPILER="g++" \
     && if [ "$COMPILER" = "clang" ] ; then \
          CXX_COMPILER="clang++" ; \
        fi \
     && cmake /performance-layers \
      -G "$GENERATOR" \
      -DCMAKE_C_COMPILER="$COMPILER" \
      -DCMAKE_CXX_COMPILER="$CXX_COMPILER" \
      -DCMAKE_BUILD_TYPE="$CONFIG" \
      -DCMAKE_INSTALL_PREFIX=run \
      -DCMAKE_CXX_FLAGS="-Werror" \
      -DVULKAN_HEADERS_INSTALL_DIR=/dependencies/vulkan-headers-build/run \
      -DVULKAN_LOADER_GENERATED_DIR=/dependencies/Vulkan-Loader/loader/generated \
    && cmake --build . \
    && cmake --build . --target check \
    && cmake --build . --target install

# Enable and test the performance layers.
RUN timeout 20s /performance-layers/docker/test_performance_layers.sh /build/run
