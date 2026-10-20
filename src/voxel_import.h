#pragma once
#ifndef CATA_SRC_VOXEL_IMPORT_H
#define CATA_SRC_VOXEL_IMPORT_H

#include <optional>
#include <string>

#include "voxel_model.h"

struct voxel_import_options {
    int voxels_per_tile = 8;
    float height_voxels = 0.0f;
    float scale = 1.0f;
};

std::optional<voxel_grid> voxelize_obj( const std::string &path, const voxel_import_options &options,
                                        std::string &error );
bool is_mesh_file( const std::string &path );

#endif // CATA_SRC_VOXEL_IMPORT_H
