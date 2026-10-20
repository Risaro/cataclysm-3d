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
    int index = -1;
    int px = 0;
    int py = 0;
    int pw = 0;
    int ph = 0;
};

class sprite_atlas
{
    public:
        bool load( const std::string &tileset_id );
        void release();
        const sprite_ref *fg( const std::string &id ) const;
        const sprite_ref *bg( const std::string &id ) const;
        const sprite_ref *subtile( const std::string &id, const std::string &sub, int rotation,
                                   int &extra_rotation ) const;
        GLuint texture( int sheet ) const;
        int sheet_count() const;
        const uint8_t *pixel( const sprite_ref &spr, int x, int y ) const;
        int tile_width = 32;

        std::string loaded_id;
        bool ok = false;

    private:
        struct tile_entry {
            int fg = -1;
            int bg = -1;
        };
        std::vector<sprite_ref> sprites;
        std::unordered_map<std::string, tile_entry> tiles;
        std::unordered_map<std::string, std::vector<int>> subtiles;
        std::vector<GLuint> textures;
        struct sheet_pixels {
            int w = 0;
            int h = 0;
            std::vector<uint8_t> rgba;
        };
        std::vector<sheet_pixels> pixels;
};

#endif

#endif // CATA_SRC_VOXEL_SPRITES_H
