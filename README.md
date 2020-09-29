# Vulkan Performance Layers

This project contains three Vulkan layers:
1. Compile time layer for measuring pipeline compilation times. The output log file location can be set with the `VK_COMPILE_TIME_LOG` environment variable.
2. Runtime layer for measuring pipeline execution times. The output log file location can be set with the `VK_RUNTIME_LOG` environment variable.
3. Frame time layer for measuring time between calls to vkQueuePresentKHR, in nanoseconds. This layer can also terminate the parent Vulkan application after a given number of frames, controlled by the `VK_FRAME_TIME_EXIT_AFTER_FRAME` environment variable. The output log file location can be set with the `VK_FRAME_TIME_LOG` environment variable. Benchmark start detection is controlled by the `VK_FRAME_TIME_BENCHMARK_WATCH_FILE` (which file to incrementally scan) and `VK_FRAME_TIME_BENCHMARK_START_STRING` (string that denotes benchmark start) environment variables.

The results are saved as CSV files. Setting the `VK_PERFORMANCE_LAYERS_EVENT_LOG_FILE` environment variable makes all layers append their events (with timestamps) to a single file.

The layers are considered experimental.
We welcome contributions and suggestions for improvements; see [docs/contributing.md](docs/contributing.md).

## Analysis Scripts

The project comes with a set of simple log analysis scripts:
1. [analyze_frametimes.py](scripts/analyze_analyze_frametimes.py) -- processes frame time layer logs. Prints summarized results, outputs frames per second (FPS) as CSV files, and plots frame time distributions.
2. [plot_timeline.py](scripts/plot_timeline.py) -- processes event log files. Plots frames per second (FPS) and pipeline creation times. Sample output:
    ![Timeline View](sample_output/timeline.svg)

You can find more details in the descriptions included in each script file.

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

See [docker/build.Dockerfile](docker/build.Dockerfile) for detailed Ubuntu build instructions.

## Disclaimer

This is not an officially supported Google product. Support and/or new releases may be limited.

