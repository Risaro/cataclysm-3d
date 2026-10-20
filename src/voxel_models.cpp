#include "voxel_models.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{

constexpr int lh = voxel_level_height;

uint64_t quant( const voxel_rgb &c )
{
    return ( static_cast<uint64_t>( c.r >> 3 ) << 10 ) | ( static_cast<uint64_t>( c.g >> 3 ) << 5 ) |
           static_cast<uint64_t>( c.b >> 3 );
}

voxel_rgb dequant( const voxel_rgb &c )
{
    return { static_cast<uint8_t>( ( c.r & 0xF8 ) | 4 ), static_cast<uint8_t>( ( c.g & 0xF8 ) | 4 ),
             static_cast<uint8_t>( ( c.b & 0xF8 ) | 4 ) };
}

constexpr voxel_rgb rgb( uint8_t r, uint8_t g, uint8_t b )
{
    return { r, g, b };
}

float nz( int x, int y, int z, uint32_t seed, float amount )
{
    return 1.0f + ( voxel_noise( x, y, z, seed ) - 0.5f ) * 2.0f * amount;
}

void swap_xz( voxel_grid &g )
{
    voxel_grid out( g.sz, g.sy, g.sx, g.oz, g.ox );
    for( int y = 0; y < g.sy; ++y ) {
        for( int z = 0; z < g.sz; ++z ) {
            for( int x = 0; x < g.sx; ++x ) {
                if( g.solid( x, y, z ) ) {
                    out.set( z, y, x, g.color( x, y, z ), g.glass( x, y, z ) );
                }
            }
        }
    }
    g = out;
}

voxel_rgb floor_voxel( vm_material m, const voxel_rgb &base, int x, int z, uint32_t seed,
                       uint8_t param )
{
    switch( m ) {
        case vm_material::grass:
            return voxel_shade( base, nz( x, 0, z, seed, 0.14f ) );
        case vm_material::dirt:
            return voxel_shade( base, nz( x, 0, z, seed, 0.16f ) );
        case vm_material::sand:
            return voxel_shade( base, nz( x, 0, z, seed, 0.06f ) );
        case vm_material::pavement: {
            const bool stripe_z = ( param & 1 ) != 0 && ( x == 3 || x == 4 ) && z % 4 < 2;
            const bool stripe_x = ( param & 2 ) != 0 && ( z == 3 || z == 4 ) && x % 4 < 2;
            if( stripe_z || stripe_x ) {
                return rgb( 200, 170, 50 );
            }
            if( ( param & 4 ) != 0 && x % 4 < 2 ) {
                return rgb( 225, 225, 220 );
            }
            return voxel_shade( base, nz( x, 0, z, seed, 0.07f ) );
        }
        case vm_material::sidewalk:
            return voxel_shade( base, ( x == 0 || z == 0 ? 0.82f : 1.0f ) * nz( x, 0, z, seed, 0.04f ) );
        case vm_material::concrete:
            return voxel_shade( base, ( x == 0 || z == 0 ? 0.92f : 1.0f ) * nz( x, 0, z, seed, 0.03f ) );
        case vm_material::wood: {
            const int plank = z / 2;
            float s = 0.85f + 0.3f * voxel_noise( plank, 0, 0, seed );
            if( x == static_cast<int>( ( plank * 3 + seed ) % 8 ) ) {
                s *= 0.72f;
            }
            if( z % 2 == 1 ) {
                s *= 0.92f;
            }
            return voxel_shade( base, s );
        }
        case vm_material::tile: {
            const bool checker = ( ( x / 4 ) + ( z / 4 ) ) % 2 == 0;
            const float s = checker ? 1.0f : 0.93f;
            return voxel_shade( base, s * nz( x, 0, z, seed, 0.015f ) );
        }
        case vm_material::carpet:
            return voxel_shade( base, nz( x, 0, z, seed, 0.05f ) );
        case vm_material::rock:
            return voxel_shade( base, nz( x, 0, z, seed, 0.2f ) );
        case vm_material::metal:
            return voxel_shade( base, ( ( x + z ) % 4 == 0 ? 0.88f : 1.0f ) * nz( x, 0, z, seed, 0.03f ) );
        case vm_material::snow:
            return voxel_shade( base, nz( x, 0, z, seed, 0.04f ) );
        case vm_material::brick:
            return voxel_shade( base, ( z % 2 == 0 && x % 4 == 0 ? 0.8f : 1.0f ) * nz( x, 0, z, seed, 0.08f ) );
        case vm_material::log:
        case vm_material::glass:
        case vm_material::generic:
            break;
    }
    return voxel_shade( base, nz( x, 0, z, seed, 0.04f ) );
}

voxel_rgb wall_voxel( vm_material m, const voxel_rgb &base, int x, int y, int z, uint32_t seed )
{
    switch( m ) {
        case vm_material::brick: {
            const int row = y / 3;
            const int offset = ( row % 2 ) * 2;
            if( y % 3 == 2 || ( x + z + offset ) % 4 == 0 ) {
                return voxel_shade( rgb( 175, 168, 155 ), nz( x, y, z, seed, 0.04f ) );
            }
            const int brick = ( x + z + offset ) / 4;
            return voxel_shade( base, 0.85f + 0.3f * voxel_noise( brick, row, 0, seed ) );
        }
        case vm_material::wood:
        case vm_material::log: {
            const int row = y / 2;
            float s = 0.82f + 0.3f * voxel_noise( row, 0, ( x + z ) / 5, seed );
            if( y % 2 == 1 ) {
                s *= 0.78f;
            }
            return voxel_shade( base, s );
        }
        case vm_material::concrete:
            return voxel_shade( base, ( y % 8 == 7 ? 0.86f : 1.0f ) * nz( x, y, z, seed, 0.05f ) );
        case vm_material::metal:
            return voxel_shade( base, ( ( x + z ) % 2 == 0 ? 0.88f : 1.0f ) * nz( x, y, z, seed, 0.03f ) );
        case vm_material::rock:
            return voxel_shade( base, nz( x / 2, y / 2, z / 2, seed, 0.18f ) * nz( x, y, z, seed + 3, 0.08f ) );
        default:
            break;
    }
    return voxel_shade( base, nz( x, y, z, seed, 0.06f ) );
}

voxel_grid make_floor( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 3, 8 );
    for( int z = 0; z < 8; ++z ) {
        for( int x = 0; x < 8; ++x ) {
            g.set( x, 0, z, floor_voxel( k.material, k.color, x, z, seed, k.param ) );
            const float n = voxel_noise( x, 1, z, seed + 11 );
            if( k.material == vm_material::grass ) {
                if( n < 0.16f ) {
                    g.set( x, 1, z, voxel_shade( k.color, 1.15f ) );
                }
                if( n < 0.05f ) {
                    g.set( x, 2, z, voxel_shade( k.color, 1.3f ) );
                }
            } else if( k.material == vm_material::dirt && n < 0.03f ) {
                g.set( x, 1, z, rgb( 120, 115, 105 ) );
            } else if( k.material == vm_material::rock && n < 0.08f ) {
                g.set( x, 1, z, voxel_shade( k.color, 0.9f ) );
            }
        }
    }
    return g;
}

voxel_grid make_wall( const vm_key &k, uint32_t seed )
{
    const int h = k.param == 1 ? lh / 2 : lh;
    voxel_grid g( 8, h, 8 );
    if( k.material == vm_material::glass ) {
        for( int y = 0; y < h; ++y ) {
            for( int z = 0; z < 8; ++z ) {
                for( int x = 0; x < 8; ++x ) {
                    const bool edge = ( x == 0 || x == 7 ) && ( z == 0 || z == 7 );
                    if( edge || y == 0 || y == h - 1 ) {
                        g.set( x, y, z, rgb( 90, 95, 100 ) );
                    } else if( x == 0 || x == 7 || z == 0 || z == 7 ) {
                        g.set( x, y, z, voxel_mix( k.color, rgb( 170, 210, 235 ), 0.6f ), true );
                    }
                }
            }
        }
        return g;
    }
    for( int y = 0; y < h; ++y ) {
        for( int z = 0; z < 8; ++z ) {
            for( int x = 0; x < 8; ++x ) {
                g.set( x, y, z, wall_voxel( k.material, k.color, x, y, z, seed ) );
            }
        }
    }
    return g;
}

voxel_grid make_window( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, lh, 8 );
    for( int y = 0; y < lh; ++y ) {
        const bool solid_row = y < 5 || y >= 13;
        for( int z = 0; z < 8; ++z ) {
            for( int x = 0; x < 8; ++x ) {
                if( solid_row ) {
                    g.set( x, y, z, wall_voxel( k.material, k.color, x, y, z, seed ) );
                } else if( z == 3 || z == 4 ) {
                    if( x == 0 || x == 7 || y == 5 ) {
                        g.set( x, y, z, voxel_shade( k.color2, 0.9f ) );
                    }
                }
            }
        }
    }
    const voxel_rgb glass = rgb( 150, 200, 230 );
    for( int y = 6; y < 13; ++y ) {
        for( int x = 1; x < 7; ++x ) {
            switch( k.variant ) {
                case 0:
                    g.set( x, y, 3, glass, true );
                    if( x == 3 || y == 9 ) {
                        g.set( x, y, 4, voxel_shade( k.color2, 0.9f ) );
                    }
                    break;
                case 1:
                    if( voxel_noise( x, y, 0, seed ) < 0.3f || y < 7 ) {
                        g.set( x, y, 3, glass, true );
                    }
                    break;
                case 3:
                    if( y % 3 != 0 ) {
                        g.set( x, y, 2, voxel_shade( rgb( 140, 100, 60 ), 0.8f + 0.3f * voxel_noise( 0, y / 3, 0,
                                                     seed ) ) );
                    }
                    break;
                case 4:
                    g.set( x, y, 3, glass, true );
                    g.set( x, y, 4, voxel_shade( k.color2, ( x % 2 ) ? 0.85f : 1.0f ) );
                    break;
                default:
                    break;
            }
        }
    }
    if( k.param & 1 ) {
        swap_xz( g );
    }
    return g;
}

