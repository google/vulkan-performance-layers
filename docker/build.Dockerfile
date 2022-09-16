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

FROM ubuntu:20.04

ARG CONFIG
ARG COMPILER
ARG GENERATOR

# Install required packages.
RUN export DEBIAN_FRONTEND=noninteractive && export TZ=America/New_York \
    && apt-get update \
    && apt-get install -yqq --no-install-recommends \
       build-essential pkg-config ninja-build \
       gcc g++ binutils-gold \
       llvm-11 clang-11 clang-tidy-12 libclang-common-11-dev lld-11 \
       python python3 python3-distutils python3-pip \
       libssl-dev libx11-dev libxcb1-dev x11proto-dri2-dev libxcb-dri3-dev \
       libxcb-dri2-0-dev lib32z1-dev libxcb-present-dev libxcb-xinerama0 libxshmfence-dev libxrandr-dev \
       libwayland-dev \
       git curl wget openssh-client \
       gpg gpg-agent \
    && wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc | apt-key add - \
    && wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.3.216-bionic.list https://packages.lunarg.com/vulkan/1.3.216/lunarg-vulkan-1.3.216-bionic.list \
    && apt-get update \
    && apt-get install -yqq --no-install-recommends \
    vulkan-sdk x11-xserver-utils libvulkan-dev libvulkan1 xvfb mesa-vulkan-drivers \
    && rm -rf /var/lib/apt/lists/* \
    && python3 -m pip install --no-cache-dir --upgrade pip \
    && python3 -m pip install --no-cache-dir --upgrade cmake \
    && for tool in clang clang++ llvm-cov llvm-profdata llvm-symbolizer lld ld.lld ; do \
         update-alternatives --install /usr/bin/"$tool" "$tool" /usr/bin/"$tool"-11 10 ; \
        done \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-12 10 \
    && update-alternatives --install /usr/bin/ld ld /usr/bin/ld.gold 10

COPY . /performance-layers

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

# Build performance layers.
WORKDIR /performance-layers/build
RUN  CXX_COMPILER="g++" \
     && if [ "$COMPILER" = "clang" ] ; then \
          CXX_COMPILER="clang++" ; \
        fi \
     && cmake .. \
      -G "$GENERATOR" \
      -DCMAKE_C_COMPILER="$COMPILER" \
      -DCMAKE_CXX_COMPILER="$CXX_COMPILER" \
      -DCMAKE_BUILD_TYPE="$CONFIG" \
      -DCMAKE_INSTALL_PREFIX=run \
      -DVULKAN_HEADERS_INSTALL_DIR=/dependencies/vulkan-headers-build/run \
      -DVULKAN_LOADER_GENERATED_DIR=/dependencies/Vulkan-Loader/loader/generated \
    && cmake --build . \
    && cmake --build . --target check \
    && cmake --build . --target install

# Enable perfomance layers and test with `vkcube`
RUN export LD_LIBRARY_PATH=/performance-layers/build:$LD_LIBRARY_PATH \ 
    && export VK_INSTANCE_LAYERS=VK_LAYER_STADIA_pipeline_compile_time \
    && export VK_INSTANCE_LAYERS=$VK_INSTANCE_LAYERS:VK_LAYER_STADIA_pipeline_runtime \
    && export VK_INSTANCE_LAYERS=$VK_INSTANCE_LAYERS:VK_LAYER_STADIA_pipeline_cache_sideload \
    && export VK_INSTANCE_LAYERS=$VK_INSTANCE_LAYERS:VK_LAYER_STADIA_pipeline_memory_usage \
    && export VK_INSTANCE_LAYERS=$VK_INSTANCE_LAYERS:VK_LAYER_STADIA_pipeline_frame_time \
    && export VK_LAYER_PATH=/performance-layers/layer \
    && xvfb-run vkcube --c 1000 && echo Done!