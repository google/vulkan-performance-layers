# Vulkan Performance Layers

This project contains two Vulkan layers:
1. Compile-time layer for measuring pipeline compilation times.
2. Runtime layer for measuring pipeline execution times.

The results are saved as CSV files.

The layers are considered experimental.
We welcome contributions and suggestions for improvements; see [docs/contributing.md](docs/contributing.md).

## Build Instructions

Sample build instructions:

```
# Checkout the submodules.
git submodule update --init

# Build and install performance layers.
mkdir -p <BUILD_DIR> ; cd <BUILD_DIR>
cmake .. \
      -GNinja \
      -DCMAKE_C_COMPILER=<COMPILER> \
      -DCMAKE_CXX_COMPILER=<COMPILER> \
      -DCMAKE_BUILD_TYPE=<CONFIGURATION> \
      -DCMAKE_INSTALL_PREFIX=run \
      -DVULKAN_HEADERS_INSTALL_DIR=<PATH_TO_VULKAN_HEADERS_INSTALL> \
      -DVULKAN_LOADER_GENERATED_DIR=<PATH_TO_VULKAN_LOADER>/loader/generated \
    && ninja \
    && ninja install
```

See docker/build.Dockerfile for detailed Ubuntu build instructions.

## Disclaimer

This is not an officially supported Google product. Support and/or new releases may be limited.

