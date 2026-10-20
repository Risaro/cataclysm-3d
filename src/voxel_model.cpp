#include "voxel_model.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{

constexpr uint32_t present_bit = 1u << 24;
constexpr uint32_t glass_bit = 1u << 25;

uint32_t pack( const voxel_rgb &c, bool glass )
{
    return present_bit | ( glass ? glass_bit : 0u ) | ( static_cast<uint32_t>( c.r ) << 16 ) |
           ( static_cast<uint32_t>( c.g ) << 8 ) | c.b;
}

struct face_dir {
    int axis;
    int sign;
    int u;
    int v;
};

constexpr std::array<face_dir, 6> face_dirs = { {
        { 0, 1, 1, 2 },
        { 0, -1, 2, 1 },
        { 1, 1, 2, 0 },
        { 1, -1, 0, 2 },
        { 2, 1, 0, 1 },
        { 2, -1, 1, 0 }
    }
};

constexpr std::array<float, 4> ao_curve = { 0.45f, 0.65f, 0.83f, 1.0f };

} // namespace

voxel_rgb voxel_shade( const voxel_rgb &c, float factor )
{
    const auto ch = [factor]( uint8_t v ) {
        return static_cast<uint8_t>( std::clamp( v * factor, 0.0f, 255.0f ) );
    };
    return { ch( c.r ), ch( c.g ), ch( c.b ) };
}

voxel_rgb voxel_mix( const voxel_rgb &a, const voxel_rgb &b, float t )
{
    const auto ch = [t]( uint8_t x, uint8_t y ) {
        return static_cast<uint8_t>( std::clamp( x + ( y - x ) * t, 0.0f, 255.0f ) );
    };
    return { ch( a.r, b.r ), ch( a.g, b.g ), ch( a.b, b.b ) };
}

