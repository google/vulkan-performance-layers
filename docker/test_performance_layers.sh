#!/usr/bin/env bash
#
# Enables the performance layers by setting the evironment variables.
# Executes vkcube and makes sure it runs without fault when performance layers
# are enabled.
# Tests if each layer writes to its corresponding output file.

set -euo pipefail

readonly SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" > /dev/null 2>&1 && pwd )"
readonly PROJECT_ROOT_DIR="${SCRIPT_DIR}/.."

readonly INSTALL_DIR="$1"
readonly LAYER_DIR="${INSTALL_DIR}/layer"
readonly MANIFEST_DIR="${INSTALL_DIR}/manifest"

# Create a temp output directory for log files.
readonly OUTPUT_DIR="$(mktemp -d)"

declare -a output_files
output_files=("compile_time.csv" "run_time.csv" "memory_usage.csv"
              "frame_time.csv" "events.log")

#######################################
# Checks if the layers write data in their specified log files.
# If data does not exist in the file, writes an error message to stdout and
# exits.
# Globals:
#   OUTPUT_DIR
# Arguments:
#   layer's log file name
#######################################
check_layer_log() {
  local file_name="$1"

  if [[ $(wc -l <"${OUTPUT_DIR}"/"${file_name}") -lt 2 ]]; then
    echo "Error: ${file_name} has not been populated properly."
    exit 1
  fi
}

export LD_LIBRARY_PATH="${LAYER_DIR}":"${LD_LIBRARY_PATH-}"
export VK_INSTANCE_LAYERS=VK_LAYER_STADIA_pipeline_compile_time
export VK_INSTANCE_LAYERS=$VK_INSTANCE_LAYERS:VK_LAYER_STADIA_pipeline_runtime
export VK_INSTANCE_LAYERS=$VK_INSTANCE_LAYERS:VK_LAYER_STADIA_frame_time
export VK_INSTANCE_LAYERS=$VK_INSTANCE_LAYERS:VK_LAYER_STADIA_memory_usage
export VK_INSTANCE_LAYERS=$VK_INSTANCE_LAYERS:VK_LAYER_STADIA_pipeline_cache_sideload
export VK_LAYER_PATH="${MANIFEST_DIR}"
export VK_COMPILE_TIME_LOG="${OUTPUT_DIR}"/compile_time.csv
export VK_RUNTIME_LOG="${OUTPUT_DIR}"/run_time.csv
export VK_FRAME_TIME_LOG="${OUTPUT_DIR}"/frame_time.csv
export VK_MEMORY_USAGE_LOG="${OUTPUT_DIR}"/memory_usage.csv
export VK_PERFORMANCE_LAYERS_EVENT_LOG_FILE="${OUTPUT_DIR}"/events.log
export VK_PERFORMANCE_LAYERS_TRACE_EVENT_LOG_FILE="${OUTPUT_DIR}"/trace_events.log

# Test if an application can run with performance layers enabled.
xvfb-run vkcube --c 100
echo "vkcube finished successfully!"

# Check if each enabled layer produced its log file.
for file in "${output_files[@]}"; do
  check_layer_log "${file}"
done

# Check that log file's contents match what is expected.
FileCheck "${PROJECT_ROOT_DIR}/test/check_compile_time_log.txt" \
  --match-full-lines --input-file "${OUTPUT_DIR}"/compile_time.csv

FileCheck "${PROJECT_ROOT_DIR}/test/check_run_time_log.txt" \
  --match-full-lines --input-file "${OUTPUT_DIR}"/run_time.csv

FileCheck "${PROJECT_ROOT_DIR}/test/check_memory_usage_log.txt" \
  --match-full-lines --input-file "${OUTPUT_DIR}"/memory_usage.csv

FileCheck "${PROJECT_ROOT_DIR}/test/check_frame_time_log.txt" \
  --match-full-lines --input-file "${OUTPUT_DIR}"/frame_time.csv

FileCheck "${PROJECT_ROOT_DIR}/test/check_event_log.txt" \
  --match-full-lines --input-file "${OUTPUT_DIR}"/events.log

FileCheck "${PROJECT_ROOT_DIR}/test/check_trace_event.txt" \
  --match-full-lines --input-file "${OUTPUT_DIR}"/trace_events.log
