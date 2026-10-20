#!/bin/sh
set -e
cd "$(dirname "$0")/../.."
${CXX:-c++} -std=c++17 -O2 -Isrc tools/voxelize/voxelize.cpp src/voxel_import.cpp src/voxel_model.cpp \
    src/voxel_vox.cpp $(pkg-config --cflags --libs sdl3 sdl3-image) -o voxelize
echo "built ./voxelize"