voxel_grid make_door( const vm_key &k, bool open, uint32_t seed )
{
    voxel_grid g( 8, lh, 8 );
    for( int y = 13; y < lh; ++y ) {
        for( int z = 2; z < 6; ++z ) {
            for( int x = 0; x < 8; ++x ) {
                g.set( x, y, z, wall_voxel( vm_material::generic, k.color2, x, y, z, seed ) );
            }
        }
    }
    for( int y = 0; y < 13; ++y ) {
        for( int z = 2; z < 6; ++z ) {
            g.set( 0, y, z, voxel_shade( k.color2, 0.9f ) );
            g.set( 7, y, z, voxel_shade( k.color2, 0.9f ) );
        }
    }
    const bool glass = k.material == vm_material::glass;
    for( int y = 0; y < 13; ++y ) {
        for( int a = 1; a < 7; ++a ) {
            for( int t = 3; t < 5; ++t ) {
                const bool panel = ( a >= 2 && a < 6 ) && ( ( y >= 2 && y < 6 ) || ( y >= 7 && y < 11 ) );
                voxel_rgb c = voxel_shade( k.color, ( panel ? 0.86f : 1.0f ) * nz( a, y, t, seed, 0.04f ) );
                const bool pane = glass && a >= 1 && a < 7 && y >= 1 && y < 12;
                if( open ) {
                    g.set( 7 - ( t - 3 ), y, a, c, pane );
                } else {
                    g.set( a, y, t, c, pane );
                }
            }
        }
    }
    if( !open ) {
        g.set( 6, 6, 2, rgb( 210, 180, 60 ) );
        g.set( 6, 6, 5, rgb( 210, 180, 60 ) );
    }
    if( k.param & 1 ) {
        swap_xz( g );
    }
    return g;
}

voxel_grid make_tree( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 24, 40, 24, 8, 8 );
    const int trunk_h = 14 + static_cast<int>( k.variant % 6 );
    const voxel_rgb bark = k.color2;
    for( int y = 0; y < trunk_h; ++y ) {
        g.box( 11, y, 11, 13, y + 1, 13, bark, 0.12f, seed + y );
    }
    g.set( 10, 0, 11, voxel_shade( bark, 0.85f ) );
    g.set( 13, 0, 12, voxel_shade( bark, 0.85f ) );
    g.set( 12, 0, 13, voxel_shade( bark, 0.85f ) );
    const float r = 6.0f + static_cast<float>( k.variant % 3 );
    g.sphere( 12.0f, trunk_h + 4.0f, 12.0f, r, r * 0.85f, r, k.color, 0.16f, seed, 0.3f );
    const float ox = ( voxel_noise( 1, 2, 3, seed ) - 0.5f ) * 6.0f;
    const float oz = ( voxel_noise( 3, 2, 1, seed ) - 0.5f ) * 6.0f;
    g.sphere( 12.0f + ox, trunk_h + 7.0f, 12.0f + oz, r * 0.7f, r * 0.6f, r * 0.7f,
              voxel_shade( k.color, 1.12f ), 0.16f, seed + 5, 0.3f );
    if( k.param != 0 ) {
        const voxel_rgb fruit = k.param == 1 ? rgb( 200, 40, 30 ) : k.param == 2 ? rgb( 230, 140, 30 ) :
                                k.param == 3 ? rgb( 230, 210, 60 ) : rgb( 110, 40, 130 );
        for( int i = 0; i < 18; ++i ) {
            const int x = 6 + static_cast<int>( voxel_hash( i, 0, 0, seed ) % 13 );
            const int y = trunk_h + static_cast<int>( voxel_hash( i, 1, 0, seed ) % 9 );
            const int z = 6 + static_cast<int>( voxel_hash( i, 2, 0, seed ) % 13 );
            if( g.solid( x, y, z ) ) {
                g.set( x, y, z, fruit );
            }
        }
    }
    return g;
}

voxel_grid make_pine( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 24, 40, 24, 8, 8 );
    const int top = 30 + static_cast<int>( k.variant % 6 );
    for( int y = 0; y < top - 4; ++y ) {
        g.box( 11, y, 11, 13, y + 1, 13, k.color2, 0.1f, seed + y );
    }
    for( int y = 5; y < top; ++y ) {
        const float t = static_cast<float>( top - y ) / static_cast<float>( top - 5 );
        const float radius = 1.0f + t * 8.0f * ( ( y % 4 == 0 ) ? 0.75f : 1.0f );
        for( int z = 0; z < 24; ++z ) {
            for( int x = 0; x < 24; ++x ) {
                const float dx = x + 0.5f - 12.0f;
                const float dz = z + 0.5f - 12.0f;
                const float d = std::sqrt( dx * dx + dz * dz );
                if( d <= radius && voxel_noise( x, y, z, seed ) > 0.15f ) {
                    g.set( x, y, z, voxel_shade( k.color, nz( x, y, z, seed + 9, 0.15f ) * ( 0.85f + 0.3f * t ) ) );
                }
            }
        }
    }
    return g;
}

voxel_grid make_dead_tree( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 24, 40, 24, 8, 8 );
    const int trunk_h = 18 + static_cast<int>( k.variant % 6 );
    for( int y = 0; y < trunk_h; ++y ) {
        g.box( 11, y, 11, 13, y + 1, 13, k.color2, 0.12f, seed + y );
    }
    for( int b = 0; b < 5; ++b ) {
        const int start = 8 + static_cast<int>( voxel_hash( b, 0, 0, seed ) % ( trunk_h - 8 ) );
        const int dx = static_cast<int>( voxel_hash( b, 1, 0, seed ) % 3 ) - 1;
        const int dz = static_cast<int>( voxel_hash( b, 2, 0, seed ) % 3 ) - 1;
        for( int s = 1; s < 7; ++s ) {
            g.set( 12 + dx * s, start + s, 12 + dz * s, voxel_shade( k.color2, 0.9f ) );
        }
    }
    return g;
}

voxel_grid make_young_tree( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 16, 8 );
    for( int y = 0; y < 8; ++y ) {
        g.set( 4, y, 4, k.color2 );
    }
    g.sphere( 4.5f, 10.0f, 4.5f, 3.5f, 3.5f, 3.5f, k.color, 0.16f, seed, 0.25f );
    return g;
}

voxel_grid make_shrub( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 8, 8 );
    g.sphere( 4.0f, 3.0f, 4.0f, 3.8f, 3.4f, 3.8f, k.color, 0.18f, seed, 0.2f );
    if( k.param != 0 ) {
        for( int i = 0; i < 8; ++i ) {
            const int x = static_cast<int>( voxel_hash( i, 0, 0, seed ) % 8 );
            const int y = 1 + static_cast<int>( voxel_hash( i, 1, 0, seed ) % 6 );
            const int z = static_cast<int>( voxel_hash( i, 2, 0, seed ) % 8 );
            if( g.solid( x, y, z ) ) {
                g.set( x, y, z, k.color2 );
            }
        }
    }
    return g;
}

voxel_grid make_flower( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 6, 8 );
    for( int i = 0; i < 4; ++i ) {
        const int x = 1 + static_cast<int>( voxel_hash( i, 0, 0, seed ) % 6 );
        const int z = 1 + static_cast<int>( voxel_hash( i, 1, 0, seed ) % 6 );
        const int h = 2 + static_cast<int>( voxel_hash( i, 2, 0, seed ) % 3 );
        for( int y = 0; y < h; ++y ) {
            g.set( x, y, z, rgb( 70, 130, 50 ) );
        }
        g.set( x, h, z, k.color );
        g.set( x + 1, h, z, voxel_shade( k.color, 0.9f ) );
        g.set( x, h, z + 1, voxel_shade( k.color, 0.9f ) );
        g.set( x - 1, h, z, voxel_shade( k.color, 0.9f ) );
        g.set( x, h, z - 1, voxel_shade( k.color, 0.9f ) );
    }
    return g;
}

voxel_grid make_tall_grass( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 8, 8 );
    for( int z = 0; z < 8; ++z ) {
        for( int x = 0; x < 8; ++x ) {
            if( voxel_noise( x, 0, z, seed ) < 0.45f ) {
                const int h = 2 + static_cast<int>( voxel_hash( x, 1, z, seed ) % 5 );
                for( int y = 0; y < h; ++y ) {
                    g.set( x, y, z, voxel_shade( k.color, 0.8f + 0.08f * y ) );
                }
            }
        }
    }
    return g;
}

voxel_grid make_water( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 1, 8 );
    for( int z = 0; z < 8; ++z ) {
        for( int x = 0; x < 8; ++x ) {
            const float n = voxel_noise( x, 0, z, seed );
            g.set( x, 0, z, n > 0.9f ? voxel_shade( k.color, 1.35f ) : voxel_shade( k.color,
                    0.92f + 0.12f * n ) );
        }
    }
    return g;
}

