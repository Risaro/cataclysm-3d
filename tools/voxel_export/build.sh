#!/bin/sh
set -e
cd "$(dirname "$0")/../.."
${CXX:-c++} -std=c++17 -O2 -Isrc tools/voxel_export/voxel_export.cpp src/voxel_model.cpp \
    src/voxel_models.cpp src/voxel_vox.cpp -o voxel-export
echo "built ./voxel-export"
