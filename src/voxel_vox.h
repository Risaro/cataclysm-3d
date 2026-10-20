#pragma once
#ifndef CATA_SRC_VOXEL_VOX_H
#define CATA_SRC_VOXEL_VOX_H

#include <optional>
#include <string>

#include "voxel_model.h"

std::optional<voxel_grid> load_vox_file( const std::string &path, int voxels_per_tile,
        std::string &error );
bool save_vox_file( const std::string &path, const voxel_grid &grid, std::string &error );

#endif // CATA_SRC_VOXEL_VOX_H