voxel_grid make_stairs_up( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, lh, 8 );
    for( int s = 0; s < 8; ++s ) {
        g.box( 1, 0, 7 - s, 7, ( s + 1 ) * 2, 8 - s, voxel_shade( k.color, s % 2 ? 0.9f : 1.0f ), 0.05f,
               seed );
    }
    for( int s = 0; s < 8; ++s ) {
        g.box( 0, 0, 7 - s, 1, ( s + 1 ) * 2 + 2, 8 - s, voxel_shade( k.color2, 0.8f ) );
        g.box( 7, 0, 7 - s, 8, ( s + 1 ) * 2 + 2, 8 - s, voxel_shade( k.color2, 0.8f ) );
    }
    return g;
}

voxel_grid make_stairs_down( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 1, 8 );
    for( int z = 0; z < 8; ++z ) {
        for( int x = 0; x < 8; ++x ) {
            const bool hole = x >= 1 && x < 7 && z >= 1 && z < 7;
            if( hole ) {
                g.set( x, 0, z, voxel_shade( k.color, 0.18f + 0.08f * ( z % 2 ) ) );
            } else {
                g.set( x, 0, z, voxel_shade( k.color2, nz( x, 0, z, seed, 0.06f ) ) );
            }
        }
    }
    return g;
}

voxel_grid make_hole( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 1, 8 );
    for( int z = 0; z < 8; ++z ) {
        for( int x = 0; x < 8; ++x ) {
            const float dx = x + 0.5f - 4.0f;
            const float dz = z + 0.5f - 4.0f;
            const bool hole = dx * dx + dz * dz < 9.0f;
            g.set( x, 0, z, hole ? rgb( 15, 15, 18 ) : voxel_shade( k.color, nz( x, 0, z, seed, 0.06f ) ) );
        }
    }
    return g;
}

voxel_grid make_fence( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 10, 8 );
    uint8_t mask = k.param & 0xF;
    if( mask == 0 ) {
        mask = 2 | 8;
    }
    const bool chain = k.variant == 1;
    g.box( 3, 0, 3, 5, 10, 5, voxel_shade( k.color, 0.85f ), 0.05f, seed );
    const auto rail = [&]( int x0, int z0, int x1, int z1 ) {
        if( chain ) {
            for( int y = 1; y < 9; ++y ) {
                for( int z = z0; z < z1; ++z ) {
                    for( int x = x0; x < x1; ++x ) {
                        if( ( x + y + z ) % 2 == 0 || y == 8 ) {
                            g.set( x, y, z, k.color );
                        }
                    }
                }
            }
        } else {
            g.box( x0, 3, z0, x1, 4, z1, k.color, 0.08f, seed + 1 );
            g.box( x0, 7, z0, x1, 8, z1, k.color, 0.08f, seed + 2 );
        }
    };
    if( mask & 1 ) {
        rail( 3, 0, 5, 3 );
    }
    if( mask & 2 ) {
        rail( 5, 3, 8, 5 );
    }
    if( mask & 4 ) {
        rail( 3, 5, 5, 8 );
    }
    if( mask & 8 ) {
        rail( 0, 3, 3, 5 );
    }
    return g;
}

voxel_grid make_rubble( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 5, 8 );
    for( int i = 0; i < 6; ++i ) {
        const float x = 1.0f + 6.0f * voxel_noise( i, 0, 0, seed );
        const float z = 1.0f + 6.0f * voxel_noise( i, 1, 0, seed );
        const float r = 1.2f + 1.5f * voxel_noise( i, 2, 0, seed );
        g.sphere( x, 0.5f, z, r, r, r, voxel_shade( k.color, 0.85f + 0.3f * voxel_noise( i, 3, 0, seed ) ),
                  0.12f, seed + i );
    }
    return g;
}

voxel_grid make_boulder( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 8, 8 );
    g.sphere( 4.0f, 2.5f, 4.0f, 3.8f, 3.2f, 3.6f, k.color, 0.15f, seed );
    return g;
}

voxel_grid make_bed( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 7, 8 );
    const voxel_rgb frame = voxel_shade( k.color, 0.8f );
    g.box( 1, 0, 0, 7, 2, 8, frame, 0.05f, seed );
    g.box( 1, 2, 1, 7, 3, 8, rgb( 225, 225, 215 ), 0.03f, seed );
    g.box( 1, 3, 3, 7, 4, 8, k.color2, 0.06f, seed + 1 );
    g.box( 2, 3, 1, 6, 4, 3, rgb( 240, 240, 235 ), 0.02f, seed );
    g.box( 1, 0, 0, 7, 6, 1, frame, 0.05f, seed + 2 );
    return g;
}

voxel_grid make_table( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 7, 8 );
    g.box( 0, 5, 0, 8, 6, 8, k.color, 0.06f, seed );
    for( int lx : { 0, 7 } ) {
        for( int lz : { 0, 7 } ) {
            g.box( lx, 0, lz, lx + 1, 5, lz + 1, voxel_shade( k.color, 0.8f ) );
        }
    }
    return g;
}

voxel_grid make_chair( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 9, 8 );
    g.box( 2, 3, 2, 6, 4, 6, k.color, 0.06f, seed );
    g.box( 2, 4, 5, 6, 8, 6, voxel_shade( k.color, 0.9f ), 0.06f, seed + 1 );
    for( int lx : { 2, 5 } ) {
        for( int lz : { 2, 5 } ) {
            g.box( lx, 0, lz, lx + 1, 3, lz + 1, voxel_shade( k.color, 0.75f ) );
        }
    }
    return g;
}

voxel_grid make_sofa( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 7, 8 );
    g.box( 0, 0, 1, 8, 3, 7, k.color, 0.06f, seed );
    g.box( 0, 3, 5, 8, 6, 7, voxel_shade( k.color, 0.9f ), 0.06f, seed + 1 );
    g.box( 0, 3, 1, 1, 4, 5, voxel_shade( k.color, 0.85f ) );
    g.box( 7, 3, 1, 8, 4, 5, voxel_shade( k.color, 0.85f ) );
    return g;
}

voxel_grid make_counter( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 8, 8 );
    g.box( 0, 0, 0, 8, 7, 8, k.color, 0.05f, seed );
    g.box( 0, 7, 0, 8, 8, 8, k.color2, 0.04f, seed + 1 );
    for( int x = 0; x < 8; ++x ) {
        g.set( x, 5, 0, voxel_shade( k.color, 0.75f ) );
    }
    g.set( 2, 4, 0, rgb( 190, 190, 190 ) );
    g.set( 5, 4, 0, rgb( 190, 190, 190 ) );
    return g;
}

voxel_grid make_cabinet( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 14, 8 );
    g.box( 1, 0, 1, 7, 13, 7, k.color, 0.05f, seed );
    for( int y = 0; y < 13; ++y ) {
        g.set( 4, y, 1, voxel_shade( k.color, 0.7f ) );
    }
    g.set( 3, 7, 0, k.color2 );
    g.set( 5, 7, 0, k.color2 );
    for( int x = 1; x < 7; ++x ) {
        g.set( x, 13, 1, voxel_shade( k.color, 0.9f ) );
        g.set( x, 13, 6, voxel_shade( k.color, 0.9f ) );
    }
    return g;
}

voxel_grid make_toilet( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 8, 8 );
    g.box( 2, 0, 2, 6, 4, 7, k.color, 0.03f, seed );
    g.box( 3, 3, 3, 5, 4, 6, rgb( 120, 160, 190 ) );
    g.box( 2, 4, 6, 6, 7, 8, k.color, 0.03f, seed + 1 );
    return g;
}

voxel_grid make_sink( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 8, 8 );
    g.box( 1, 0, 2, 7, 6, 8, k.color2, 0.05f, seed );
    g.box( 1, 6, 2, 7, 7, 8, k.color, 0.03f, seed );
    g.box( 2, 6, 3, 6, 7, 7, rgb( 150, 170, 185 ) );
    g.box( 4, 7, 7, 5, 9, 8, rgb( 190, 190, 195 ) );
    return g;
}

voxel_grid make_bathtub( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 6, 8 );
    g.box( 0, 0, 1, 8, 5, 7, k.color, 0.03f, seed );
    for( int z = 2; z < 6; ++z ) {
        for( int x = 1; x < 7; ++x ) {
            g.clear( x, 2, z );
            g.clear( x, 3, z );
            g.clear( x, 4, z );
        }
    }
    return g;
}

voxel_grid make_stove( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 8, 8 );
    g.box( 0, 0, 0, 8, 7, 8, k.color, 0.04f, seed );
    g.box( 1, 1, 0, 7, 5, 1, rgb( 30, 30, 32 ) );
    for( int bx : { 2, 5 } ) {
        for( int bz : { 2, 5 } ) {
            g.box( bx - 1, 7, bz - 1, bx + 1, 8, bz + 1, rgb( 40, 40, 40 ) );
        }
    }
    return g;
}

voxel_grid make_screen( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 12, 8 );
    g.box( 0, 0, 1, 8, 6, 7, k.color2, 0.05f, seed );
    g.box( 1, 6, 4, 7, 11, 6, rgb( 40, 40, 45 ) );
    g.box( 2, 7, 3, 6, 10, 4, k.color, 0.1f, seed + 1 );
    return g;
}

voxel_grid make_crate( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 7, 8 );
    for( int y = 0; y < 6; ++y ) {
        for( int z = 1; z < 7; ++z ) {
            for( int x = 1; x < 7; ++x ) {
                const bool edge = ( x == 1 || x == 6 ) + ( z == 1 || z == 6 ) + ( y == 0 || y == 5 ) >= 2;
                g.set( x, y, z, voxel_shade( k.color, ( edge ? 0.75f : 1.0f ) * ( y % 2 ? 0.92f : 1.0f ) *
                                             nz( x, y, z, seed, 0.05f ) ) );
            }
        }
    }
    return g;
}

