# Copyright 2020 Google LLC
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

FROM ubuntu:18.04

ARG CONFIG
ARG COMPILER
ARG GENERATOR

# Install required packages.
RUN apt-get update \
    && apt-get install -yqq \
    	build-essential cmake gcc g++ clang-8 ninja-build binutils-gold \
    	python python-distutils-extra python3 python3-distutils \
    	git vim-tiny \
      libglm-dev libxcb-dri3-0 libxcb-present0 libpciaccess0 \
			libpng-dev libxcb-keysyms1-dev libxcb-dri3-dev libx11-dev \
      libmirclient-dev libwayland-dev libxrandr-dev libxcb-ewmh-dev \
    && update-alternatives --install /usr/bin/ld ld /usr/bin/ld.gold 10 \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-8 10 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-8 10

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
RUN cmake .. \
      -G "$GENERATOR" \
      -DCMAKE_C_COMPILER="$COMPILER" \
      -DCMAKE_CXX_COMPILER="$COMPILER" \
      -DCMAKE_BUILD_TYPE="$CONFIG" \
      -DCMAKE_INSTALL_PREFIX=run \
      -DVULKAN_HEADERS_INSTALL_DIR=/dependencies/vulkan-headers-build/run \
      -DVULKAN_LOADER_GENERATED_DIR=/dependencies/Vulkan-Loader/loader/generated \
    && cmake --build . \
    && cmake --build . --target install

