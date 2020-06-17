#!/usr/bin/env bash
#
# This script generates sample output for a performance layer. The arguments
# must be the json manifest for the layer, the environment variable name which
# tells the layer where to log, and the desired output file.
#
# NOTE: the layer libraries must be available to the runtime environment (ex.
# via LD_LIBRARY_PATH). Running this script from the build directory usually is
# sufficient.
#
# Usage:
#   gen_sample_output.sh path/to/VkLayer_stadia_XXX.json VK_XXX_LOG path/to/output

set -e

JSON="${1?}"
LOGVAR="${2?}"
OUTPUT="${3?}"
ENABLE=$(grep ENABLE "${JSON?}" | sed -e 's/"//g' -e 's/ //g' -e 's/:.*//')
cp "${JSON?}" ~/.local/share/vulkan/implicit_layer.d
printf -v "${ENABLE?}" "1"
printf -v "${LOGVAR?}" "${OUTPUT?}"
export "${ENABLE?}"
export "${LOGVAR?}"

vkcube --c 10