voxel_grid make_barrel( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 9, 8 );
    for( int y = 0; y < 9; ++y ) {
        for( int z = 0; z < 8; ++z ) {
            for( int x = 0; x < 8; ++x ) {
                const float dx = x + 0.5f - 4.0f;
                const float dz = z + 0.5f - 4.0f;
                if( dx * dx + dz * dz <= 9.5f ) {
                    const float s = ( y == 2 || y == 6 ) ? 0.75f : 1.0f;
                    g.set( x, y, z, voxel_shade( k.color, s * nz( x, y, z, seed, 0.05f ) ) );
                }
            }
        }
    }
    return g;
}

voxel_grid make_dumpster( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 9, 8 );
    g.box( 0, 0, 1, 8, 8, 7, k.color, 0.05f, seed );
    g.box( 0, 8, 1, 8, 9, 7, voxel_shade( k.color, 0.7f ) );
    return g;
}

voxel_grid make_sign( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 14, 8 );
    g.box( 3, 0, 3, 5, 9, 5, rgb( 120, 120, 125 ) );
    g.box( 0, 9, 3, 8, 14, 5, k.color, 0.05f, seed );
    return g;
}

voxel_grid make_lamp( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 20, 8 );
    g.box( 3, 0, 3, 5, 18, 5, k.color2, 0.04f, seed );
    g.box( 2, 18, 2, 6, 20, 6, rgb( 255, 235, 160 ) );
    return g;
}

voxel_grid make_furniture( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 7, 8 );
    g.box( 1, 0, 1, 7, 6, 7, k.color, 0.08f, seed );
    g.box( 1, 6, 1, 7, 7, 7, voxel_shade( k.color, 1.1f ) );
    return g;
}

voxel_grid make_humanoid( const vm_key &k, uint32_t seed, bool zombie )
{
    voxel_grid g( 8, 14, 8 );
    const voxel_rgb skin = k.color;
    const voxel_rgb shirt = k.color2;
    static const std::array<voxel_rgb, 8> pants_palette = { {
            rgb( 55, 60, 85 ), rgb( 40, 40, 44 ), rgb( 110, 90, 60 ), rgb( 70, 85, 55 ),
            rgb( 130, 125, 115 ), rgb( 90, 40, 40 ), rgb( 180, 175, 165 ), rgb( 60, 75, 110 )
        }
    };
    const voxel_rgb pants = zombie ? voxel_shade( rgb( 70, 70, 85 ), 0.8f ) :
                            pants_palette[( k.variant / 3 ) % 8];
    const voxel_rgb hair = zombie ? rgb( 50, 45, 40 ) : ( k.variant % 3 == 0 ? rgb( 60, 40, 25 ) :
                           k.variant % 3 == 1 ? rgb( 25, 20, 18 ) : rgb( 170, 130, 60 ) );
    g.box( 2, 0, 3, 4, 5, 5, pants, 0.05f, seed );
    g.box( 4, 0, 3, 6, 5, 5, voxel_shade( pants, 0.92f ), 0.05f, seed + 1 );
    g.box( 2, 0, 2, 6, 1, 5, rgb( 50, 35, 25 ) );
    g.box( 2, 5, 3, 6, 9, 5, shirt, 0.06f, seed + 2 );
    if( zombie ) {
        g.box( 1, 7, 0, 2, 9, 4, skin, 0.1f, seed + 3 );
        g.box( 6, 7, 0, 7, 9, 4, skin, 0.1f, seed + 4 );
        g.box( 1, 7, 3, 2, 9, 5, shirt );
        g.box( 6, 7, 3, 7, 9, 5, shirt );
        for( int y = 5; y < 9; ++y ) {
            for( int z = 3; z < 5; ++z ) {
                for( int x = 2; x < 6; ++x ) {
                    const float n = voxel_noise( x, y, z, seed + 21 );
                    if( n < 0.18f ) {
                        g.set( x, y, z, skin );
                    } else if( n < 0.26f ) {
                        g.set( x, y, z, rgb( 120, 20, 20 ) );
                    }
                }
            }
        }
    } else {
        g.box( 1, 7, 3, 2, 9, 5, shirt, 0.06f, seed + 3 );
        g.box( 6, 7, 3, 7, 9, 5, shirt, 0.06f, seed + 4 );
        g.box( 1, 5, 3, 2, 7, 5, skin );
        g.box( 6, 5, 3, 7, 7, 5, skin );
        if( k.param == 1 ) {
            g.box( 2, 5, 5, 6, 9, 6, rgb( 110, 80, 45 ), 0.08f, seed + 5 );
        }
    }
    if( zombie ) {
        switch( k.param ) {
            case 1:
                g.box( 1, 5, 2, 7, 9, 6, shirt, 0.08f, seed + 30 );
                g.box( 2, 5, 1, 6, 8, 2, skin, 0.1f, seed + 31 );
                break;
            case 2:
                g.box( 1, 7, 2, 7, 10, 6, shirt, 0.06f, seed + 30 );
                g.box( 0, 6, 0, 2, 9, 4, skin, 0.1f, seed + 31 );
                g.box( 6, 6, 0, 8, 9, 4, skin, 0.1f, seed + 32 );
                break;
            case 3:
                for( int y = 5; y < 9; ++y ) {
                    for( int x = 2; x < 6; ++x ) {
                        if( ( x + y ) % 2 == 0 ) {
                            g.set( x, y, 3, rgb( 220, 215, 195 ) );
                        } else {
                            g.clear( x, y, 3 );
                        }
                    }
                }
                break;
            case 4:
                for( int i = 0; i < 6; ++i ) {
                    const int x = 1 + static_cast<int>( voxel_hash( i, 0, 0, seed ) % 6 );
                    const int y = 5 + static_cast<int>( voxel_hash( i, 1, 0, seed ) % 8 );
                    g.set( x, y, 1 + static_cast<int>( voxel_hash( i, 2, 0, seed ) % 5 ), rgb( 120, 200, 255 ) );
                }
                break;
            case 5:
                for( int i = 0; i < 5; ++i ) {
                    const int x = 2 + static_cast<int>( voxel_hash( i, 0, 0, seed ) % 4 );
                    g.box( x, 4 + static_cast<int>( voxel_hash( i, 1, 0, seed ) % 4 ), 2, x + 1, 9, 3, rgb( 150, 220,
                           60 ) );
                }
                break;
            case 6:
                g.box( 2, 5, 2, 6, 9, 6, rgb( 80, 95, 70 ), 0.08f, seed + 30 );
                g.box( 2, 0, 3, 6, 5, 5, rgb( 75, 85, 65 ), 0.06f, seed + 33 );
                break;
            case 7:
                g.box( 2, 0, 3, 6, 9, 5, rgb( 50, 40, 35 ), 0.2f, seed + 30 );
                break;
            default:
                break;
        }
    }
    g.box( 2, 9, 2, 6, 13, 6, skin, 0.05f, seed + 6 );
    if( zombie && k.param == 6 ) {
        g.box( 1, 12, 1, 7, 14, 7, rgb( 70, 85, 60 ), 0.05f, seed + 34 );
    }
    g.box( 2, 12, 2, 6, 13, 6, hair, 0.08f, seed + 7 );
    g.box( 2, 10, 5, 6, 12, 6, hair, 0.08f, seed + 8 );
    const voxel_rgb eye = zombie ? rgb( 230, 60, 30 ) : rgb( 30, 30, 40 );
    g.set( 2, 10, 2, eye );
    g.set( 5, 10, 2, eye );
    if( zombie ) {
        g.set( 3, 9, 2, rgb( 90, 20, 20 ) );
        g.set( 4, 9, 2, rgb( 90, 20, 20 ) );
    }
    return g;
}

