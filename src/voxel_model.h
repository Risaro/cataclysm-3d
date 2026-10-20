#pragma once
#ifndef CATA_SRC_VOXEL_MODEL_H
#define CATA_SRC_VOXEL_MODEL_H

#include <cstdint>
#include <vector>

struct voxel_rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

voxel_rgb voxel_shade( const voxel_rgb &c, float factor );
voxel_rgb voxel_mix( const voxel_rgb &a, const voxel_rgb &b, float t );
uint32_t voxel_hash( int x, int y, int z, uint32_t seed );
float voxel_noise( int x, int y, int z, uint32_t seed );

class voxel_grid
{
    public:
        voxel_grid( int sx, int sy, int sz, int ox = 0, int oz = 0 );

        bool inside( int x, int y, int z ) const;
        void set( int x, int y, int z, const voxel_rgb &c, bool glass = false );
        void clear( int x, int y, int z );
        bool solid( int x, int y, int z ) const;
        bool opaque( int x, int y, int z ) const;
        bool glass( int x, int y, int z ) const;
        voxel_rgb color( int x, int y, int z ) const;

        void box( int x0, int y0, int z0, int x1, int y1, int z1, const voxel_rgb &c,
                  float noise = 0.0f, uint32_t seed = 0, bool glass = false );
        void sphere( float cx, float cy, float cz, float rx, float ry, float rz, const voxel_rgb &c,
                     float noise = 0.0f, uint32_t seed = 0, float holes = 0.0f );

        int sx;
        int sy;
        int sz;
        int ox;
        int oz;

    private:
        int index( int x, int y, int z ) const;
        std::vector<uint32_t> cells;
};

struct voxel_vertex {
    float x;
    float y;
    float z;
    float nx;
    float ny;
    float nz;
    float r;
    float g;
    float b;
    float ao;
};

struct voxel_mesh {
    std::vector<voxel_vertex> opaque;
    std::vector<voxel_vertex> glass;
};

constexpr float voxel_size = 1.0f / 8.0f;
constexpr int voxel_level_height = 16;

voxel_mesh build_voxel_mesh( const voxel_grid &grid );

#endif // CATA_SRC_VOXEL_MODEL_H
