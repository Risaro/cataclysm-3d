#pragma once
#ifndef CATA_SRC_VOXEL_TILESET_H
#define CATA_SRC_VOXEL_TILESET_H

#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "voxel_models.h"

struct voxelset_entry {
    std::vector<std::string> files;
    std::array<int, 3> offset = { 0, 0, 0 };
    float scale = 1.0f;
    bool random_rotation = false;
    bool rotates = true;
    bool tint = false;
    bool emissive = false;
};

class voxelset
{
    public:
        std::string id;
        std::string name;
        std::string dir;
        int voxels_per_tile = 8;
        std::vector<voxelset_entry> entries;

        const voxelset_entry *find_id( const std::string &game_id ) const;
        const voxelset_entry *find_builtin( vm_kind kind, vm_material material ) const;
        void add_id( const std::string &game_id, int entry );
        void add_builtin( const std::string &key, int entry );

    private:
        std::unordered_map<std::string, int> by_id;
        std::unordered_map<std::string, int> by_builtin;
};

std::vector<std::pair<std::string, std::string>> list_voxelsets();
const voxelset *active_voxelset();
int active_voxelset_generation();

#endif // CATA_SRC_VOXEL_TILESET_H