voxel_grid make_quadruped( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 12, 10, 0, 1 );
    const voxel_rgb fur = k.color;
    const voxel_rgb dark = voxel_shade( fur, 0.7f );
    const voxel_rgb eye = rgb( 15, 15, 15 );
    const int kind = k.param;
    int leg = 3;
    int body_x0 = 2;
    int body_x1 = 6;
    int body_y0 = 3;
    int body_y1 = 6;
    int body_z0 = 3;
    int body_z1 = 8;
    if( kind == 1 ) {
        leg = 2;
        body_x0 = 3;
        body_x1 = 5;
        body_y1 = 5;
    } else if( kind == 2 || kind == 4 ) {
        leg = 5;
        body_y0 = 5;
        body_y1 = 8;
        body_z0 = 3;
        body_z1 = 10;
    } else if( kind == 3 ) {
        body_x0 = 1;
        body_x1 = 7;
        body_y1 = 7;
        body_z0 = 3;
        body_z1 = 10;
    } else if( kind == 5 ) {
        leg = 1;
        body_x0 = 3;
        body_x1 = 5;
        body_y0 = 1;
        body_y1 = 4;
        body_z0 = 4;
        body_z1 = 7;
    } else if( kind == 6 ) {
        leg = 2;
        body_x0 = 2;
        body_x1 = 6;
        body_y0 = 2;
        body_y1 = 6;
    }
    if( kind != 5 ) {
        body_y0 = std::max( body_y0, leg );
    }
    g.box( body_x0, body_y0, body_z0, body_x1, body_y1, body_z1, fur, 0.12f, seed );
    g.box( body_x0, body_y0, body_z0 + 1, body_x1, body_y0 + 1, body_z1 - 1, voxel_shade( fur, 1.15f ),
           0.06f, seed + 1 );
    for( int lx : { body_x0, body_x1 - 1 } ) {
        for( int lz : { body_z0, body_z1 - 1 } ) {
            g.box( lx, 0, lz, lx + 1, body_y0, lz + 1, dark, 0.06f, seed + 2 );
        }
    }
    const int hy = body_y1 - 1;
    const int hz = body_z0 - 2;
    const int hw = kind == 3 ? 2 : 1;
    g.box( 4 - hw - 1, hy - 1, hz, 4 + hw + 1, hy + 2, hz + 3, fur, 0.1f, seed + 3 );
    g.box( 3, hy - 1, hz - 1, 5, hy + ( kind == 2 || kind == 4 ? 1 : 0 ), hz, voxel_shade( fur, 0.85f ) );
    g.set( 3, hy, hz - 1, rgb( 20, 20, 20 ) );
    g.set( 4 - hw - 1, hy + 1, hz, eye );
    g.set( 4 + hw, hy + 1, hz, eye );
    if( kind == 1 || kind == 5 ) {
        g.box( 3, hy + 2, hz + 1, 4, hy + ( kind == 5 ? 5 : 3 ), hz + 2, dark );
        g.box( 4, hy + 2, hz + 1, 5, hy + ( kind == 5 ? 5 : 3 ), hz + 2, dark );
    } else if( kind == 2 ) {
        const voxel_rgb antler = rgb( 200, 185, 150 );
        for( int i = 0; i < 3; ++i ) {
            g.set( 2 - i / 2, hy + 2 + i, hz + 1, antler );
            g.set( 5 + i / 2, hy + 2 + i, hz + 1, antler );
        }
        g.set( 1, hy + 4, hz, antler );
        g.set( 6, hy + 4, hz, antler );
    } else if( kind == 6 ) {
        g.set( 2, hy - 1, hz - 1, rgb( 230, 225, 210 ) );
        g.set( 5, hy - 1, hz - 1, rgb( 230, 225, 210 ) );
    } else {
        g.set( 2, hy + 2, hz + 1, dark );
        g.set( 5, hy + 2, hz + 1, dark );
    }
    if( kind == 1 ) {
        g.box( 4, body_y1, body_z1 - 1, 5, body_y1 + 3, body_z1, dark );
    } else if( kind != 3 && kind != 5 ) {
        g.box( 3, body_y1 - 1, body_z1, 5, body_y1, body_z1 + 1, dark );
    } else if( kind == 5 ) {
        g.set( 4, body_y1 - 1, body_z1, rgb( 235, 235, 230 ) );
    }
    return g;
}

voxel_grid make_bird( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 7, 8 );
    g.sphere( 4.0f, 3.5f, 4.5f, 2.0f, 1.8f, 2.6f, k.color, 0.1f, seed );
    g.box( 1, 3, 3, 2, 4, 6, voxel_shade( k.color, 0.8f ) );
    g.box( 6, 3, 3, 7, 4, 6, voxel_shade( k.color, 0.8f ) );
    g.box( 3, 4, 1, 5, 6, 3, k.color, 0.05f, seed + 1 );
    g.set( 4, 4, 0, rgb( 230, 160, 40 ) );
    g.set( 3, 5, 1, rgb( 15, 15, 15 ) );
    g.set( 4, 5, 1, rgb( 15, 15, 15 ) );
    g.box( 3, 0, 4, 4, 2, 5, rgb( 200, 140, 40 ) );
    g.box( 4, 0, 4, 5, 2, 5, rgb( 200, 140, 40 ) );
    return g;
}

voxel_grid make_bug( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 6, 8 );
    const voxel_rgb legc = voxel_shade( k.color, 0.6f );
    if( k.param == 3 ) {
        for( int z = 0; z < 8; ++z ) {
            const int x = 3 + ( z % 4 < 2 ? 0 : 1 );
            g.box( x - 1, 0, z, x + 1, 2, z + 1, voxel_shade( k.color, z % 2 ? 0.85f : 1.0f ), 0.05f, seed + z );
            if( z % 2 == 0 ) {
                g.set( x - 2, 0, z, legc );
                g.set( x + 1, 0, z, legc );
            }
        }
        g.set( 3, 1, 0, rgb( 200, 30, 30 ) );
        return g;
    }
    g.box( 3, 1, 0, 5, 3, 2, voxel_shade( k.color, 0.85f ), 0.08f, seed );
    g.box( 2, 1, 2, 6, 3, 4, k.color, 0.08f, seed + 1 );
    g.sphere( 4.0f, 2.5f, 6.0f, 2.2f, 1.7f, 2.2f, k.color, 0.1f, seed + 2 );
    const bool spider = k.param == 1;
    for( int lz : spider ? std::vector<int> { 1, 2, 3, 4 } : std::vector<int> { 2, 3, 4 } ) {
        g.box( 0, 0, lz, 2, 2, lz + 1, legc );
        g.box( 6, 0, lz, 8, 2, lz + 1, legc );
        if( spider ) {
            g.set( 0, 2, lz, legc );
            g.set( 7, 2, lz, legc );
        }
    }
    if( k.param == 2 ) {
        const voxel_rgb wing = rgb( 210, 225, 235 );
        g.box( 0, 3, 2, 3, 4, 6, wing, 0.0f, 0, true );
        g.box( 5, 3, 2, 8, 4, 6, wing, 0.0f, 0, true );
    }
    g.set( 3, 2, 0, rgb( 200, 30, 30 ) );
    g.set( 4, 2, 0, rgb( 200, 30, 30 ) );
    return g;
}

voxel_grid make_robot( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 12, 8 );
    g.box( 0, 0, 1, 2, 3, 7, rgb( 40, 40, 42 ) );
    g.box( 6, 0, 1, 8, 3, 7, rgb( 40, 40, 42 ) );
    g.box( 1, 3, 2, 7, 9, 6, k.color, 0.05f, seed );
    g.box( 2, 9, 2, 6, 11, 5, voxel_shade( k.color, 0.85f ) );
    g.box( 2, 10, 1, 6, 11, 2, rgb( 255, 60, 40 ) );
    g.box( 3, 11, 3, 4, 12, 4, rgb( 150, 150, 150 ) );
    return g;
}

voxel_grid make_blob( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 8, 8 );
    g.sphere( 4.0f, 3.0f, 4.0f, 3.8f, 3.0f, 3.8f, k.color, 0.2f, seed );
    g.set( 3, 4, 0, rgb( 20, 20, 20 ) );
    g.set( 5, 4, 0, rgb( 20, 20, 20 ) );
    return g;
}

voxel_grid make_fish( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 5, 8 );
    g.sphere( 4.0f, 2.0f, 4.0f, 1.6f, 1.8f, 3.4f, k.color, 0.1f, seed );
    g.box( 4, 1, 7, 5, 4, 8, voxel_shade( k.color, 0.8f ) );
    g.set( 3, 2, 1, rgb( 15, 15, 15 ) );
    g.set( 5, 2, 1, rgb( 15, 15, 15 ) );
    return g;
}

voxel_grid make_plant_creature( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 12, 8 );
    g.box( 3, 0, 3, 5, 7, 5, rgb( 80, 110, 50 ), 0.1f, seed );
    g.sphere( 4.0f, 9.0f, 4.0f, 3.5f, 2.5f, 3.5f, k.color, 0.15f, seed + 1 );
    return g;
}

voxel_grid make_item_pile( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 3, 8 );
    const int count = 1 + static_cast<int>( k.variant % 3 );
    for( int i = 0; i < count; ++i ) {
        const int x = 1 + static_cast<int>( voxel_hash( i, 0, 0, seed ) % 5 );
        const int z = 1 + static_cast<int>( voxel_hash( i, 1, 0, seed ) % 5 );
        const int h = 1 + static_cast<int>( voxel_hash( i, 2, 0, seed ) % 2 );
        g.box( x, 0, z, x + 2, h, z + 2, voxel_shade( rgb( 235, 235, 235 ), 0.85f + 0.15f * i ) );
    }
    return g;
}

void vehicle_chassis( voxel_grid &g, const vm_key &k, uint32_t seed )
{
    g.box( 0, 2, 0, 8, 4, 8, voxel_shade( k.color, 0.75f ), 0.05f, seed );
}

voxel_grid make_vehicle( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 14, 8 );
    switch( k.kind ) {
        case vm_kind::veh_frame:
            vehicle_chassis( g, k, seed );
            break;
        case vm_kind::veh_wheel:
            vehicle_chassis( g, k, seed );
            g.box( 2, 0, 1, 6, 5, 7, rgb( 28, 28, 30 ), 0.05f, seed );
            g.box( 1, 1, 3, 7, 3, 5, rgb( 150, 150, 155 ) );
            break;
        case vm_kind::veh_body:
            vehicle_chassis( g, k, seed );
            g.box( 0, 4, 0, 8, 11, 8, k.color, 0.05f, seed + 1 );
            g.box( 0, 7, 0, 8, 8, 8, voxel_shade( k.color, 0.85f ) );
            break;
        case vm_kind::veh_window:
            vehicle_chassis( g, k, seed );
            g.box( 0, 4, 0, 8, 7, 8, k.color, 0.05f, seed + 1 );
            g.box( 0, 7, 0, 8, 11, 8, rgb( 140, 180, 210 ), 0.0f, 0, true );
            g.box( 0, 11, 0, 8, 12, 8, k.color );
            break;
        case vm_kind::veh_seat:
            vehicle_chassis( g, k, seed );
            g.box( 1, 4, 1, 7, 6, 7, k.color2, 0.06f, seed + 1 );
            g.box( 1, 6, 6, 7, 10, 8, k.color2, 0.06f, seed + 2 );
            break;
        default:
            vehicle_chassis( g, k, seed );
            g.box( 2, 4, 2, 6, 7, 6, k.color2, 0.08f, seed + 1 );
            break;
    }
    return g;
}

