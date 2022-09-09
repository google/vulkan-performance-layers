# Vulkan Performance Layers

This project contains 5 Vulkan layers:
1. Compile time layer for measuring pipeline compilation times. The output log file location can be set with the `VK_COMPILE_TIME_LOG` environment variable.
2. Runtime layer for measuring pipeline execution times. The output log file location can be set with the `VK_RUNTIME_LOG` environment variable.
3. Frame time layer for measuring time between calls to vkQueuePresentKHR, in nanoseconds. This layer can also terminate the parent Vulkan application after a given number of frames, controlled by the `VK_FRAME_TIME_EXIT_AFTER_FRAME` environment variable. The output log file location can be set with the `VK_FRAME_TIME_LOG` environment variable. Benchmark start detection is controlled by the `VK_FRAME_TIME_BENCHMARK_WATCH_FILE` (which file to incrementally scan) and `VK_FRAME_TIME_BENCHMARK_START_STRING` (string that denotes benchmark start) environment variables.
4. Pipeline cache sideloading layer for supplying pipeline caches to applications that either do not use pipeline caches, or do not initialize them with the intended initial data. The pipeline cache file to load can be specified by setting the `VK_PIPELINE_CACHE_SIDELOAD_FILE` environment variable. The layer creates an implicit pipeline cache object for each device, initialized with the specified file contents, which then gets merged into application pipeline caches (if any), and makes sure that a valid pipeline cache handle is passed to every pipeline creation. This layer does not produce `.csv` log files.
5. Device memory usage layer. This layer tracks memory explicitly allocated by the application (VkAllocateMemory), usually for images and buffers. For each frame, current allocation and maximum allocation is written to the log file. The output log file location can be set with the `VK_MEMORY_USAGE_LOG` environment variable.

The results are saved as CSV files. Setting the `VK_PERFORMANCE_LAYERS_EVENT_LOG_FILE` environment variable makes all layers append their events (with timestamps) to a single file.

The layers are considered experimental.
We welcome contributions and suggestions for improvements; see [docs/contributing.md](docs/contributing.md).

## Analysis Scripts

The project comes with a set of simple log analysis scripts:
1. [analyze_frametimes.py](scripts/analyze_frametimes.py) -- processes frame time layer logs. Prints summarized results, outputs frames per second (FPS) as CSV files, and plots frame time distributions.
2. [plot_timeline.py](scripts/plot_timeline.py) -- processes event log files. Plots frames per second (FPS) and pipeline creation times. Sample output:
    ![Timeline View](sample_output/timeline.svg)

You can find more details in the descriptions included in each script file.

## Build Instructions

Sample build instructions:

```shell
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

`CMAKE_BUILD_TYPE` options are: `Release, Debug, RelWithDebInfo, MinSizeRel`

NOTE: `VULKAN_HEADERS_INSTALL_DIR` and `VULKAN_LOADER_GENERATED_DIR` must be absolute paths.

NOTE: `VULKAN_HEADERS_INSTALL_DIR` must be set to the installation directory of Vulkan-Headers. For example, if you build directory is `VULKAN_HEADERS_BUILD` and your build commands were:

   ```shell
   cmake <PATH_TO_VULKAN_HEADERS> -GNinja -DCMAKE_INSTALL_PREFIX=run
   ninja
   ninja install
   ```

   you should set `VULKAN_HEADERS_INSTALL_DIR` to `VULKAN_HEADERS_BUILD/run`.

NOTE: `VULKAN_LOADER_GENERATED_DIR` should be the directory that contains `vk_layer_dispatch_table.h`. For example, if you cloned Vulkan-Loader to `PATH_TO_VULKAN_LOADER`, you should set `VULKAN_LOADER_GENERATED_DIR` to `PATH_TO_VULKAN_LOADER/loader/generated`.

See [docker/build.Dockerfile](docker/build.Dockerfile) for detailed Ubuntu build instructions.

## Enabling the layers:
To enable the performance layers, set these environent variables:

`$LD_LIBRARY_PATH`: Need to append the directory containing the .so files of performance layers to the `$LD_LIBRARY_PATH`.
```
export LD_LIBRARY_PATH=<path-with-layers.so>:$LD_LIBRARY_PATH
```
`$VK_INSTANCE_LAYERS`: Specify which layers from the following set to enable.
1. VK_LAYER_STADIA_pipeline_compile_time
2. VK_LAYER_STADIA_pipeline_runtime
3. VK_LAYER_STADIA_pipeline_cache_sideload
4. VK_LAYER_STADIA_pipeline_memory_usage
5. VK_LAYER_STADIA_pipeline_frame_time

To enable multiple layers, you can separated them using colon. The following command enables compile time and runtime layers. 
```
export VK_INSTANCE_LAYERS=VK_LAYER_STADIA_pipeline_compile_time:VK_LAYER_STADIA_pipeline_runtime
```

`VK_LAYER_PATH`: Path to the directory containing the json files with the name of the layers. 
``` 
export VK_LAYER_PATH=<path-with-layer-json>
```
`VK_PERFORMANCE_LAYERS_EVENT_LOG_FILE`: Path to the file in which the logs will be written. 
```
export VK_PERFORMANCE_LAYERS_EVENT_LOG_FILE=<log-file-path>
```

To check whether the layers are enabled, you can run a sample vulkan application (like `vkcube`) and check the log file specified in the `$VK_PERFORMANCE_LAYERS_EVENT_LOG_FILE`.

## Useful links
[This links](https://vulkan.lunarg.com/doc/view/1.3.211.0/linux/layer_configuration.html) is also useful regarding the layer configuration.
## Disclaimer

This is not an officially supported Google product. Support and/or new releases may be limited.