uint32_t voxel_hash( int x, int y, int z, uint32_t seed )
{
    uint32_t h = seed * 0x9E3779B9u;
    h ^= static_cast<uint32_t>( x ) * 0x85EBCA6Bu;
    h = ( h << 13 ) | ( h >> 19 );
    h ^= static_cast<uint32_t>( y ) * 0xC2B2AE35u;
    h = ( h << 11 ) | ( h >> 21 );
    h ^= static_cast<uint32_t>( z ) * 0x27D4EB2Fu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

float voxel_noise( int x, int y, int z, uint32_t seed )
{
    return static_cast<float>( voxel_hash( x, y, z, seed ) & 0xFFFFu ) / 65535.0f;
}

voxel_grid::voxel_grid( int sx, int sy, int sz, int ox, int oz )
    : sx( sx ), sy( sy ), sz( sz ), ox( ox ), oz( oz ),
      cells( static_cast<size_t>( sx ) * sy * sz, 0u )
{
}

int voxel_grid::index( int x, int y, int z ) const
{
    return ( y * sz + z ) * sx + x;
}

bool voxel_grid::inside( int x, int y, int z ) const
{
    return x >= 0 && y >= 0 && z >= 0 && x < sx && y < sy && z < sz;
}

void voxel_grid::set( int x, int y, int z, const voxel_rgb &c, bool glass )
{
    if( inside( x, y, z ) ) {
        cells[index( x, y, z )] = pack( c, glass );
    }
}

void voxel_grid::clear( int x, int y, int z )
{
    if( inside( x, y, z ) ) {
        cells[index( x, y, z )] = 0u;
    }
}

bool voxel_grid::solid( int x, int y, int z ) const
{
    return inside( x, y, z ) && ( cells[index( x, y, z )] & present_bit ) != 0;
}

bool voxel_grid::opaque( int x, int y, int z ) const
{
    if( !inside( x, y, z ) ) {
        return false;
    }
    const uint32_t c = cells[index( x, y, z )];
    return ( c & present_bit ) != 0 && ( c & glass_bit ) == 0;
}

bool voxel_grid::glass( int x, int y, int z ) const
{
    return inside( x, y, z ) && ( cells[index( x, y, z )] & glass_bit ) != 0;
}

voxel_rgb voxel_grid::color( int x, int y, int z ) const
{
    const uint32_t c = cells[index( x, y, z )];
    return { static_cast<uint8_t>( ( c >> 16 ) & 0xFF ), static_cast<uint8_t>( ( c >> 8 ) & 0xFF ),
             static_cast<uint8_t>( c & 0xFF ) };
}

void voxel_grid::box( int x0, int y0, int z0, int x1, int y1, int z1, const voxel_rgb &c,
                      float noise, uint32_t seed, bool glass )
{
    for( int y = std::max( y0, 0 ); y < std::min( y1, sy ); ++y ) {
        for( int z = std::max( z0, 0 ); z < std::min( z1, sz ); ++z ) {
            for( int x = std::max( x0, 0 ); x < std::min( x1, sx ); ++x ) {
                const float n = noise > 0.0f ? 1.0f + ( voxel_noise( x, y, z, seed ) - 0.5f ) * 2.0f * noise :
                                1.0f;
                set( x, y, z, voxel_shade( c, n ), glass );
            }
        }
    }
}

void voxel_grid::sphere( float cx, float cy, float cz, float rx, float ry, float rz,
                         const voxel_rgb &c, float noise, uint32_t seed, float holes )
{
    const int x0 = static_cast<int>( std::floor( cx - rx ) );
    const int x1 = static_cast<int>( std::ceil( cx + rx ) );
    const int y0 = static_cast<int>( std::floor( cy - ry ) );
    const int y1 = static_cast<int>( std::ceil( cy + ry ) );
    const int z0 = static_cast<int>( std::floor( cz - rz ) );
    const int z1 = static_cast<int>( std::ceil( cz + rz ) );
    for( int y = y0; y <= y1; ++y ) {
        for( int z = z0; z <= z1; ++z ) {
            for( int x = x0; x <= x1; ++x ) {
                const float dx = ( x + 0.5f - cx ) / rx;
                const float dy = ( y + 0.5f - cy ) / ry;
                const float dz = ( z + 0.5f - cz ) / rz;
                const float d = dx * dx + dy * dy + dz * dz;
                if( d > 1.0f ) {
                    continue;
                }
                if( holes > 0.0f && d > 0.45f && voxel_noise( x, y, z, seed + 77 ) < holes ) {
                    continue;
                }
                const float n = noise > 0.0f ? 1.0f + ( voxel_noise( x, y, z, seed ) - 0.5f ) * 2.0f * noise :
                                1.0f;
                set( x, y, z, voxel_shade( c, n ) );
            }
        }
    }
}

voxel_mesh build_voxel_mesh( const voxel_grid &grid, float vsize )
{
    voxel_mesh mesh;
    const std::array<int, 3> dims = { grid.sx, grid.sy, grid.sz };
    std::vector<uint64_t> mask;

    for( const face_dir &fd : face_dirs ) {
        const int a = fd.axis;
        const int ua = fd.u;
        const int va = fd.v;
        const int du = dims[ua];
        const int dv = dims[va];
        mask.assign( static_cast<size_t>( du ) * dv, 0 );

        for( int d = 0; d < dims[a]; ++d ) {
            std::fill( mask.begin(), mask.end(), 0 );
            for( int j = 0; j < dv; ++j ) {
                for( int i = 0; i < du; ++i ) {
                    std::array<int, 3> p = { 0, 0, 0 };
                    p[a] = d;
                    p[ua] = i;
                    p[va] = j;
                    if( !grid.solid( p[0], p[1], p[2] ) ) {
                        continue;
                    }
                    std::array<int, 3> n = p;
                    n[a] += fd.sign;
                    const bool is_glass = grid.glass( p[0], p[1], p[2] );
                    if( is_glass ? grid.solid( n[0], n[1], n[2] ) : grid.opaque( n[0], n[1], n[2] ) ) {
                        continue;
                    }
                    uint64_t ao_bits = 0;
                    if( !is_glass ) {
                        for( int c = 0; c < 4; ++c ) {
                            const int cu = ( c == 1 || c == 2 ) ? 1 : -1;
                            const int cv = ( c >= 2 ) ? 1 : -1;
                            std::array<int, 3> s1 = n;
                            s1[ua] += cu;
                            std::array<int, 3> s2 = n;
                            s2[va] += cv;
                            std::array<int, 3> cr = n;
                            cr[ua] += cu;
                            cr[va] += cv;
                            const bool b1 = grid.opaque( s1[0], s1[1], s1[2] );
                            const bool b2 = grid.opaque( s2[0], s2[1], s2[2] );
                            const bool bc = grid.opaque( cr[0], cr[1], cr[2] );
                            const int level = b1 && b2 ? 0 : 3 - ( static_cast<int>( b1 ) + static_cast<int>( b2 ) +
                                              static_cast<int>( bc ) );
                            ao_bits |= static_cast<uint64_t>( level ) << ( c * 2 );
                        }
                    } else {
                        ao_bits = 0xFF;
                    }
                    const voxel_rgb col = grid.color( p[0], p[1], p[2] );
                    mask[j * du + i] = ( 1ull << 40 ) | ( static_cast<uint64_t>( is_glass ) << 39 ) |
                                       ( ao_bits << 24 ) | ( static_cast<uint64_t>( col.r ) << 16 ) |
                                       ( static_cast<uint64_t>( col.g ) << 8 ) | col.b;
                }
            }

            for( int j = 0; j < dv; ++j ) {
                for( int i = 0; i < du; ) {
                    const uint64_t m = mask[j * du + i];
                    if( m == 0 ) {
                        ++i;
                        continue;
                    }
                    const uint64_t ao_bits = ( m >> 24 ) & 0xFF;
                    const bool uniform = ao_bits == 0x00 || ao_bits == 0x55 || ao_bits == 0xAA || ao_bits == 0xFF;
                    int w = 1;
                    int h = 1;
                    if( uniform ) {
                        while( i + w < du && mask[j * du + i + w] == m ) {
                            ++w;
                        }
                        bool grow = true;
                        while( grow && j + h < dv ) {
                            for( int k = 0; k < w; ++k ) {
                                if( mask[( j + h ) * du + i + k] != m ) {
                                    grow = false;
                                    break;
                                }
                            }
                            if( grow ) {
                                ++h;
                            }
                        }
                    }
                    for( int y = 0; y < h; ++y ) {
                        for( int x = 0; x < w; ++x ) {
                            mask[( j + y ) * du + i + x] = 0;
                        }
                    }

                    const bool is_glass = ( m >> 39 ) & 1;
                    const float r = static_cast<float>( ( m >> 16 ) & 0xFF ) / 255.0f;
                    const float g = static_cast<float>( ( m >> 8 ) & 0xFF ) / 255.0f;
                    const float b = static_cast<float>( m & 0xFF ) / 255.0f;
                    std::array<float, 3> normal = { 0.0f, 0.0f, 0.0f };
                    normal[a] = static_cast<float>( fd.sign );

                    std::array<voxel_vertex, 4> quad;
                    std::array<float, 4> ao;
                    for( int c = 0; c < 4; ++c ) {
                        const int cu = ( c == 1 || c == 2 ) ? w : 0;
                        const int cv = ( c >= 2 ) ? h : 0;
                        std::array<float, 3> pos = { 0.0f, 0.0f, 0.0f };
                        pos[a] = static_cast<float>( d + ( fd.sign > 0 ? 1 : 0 ) );
                        pos[ua] = static_cast<float>( i + cu );
                        pos[va] = static_cast<float>( j + cv );
                        ao[c] = ao_curve[( ao_bits >> ( c * 2 ) ) & 3];
                        quad[c] = voxel_vertex{
                            ( pos[0] - grid.ox ) * vsize, pos[1] * vsize, ( pos[2] - grid.oz ) * vsize,
                            normal[0], normal[1], normal[2], r, g, b, ao[c]
                        };
                    }
                    std::vector<voxel_vertex> &out = is_glass ? mesh.glass : mesh.opaque;
                    if( ao[0] + ao[2] >= ao[1] + ao[3] ) {
                        out.insert( out.end(), { quad[0], quad[1], quad[2], quad[0], quad[2], quad[3] } );
                    } else {
                        out.insert( out.end(), { quad[1], quad[2], quad[3], quad[1], quad[3], quad[0] } );
                    }
                    i += w;
                }
            }
        }
    }
    return mesh;
}