voxel_grid make_fire( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 12, 8 );
    const int strength = 1 + k.variant;
    for( int z = 1; z < 7; ++z ) {
        for( int x = 1; x < 7; ++x ) {
            const float dx = x + 0.5f - 4.0f;
            const float dz = z + 0.5f - 4.0f;
            const float center = 1.0f - std::sqrt( dx * dx + dz * dz ) / 4.0f;
            const int h = static_cast<int>( ( 1.0f + 3.5f * strength * center ) * ( 0.5f + voxel_noise( x, 0, z,
                                            seed ) ) );
            for( int y = 0; y < std::min( h, 12 ); ++y ) {
                const float t = static_cast<float>( y ) / std::max( 1, h );
                g.set( x, y, z, voxel_mix( rgb( 230, 70, 20 ), rgb( 255, 220, 90 ), t ) );
            }
        }
    }
    return g;
}

voxel_grid make_splat( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 1, 8 );
    for( int z = 0; z < 8; ++z ) {
        for( int x = 0; x < 8; ++x ) {
            const float dx = x + 0.5f - 4.0f;
            const float dz = z + 0.5f - 4.0f;
            if( voxel_noise( x, 0, z, seed ) < 0.55f - ( dx * dx + dz * dz ) / 40.0f ) {
                g.set( x, 0, z, voxel_shade( k.color, nz( x, 0, z, seed + 1, 0.15f ) ) );
            }
        }
    }
    return g;
}

voxel_grid make_cloud( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 16, 8 );
    for( int y = 1; y < 15; ++y ) {
        for( int z = 0; z < 8; ++z ) {
            for( int x = 0; x < 8; ++x ) {
                if( voxel_noise( x, y, z, seed ) < 0.06f + 0.02f * k.variant ) {
                    g.set( x, y, z, k.color, true );
                }
            }
        }
    }
    return g;
}

voxel_grid make_cursor( const vm_key &k )
{
    voxel_grid g( 8, 17, 8 );
    for( int y = 0; y < 17; ++y ) {
        for( int z = 0; z < 8; ++z ) {
            for( int x = 0; x < 8; ++x ) {
                const int edges = ( x == 0 || x == 7 ) + ( z == 0 || z == 7 ) + ( y == 0 || y == 16 );
                if( edges >= 2 ) {
                    g.set( x, y, z, k.color );
                }
            }
        }
    }
    return g;
}

voxel_grid make_machine( const vm_key &k, uint32_t seed )
{
    const bool tall = k.param == 1;
    voxel_grid g( 8, 16, 8 );
    const int top = tall ? 14 : 10;
    const int x0 = tall ? 2 : 1;
    const int x1 = tall ? 6 : 7;
    g.box( x0, 1, 1, x1, top, 7, k.color, 0.04f, seed );
    g.box( x0, 0, 1, x1, 1, 7, voxel_shade( k.color, 0.6f ) );
    for( int z = 2; z < 6; z += 2 ) {
        for( int y = 6; y < 9; ++y ) {
            g.set( x0 - 1 < 0 ? 0 : x0, y, z, voxel_shade( k.color, 0.55f ) );
            g.set( x1 - 1, y, z, voxel_shade( k.color, 0.55f ) );
        }
    }
    g.box( x0 + 1, top - 4, 0, x1 - 1, top - 2, 1, k.color2 );
    for( int x = x0; x < x1; ++x ) {
        g.set( x, top / 2, 0, voxel_shade( k.color, 0.75f ) );
    }
    if( tall ) {
        g.box( 6, 4, 3, 7, 10, 4, rgb( 30, 30, 30 ) );
        g.box( 2, top, 2, 6, top + 1, 6, voxel_shade( k.color2, 0.8f ) );
    }
    return g;
}

voxel_grid make_pole( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 26, 8 );
    g.box( 3, 0, 3, 5, 24, 5, k.color, 0.06f, seed );
    if( k.param == 1 ) {
        g.box( 0, 22, 3, 8, 23, 5, voxel_shade( k.color, 0.85f ) );
        g.set( 0, 23, 4, rgb( 200, 200, 190 ) );
        g.set( 7, 23, 4, rgb( 200, 200, 190 ) );
    } else if( k.param == 2 ) {
        for( int y = 17; y < 23; ++y ) {
            for( int x = 5; x < 8; ++x ) {
                g.set( x, y, 4, voxel_shade( k.color2, ( y % 2 ) ? 0.85f : 1.0f ) );
            }
        }
    } else if( k.param == 3 ) {
        g.sphere( 4.0f, 22.0f, 4.0f, 3.0f, 1.5f, 3.0f, voxel_shade( k.color, 0.9f ) );
    }
    return g;
}

voxel_grid make_stack( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 12, 8 );
    const int layers = 1 + k.param;
    for( int l = 0; l < layers * 2; ++l ) {
        for( int b = 0; b < 4; ++b ) {
            const voxel_rgb c = voxel_shade( k.color, 0.8f + 0.35f * voxel_noise( l, b, 0, seed ) );
            if( l % 2 == 0 ) {
                g.box( 0, l, b * 2, 8, l + 1, b * 2 + 2, c, 0.04f, seed + l );
            } else {
                g.box( b * 2, l, 0, b * 2 + 2, l + 1, 8, c, 0.04f, seed + l );
            }
        }
    }
    return g;
}

voxel_grid make_mound( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 8, 8 );
    const float h = 1.5f + 1.5f * k.param;
    g.sphere( 4.0f, 0.0f, 4.0f, 4.2f, h, 4.2f, k.color, 0.14f, seed );
    return g;
}

voxel_grid make_mat( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 1, 8 );
    for( int z = 0; z < 8; ++z ) {
        for( int x = 0; x < 8; ++x ) {
            const bool border = x == 0 || z == 0 || x == 7 || z == 7;
            g.set( x, 0, z, voxel_shade( k.color, ( border ? 0.82f : 1.0f ) * nz( x, 0, z, seed, 0.05f ) ) );
        }
    }
    return g;
}

voxel_grid make_panel( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 14, 8 );
    const int x1 = k.variant == 1 ? 2 : 8;
    for( int y = 0; y < 14; ++y ) {
        for( int x = 0; x < x1; ++x ) {
            const int t = 3 + ( ( x + y / 5 ) % 3 == 0 ? 1 : 0 );
            g.set( x, y, t, voxel_shade( k.color, ( x % 3 == 0 ? 0.88f : 1.0f ) * nz( x, y, 0, seed, 0.04f ) ) );
        }
    }
    if( k.param & 1 ) {
        swap_xz( g );
    }
    return g;
}

voxel_grid make_pillar( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, lh, 8 );
    g.box( 2, 0, 2, 6, lh, 6, k.color, 0.05f, seed );
    g.box( 1, 0, 1, 7, 2, 7, voxel_shade( k.color, 0.9f ), 0.04f, seed + 1 );
    g.box( 1, lh - 2, 1, 7, lh, 7, voxel_shade( k.color, 0.9f ), 0.04f, seed + 2 );
    return g;
}

voxel_grid make_stump( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 4, 8 );
    for( int y = 0; y < 3; ++y ) {
        for( int z = 0; z < 8; ++z ) {
            for( int x = 0; x < 8; ++x ) {
                const float dx = x + 0.5f - 4.0f;
                const float dz = z + 0.5f - 4.0f;
                const float d = dx * dx + dz * dz;
                if( d <= 8.5f ) {
                    const bool top = y == 2;
                    g.set( x, y, z, top ? voxel_shade( rgb( 190, 150, 100 ), d < 3.0f ? 0.85f : 1.0f ) :
                           voxel_shade( k.color, nz( x, y, z, seed, 0.1f ) ) );
                }
            }
        }
    }
    return g;
}

voxel_grid make_ramp( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, lh, 8 );
    for( int z = 0; z < 8; ++z ) {
        const int h = std::max( 1, ( 8 - z ) * ( k.param == 1 ? 2 : 1 ) );
        g.box( 0, 0, z, 8, h, z + 1, k.color, 0.06f, seed + z );
    }
    return g;
}

voxel_grid make_grave( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 9, 8 );
    g.sphere( 4.0f, 0.0f, 3.5f, 3.0f, 1.5f, 3.5f, rgb( 110, 82, 56 ), 0.15f, seed );
    g.box( 2, 0, 6, 6, 7, 7, k.color, 0.06f, seed + 1 );
    g.box( 3, 7, 6, 5, 8, 7, k.color, 0.06f, seed + 2 );
    return g;
}

voxel_grid make_anvil( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 6, 8 );
    g.box( 2, 0, 2, 6, 1, 6, voxel_shade( k.color, 0.8f ) );
    g.box( 3, 1, 3, 5, 3, 5, k.color, 0.04f, seed );
    g.box( 1, 3, 2, 7, 5, 6, k.color, 0.04f, seed + 1 );
    g.box( 0, 4, 3, 1, 5, 5, k.color );
    return g;
}

