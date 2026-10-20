#pragma once
#ifndef CATA_SRC_VOXEL_SPRITES_H
#define CATA_SRC_VOXEL_SPRITES_H

#if defined(TILES)

#include <string>
#include <unordered_map>
#include <vector>

#include "voxel_gl.h"

struct sprite_ref {
    int sheet = -1;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float w = 1.0f;
    float h = 1.0f;
    float shift_x = 0.0f;
    float shift_y = 0.0f;
};

class sprite_atlas
{
    public:
        bool load( const std::string &tileset_id );
        void release();
        const sprite_ref *fg( const std::string &id ) const;
        const sprite_ref *bg( const std::string &id ) const;
        GLuint texture( int sheet ) const;
        int sheet_count() const;

        std::string loaded_id;
        bool ok = false;

    private:
        struct tile_entry {
            int fg = -1;
            int bg = -1;
        };
        std::vector<sprite_ref> sprites;
        std::unordered_map<std::string, tile_entry> tiles;
        std::vector<GLuint> textures;
};

#endif

#endif // CATA_SRC_VOXEL_SPRITES_H