voxel_grid make_hedge( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 12, 8 );
    const int h = k.param == 1 ? 12 : 6;
    for( int y = 0; y < h; ++y ) {
        for( int z = 0; z < 8; ++z ) {
            for( int x = 0; x < 8; ++x ) {
                const bool edge = x == 0 || z == 0 || x == 7 || z == 7 || y == h - 1;
                if( !edge || voxel_noise( x, y, z, seed ) > 0.25f ) {
                    g.set( x, y, z, voxel_shade( k.color, nz( x, y, z, seed + 3, 0.15f ) ) );
                }
            }
        }
    }
    return g;
}

voxel_grid make_trap( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 3, 8 );
    for( int z = 1; z < 7; ++z ) {
        for( int x = 1; x < 7; ++x ) {
            const float dx = x + 0.5f - 4.0f;
            const float dz = z + 0.5f - 4.0f;
            const float d = dx * dx + dz * dz;
            if( d < 9.0f && d > 4.0f ) {
                g.set( x, 0, z, voxel_shade( k.color, nz( x, 0, z, seed, 0.08f ) ) );
                if( ( x + z ) % 2 == 0 ) {
                    g.set( x, 1, z, voxel_shade( k.color, 1.15f ) );
                }
            } else if( d <= 1.5f ) {
                g.set( x, 0, z, voxel_shade( k.color, 0.7f ) );
            }
        }
    }
    return g;
}

voxel_grid make_vehicle_composite( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 14, 8 );
    const uint8_t f = k.param;
    const uint8_t x = k.variant;
    const voxel_rgb body = k.color;
    const voxel_rgb dark = voxel_shade( body, 0.65f );
    const voxel_rgb glass = rgb( 140, 180, 210 );
    const bool broken = ( x & vmx_broken ) != 0;
    g.box( 0, 2, 0, 8, 4, 8, dark, 0.05f, seed );
    if( f & vmv_wheel ) {
        g.box( 2, 0, 1, 6, 5, 7, rgb( 28, 28, 30 ), 0.05f, seed + 1 );
        g.box( 1, 1, 3, 7, 3, 5, rgb( 160, 160, 165 ) );
    }
    if( f & vmv_engine ) {
        g.box( 1, 4, 1, 7, 8, 7, rgb( 70, 72, 76 ), 0.08f, seed + 2 );
        g.box( 2, 8, 2, 6, 9, 6, rgb( 110, 60, 40 ) );
    }
    if( x & vmx_cargo ) {
        g.box( 1, 4, 1, 7, 7, 7, voxel_shade( body, 0.85f ), 0.05f, seed + 3 );
    }
    if( x & vmx_tank ) {
        g.sphere( 4.0f, 6.0f, 4.0f, 3.0f, 2.0f, 3.5f, rgb( 120, 120, 90 ), 0.05f, seed + 4 );
    }
    if( f & vmv_seat ) {
        g.box( 1, 4, 1, 7, 6, 7, k.color2, 0.06f, seed + 5 );
        g.box( 1, 6, 6, 7, 10, 8, k.color2, 0.06f, seed + 6 );
    }
    const bool door_closed = ( x & vmx_door ) != 0;
    if( ( f & vmv_board ) || door_closed ) {
        g.box( 0, 4, 0, 8, 11, 8, body, 0.04f, seed + 7 );
        g.box( 0, 7, 0, 8, 8, 8, voxel_shade( body, 0.88f ) );
        if( door_closed ) {
            for( int y = 4; y < 11; ++y ) {
                g.set( 0, y, 1, dark );
                g.set( 7, y, 1, dark );
            }
            g.set( 0, 7, 5, rgb( 200, 200, 200 ) );
            g.set( 7, 7, 5, rgb( 200, 200, 200 ) );
        }
    } else if( f & vmv_window ) {
        g.box( 0, 4, 0, 8, 7, 8, body, 0.04f, seed + 8 );
        g.box( 0, 7, 0, 8, 11, 8, glass, 0.0f, 0, true );
        if( broken ) {
            for( int y = 7; y < 11; ++y ) {
                for( int z = 0; z < 8; ++z ) {
                    for( int xx = 0; xx < 8; ++xx ) {
                        if( voxel_noise( xx, y, z, seed ) < 0.6f ) {
                            g.clear( xx, y, z );
                        }
                    }
                }
            }
        }
    }
    if( ( f & vmv_roof ) || ( f & ( vmv_board | vmv_window ) ) ) {
        if( f & vmv_roof ) {
            g.box( 0, 11, 0, 8, 12, 8, body, 0.04f, seed + 9 );
        }
    }
    if( f & vmv_light ) {
        g.box( 1, 5, 0, 3, 7, 1, rgb( 255, 245, 200 ) );
        g.box( 5, 5, 0, 7, 7, 1, rgb( 255, 245, 200 ) );
    }
    if( x & vmx_solar ) {
        g.box( 0, 12, 0, 8, 13, 8, rgb( 30, 40, 90 ) );
        for( int i = 0; i < 8; i += 3 ) {
            g.box( i, 12, 0, i + 1, 13, 8, rgb( 150, 150, 160 ) );
        }
    }
    if( f & vmv_turret ) {
        const int base = ( f & vmv_roof ) ? 12 : 4;
        g.box( 2, base, 2, 6, base + 2, 6, rgb( 70, 75, 70 ) );
        g.box( 3, base + 2, 0, 5, base + 3, 4, rgb( 40, 40, 40 ) );
    }
    if( broken ) {
        for( int i = 0; i < 20; ++i ) {
            const int bx = static_cast<int>( voxel_hash( i, 0, 0, seed ) % 8 );
            const int by = 2 + static_cast<int>( voxel_hash( i, 1, 0, seed ) % 10 );
            const int bz = static_cast<int>( voxel_hash( i, 2, 0, seed ) % 8 );
            if( g.solid( bx, by, bz ) ) {
                g.set( bx, by, bz, rgb( 60, 45, 35 ) );
            }
        }
    }
    return g;
}

voxel_grid make_item( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 4, 8 );
    const voxel_rgb c = k.color;
    const voxel_rgb metal = rgb( 90, 92, 98 );
    const voxel_rgb wood = rgb( 120, 85, 55 );
    switch( k.kind ) {
        case vm_kind::item_gun:
            g.box( 6, 0, 3, 8, 2, 5, wood, 0.05f, seed );
            g.box( 3, 0, 3, 6, 2, 5, voxel_mix( c, metal, 0.6f ), 0.04f, seed + 1 );
            g.box( 0, 1, 4, 3, 2, 5, metal );
            g.box( 4, 0, 5, 5, 1, 6, wood );
            break;
        case vm_kind::item_blade:
            g.box( 5, 0, 4, 8, 1, 5, wood );
            g.box( 5, 0, 3, 6, 1, 6, metal );
            g.box( 0, 0, 4, 5, 1, 5, voxel_mix( c, rgb( 210, 215, 220 ), 0.6f ) );
            break;
        case vm_kind::item_tool:
            g.box( 2, 0, 4, 8, 1, 5, voxel_mix( c, wood, 0.5f ) );
            g.box( 0, 0, 2, 2, 2, 7, metal, 0.04f, seed );
            break;
        case vm_kind::item_clothing:
            g.box( 1, 0, 2, 7, 1, 6, c, 0.06f, seed );
            g.box( 2, 1, 2, 6, 2, 5, voxel_shade( c, 0.92f ), 0.06f, seed + 1 );
            break;
        case vm_kind::item_food:
            g.box( 2, 0, 3, 5, 3, 6, c, 0.05f, seed );
            g.box( 2, 1, 3, 5, 2, 6, voxel_shade( c, 1.2f ) );
            g.box( 5, 0, 2, 7, 2, 4, voxel_shade( c, 0.85f ) );
            break;
        case vm_kind::item_bottle:
            g.box( 3, 0, 3, 5, 3, 5, c, 0.04f, seed );
            g.box( 3, 3, 3, 4, 4, 4, voxel_shade( c, 0.8f ) );
            break;
        case vm_kind::item_book:
            g.box( 2, 0, 2, 6, 1, 7, c, 0.04f, seed );
            g.box( 2, 0, 2, 3, 1, 7, rgb( 235, 230, 215 ) );
            break;
        case vm_kind::item_ammo:
            g.box( 2, 0, 3, 6, 2, 6, c, 0.04f, seed );
            g.set( 3, 2, 4, rgb( 200, 160, 70 ) );
            g.set( 4, 2, 4, rgb( 200, 160, 70 ) );
            break;
        case vm_kind::item_corpse:
            g = voxel_grid( 8, 3, 8 );
            g.box( 2, 0, 0, 6, 2, 2, voxel_shade( c, 0.95f ), 0.08f, seed );
            g.box( 2, 0, 2, 6, 2, 5, c, 0.1f, seed + 1 );
            g.box( 2, 0, 5, 4, 1, 8, voxel_shade( c, 0.85f ), 0.08f, seed + 2 );
            g.box( 4, 0, 5, 6, 1, 8, voxel_shade( c, 0.8f ), 0.08f, seed + 3 );
            g.box( 0, 0, 2, 2, 1, 4, voxel_shade( c, 0.9f ) );
            g.box( 6, 0, 2, 8, 1, 4, voxel_shade( c, 0.9f ) );
            for( int i = 0; i < 6; ++i ) {
                g.set( static_cast<int>( voxel_hash( i, 0, 0, seed ) % 8 ), 0,
                       static_cast<int>( voxel_hash( i, 1, 0, seed ) % 8 ), rgb( 110, 15, 15 ) );
            }
            break;
        default:
            break;
    }
    return g;
}

voxel_grid make_held( const vm_key &k, uint32_t seed )
{
    voxel_grid g( 8, 10, 8 );
    const voxel_rgb metal = rgb( 80, 82, 88 );
    const voxel_rgb wood = rgb( 120, 85, 55 );
    switch( k.param ) {
        case 0:
            g.box( 6, 6, 3, 7, 8, 6, wood );
            g.box( 6, 7, 0, 7, 8, 3, voxel_mix( k.color, metal, 0.6f ), 0.04f, seed );
            break;
        case 1:
            g.box( 6, 5, 3, 7, 6, 5, wood );
            g.box( 6, 5, 0, 7, 6, 3, voxel_mix( k.color, rgb( 210, 215, 220 ), 0.6f ) );
            break;
        default:
            g.box( 6, 5, 2, 7, 6, 5, voxel_mix( k.color, wood, 0.5f ) );
            g.box( 6, 5, 0, 7, 7, 2, metal );
            break;
    }
    return g;
}

voxel_grid make_marker( const vm_key &k )
{
    voxel_grid g( 8, 6, 8 );
    if( k.kind == vm_kind::bolt ) {
        g.box( 3, 3, 3, 5, 5, 5, k.color );
    } else {
        for( int z = 1; z < 7; ++z ) {
            for( int x = 1; x < 7; ++x ) {
                if( x == 1 || x == 6 || z == 1 || z == 6 ) {
                    g.set( x, 0, z, k.color );
                }
            }
        }
        g.box( 3, 0, 3, 5, 1, 5, k.color );
    }
    return g;
}

voxel_grid make_particle( const vm_key &k )
{
    voxel_grid g( 8, 6, 8 );
    if( k.kind == vm_kind::rain ) {
        g.box( 2, 0, 3, 3, 5, 4, k.color );
        g.box( 6, 2, 6, 7, 6, 7, k.color );
    } else {
        g.set( 2, 0, 3, k.color );
        g.set( 6, 3, 6, k.color );
        g.set( 4, 5, 1, k.color );
    }
    return g;
}

} // namespace

uint64_t vm_key::pack() const
{
    return ( static_cast<uint64_t>( kind ) << 56 ) | ( static_cast<uint64_t>( material ) << 50 ) |
           ( static_cast<uint64_t>( variant ) << 42 ) | ( static_cast<uint64_t>( param ) << 34 ) |
           ( quant( color ) << 17 ) | ( quant( color2 ) << 2 );
}

const char *vm_kind_name( vm_kind kind )
{
    static const std::array<const char *, vm_kind_count> names = { {
            "floor", "wall", "window", "door", "door_open", "tree", "pine", "dead_tree", "young_tree",
            "shrub", "flower", "tall_grass", "water", "stairs_down", "stairs_up", "hole", "fence", "rubble",
            "boulder", "bed", "table", "chair", "sofa", "counter", "cabinet", "toilet", "sink", "bathtub",
            "stove", "screen", "crate", "barrel", "dumpster", "sign", "lamp", "furniture", "humanoid",
            "zombie", "quadruped", "bird", "bug", "robot", "blob", "fish", "plant_creature", "item_pile",
            "veh_frame", "veh_wheel", "veh_body", "veh_window", "veh_seat", "veh_part", "fire", "splat",
            "cloud", "cursor", "machine", "pole", "stack", "mound", "mat", "panel", "pillar", "stump",
            "ramp", "grave", "anvil", "hedge", "trap", "vehicle", "item_gun", "item_blade", "item_tool",
            "item_clothing", "item_food", "item_bottle", "item_book", "item_ammo", "item_corpse", "held",
            "rain", "snow", "bolt", "marker"
        }
    };
    return names[static_cast<int>( kind )];
}

const char *vm_material_name( vm_material material )
{
    static const std::array<const char *, vm_material_count> names = { {
            "generic", "grass", "dirt", "sand", "pavement", "sidewalk", "concrete", "wood", "tile",
            "carpet", "rock", "metal", "brick", "glass", "snow", "log"
        }
    };
    return names[static_cast<int>( material )];
}

bool vm_is_emissive( vm_kind kind )
{
    return kind == vm_kind::fire || kind == vm_kind::cursor || kind == vm_kind::rain ||
           kind == vm_kind::snow || kind == vm_kind::bolt || kind == vm_kind::marker;
}

voxel_grid make_voxel_model( const vm_key &raw )
{
    vm_key k = raw;
    k.color = dequant( raw.color );
    k.color2 = dequant( raw.color2 );
    const uint32_t seed = static_cast<uint32_t>( k.variant ) * 7919u + static_cast<uint32_t>
                          ( k.kind ) * 31u + 17u;
    switch( k.kind ) {
        case vm_kind::floor:
            return make_floor( k, seed );
        case vm_kind::wall:
            return make_wall( k, seed );
        case vm_kind::window:
            return make_window( k, seed );
        case vm_kind::door:
            return make_door( k, false, seed );
        case vm_kind::door_open:
            return make_door( k, true, seed );
        case vm_kind::tree:
            return make_tree( k, seed );
        case vm_kind::pine:
            return make_pine( k, seed );
        case vm_kind::dead_tree:
            return make_dead_tree( k, seed );
        case vm_kind::young_tree:
            return make_young_tree( k, seed );
        case vm_kind::shrub:
            return make_shrub( k, seed );
        case vm_kind::flower:
            return make_flower( k, seed );
        case vm_kind::tall_grass:
            return make_tall_grass( k, seed );
        case vm_kind::water:
            return make_water( k, seed );
        case vm_kind::stairs_down:
            return make_stairs_down( k, seed );
        case vm_kind::stairs_up:
            return make_stairs_up( k, seed );
        case vm_kind::hole:
            return make_hole( k, seed );
        case vm_kind::fence:
            return make_fence( k, seed );
        case vm_kind::rubble:
            return make_rubble( k, seed );
        case vm_kind::boulder:
            return make_boulder( k, seed );
        case vm_kind::bed:
            return make_bed( k, seed );
        case vm_kind::table:
            return make_table( k, seed );
        case vm_kind::chair:
            return make_chair( k, seed );
        case vm_kind::sofa:
            return make_sofa( k, seed );
        case vm_kind::counter:
            return make_counter( k, seed );
        case vm_kind::cabinet:
            return make_cabinet( k, seed );
        case vm_kind::toilet:
            return make_toilet( k, seed );
        case vm_kind::sink:
            return make_sink( k, seed );
        case vm_kind::bathtub:
            return make_bathtub( k, seed );
        case vm_kind::stove:
            return make_stove( k, seed );
        case vm_kind::screen:
            return make_screen( k, seed );
        case vm_kind::crate:
            return make_crate( k, seed );
        case vm_kind::barrel:
            return make_barrel( k, seed );
        case vm_kind::dumpster:
            return make_dumpster( k, seed );
        case vm_kind::sign:
            return make_sign( k, seed );
        case vm_kind::lamp:
            return make_lamp( k, seed );
        case vm_kind::furniture:
            return make_furniture( k, seed );
        case vm_kind::humanoid:
            return make_humanoid( k, seed, false );
        case vm_kind::zombie:
            return make_humanoid( k, seed, true );
        case vm_kind::quadruped:
            return make_quadruped( k, seed );
        case vm_kind::bird:
            return make_bird( k, seed );
        case vm_kind::bug:
            return make_bug( k, seed );
        case vm_kind::robot:
            return make_robot( k, seed );
        case vm_kind::blob:
            return make_blob( k, seed );
        case vm_kind::fish:
            return make_fish( k, seed );
        case vm_kind::plant_creature:
            return make_plant_creature( k, seed );
        case vm_kind::item_pile:
            return make_item_pile( k, seed );
        case vm_kind::veh_frame:
        case vm_kind::veh_wheel:
        case vm_kind::veh_body:
        case vm_kind::veh_window:
        case vm_kind::veh_seat:
        case vm_kind::veh_part:
            return make_vehicle( k, seed );
        case vm_kind::fire:
            return make_fire( k, seed );
        case vm_kind::splat:
            return make_splat( k, seed );
        case vm_kind::cloud:
            return make_cloud( k, seed );
        case vm_kind::cursor:
            return make_cursor( k );
        case vm_kind::machine:
            return make_machine( k, seed );
        case vm_kind::pole:
            return make_pole( k, seed );
        case vm_kind::stack:
            return make_stack( k, seed );
        case vm_kind::mound:
            return make_mound( k, seed );
        case vm_kind::mat:
            return make_mat( k, seed );
        case vm_kind::panel:
            return make_panel( k, seed );
        case vm_kind::pillar:
            return make_pillar( k, seed );
        case vm_kind::stump:
            return make_stump( k, seed );
        case vm_kind::ramp:
            return make_ramp( k, seed );
        case vm_kind::grave:
            return make_grave( k, seed );
        case vm_kind::anvil:
            return make_anvil( k, seed );
        case vm_kind::hedge:
            return make_hedge( k, seed );
        case vm_kind::trap:
            return make_trap( k, seed );
        case vm_kind::vehicle:
            return make_vehicle_composite( k, seed );
        case vm_kind::item_gun:
        case vm_kind::item_blade:
        case vm_kind::item_tool:
        case vm_kind::item_clothing:
        case vm_kind::item_food:
        case vm_kind::item_bottle:
        case vm_kind::item_book:
        case vm_kind::item_ammo:
        case vm_kind::item_corpse:
            return make_item( k, seed );
        case vm_kind::held:
            return make_held( k, seed );
        case vm_kind::rain:
        case vm_kind::snow:
            return make_particle( k );
        case vm_kind::bolt:
        case vm_kind::marker:
            return make_marker( k );
    }
    return make_furniture( k, seed );
}
