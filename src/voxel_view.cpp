#include "voxel_view.h"

#if defined(TILES)

#include <algorithm>
#include <array>
#include <clocale>
#include <cmath>
#include <string>
#include <map>
#include <unordered_map>
#include <utility>

#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#include "avatar.h"
#include "bodypart.h"
#include "cached_options.h"
#include "calendar.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "field.h"
#include "field_type.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "map.h"
#include "map_memory.h"
#include "mapdata.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options.h"
#include "sdl_utils.h"
#include "trap.h"
#include "type_id.h"
#include "uistate.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"
#include "voxel_gl.h"
#include "voxel_model.h"
#include "voxel_models.h"
#include "voxel_sprites.h"
#include "voxel_tileset.h"
#include "voxel_vox.h"
#include "vpart_position.h"
#include "weather.h"
#include "weather_type.h"

namespace
{

constexpr float level_height = voxel_level_height * voxel_size;
constexpr float pi = 3.14159265358979f;

struct vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

vec3 operator+( const vec3 &a, const vec3 &b )
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

vec3 operator-( const vec3 &a, const vec3 &b )
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

vec3 operator*( const vec3 &a, float s )
{
    return { a.x * s, a.y * s, a.z * s };
}

float dot( const vec3 &a, const vec3 &b )
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3 cross( const vec3 &a, const vec3 &b )
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

vec3 normalize( const vec3 &a )
{
    const float l = std::sqrt( dot( a, a ) );
    return l > 0.0f ? a * ( 1.0f / l ) : a;
}

struct mat4 {
    std::array<float, 16> m = {};

    static mat4 identity() {
        mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
};

mat4 operator*( const mat4 &a, const mat4 &b )
{
    mat4 r;
    for( int c = 0; c < 4; ++c ) {
        for( int row = 0; row < 4; ++row ) {
            float s = 0.0f;
            for( int k = 0; k < 4; ++k ) {
                s += a.m[k * 4 + row] * b.m[c * 4 + k];
            }
            r.m[c * 4 + row] = s;
        }
    }
    return r;
}

mat4 perspective( float fovy, float aspect, float znear, float zfar )
{
    const float f = 1.0f / std::tan( fovy / 2.0f );
    mat4 r;
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = ( zfar + znear ) / ( znear - zfar );
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * zfar * znear / ( znear - zfar );
    return r;
}

mat4 ortho( float l, float r, float b, float t, float n, float f )
{
    mat4 o = mat4::identity();
    o.m[0] = 2.0f / ( r - l );
    o.m[5] = 2.0f / ( t - b );
    o.m[10] = -2.0f / ( f - n );
    o.m[12] = -( r + l ) / ( r - l );
    o.m[13] = -( t + b ) / ( t - b );
    o.m[14] = -( f + n ) / ( f - n );
    return o;
}

mat4 look_at( const vec3 &eye, const vec3 &target, const vec3 &up )
{
    const vec3 f = normalize( target - eye );
    const vec3 s = normalize( cross( f, up ) );
    const vec3 u = cross( s, f );
    mat4 r = mat4::identity();
    r.m[0] = s.x;
    r.m[4] = s.y;
    r.m[8] = s.z;
    r.m[1] = u.x;
    r.m[5] = u.y;
    r.m[9] = u.z;
    r.m[2] = -f.x;
    r.m[6] = -f.y;
    r.m[10] = -f.z;
    r.m[12] = -dot( s, eye );
    r.m[13] = -dot( u, eye );
    r.m[14] = dot( f, eye );
    return r;
}

bool invert( const mat4 &in, mat4 &out )
{
    const std::array<float, 16> &m = in.m;
    std::array<float, 16> inv;
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if( std::fabs( det ) < 1e-12f ) {
        return false;
    }
    det = 1.0f / det;
    for( int i = 0; i < 16; ++i ) {
        out.m[i] = inv[i] * det;
    }
    return true;
}

struct instance {
    float x;
    float y;
    float z;
    float yaw;
    float r;
    float g;
    float b;
    float light;
    float scale;
    float emissive;
    float desat;
    float pad;
};

struct gpu_mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLsizei count = 0;
};

struct gpu_model {
    gpu_mesh opaque;
    gpu_mesh glass;
    bool emissive = false;
};

struct instance_lists {
    std::vector<std::vector<instance>> per_model;
    std::vector<int> touched;

    void add( int model, const instance &inst ) {
        if( model >= static_cast<int>( per_model.size() ) ) {
            per_model.resize( model + 1 );
        }
        if( per_model[model].empty() ) {
            touched.push_back( model );
        }
        per_model[model].push_back( inst );
    }

    void clear() {
        for( int m : touched ) {
            per_model[m].clear();
        }
        touched.clear();
    }
};

struct tile_style {
    bool draw = true;
    vm_kind kind = vm_kind::floor;
    vm_material material = vm_material::generic;
    voxel_rgb color;
    voxel_rgb color2;
    uint8_t variant = 0;
    uint8_t param = 0;
    uint8_t variants = 1;
    bool overlay = false;
    vm_material floor_material = vm_material::grass;
    voxel_rgb floor_color;
    bool wall_like = false;
    bool oriented = false;
    bool fence = false;
    bool stripes = false;
    bool spin = false;
    bool no_floor = false;
    const voxelset_entry *custom = nullptr;
};

struct camera_state {
    float yaw = 0.0f;
    float pitch = 0.95f;
    bool initialized = false;
    mat4 view_proj;
    int width = 0;
    int height = 0;
    int center_z = 0;
    bool valid = false;
};

camera_state cam;
voxel_view::overlays overlay_state;

constexpr voxel_rgb rgb( uint8_t r, uint8_t g, uint8_t b )
{
    return { r, g, b };
}

voxel_rgb to_rgb( const nc_color &c )
{
    const SDL_Color s = curses_color_to_SDL( c );
    return { s.r, s.g, s.b };
}

voxel_rgb soften( const voxel_rgb &c )
{
    return voxel_mix( c, rgb( 128, 124, 118 ), 0.3f );
}

bool has( const std::string &s, const char *key )
{
    return s.find( key ) != std::string::npos;
}

template<typename... Keys>
bool has_any( const std::string &s, Keys... keys )
{
    return ( has( s, keys ) || ... );
}

vm_material material_from_id( const std::string &id, vm_material fallback )
{
    if( has_any( id, "brick" ) ) {
        return vm_material::brick;
    }
    if( has_any( id, "log", "wood", "plank" ) ) {
        return vm_material::wood;
    }
    if( has_any( id, "metal", "steel", "scrap" ) ) {
        return vm_material::metal;
    }
    if( has_any( id, "concrete", "cement", "cinder" ) ) {
        return vm_material::concrete;
    }
    if( has_any( id, "glass" ) ) {
        return vm_material::glass;
    }
    if( has_any( id, "rock", "stone" ) ) {
        return vm_material::rock;
    }
    return fallback;
}

voxel_rgb material_color( vm_material m, const voxel_rgb &fallback )
{
    switch( m ) {
        case vm_material::brick:
            return rgb( 150, 72, 55 );
        case vm_material::wood:
        case vm_material::log:
            return rgb( 135, 98, 62 );
        case vm_material::metal:
            return rgb( 125, 130, 136 );
        case vm_material::concrete:
            return rgb( 158, 156, 150 );
        case vm_material::glass:
            return rgb( 150, 200, 230 );
        case vm_material::rock:
            return rgb( 108, 102, 96 );
        case vm_material::grass:
            return rgb( 80, 128, 52 );
        case vm_material::dirt:
            return rgb( 112, 84, 56 );
        case vm_material::sand:
            return rgb( 205, 185, 130 );
        case vm_material::pavement:
            return rgb( 62, 62, 66 );
        case vm_material::sidewalk:
            return rgb( 165, 163, 156 );
        case vm_material::tile:
            return voxel_mix( fallback, rgb( 220, 220, 214 ), 0.5f );
        case vm_material::snow:
            return rgb( 232, 236, 242 );
        case vm_material::carpet:
        case vm_material::generic:
            break;
    }
    return fallback;
}

void classify_floor( const std::string &id, const voxel_rgb &base, tile_style &st )
{
    st.kind = vm_kind::floor;
    st.variants = 4;
    st.spin = true;
    if( has_any( id, "grass", "lawn", "moss", "fungus" ) ) {
        st.material = vm_material::grass;
        st.color = has( id, "dead" ) ? rgb( 132, 120, 70 ) : has( id, "fungus" ) ? rgb( 120, 120,
                   100 ) : rgb( 80, 128, 52 );
    } else if( has_any( id, "pavement", "asphalt", "road", "parking" ) ) {
        st.material = vm_material::pavement;
        st.color = material_color( st.material, base );
        st.stripes = has( id, "_y" );
        st.spin = false;
    } else if( has_any( id, "sidewalk" ) ) {
        st.material = vm_material::sidewalk;
        st.color = material_color( st.material, base );
    } else if( has_any( id, "concrete", "thconc", "cement", "bunker", "lab" ) ) {
        st.material = vm_material::concrete;
        st.color = material_color( st.material, base );
    } else if( has_any( id, "linoleum", "tile", "marble" ) ) {
        st.material = vm_material::tile;
        st.color = material_color( st.material, base );
    } else if( has_any( id, "carpet", "rug" ) ) {
        st.material = vm_material::carpet;
        st.color = base;
    } else if( has_any( id, "metal", "grate", "steel" ) ) {
        st.material = vm_material::metal;
        st.color = material_color( vm_material::metal, base );
    } else if( has_any( id, "sand", "beach" ) ) {
        st.material = vm_material::sand;
        st.color = material_color( st.material, base );
    } else if( has_any( id, "snow", "ice" ) ) {
        st.material = vm_material::snow;
        st.color = material_color( st.material, base );
    } else if( has_any( id, "rock", "stone", "gravel", "slab" ) ) {
        st.material = vm_material::rock;
        st.color = material_color( st.material, base );
    } else if( has_any( id, "dirt", "mud", "soil", "clay", "pit", "furrow", "trail" ) ) {
        st.material = vm_material::dirt;
        st.color = has( id, "clay" ) ? rgb( 150, 92, 60 ) : material_color( vm_material::dirt, base );
    } else if( has_any( id, "floor", "wood", "plank", "boards" ) ) {
        st.material = vm_material::wood;
        st.color = rgb( 140, 104, 68 );
        st.spin = false;
    } else {
        st.material = vm_material::generic;
        st.color = base;
    }
}

tile_style classify_terrain( const ter_t &t )
{
    tile_style st;
    const std::string id = t.id.str();
    const voxel_rgb base = soften( to_rgb( t.color() ) );
    st.color = base;
    st.color2 = rgb( 95, 70, 50 );

    if( has( id, "treetop" ) || ( t.has_flag( ter_furn_flag::TFLAG_NO_FLOOR ) &&
                                  !t.has_flag( ter_furn_flag::TFLAG_WALL ) ) ) {
        st.draw = false;
        st.no_floor = true;
        return st;
    }
    const auto vegetation_floor = [&]() {
        st.overlay = true;
        st.floor_material = has_any( id, "dirt", "underbrush" ) ? vm_material::dirt : vm_material::grass;
        st.floor_color = material_color( st.floor_material, base );
    };

    if( t.has_flag( ter_furn_flag::TFLAG_TREE ) || has( id, "t_tree" ) ) {
        vegetation_floor();
        st.variants = 6;
        st.spin = true;
        st.color = rgb( 70, 122, 46 );
        st.color2 = has( id, "birch" ) ? rgb( 205, 205, 195 ) : rgb( 92, 68, 48 );
        if( has_any( id, "dead", "burnt", "charred" ) ) {
            st.kind = vm_kind::dead_tree;
            st.color2 = rgb( 85, 72, 60 );
        } else if( has_any( id, "pine", "spruce", "fir", "hemlock", "cedar", "juniper", "conifer" ) ) {
            st.kind = vm_kind::pine;
            st.color = rgb( 38, 88, 50 );
        } else if( has_any( id, "young", "sapling" ) ) {
            st.kind = vm_kind::young_tree;
        } else {
            st.kind = vm_kind::tree;
            if( has_any( id, "apple", "cherry" ) ) {
                st.param = 1;
            } else if( has_any( id, "peach", "apricot", "orange" ) ) {
                st.param = 2;
            } else if( has_any( id, "pear", "lemon" ) ) {
                st.param = 3;
            } else if( has_any( id, "plum", "mulberry", "elderberry" ) ) {
                st.param = 4;
            }
        }
        return st;
    }
    if( t.has_flag( ter_furn_flag::TFLAG_SHRUB ) || has_any( id, "shrub", "underbrush", "bush" ) ) {
        vegetation_floor();
        st.kind = vm_kind::shrub;
        st.variants = 4;
        st.spin = true;
        st.color = rgb( 62, 112, 44 );
        if( has_any( id, "berry", "rose", "hibiscus", "flower" ) ) {
            st.param = 1;
            st.color2 = has( id, "blue" ) ? rgb( 60, 70, 180 ) : rgb( 190, 40, 60 );
        }
        return st;
    }
    if( t.has_flag( ter_furn_flag::TFLAG_FLOWER ) ) {
        vegetation_floor();
        st.kind = vm_kind::flower;
        st.variants = 4;
        return st;
    }
    if( has_any( id, "grass_long", "grass_tall", "tallgrass", "long_grass", "tall_grass" ) ) {
        vegetation_floor();
        st.kind = vm_kind::tall_grass;
        st.variants = 4;
        st.spin = true;
        st.color = rgb( 92, 140, 58 );
        return st;
    }
    if( t.has_flag( ter_furn_flag::TFLAG_DOOR ) || has( id, "door" ) ) {
        st.kind = ( has_any( id, "_o", "open" ) && !has( id, "_off" ) ) ? vm_kind::door_open : vm_kind::door;
        if( has( id, "broken" ) || has( id, "_b" ) ) {
            st.kind = vm_kind::door_open;
        }
        st.material = has( id, "glass" ) ? vm_material::glass : vm_material::generic;
        st.color = has_any( id, "metal", "steel", "bar", "vault" ) ? rgb( 120, 126, 132 ) : rgb( 128, 88,
                   54 );
        st.color2 = rgb( 176, 166, 150 );
        st.oriented = true;
        st.wall_like = true;
        return st;
    }
    if( t.has_flag( ter_furn_flag::TFLAG_WINDOW ) || has( id, "window" ) ) {
        st.kind = vm_kind::window;
        st.material = material_from_id( id, vm_material::generic );
        st.color = st.material == vm_material::generic ? rgb( 185, 175, 160 ) : material_color(
                       st.material, base );
        if( st.material == vm_material::glass ) {
            st.material = vm_material::generic;
            st.color = rgb( 185, 175, 160 );
        }
        st.color2 = rgb( 230, 230, 225 );
        if( has( id, "board" ) ) {
            st.variant = 3;
        } else if( has_any( id, "empty", "no_glass", "frame", "open" ) ) {
            st.variant = 2;
        } else if( has( id, "broken" ) ) {
            st.variant = 1;
        } else if( has_any( id, "curtain", "domestic" ) ) {
            st.variant = 4;
            st.color2 = rgb( 170, 60, 60 );
        }
        st.oriented = true;
        st.wall_like = true;
        return st;
    }
    if( has_any( id, "column", "pillar", "missile" ) && !has( id, "exploded" ) ) {
        st.kind = vm_kind::pillar;
        st.color = has( id, "missile" ) ? rgb( 200, 200, 195 ) : rgb( 190, 185, 175 );
        st.overlay = true;
        st.floor_material = vm_material::concrete;
        st.floor_color = material_color( vm_material::concrete, base );
        return st;
    }
    const bool glass_wall = has( id, "glass" ) && t.movecost == 0 && !has( id, "roof" );
    if( t.has_flag( ter_furn_flag::TFLAG_WALL ) || glass_wall || ( !t.has_flag(
                ter_furn_flag::TFLAG_TRANSPARENT ) &&
            t.movecost == 0 && !t.has_flag( ter_furn_flag::TFLAG_THIN_OBSTACLE ) ) ) {
        st.kind = vm_kind::wall;
        st.material = glass_wall ? vm_material::glass : material_from_id( id, vm_material::generic );
        if( id == "t_rock" || has_any( id, "t_rock_", "t_soil", "_cave", "natural", "root_wall",
                                       "rammed_earth" ) ) {
            st.material = vm_material::rock;
        }
        if( has_any( id, "sconc", "strconc", "bulkhead" ) ) {
            st.material = vm_material::concrete;
        }
        if( has_any( id, "wattle", "bark", "palisade" ) ) {
            st.material = vm_material::wood;
        }
        static const std::array<const char *, 12> paints = { {
                "_w", "_r", "_b", "_g", "_y", "_p", "_P", "_cyan", "_black", "_brown", "_gray", "_orange"
            }
        };
        bool painted = false;
        for( const char *suffix : paints ) {
            const std::string sfx( suffix );
            if( id.size() > sfx.size() && id.compare( id.size() - sfx.size(), sfx.size(), sfx ) == 0 ) {
                painted = true;
            }
        }
        if( painted && st.material != vm_material::brick ) {
            st.color = voxel_mix( base, rgb( 210, 205, 195 ), 0.2f );
        } else {
            st.color = st.material == vm_material::generic ? voxel_mix( base, rgb( 200, 190, 172 ),
                       0.55f ) : material_color( st.material, base );
        }
        if( has_any( id, "burnt", "charred" ) ) {
            st.color = voxel_shade( st.color, 0.4f );
        }
        if( has_any( id, "halfway", "_half" ) ) {
            st.param = 1;
        }
        st.wall_like = true;
        st.variants = 3;
        return st;
    }
    if( t.has_flag( ter_furn_flag::TFLAG_THIN_OBSTACLE ) || has_any( id, "fence", "railing", "rail" ) ) {
        st.overlay = true;
        st.floor_material = vm_material::grass;
        st.floor_color = material_color( vm_material::grass, base );
        if( has_any( id, "railing", "guardrail" ) ) {
            st.floor_material = vm_material::concrete;
            st.floor_color = material_color( vm_material::concrete, base );
        }
        st.kind = vm_kind::fence;
        st.fence = true;
        st.variant = has( id, "chain" ) ? 1 : 0;
        st.color = has( id, "chain" ) ? rgb( 150, 152, 156 ) : has_any( id, "metal", "railing",
                   "guardrail", "wire" ) ? rgb( 105, 108, 112 ) : rgb( 120, 88, 56 );
        return st;
    }
    if( t.has_flag( ter_furn_flag::TFLAG_GOES_UP ) ) {
        st.kind = vm_kind::stairs_up;
        st.color = rgb( 140, 120, 100 );
        st.color2 = rgb( 100, 90, 80 );
        return st;
    }
    if( t.has_flag( ter_furn_flag::TFLAG_GOES_DOWN ) ) {
        st.kind = has( id, "stairs" ) ? vm_kind::stairs_down : vm_kind::hole;
        st.color = rgb( 140, 120, 100 );
        st.color2 = rgb( 130, 125, 118 );
        return st;
    }
    if( t.has_flag( ter_furn_flag::TFLAG_SWIMMABLE ) || t.has_flag( ter_furn_flag::TFLAG_LIQUID ) ||
        t.has_flag( ter_furn_flag::TFLAG_DEEP_WATER ) || t.has_flag( ter_furn_flag::TFLAG_SHALLOW_WATER ) ||
        has( id, "water" ) ) {
        st.kind = vm_kind::water;
        st.variants = 4;
        st.spin = true;
        if( has_any( id, "sewage", "sludge" ) ) {
            st.color = rgb( 92, 96, 60 );
        } else if( has_any( id, "swamp", "murky" ) ) {
            st.color = rgb( 62, 92, 72 );
        } else if( t.has_flag( ter_furn_flag::TFLAG_DEEP_WATER ) || has( id, "deep" ) ) {
            st.color = rgb( 34, 72, 138 );
        } else {
            st.color = rgb( 62, 112, 162 );
        }
        return st;
    }
    if( t.has_flag( ter_furn_flag::TFLAG_RUBBLE ) || has_any( id, "rubble", "debris" ) ) {
        st.overlay = true;
        st.floor_material = vm_material::dirt;
        st.floor_color = material_color( vm_material::dirt, base );
        st.kind = vm_kind::rubble;
        st.variants = 4;
        st.spin = true;
        st.color = rgb( 130, 124, 116 );
        return st;
    }
    if( has_any( id, "gas_pump", "diesel_pump", "avgas_pump", "jp8_pump", "sewage_pump", "atm",
                 "ticket", "slot_machine", "vending", "radio_controls", "t_card", "card_reader", "intercom",
                 "t_switch", "elevator_control", "nanofab", "car_charger", "recycler", "milking", "nuclear_reactor",
                 "apiary", "beehive", "brick_oven", "covered_well" ) ) {
        st.overlay = true;
        st.floor_material = vm_material::concrete;
        st.floor_color = material_color( vm_material::concrete, base );
        st.kind = vm_kind::machine;
        st.color = has_any( id, "apiary", "beehive" ) ? rgb( 200, 170, 80 ) : has( id, "brick_oven" ) ?
                   rgb( 150, 72, 55 ) : voxel_mix( base, rgb( 150, 150, 155 ), 0.5f );
        st.color2 = has( id, "pump" ) ? rgb( 210, 60, 50 ) : rgb( 80, 180, 120 );
        st.param = has_any( id, "pump", "atm", "ticket", "vending", "slot" ) ? 1 : 0;
        return st;
    }
    if( has_any( id, "radio_tower", "utility_light", "tower" ) ) {
        st.overlay = true;
        st.floor_material = vm_material::concrete;
        st.floor_color = material_color( vm_material::concrete, base );
        st.kind = vm_kind::pole;
        st.color = rgb( 140, 140, 145 );
        st.param = 3;
        return st;
    }
    if( has_any( id, "stump", "t_trunk" ) ) {
        vegetation_floor();
        st.kind = vm_kind::stump;
        st.color = rgb( 92, 68, 48 );
        return st;
    }
    if( has( id, "grave" ) ) {
        st.overlay = true;
        st.floor_material = vm_material::grass;
        st.floor_color = material_color( vm_material::grass, base );
        st.kind = vm_kind::grave;
        st.color = rgb( 150, 148, 142 );
        return st;
    }
    if( has( id, "ramp" ) ) {
        st.kind = vm_kind::ramp;
        st.color = has( id, "earth" ) ? material_color( vm_material::dirt, base ) : rgb( 140, 138, 132 );
        st.param = has( id, "high" ) ? 1 : 0;
        return st;
    }
    if( has_any( id, "bamboo", "mega_fern", "fern" ) ) {
        vegetation_floor();
        st.kind = has( id, "fern" ) ? vm_kind::shrub : vm_kind::young_tree;
        st.color = rgb( 96, 150, 64 );
        st.color2 = rgb( 150, 170, 80 );
        st.spin = true;
        st.variants = 3;
        return st;
    }
    if( has_any( id, "slime", "goo", "tidepool" ) ) {
        st.kind = vm_kind::water;
        st.color = has( id, "tidepool" ) ? rgb( 62, 112, 162 ) : soften( base );
        st.variants = 4;
        st.spin = true;
        return st;
    }
    if( has( id, "den" ) ) {
        st.overlay = true;
        st.floor_material = vm_material::dirt;
        st.floor_color = material_color( vm_material::dirt, base );
        st.kind = vm_kind::mound;
        st.color = rgb( 110, 82, 56 );
        st.param = 1;
        return st;
    }
    if( has( id, "roof" ) ) {
        st.kind = vm_kind::floor;
        st.material = vm_material::concrete;
        st.variants = 4;
        st.color = has( id, "shingle" ) ? rgb( 96, 62, 52 ) : has( id, "thatch" ) ? rgb( 176, 150, 82 ) :
                   has( id, "tar" ) ? rgb( 54, 54, 58 ) : has( id, "canvas" ) ? rgb( 150, 140, 110 ) :
                   has( id, "glass" ) ? rgb( 150, 200, 230 ) : rgb( 120, 118, 114 );
        return st;
    }
    if( has_any( id, "bridge", "dock", "pontoon", "catwalk", "boardwalk" ) ) {
        st.kind = vm_kind::floor;
        st.material = vm_material::wood;
        st.variants = 4;
        st.color = has( id, "riverbridge" ) ? rgb( 150, 148, 142 ) : rgb( 130, 100, 70 );
        return st;
    }
    if( has_any( id, "elevator", "conveyor", "manhole_cover" ) ) {
        st.kind = vm_kind::floor;
        st.material = vm_material::metal;
        st.variants = 4;
        st.color = material_color( vm_material::metal, base );
        return st;
    }
    if( has( id, "zebra" ) ) {
        st.kind = vm_kind::floor;
        st.material = vm_material::pavement;
        st.color = material_color( vm_material::pavement, base );
        st.param = 4;
        return st;
    }
    if( has( id, "groundcover" ) ) {
        st.kind = vm_kind::floor;
        st.material = vm_material::grass;
        st.color = has( id, "barren" ) ? rgb( 132, 120, 70 ) : material_color( vm_material::grass, base );
        st.variants = 4;
        st.spin = true;
        return st;
    }
    if( t.movecost == 0 && t.has_flag( ter_furn_flag::TFLAG_TRANSPARENT ) ) {
        st.overlay = true;
        st.floor_material = vm_material::concrete;
        st.floor_color = material_color( vm_material::concrete, base );
        st.kind = vm_kind::fence;
        st.fence = true;
        st.variant = 1;
        st.color = voxel_mix( base, rgb( 120, 122, 126 ), 0.5f );
        return st;
    }
    classify_floor( id, base, st );
    return st;
}

bool classify_furniture_extra( const std::string &id, const voxel_rgb &base, tile_style &st );

tile_style classify_furniture( const furn_t &f )
{
    tile_style st;
    const std::string id = f.id.str();
    const voxel_rgb base = soften( to_rgb( f.color() ) );
    st.color = base;
    st.color2 = voxel_shade( base, 0.8f );
    if( classify_furniture_extra( id, base, st ) ) {
        return st;
    }
    const voxel_rgb white = rgb( 228, 228, 222 );
    const voxel_rgb wood = rgb( 128, 90, 58 );
    const voxel_rgb metal = rgb( 128, 133, 138 );

    if( has( id, "toilet" ) ) {
        st.kind = vm_kind::toilet;
        st.color = white;
    } else if( has( id, "bathtub" ) ) {
        st.kind = vm_kind::bathtub;
        st.color = white;
    } else if( has( id, "sink" ) ) {
        st.kind = vm_kind::sink;
        st.color = white;
        st.color2 = wood;
    } else if( has_any( id, "fridge", "freezer", "refrigerator", "washer", "dryer" ) ) {
        st.kind = vm_kind::cabinet;
        st.color = white;
        st.color2 = metal;
    } else if( has_any( id, "oven", "stove", "fireplace", "furnace", "kiln", "forge", "smoker" ) ) {
        st.kind = vm_kind::stove;
        st.color = rgb( 70, 70, 74 );
    } else if( has_any( id, "tv", "computer", "console", "terminal", "arcade", "monitor", "pinball" ) ) {
        st.kind = vm_kind::screen;
        st.color = rgb( 70, 140, 200 );
        st.color2 = has( id, "console" ) ? metal : wood;
    } else if( has_any( id, "bed", "mattress", "cot", "bunk" ) ) {
        st.kind = vm_kind::bed;
        st.color = wood;
        st.color2 = base;
    } else if( has_any( id, "sofa", "couch", "armchair", "loveseat" ) ) {
        st.kind = vm_kind::sofa;
    } else if( has_any( id, "chair", "stool", "seat", "bench", "pew", "throne" ) ) {
        st.kind = vm_kind::chair;
        st.spin = true;
    } else if( has_any( id, "table", "desk", "workbench", "lectern", "altar" ) ) {
        st.kind = vm_kind::table;
        if( has_any( id, "metal", "steel" ) ) {
            st.color = metal;
        } else if( !has( id, "pool" ) ) {
            st.color = wood;
        }
    } else if( has_any( id, "counter", "cupboard", "kitchen", "register", "cash", "reception" ) ) {
        st.kind = vm_kind::counter;
        st.color = has( id, "metal" ) ? metal : wood;
        st.color2 = rgb( 200, 196, 186 );
    } else if( has_any( id, "bookcase", "locker", "cabinet", "wardrobe", "dresser", "shelf", "rack",
                        "display", "vending", "armoire", "safe", "filing", "closet", "machine" ) ) {
        st.kind = vm_kind::cabinet;
        if( has_any( id, "locker", "safe", "filing", "rack", "vending", "machine" ) ) {
            st.color = metal;
        } else {
            st.color = wood;
        }
        st.color2 = rgb( 200, 190, 120 );
    } else if( has_any( id, "dumpster", "recycle" ) ) {
        st.kind = vm_kind::dumpster;
        st.color = rgb( 50, 100, 60 );
    } else if( has_any( id, "barrel", "keg", "drum", "trashcan", "trash_can", "tank", "bin" ) ) {
        st.kind = vm_kind::barrel;
    } else if( has_any( id, "crate", "box", "cardboard", "chest", "coffin", "casket" ) ) {
        st.kind = vm_kind::crate;
        st.color = has( id, "cardboard" ) ? rgb( 170, 135, 90 ) : wood;
    } else if( has_any( id, "sign", "billboard", "board" ) ) {
        st.kind = vm_kind::sign;
    } else if( has_any( id, "lamp", "light", "lantern", "torch" ) ) {
        st.kind = vm_kind::lamp;
        st.color2 = metal;
    } else if( has_any( id, "boulder", "rock", "stone", "statue" ) ) {
        st.kind = vm_kind::boulder;
        st.color = rgb( 120, 115, 108 );
        st.spin = true;
        st.variants = 3;
    } else if( has_any( id, "rubble", "debris", "wreckage", "ash", "junk", "pile" ) ) {
        st.kind = vm_kind::rubble;
        st.spin = true;
        st.variants = 3;
    } else if( has_any( id, "bush", "shrub", "underbrush", "fern" ) ) {
        st.kind = vm_kind::shrub;
        st.color = rgb( 62, 112, 44 );
        st.spin = true;
        st.variants = 3;
        if( has( id, "berry" ) ) {
            st.param = 1;
            st.color2 = rgb( 160, 30, 60 );
        }
    } else if( has_any( id, "flower", "dandelion", "mushroom", "cattail", "lily", "bluebell", "dahlia",
                        "chamomile", "lotus", "poppy", "sunflower", "tulip", "rose", "datura", "plant", "seed" ) ) {
        st.kind = vm_kind::flower;
        st.variants = 3;
    } else if( has_any( id, "tall_grass", "grass", "hay", "reed" ) ) {
        st.kind = vm_kind::tall_grass;
        st.color = has( id, "hay" ) ? rgb( 200, 180, 90 ) : rgb( 92, 140, 58 );
        st.spin = true;
        st.variants = 3;
    } else if( has_any( id, "tree", "sapling" ) ) {
        st.kind = vm_kind::young_tree;
        st.color = rgb( 70, 122, 46 );
        st.color2 = rgb( 92, 68, 48 );
    } else if( has_any( id, "fence", "railing", "barricade", "sandbag", "barbed", "wire" ) ) {
        st.kind = vm_kind::fence;
        st.fence = true;
        st.color = has( id, "sandbag" ) ? rgb( 170, 150, 110 ) : base;
    } else if( has( id, "fire" ) ) {
        st.kind = vm_kind::fire;
    } else {
        st.kind = vm_kind::furniture;
    }
    return st;
}

bool classify_furniture_extra( const std::string &id, const voxel_rgb &base, tile_style &st )
{
    const voxel_rgb metal = rgb( 128, 133, 138 );
    const auto size_param = [&id]() -> uint8_t {
        return has( id, "tall" ) ? 2 : has( id, "mid" ) ? 1 : 0;
    };
    if( has_any( id, "f_fog", "swamp_gas", "f_clear", "absence", "no_item", "region_" ) ) {
        st.draw = false;
        return true;
    }
    if( has_any( id, "_stack", "stack_", "pallet", "beach_log" ) ) {
        st.kind = vm_kind::stack;
        st.param = has( id, "pallet" ) ? 0 : size_param();
        st.color = has_any( id, "brick" ) ? rgb( 150, 72, 55 ) : has( id, "adobe" ) ? rgb( 170, 130, 90 ) :
                   has_any( id, "pipe", "rebar" ) ? rgb( 110, 110, 115 ) : has( id, "cement" ) ? rgb( 170, 170, 165 ) :
                   has_any( id, "plank", "pallet" ) ? rgb( 160, 125, 80 ) : rgb( 115, 85, 55 );
        return true;
    }
    if( has_any( id, "earthbag", "gravelbag" ) ) {
        st.kind = vm_kind::stack;
        st.param = has( id, "half" ) ? 1 : 3;
        st.color = has( id, "gravel" ) ? rgb( 140, 135, 125 ) : rgb( 160, 135, 95 );
        return true;
    }
    if( has_any( id, "mound", "midden", "compost" ) ) {
        st.kind = vm_kind::mound;
        st.param = size_param();
        st.color = has( id, "sand" ) ? rgb( 205, 185, 130 ) : has_any( id, "gravel", "pebble" ) ? rgb( 140,
                   135, 125 ) : has( id, "clay" ) ? rgb( 150, 92, 60 ) : has( id, "glass" ) ? rgb( 170, 210, 220 ) :
                   rgb( 110, 82, 56 );
        st.spin = true;
        return true;
    }
    if( has_any( id, "groundsheet", "tarp", "_mat", "tatami", "canvas_floor", "woodchips", "f_slab",
                 "f_lane" ) ) {
        st.kind = vm_kind::mat;
        return true;
    }
    if( has_any( id, "canvas_wall", "skin_wall", "curtain", "room_divider", "canvas_door", "skin_door",
                 "beaded_door", "mirror" ) ) {
        st.kind = vm_kind::panel;
        st.oriented = true;
        st.variant = ( has( id, "_o" ) || has( id, "open" ) ) ? 1 : 0;
        if( has( id, "mirror" ) ) {
            st.color = rgb( 190, 210, 225 );
        }
        return true;
    }
    if( has_any( id, "hedge", "topiary" ) ) {
        st.kind = vm_kind::hedge;
        st.param = has_any( id, "tall", "topiary" ) ? 1 : 0;
        st.color = rgb( 58, 108, 42 );
        return true;
    }
    if( has_any( id, "brambles", "knotweed", "japanese" ) ) {
        st.kind = vm_kind::shrub;
        st.color = rgb( 70, 100, 45 );
        st.spin = true;
        st.variants = 3;
        return true;
    }
    if( has_any( id, "asparagus", "carrot", "burdock", "chicory", "dogbane", "mustard", "salsify",
                 "thistle", "wintergreen", "selfheal", "loosestrife", "susan", "artichoke", "maianthemum",
                 "sarsaparilla", "wild_rice", "kelp", "algae", "sea_lettuce", "seaweed", "weed", "anemone",
                 "sponge", "lichen" ) ) {
        st.kind = vm_kind::flower;
        st.variants = 3;
        return true;
    }
    if( has_any( id, "grave", "sarcophagus", "battlefield_cross", "monument" ) ) {
        st.kind = vm_kind::grave;
        st.color = rgb( 150, 148, 142 );
        return true;
    }
    if( has( id, "anvil" ) ) {
        st.kind = vm_kind::anvil;
        st.color = rgb( 60, 62, 66 );
        return true;
    }
    if( has_any( id, "mannequin", "training_dummy", "impaled" ) ) {
        st.kind = vm_kind::humanoid;
        st.color = has( id, "impaled" ) ? rgb( 170, 130, 110 ) : rgb( 215, 200, 175 );
        st.color2 = has( id, "impaled" ) ? rgb( 90, 100, 70 ) : rgb( 215, 200, 175 );
        return true;
    }
    if( has_any( id, "flagpole", "utility_pole", "antenna", "satellite", "dish", "chimney", "vent_pipe",
                 "iv_pole", "IV_pole", "wind_vane", "hoist", "ladder", "rope_up", "vine_up", "web_up", "pylon",
                 "beacon", "tower" ) ) {
        st.kind = vm_kind::pole;
        st.color = has_any( id, "wooden", "ladder", "rope", "vine" ) ? rgb( 120, 88, 56 ) : rgb( 140, 140,
                   145 );
        st.color2 = has( id, "flag" ) ? rgb( 60, 80, 170 ) : base;
        st.param = has( id, "flag" ) ? 2 : has_any( id, "antenna", "satellite", "dish" ) ? 3 : has( id,
                   "utility" ) ? 1 : 0;
        return true;
    }
    if( has_any( id, "generator", "compressor", "conditioner", "air_filter", "lathe", "press", "saw",
                 "drill", "mixer", "centrifuge", "microscope", "MRI", "CTscan", "xray", "NMR", "HPLC", "f_GC",
                 "f_MS", "autoclave", "dialysis", "ventilator", "oxygen", "water_heater", "purifier", "server",
                 "robotic_arm", "shredder", "router", "planer", "jointer", "still", "fume_hood", "fryer", "atm",
                 "piano", "organ", "entertainment", "treadmill", "ergometer", "exercise", "autodoc", "capacitor",
                 "reactor", "fuel_cell", "charger", "printer", "scanner", "pump", "tire_changer", "hammer",
                 "ball_mach", "churn", "mill", "detector", "shaker", "spinwheel", "styling", "control_station",
                 "melchior", "balthazar", "boiler", "cooler", "solar", "housing", "compact", "mortar", "bellows",
                 "scutch", "shear", "zapper", "concentrator", "conduit", "cable", "booster" ) ) {
        st.kind = vm_kind::machine;
        st.color = has_any( id, "piano", "organ", "spinwheel", "churn", "mill", "bellows" ) ? rgb( 90, 60,
                   40 ) : voxel_mix( base, metal, 0.6f );
        st.color2 = rgb( 70, 170, 120 );
        return true;
    }
    if( has_any( id, "hammock" ) ) {
        st.kind = vm_kind::bed;
        st.color = rgb( 128, 90, 58 );
        st.color2 = base;
        return true;
    }
    if( has_any( id, "beanbag", "pillow_fort" ) ) {
        st.kind = vm_kind::sofa;
        return true;
    }
    if( has_any( id, "birdbath", "gazing_ball", "sand_castle", "bitts", "cleat", "doll", "egg_sack",
                 "pod", "fungal", "tendril", "spike", "meathook", "shackle" ) ) {
        st.kind = vm_kind::boulder;
        st.spin = true;
        return true;
    }
    if( has_any( id, "shower", "clothing_rail", "torah_ark" ) ) {
        st.kind = vm_kind::cabinet;
        return true;
    }
    if( has_any( id, "target", "bulletin" ) ) {
        st.kind = vm_kind::sign;
        return true;
    }
    if( has_any( id, "punching_bag" ) ) {
        st.kind = vm_kind::barrel;
        st.color = rgb( 150, 40, 40 );
        return true;
    }
    if( has_any( id, "vent_", "groundsheet", "ground_" ) ) {
        st.kind = vm_kind::mat;
        st.color = rgb( 70, 70, 74 );
        return true;
    }
    return false;
}

const voxel_rgb &palette_pick( const std::vector<voxel_rgb> &p, uint32_t h )
{
    return p[h % p.size()];
}

const std::vector<voxel_rgb> &shirt_palette()
{
    static const std::vector<voxel_rgb> p = {
        rgb( 160, 60, 55 ), rgb( 60, 100, 160 ), rgb( 70, 120, 70 ), rgb( 170, 150, 70 ),
        rgb( 110, 70, 130 ), rgb( 200, 200, 195 ), rgb( 70, 70, 75 ), rgb( 180, 110, 60 )
    };
    return p;
}

const std::vector<voxel_rgb> &skin_palette()
{
    static const std::vector<voxel_rgb> p = {
        rgb( 232, 190, 150 ), rgb( 200, 150, 110 ), rgb( 150, 105, 70 ), rgb( 110, 75, 50 ),
        rgb( 240, 205, 175 )
    };
    return p;
}

uint32_t string_hash( const std::string &s )
{
    uint32_t h = 2166136261u;
    for( const char c : s ) {
        h = ( h ^ static_cast<uint8_t>( c ) ) * 16777619u;
    }
    return h;
}

float size_scale( creature_size s )
{
    switch( s ) {
        case creature_size::tiny:
            return 0.45f;
        case creature_size::small:
            return 0.7f;
        case creature_size::medium:
            return 1.0f;
        case creature_size::large:
            return 1.35f;
        case creature_size::huge:
            return 1.8f;
        case creature_size::num_sizes:
            break;
    }
    return 1.0f;
}

vm_key monster_key( const monster &m )
{
    static const species_id zombie( "ZOMBIE" );
    static const species_id human( "HUMAN" );
    static const species_id feral( "FERAL" );
    static const species_id ghoul( "GHOUL" );
    static const species_id robot( "ROBOT" );
    static const species_id robot_flying( "ROBOT_FLYING" );
    static const species_id cyborg( "CYBORG" );
    static const species_id bird( "BIRD" );
    static const species_id insect( "INSECT" );
    static const species_id insect_flying( "INSECT_FLYING" );
    static const species_id spider( "SPIDER" );
    static const species_id centipede( "CENTIPEDE" );
    static const species_id worm( "WORM" );
    static const species_id fish( "FISH" );
    static const species_id amphibian( "AMPHIBIAN" );
    static const species_id mammal( "MAMMAL" );
    static const species_id reptile( "REPTILE" );
    static const species_id fungus( "FUNGUS" );
    static const species_id plant( "PLANT" );
    static const species_id leech_plant( "LEECH_PLANT" );

    const std::string id = m.type->id.str();
    const uint32_t h = string_hash( id );
    const voxel_rgb col = soften( to_rgb( m.basic_symbol_color() ) );
    vm_key k;
    k.color = col;
    k.color2 = voxel_shade( col, 0.8f );
    k.variant = static_cast<uint8_t>( h % 3 );
    const auto animal_param = [&id]() -> int {
        if( has_any( id, "cat", "cougar", "lynx", "bobcat", "panther" ) )
        {
            return 1;
        }
        if( has_any( id, "deer", "moose", "elk", "caribou" ) )
        {
            return 2;
        }
        if( has_any( id, "bear" ) )
        {
            return 3;
        }
        if( has_any( id, "horse", "cow", "bull", "bison", "ox", "mule", "donkey", "pony" ) )
        {
            return 4;
        }
        if( has_any( id, "rabbit", "squirrel", "rat", "mouse", "groundhog", "beaver", "chipmunk",
                     "opossum", "raccoon", "mink", "weasel", "skunk", "hare", "vole", "lemming" ) )
        {
            return 5;
        }
        if( has_any( id, "boar", "pig", "hog", "sow" ) )
        {
            return 6;
        }
        if( has_any( id, "dog", "wolf", "coyote", "fox", "jackal", "hound" ) )
        {
            return 0;
        }
        return -1;
    };
    if( ( m.in_species( zombie ) || m.in_species( ghoul ) ) && animal_param() >= 0 ) {
        k.kind = vm_kind::quadruped;
        k.param = static_cast<uint8_t>( animal_param() );
        k.color = voxel_mix( col, rgb( 120, 135, 100 ), 0.6f );
    } else if( m.in_species( zombie ) || m.in_species( ghoul ) ) {
        k.kind = vm_kind::zombie;
        k.color = voxel_mix( col, rgb( 128, 146, 112 ), 0.6f );
        k.color2 = voxel_shade( palette_pick( shirt_palette(), h ), 0.7f );
        if( has_any( id, "fat", "bloat", "boomer", "gasbag" ) ) {
            k.param = 1;
        } else if( has_any( id, "brute", "hulk", "giant", "mass", "smash", "master", "juggernaut" ) ) {
            k.param = 2;
        } else if( has_any( id, "skeleton", "skeletal", "bone", "withered" ) ) {
            k.param = 3;
        } else if( has_any( id, "shock", "electric", "tesla" ) ) {
            k.param = 4;
            k.color = voxel_mix( k.color, rgb( 120, 160, 210 ), 0.4f );
        } else if( has_any( id, "acid", "spitter", "corrosive", "toxic" ) ) {
            k.param = 5;
            k.color = voxel_mix( k.color, rgb( 140, 200, 80 ), 0.4f );
        } else if( has_any( id, "soldier", "military", "swat", "armor", "riot", "police", "cop" ) ) {
            k.param = 6;
        } else if( has_any( id, "burn", "smoke", "scorch", "fire" ) ) {
            k.param = 7;
            k.color = voxel_shade( k.color, 0.6f );
        }
    } else if( m.in_species( human ) || m.in_species( feral ) || m.in_species( cyborg ) ) {
        k.kind = vm_kind::humanoid;
        k.color = palette_pick( skin_palette(), h );
        k.color2 = palette_pick( shirt_palette(), h >> 4 );
    } else if( m.in_species( robot ) || m.in_species( robot_flying ) ) {
        k.kind = vm_kind::robot;
        k.color = voxel_mix( col, rgb( 150, 155, 160 ), 0.5f );
    } else if( m.in_species( bird ) ) {
        k.kind = vm_kind::bird;
    } else if( m.in_species( insect_flying ) ) {
        k.kind = vm_kind::bug;
        k.param = 2;
    } else if( m.in_species( spider ) ) {
        k.kind = vm_kind::bug;
        k.param = 1;
    } else if( m.in_species( centipede ) || m.in_species( worm ) ) {
        k.kind = vm_kind::bug;
        k.param = 3;
    } else if( m.in_species( insect ) ) {
        k.kind = vm_kind::bug;
    } else if( m.in_species( fish ) || m.in_species( amphibian ) ) {
        k.kind = vm_kind::fish;
    } else if( m.in_species( mammal ) || m.in_species( reptile ) ) {
        k.kind = vm_kind::quadruped;
        k.param = static_cast<uint8_t>( std::max( 0, animal_param() ) );
    } else if( m.in_species( fungus ) || m.in_species( plant ) || m.in_species( leech_plant ) ) {
        k.kind = vm_kind::plant_creature;
    } else {
        k.kind = vm_kind::blob;
    }
    return k;
}

vm_kind item_kind( const item &it )
{
    if( it.is_corpse() ) {
        return vm_kind::item_corpse;
    }
    if( it.is_gun() ) {
        return vm_kind::item_gun;
    }
    if( it.is_ammo() || it.is_magazine() ) {
        return vm_kind::item_ammo;
    }
    if( it.is_book() ) {
        return vm_kind::item_book;
    }
    if( it.is_armor() ) {
        return vm_kind::item_clothing;
    }
    const std::string id = it.typeId().str();
    if( has_any( id, "bottle", "jar", "jug", "canteen", "flask", "thermos", "can_", "_can" ) ) {
        return vm_kind::item_bottle;
    }
    if( it.is_comestible() ) {
        return vm_kind::item_food;
    }
    if( has_any( id, "knife", "sword", "machete", "blade", "katana", "axe", "spear", "bayonet",
                 "dagger", "sabre", "scythe", "cleaver", "glaive", "halberd" ) ) {
        return vm_kind::item_blade;
    }
    if( it.is_tool() || it.is_melee() ) {
        return vm_kind::item_tool;
    }
    if( it.is_container() ) {
        return vm_kind::item_food;
    }
    return vm_kind::item_pile;
}

uint8_t nearest_pants( const voxel_rgb &c )
{
    static const std::array<voxel_rgb, 8> palette = { {
            rgb( 55, 60, 85 ), rgb( 40, 40, 44 ), rgb( 110, 90, 60 ), rgb( 70, 85, 55 ),
            rgb( 130, 125, 115 ), rgb( 90, 40, 40 ), rgb( 180, 175, 165 ), rgb( 60, 75, 110 )
        }
    };
    int best = 0;
    int best_d = 1 << 30;
    for( int i = 0; i < 8; ++i ) {
        const int dr = palette[i].r - c.r;
        const int dg = palette[i].g - c.g;
        const int db = palette[i].b - c.b;
        const int d = dr * dr + dg * dg + db * db;
        if( d < best_d ) {
            best_d = d;
            best = i;
        }
    }
    return static_cast<uint8_t>( best );
}

vm_key character_key( const Character &ch, const voxel_rgb &skin, const voxel_rgb &shirt,
                      uint8_t hair, bool backpack )
{
    static const bodypart_str_id torso( "torso" );
    static const bodypart_str_id leg_l( "leg_l" );
    vm_key k;
    k.kind = vm_kind::humanoid;
    k.color = skin;
    k.color2 = shirt;
    voxel_rgb pants = rgb( 55, 60, 85 );
    bool has_pack = false;
    for( const item_location &loc : ch.get_visible_worn_items() ) {
        if( !loc ) {
            continue;
        }
        const voxel_rgb c = soften( to_rgb( loc->color() ) );
        if( loc->covers( torso.id() ) ) {
            k.color2 = c;
        }
        if( loc->covers( leg_l.id() ) ) {
            pants = c;
        }
        if( has_any( loc->typeId().str(), "backpack", "rucksack", "duffel", "pack" ) ) {
            has_pack = true;
        }
    }
    k.variant = static_cast<uint8_t>( hair % 3 + 3 * nearest_pants( pants ) );
    k.param = backpack || has_pack ? 1 : 0;
    return k;
}

std::optional<vm_key> held_key( const Character &ch )
{
    const item_location weapon = ch.get_wielded_item();
    if( !weapon ) {
        return std::nullopt;
    }
    vm_key k;
    k.kind = vm_kind::held;
    k.color = soften( to_rgb( weapon->color() ) );
    const vm_kind ik = item_kind( *weapon );
    k.param = ik == vm_kind::item_gun ? 0 : ik == vm_kind::item_blade ? 1 : 2;
    return k;
}

struct sprite_instance {
    float x;
    float y;
    float z;
    float yaw;
    float w;
    float h;
    float layer;
    float light;
    float u0;
    float v0;
    float u1;
    float v1;
    float shift_x;
    float shift_y;
    float desat;
    float height;
};

enum sprite_mode : int {
    sprite_billboard = 0,
    sprite_flat = 1,
    sprite_cube = 2
};

template<typename Next>
const voxelset_entry *find_chain( const voxelset *vs, std::string id, Next next )
{
    if( vs == nullptr ) {
        return nullptr;
    }
    for( int i = 0; i < 8 && !id.empty(); ++i ) {
        if( const voxelset_entry *e = vs->find_id( id ) ) {
            return e;
        }
        id = next( id );
    }
    return nullptr;
}

std::string map_looks_like( const std::string &id )
{
    const ter_str_id t( id );
    if( t.is_valid() ) {
        return t->looks_like;
    }
    const furn_str_id f( id );
    if( f.is_valid() ) {
        return f->looks_like;
    }
    return std::string();
}

std::string monster_looks_like( const std::string &id )
{
    const mtype_id m( id );
    return m.is_valid() ? m->looks_like : std::string();
}

std::string item_looks_like( const std::string &id )
{
    const itype_id i( id );
    return i.is_valid() ? i->looks_like.str() : std::string();
}

std::string vpart_looks_like( const std::string &id )
{
    if( id.compare( 0, 3, "vp_" ) != 0 ) {
        return std::string();
    }
    const vpart_id v( id.substr( 3 ) );
    return v.is_valid() && !v->looks_like.empty() ? "vp_" + v->looks_like : std::string();
}

std::string no_looks_like( const std::string & )
{
    return std::string();
}

class voxel_renderer
{
    public:
        bool render( const tripoint_bub_ms &center, int width, int height, std::vector<uint8_t> &pixels );
        void shutdown();
        bool failed = false;

    private:
        bool init();
        bool ensure_targets( int w, int h );
        int model_for( const vm_key &key );
        int upload_model( const voxel_mesh &mesh, bool emissive );
        GLuint compile( const char *vs, const char *fs );
        GLuint compile_program( const char *vs, const char *fs );
        void collect( const tripoint_bub_ms &center, int radius, float daylight );
        void draw_pass( instance_lists &lists, bool glass, GLuint program );
        void upload_instances( instance_lists &main, instance_lists &extra );

        void add( instance_lists &lists, const vm_key &key, const tripoint_bub_ms &p, float lift, float yaw,
                  float light, float desat, float scale = 1.0f, const voxel_rgb &tint = { 255, 255, 255 } );
        void add_at( instance_lists &lists, const vm_key &key, const vec3 &pos, float lift, float yaw,
                     float light, float desat, float scale = 1.0f, const voxel_rgb &tint = { 255, 255, 255 },
                     const voxelset_entry *custom = nullptr, uint32_t pick = 0 );
        int custom_model( const voxelset_entry &entry, uint32_t pick );
        void add_sprite( sprite_mode mode, const sprite_ref &spr, const vec3 &pos, float yaw, float lift,
                         float light, float desat, bool flip = false, float height = 1.0f, float layer = 0.0f );
        template<typename Next>
        const sprite_ref *find_sprite( const std::string &id, Next next, bool background = false ) const;
        bool sprite_terrain( const tripoint_bub_ms &p, const std::string &id, const tile_style &st,
                             float light, float desat );
        bool sprite_furniture( const tripoint_bub_ms &p, const std::string &id, const tile_style &st,
                               float light, float desat );
        bool sprite_character( const Character &ch, const vec3 &pos, float light );
        void draw_sprites( const mat4 &view, const mat4 &view_proj, const mat4 &light_vp,
                           const vec3 &sun_dir, float daylight, bool use_shadow, const vec3 &focus, float fog_far,
                           const vec3 &fog );
        void upload_sprites();
        void draw_sprite_ranges( GLuint program, bool shadow_pass );
        void draw_sprite_shadows( const mat4 &light_vp, const vec3 &sun_dir );
        GLuint sprite_shadow_program = 0;
        std::vector<std::pair<std::pair<int, int>, std::pair<size_t, size_t>>> sprite_ranges;
        bool init_sprites();
        void reset_models();
        void sync_voxelset();
        vec3 animated( const void *id, const tripoint_bub_ms &p );
        void add_style( const tile_style &st, const tripoint_bub_ms &p, float light, float desat,
                        bool furniture );
        const tile_style &terrain_style( const ter_id &t );
        const tile_style &furniture_style( const furn_id &f );
        bool wall_like_at( const tripoint_bub_ms &p );
        bool cut_here( const tripoint_bub_ms &p ) const;
        uint8_t orientation_at( const tripoint_bub_ms &p );
        bool fence_like_at( const tripoint_bub_ms &p );

        SDL_Window *window = nullptr;
        SDL_GLContext context = nullptr;
        bool initialized = false;

        GLuint main_program = 0;
        GLuint shadow_program = 0;
        GLuint instance_vbo = 0;

        GLuint msaa_fbo = 0;
        GLuint msaa_color = 0;
        GLuint msaa_depth = 0;
        GLuint resolve_fbo = 0;
        GLuint resolve_color = 0;
        GLuint shadow_fbo = 0;
        GLuint shadow_tex = 0;
        int samples = 4;
        int target_w = 0;
        int target_h = 0;

        std::vector<gpu_model> models;
        std::unordered_map<uint64_t, int> model_lookup;
        std::unordered_map<int, tile_style> ter_styles;
        std::unordered_map<int, tile_style> furn_styles;
        std::unordered_map<std::string, int> custom_lookup;
        const voxelset *vset = nullptr;
        int vset_generation = -1;
        sprite_atlas atlas;
        bool sprite_style = false;
        bool cutaway = false;
        bool night = false;
        tripoint_bub_ms cut_center;
        GLuint sprite_program = 0;
        GLuint sprite_vbo = 0;
        GLuint quad_vao = 0;
        GLuint quad_vbo = 0;
        GLuint cube_vao = 0;
        GLuint cube_vbo = 0;
        std::map<std::pair<int, int>, std::vector<sprite_instance>> sprite_batches;

        instance_lists main_lists;
        instance_lists shadow_lists;
        std::vector<instance> upload;
        std::vector<std::pair<size_t, size_t>> main_ranges;
        std::vector<std::pair<size_t, size_t>> extra_ranges;
        bool drawing_shadow = false;

        tripoint_bub_ms last_avatar_pos;
        float avatar_yaw = 0.0f;
        uint32_t weather_frame = 0;
        int shadow_size = 2048;
        int max_samples = 4;
        int wanted_samples = 4;
        float focus_y = 0.0f;
        bool have_focus_y = false;
        std::vector<std::array<float, 4>> light_pos;
        std::vector<std::array<float, 3>> light_col;
        std::unordered_map<const void *, std::pair<int, uint64_t>> hp_track;
        voxel_rgb flash_tint( const Creature &c );
        void add_light( const tripoint_bub_ms &p, float lift, float radius, float r, float g, float b );
        void upload_lights( GLuint program );
        void apply_quality();

        struct motion {
            tripoint_abs_ms from;
            tripoint_abs_ms to;
            uint64_t start = 0;
            bool seen = false;
        };
        std::unordered_map<const void *, motion> motions;
        uint64_t now_ms = 0;

    public:
        bool animating = false;
        bool ambient = false;
        std::optional<vec3> avatar_display;
};

const char *main_vs = R"(#version 330 core
layout( location = 0 ) in vec3 a_pos;
layout( location = 1 ) in vec3 a_normal;
layout( location = 2 ) in vec4 a_color;
layout( location = 3 ) in vec4 i_pos;
layout( location = 4 ) in vec4 i_tint;
layout( location = 5 ) in vec4 i_extra;
uniform mat4 u_vp;
uniform mat4 u_light_vp;
uniform float u_time;
out vec3 v_color;
out vec3 v_normal;
out vec3 v_world;
out vec4 v_shadow;
out float v_light;
out float v_emissive;
out float v_desat;
out float v_wave;
void main()
{
    float c = cos( i_pos.w );
    float s = sin( i_pos.w );
    vec3 p = ( a_pos - vec3( 0.5, 0.0, 0.5 ) ) * i_extra.x;
    p = vec3( c * p.x - s * p.z, p.y, s * p.x + c * p.z ) + vec3( 0.5, 0.0, 0.5 );
    vec3 world = p + i_pos.xyz;
    v_wave = i_extra.w;
    if( i_extra.w > 0.5 ) {
        world.y += sin( u_time * 1.6 + world.x * 1.1 + world.z * 0.8 ) * 0.035;
    }
    v_normal = vec3( c * a_normal.x - s * a_normal.z, a_normal.y, s * a_normal.x + c * a_normal.z );
    v_color = a_color.rgb * i_tint.rgb * a_color.a;
    v_light = i_tint.a;
    v_emissive = i_extra.y;
    v_desat = i_extra.z;
    v_world = world;
    v_shadow = u_light_vp * vec4( world, 1.0 );
    gl_Position = u_vp * vec4( world, 1.0 );
}
)";

const char *main_fs = R"(#version 330 core
in vec3 v_color;
in vec3 v_normal;
in vec3 v_world;
in vec4 v_shadow;
in float v_light;
in float v_emissive;
in float v_desat;
in float v_wave;
uniform sampler2DShadow u_shadow;
uniform float u_shadow_texel;
uniform vec4 u_lights[24];
uniform vec3 u_light_colors[24];
uniform int u_light_count;
uniform float u_time;
vec3 point_lights( vec3 n, vec3 p )
{
    vec3 sum = vec3( 0.0 );
    for( int i = 0; i < u_light_count; ++i ) {
        vec3 d = u_lights[i].xyz - p;
        float dist = length( d );
        float att = clamp( 1.0 - dist / u_lights[i].w, 0.0, 1.0 );
        att *= att;
        float facing = 0.45 + 0.55 * max( dot( n, d / max( dist, 0.001 ) ), 0.0 );
        sum += u_light_colors[i] * att * facing;
    }
    return sum;
}
uniform vec3 u_sun;
uniform float u_daylight;
uniform float u_use_shadow;
uniform vec3 u_focus;
uniform float u_fog_far;
uniform vec3 u_fog_color;
uniform float u_alpha;
out vec4 frag;
float shadow_at()
{
    vec3 p = v_shadow.xyz / v_shadow.w * 0.5 + 0.5;
    if( p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0 ) {
        return 1.0;
    }
    float sum = 0.0;
    vec2 texel = vec2( u_shadow_texel );
    for( int x = -1; x <= 1; ++x ) {
        for( int y = -1; y <= 1; ++y ) {
            sum += texture( u_shadow, vec3( p.xy + vec2( x, y ) * texel, p.z - 0.0012 ) );
        }
    }
    return sum / 9.0;
}
void main()
{
    vec3 n = normalize( v_normal );
    float shape = 0.6 + 0.3 * n.y + 0.07 * n.x - 0.05 * n.z;
    float sun = max( dot( n, u_sun ), 0.0 );
    float sh = u_use_shadow > 0.5 ? shadow_at() : 1.0;
    vec3 col = v_color * ( v_light * shape + u_daylight * sun * sh * 0.5 * min( v_light, 1.0 ) );
    col += v_color * point_lights( n, v_world ) * ( 1.0 - v_desat );
    if( v_wave > 0.5 ) {
        col *= 0.9 + 0.1 * sin( u_time * 2.3 + v_world.x * 2.0 - v_world.z * 1.5 );
    }
    float flicker = 0.82 + 0.18 * sin( u_time * 17.0 + v_world.x * 5.0 + v_world.y * 7.0 );
    col = max( col, v_color * v_emissive * flicker );
    col = vec3( 1.0 ) - exp( -col * 2.1 );
    float g = dot( col, vec3( 0.299, 0.587, 0.114 ) );
    col = mix( col, vec3( g ) * vec3( 0.78, 0.86, 1.05 ), v_desat );
    float d = length( v_world.xz - u_focus.xz );
    float fog = smoothstep( u_fog_far * 0.72, u_fog_far, d );
    col = mix( col, u_fog_color, fog );
    frag = vec4( col, u_alpha );
}
)";

const char *shadow_vs = R"(#version 330 core
layout( location = 0 ) in vec3 a_pos;
layout( location = 3 ) in vec4 i_pos;
layout( location = 5 ) in vec4 i_extra;
uniform mat4 u_light_vp;
void main()
{
    float c = cos( i_pos.w );
    float s = sin( i_pos.w );
    vec3 p = ( a_pos - vec3( 0.5, 0.0, 0.5 ) ) * i_extra.x;
    p = vec3( c * p.x - s * p.z, p.y, s * p.x + c * p.z ) + vec3( 0.5, 0.0, 0.5 );
    gl_Position = u_light_vp * vec4( p + i_pos.xyz, 1.0 );
}
)";

const char *shadow_fs = R"(#version 330 core
void main()
{
}
)";

const char *sprite_shadow_fs = R"(#version 330 core
in vec2 v_uv;
uniform sampler2D u_tex;
void main()
{
    if( texture( u_tex, v_uv ).a < 0.5 ) {
        discard;
    }
}
)";

const char *sprite_vs = R"(#version 330 core
layout( location = 0 ) in vec3 a_corner;
layout( location = 1 ) in vec3 a_normal;
layout( location = 2 ) in vec2 a_uv;
layout( location = 3 ) in vec4 i_pos;
layout( location = 4 ) in vec4 i_size;
layout( location = 5 ) in vec4 i_uv;
layout( location = 6 ) in vec4 i_extra;
uniform mat4 u_vp;
uniform mat4 u_light_vp;
uniform int u_mode;
uniform vec3 u_right;
uniform vec3 u_up;
uniform vec3 u_toward;
out vec2 v_uv;
out vec3 v_world;
out vec3 v_normal;
out vec4 v_shadow;
out float v_light;
out float v_desat;
void main()
{
    vec3 world;
    vec3 n;
    float c = cos( i_pos.w );
    float s = sin( i_pos.w );
    if( u_mode == 0 ) {
        vec3 base = i_pos.xyz + vec3( 0.5, 0.0, 0.5 );
        world = base + u_right * ( ( a_corner.x - 0.5 ) * i_size.x + i_extra.x ) +
                u_up * ( a_corner.y * i_size.y + i_extra.y ) + u_toward * i_size.z;
        n = u_toward;
    } else if( u_mode == 1 ) {
        vec2 p = vec2( ( a_corner.x - 0.5 ) * i_size.x + i_extra.x, ( 0.5 - a_corner.y ) * i_size.y );
        p = vec2( c * p.x - s * p.y, s * p.x + c * p.y );
        world = i_pos.xyz + vec3( 0.5 + p.x, i_size.z, 0.5 + p.y );
        n = vec3( 0.0, 1.0, 0.0 );
    } else {
        world = i_pos.xyz + a_corner * vec3( 1.0, i_extra.w, 1.0 );
        n = a_normal;
    }
    v_uv = mix( i_uv.xy, i_uv.zw, vec2( a_uv.x, 1.0 - a_uv.y ) );
    v_world = world;
    v_normal = n;
    v_light = i_size.w;
    v_desat = i_extra.z;
    v_shadow = u_light_vp * vec4( world, 1.0 );
    gl_Position = u_vp * vec4( world, 1.0 );
}
)";

const char *sprite_fs = R"(#version 330 core
in vec2 v_uv;
in vec3 v_world;
in vec3 v_normal;
in vec4 v_shadow;
in float v_light;
in float v_desat;
uniform sampler2D u_tex;
uniform sampler2DShadow u_shadow;
uniform float u_shadow_texel;
uniform vec4 u_lights[24];
uniform vec3 u_light_colors[24];
uniform int u_light_count;
uniform float u_time;
vec3 point_lights( vec3 n, vec3 p )
{
    vec3 sum = vec3( 0.0 );
    for( int i = 0; i < u_light_count; ++i ) {
        vec3 d = u_lights[i].xyz - p;
        float dist = length( d );
        float att = clamp( 1.0 - dist / u_lights[i].w, 0.0, 1.0 );
        att *= att;
        float facing = 0.45 + 0.55 * max( dot( n, d / max( dist, 0.001 ) ), 0.0 );
        sum += u_light_colors[i] * att * facing;
    }
    return sum;
}
uniform int u_mode;
uniform vec3 u_sun;
uniform float u_daylight;
uniform float u_use_shadow;
uniform vec3 u_focus;
uniform float u_fog_far;
uniform vec3 u_fog_color;
out vec4 frag;
float shadow_at()
{
    vec3 p = v_shadow.xyz / v_shadow.w * 0.5 + 0.5;
    if( p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0 ) {
        return 1.0;
    }
    float sum = 0.0;
    vec2 texel = vec2( u_shadow_texel );
    for( int x = -1; x <= 1; ++x ) {
        for( int y = -1; y <= 1; ++y ) {
            sum += texture( u_shadow, vec3( p.xy + vec2( x, y ) * texel, p.z - 0.0015 ) );
        }
    }
    return sum / 9.0;
}
void main()
{
    vec4 t = texture( u_tex, v_uv );
    if( t.a < 0.5 ) {
        discard;
    }
    vec3 n = normalize( v_normal );
    float shape = u_mode == 0 ? 0.82 : 0.62 + 0.3 * n.y + 0.06 * n.x - 0.04 * n.z;
    float sun = u_mode == 0 ? 0.55 : max( dot( n, u_sun ), 0.0 );
    float sh = u_use_shadow > 0.5 ? shadow_at() : 1.0;
    vec3 col = t.rgb * ( v_light * shape + u_daylight * sun * sh * 0.45 * min( v_light, 1.0 ) );
    col += t.rgb * point_lights( u_mode == 0 ? vec3( 0.0, 1.0, 0.0 ) : n, v_world ) * ( 1.0 - v_desat );
    col = vec3( 1.0 ) - exp( -col * 2.1 );
    float g = dot( col, vec3( 0.299, 0.587, 0.114 ) );
    col = mix( col, vec3( g ) * vec3( 0.78, 0.86, 1.05 ), v_desat );
    float d = length( v_world.xz - u_focus.xz );
    float fog = smoothstep( u_fog_far * 0.72, u_fog_far, d );
    frag = vec4( mix( col, u_fog_color, fog ), 1.0 );
}
)";


GLuint voxel_renderer::compile( const char *vs, const char *fs )
{
    const char *current = std::setlocale( LC_NUMERIC, nullptr );
    const std::string saved_locale = current ? current : "C";
    std::setlocale( LC_NUMERIC, "C" );
    const GLuint program = compile_program( vs, fs );
    std::setlocale( LC_NUMERIC, saved_locale.c_str() );
    return program;
}

GLuint voxel_renderer::compile_program( const char *vs, const char *fs )
{
    voxel_gl_api &gl = voxel_gl();
    const auto stage = [&gl]( GLenum type, const char *src ) -> GLuint {
        const GLuint s = gl.glCreateShader( type );
        gl.glShaderSource( s, 1, &src, nullptr );
        gl.glCompileShader( s );
        GLint ok = 0;
        gl.glGetShaderiv( s, GL_COMPILE_STATUS, &ok );
        if( !ok )
        {
            std::array<char, 2048> log{};
            gl.glGetShaderInfoLog( s, static_cast<GLsizei>( log.size() ), nullptr, log.data() );
            DebugLog( D_ERROR, D_SDL ) << "voxel shader compile failed: " << log.data();
            gl.glDeleteShader( s );
            return 0;
        }
        return s;
    };
    const GLuint v = stage( GL_VERTEX_SHADER, vs );
    const GLuint f = stage( GL_FRAGMENT_SHADER, fs );
    if( !v || !f ) {
        return 0;
    }
    const GLuint p = gl.glCreateProgram();
    gl.glAttachShader( p, v );
    gl.glAttachShader( p, f );
    gl.glLinkProgram( p );
    gl.glDeleteShader( v );
    gl.glDeleteShader( f );
    GLint ok = 0;
    gl.glGetProgramiv( p, GL_LINK_STATUS, &ok );
    if( !ok ) {
        std::array<char, 2048> log{};
        gl.glGetProgramInfoLog( p, static_cast<GLsizei>( log.size() ), nullptr, log.data() );
        DebugLog( D_ERROR, D_SDL ) << "voxel shader link failed: " << log.data();
        gl.glDeleteProgram( p );
        return 0;
    }
    return p;
}

bool voxel_renderer::init()
{
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 3 );
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
    window = SDL_CreateWindow( "cataclysm-voxel", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN );
    if( !window ) {
        DebugLog( D_ERROR, D_SDL ) << "voxel window failed: " << SDL_GetError();
        SDL_GL_ResetAttributes();
        return false;
    }
    context = SDL_GL_CreateContext( window );
    SDL_GL_ResetAttributes();
    if( !context ) {
        DebugLog( D_ERROR, D_SDL ) << "voxel GL context failed: " << SDL_GetError();
        return false;
    }
    if( !voxel_gl().load() ) {
        DebugLog( D_ERROR, D_SDL ) << "voxel GL functions missing";
        return false;
    }
    voxel_gl_api &gl = voxel_gl();
    main_program = compile( main_vs, main_fs );
    shadow_program = compile( shadow_vs, shadow_fs );
    if( !main_program || !shadow_program ) {
        return false;
    }
    gl.glGenBuffers( 1, &instance_vbo );
    if( !init_sprites() ) {
        return false;
    }

    max_samples = 1;
    gl.glGetIntegerv( GL_MAX_SAMPLES, &max_samples );
    samples = std::clamp( std::min( max_samples, wanted_samples ), 1, 8 );

    gl.glGenTextures( 1, &shadow_tex );
    gl.glBindTexture( GL_TEXTURE_2D, shadow_tex );
    gl.glTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadow_size, shadow_size, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr );
    gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE );
    gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL );
    gl.glGenFramebuffers( 1, &shadow_fbo );
    gl.glBindFramebuffer( GL_FRAMEBUFFER, shadow_fbo );
    gl.glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_tex, 0 );
    const bool shadow_ok = gl.glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
    gl.glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    if( !shadow_ok ) {
        DebugLog( D_ERROR, D_SDL ) << "voxel shadow framebuffer incomplete";
        return false;
    }
    return true;
}

bool voxel_renderer::init_sprites()
{
    voxel_gl_api &gl = voxel_gl();
    sprite_program = compile( sprite_vs, sprite_fs );
    sprite_shadow_program = compile( sprite_vs, sprite_shadow_fs );
    if( !sprite_program || !sprite_shadow_program ) {
        return false;
    }
    gl.glGenBuffers( 1, &sprite_vbo );
    const std::array<float, 48> quad = { {
            0, 0, 0, 0, 0, 1, 0, 0,
            1, 0, 0, 0, 0, 1, 1, 0,
            1, 1, 0, 0, 0, 1, 1, 1,
            0, 0, 0, 0, 0, 1, 0, 0,
            1, 1, 0, 0, 0, 1, 1, 1,
            0, 1, 0, 0, 0, 1, 0, 1
        }
    };
    std::vector<float> cube;
    const auto face = [&cube]( const std::array<std::array<float, 3>, 4> &c, const std::array<float, 3> &n,
    const std::array<std::array<float, 2>, 4> &uv ) {
        for( const int i : { 0, 1, 2, 0, 2, 3 } ) {
            cube.insert( cube.end(), { c[i][0], c[i][1], c[i][2], n[0], n[1], n[2], uv[i][0], uv[i][1] } );
        }
    };
    const std::array<std::array<float, 2>, 4> side_uv = { { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } } };
    face( { { { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } } }, { 0, 0, 1 }, side_uv );
    face( { { { 1, 0, 0 }, { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 } } }, { 0, 0, -1 }, side_uv );
    face( { { { 1, 0, 1 }, { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 } } }, { 1, 0, 0 }, side_uv );
    face( { { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 } } }, { -1, 0, 0 }, side_uv );
    face( { { { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 }, { 0, 1, 0 } } }, { 0, 1, 0 }, { { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } } } );
    const auto make_vao = [&gl, this]( const float * data, size_t floats, GLuint & vao, GLuint & vbo ) {
        gl.glGenVertexArrays( 1, &vao );
        gl.glGenBuffers( 1, &vbo );
        gl.glBindVertexArray( vao );
        gl.glBindBuffer( GL_ARRAY_BUFFER, vbo );
        gl.glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( floats * sizeof( float ) ), data,
                         GL_STATIC_DRAW );
        const GLsizei stride = 8 * sizeof( float );
        gl.glEnableVertexAttribArray( 0 );
        gl.glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, stride, nullptr );
        gl.glEnableVertexAttribArray( 1 );
        gl.glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>( 3 * sizeof( float ) ) );
        gl.glEnableVertexAttribArray( 2 );
        gl.glVertexAttribPointer( 2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>( 6 * sizeof( float ) ) );
        for( GLuint loc = 3; loc <= 6; ++loc ) {
            gl.glEnableVertexAttribArray( loc );
            gl.glVertexAttribDivisor( loc, 1 );
        }
        gl.glBindBuffer( GL_ARRAY_BUFFER, sprite_vbo );
        gl.glBindVertexArray( 0 );
    };
    make_vao( quad.data(), quad.size(), quad_vao, quad_vbo );
    make_vao( cube.data(), cube.size(), cube_vao, cube_vbo );
    return true;
}

template<typename Next>
const sprite_ref *voxel_renderer::find_sprite( const std::string &id, Next next, bool background ) const
{
    std::string cur = id;
    for( int i = 0; i < 8 && !cur.empty(); ++i ) {
        if( const sprite_ref *s = background ? atlas.bg( cur ) : atlas.fg( cur ) ) {
            return s;
        }
        cur = next( cur );
    }
    return nullptr;
}

void voxel_renderer::add_sprite( sprite_mode mode, const sprite_ref &spr, const vec3 &pos, float yaw,
                                 float lift, float light, float desat, bool flip, float height, float layer )
{
    sprite_instance si{};
    si.x = pos.x;
    si.y = pos.y * level_height + ( mode == sprite_flat ? 0.0f : lift );
    si.z = pos.z;
    si.yaw = yaw;
    si.w = spr.w;
    si.h = spr.h;
    si.layer = mode == sprite_flat ? lift : layer;
    si.light = light;
    si.u0 = flip ? spr.u1 : spr.u0;
    si.u1 = flip ? spr.u0 : spr.u1;
    si.v0 = spr.v0;
    si.v1 = spr.v1;
    si.shift_x = flip ? -spr.shift_x : spr.shift_x;
    si.shift_y = spr.shift_y;
    si.desat = desat;
    si.height = height;
    sprite_batches[ { static_cast<int>( mode ), spr.sheet }].push_back( si );
}

bool voxel_renderer::sprite_terrain( const tripoint_bub_ms &p, const std::string &id,
                                     const tile_style &st, float light, float desat )
{
    if( !st.draw && st.no_floor ) {
        return true;
    }
    const sprite_ref *fg = find_sprite( id, map_looks_like );
    const sprite_ref *bg = find_sprite( id, map_looks_like, true );
    if( fg == nullptr && bg == nullptr ) {
        return false;
    }
    const vec3 pos{ static_cast<float>( p.x() ), static_cast<float>( p.z() ), static_cast<float>( p.y() ) };
    const bool solid = st.kind == vm_kind::wall || st.kind == vm_kind::window || st.kind == vm_kind::door;
    const bool cut = cut_here( p );
    if( solid && fg ) {
        const float h = ( st.kind == vm_kind::wall && st.param == 1 ) || cut ? level_height * 0.5f :
                        level_height;
        add_sprite( sprite_cube, *fg, pos, 0.0f, 0.0f, light, desat, false, h );
        vm_key k;
        k.kind = vm_kind::wall;
        add( shadow_lists, k, p, 0.0f, 0.0f, 1.0f, 0.0f );
        return true;
    }
    if( bg && ( st.overlay || bg->w > 1.01f || bg->h > 1.01f ) ) {
        bg = nullptr;
    }
    if( bg == nullptr && st.overlay ) {
        const char *ground = st.floor_material == vm_material::dirt ? "t_dirt" :
                             st.floor_material == vm_material::concrete ? "t_pavement" :
                             st.floor_material == vm_material::sand ? "t_sand" : "t_grass";
        bg = atlas.fg( ground );
    }
    if( bg ) {
        add_sprite( sprite_flat, *bg, pos, 0.0f, 0.004f, light, desat );
    } else if( st.overlay || st.kind == vm_kind::door_open ) {
        vm_key fk;
        fk.kind = vm_kind::floor;
        fk.material = st.overlay ? st.floor_material : vm_material::concrete;
        fk.color = st.overlay ? st.floor_color : rgb( 150, 148, 142 );
        add( main_lists, fk, p, 0.0f, 0.0f, light, desat );
    }
    if( fg ) {
        const bool stand = st.overlay || fg->h > 1.01f || st.kind == vm_kind::door_open ||
                           st.kind == vm_kind::stairs_up;
        if( stand && cut && fg->h > 1.01f ) {
            return true;
        }
        if( stand ) {
            add_sprite( sprite_billboard, *fg, pos, 0.0f, voxel_size, light, desat );
        } else {
            add_sprite( sprite_flat, *fg, pos, 0.0f, bg ? 0.008f : 0.004f, light, desat );
        }
    }
    return true;
}

bool voxel_renderer::sprite_furniture( const tripoint_bub_ms &p, const std::string &id,
                                       const tile_style &st, float light, float desat )
{
    const sprite_ref *fg = find_sprite( id, map_looks_like );
    if( fg == nullptr ) {
        return false;
    }
    const vec3 pos{ static_cast<float>( p.x() ), static_cast<float>( p.z() ), static_cast<float>( p.y() ) };
    if( st.kind == vm_kind::mat || st.kind == vm_kind::splat ) {
        add_sprite( sprite_flat, *fg, pos, 0.0f, 0.012f, light, desat );
    } else {
        add_sprite( sprite_billboard, *fg, pos, 0.0f, voxel_size, light, desat );
    }
    return true;
}

bool voxel_renderer::sprite_character( const Character &ch, const vec3 &pos, float light )
{
    const std::string base = std::string( ch.is_npc() ? "npc_" : "player_" ) + ( ch.male ? "male" :
                             "female" );
    const sprite_ref *body = atlas.fg( base );
    if( body == nullptr ) {
        return false;
    }
    const bool flip = ch.facing == FacingDirection::LEFT;
    add_sprite( sprite_billboard, *body, pos, 0.0f, voxel_size, light, 0.0f, flip );
    float layer = 0.0f;
    for( const std::pair<std::string, std::string> &overlay : ch.get_overlay_ids() ) {
        std::string over_type;
        std::string looks = overlay.first;
        if( looks.compare( 0, 5, "worn_" ) == 0 ) {
            over_type = "worn_";
            looks = looks.substr( 5 );
        } else if( looks.compare( 0, 8, "wielded_" ) == 0 ) {
            over_type = "wielded_";
            looks = looks.substr( 8 );
        }
        const sprite_ref *spr = nullptr;
        for( int i = 0; i < 8 && !looks.empty() && spr == nullptr; ++i ) {
            spr = atlas.fg( std::string( ch.male ? "overlay_male_" : "overlay_female_" ) + over_type + looks );
            if( spr == nullptr ) {
                spr = atlas.fg( "overlay_" + over_type + looks );
            }
            if( spr == nullptr ) {
                looks = over_type.empty() ? std::string() : item_looks_like( looks );
            }
        }
        if( spr ) {
            layer += 0.004f;
            add_sprite( sprite_billboard, *spr, pos, 0.0f, voxel_size, light, 0.0f, flip, 1.0f, layer );
        }
    }
    return true;
}

void voxel_renderer::upload_sprites()
{
    sprite_ranges.clear();
    std::vector<sprite_instance> all;
    for( const auto &b : sprite_batches ) {
        if( b.second.empty() ) {
            continue;
        }
        sprite_ranges.push_back( { b.first, { all.size(), b.second.size() } } );
        all.insert( all.end(), b.second.begin(), b.second.end() );
    }
    if( all.empty() ) {
        return;
    }
    voxel_gl_api &gl = voxel_gl();
    gl.glBindBuffer( GL_ARRAY_BUFFER, sprite_vbo );
    gl.glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( all.size() * sizeof( sprite_instance ) ),
                     all.data(), GL_STREAM_DRAW );
}

void voxel_renderer::draw_sprite_ranges( GLuint program, bool shadow_pass )
{
    voxel_gl_api &gl = voxel_gl();
    const GLint mode_loc = gl.glGetUniformLocation( program, "u_mode" );
    gl.glActiveTexture( GL_TEXTURE0 );
    for( const auto &r : sprite_ranges ) {
        const int mode = r.first.first;
        if( shadow_pass && mode != sprite_billboard ) {
            continue;
        }
        gl.glUniform1i( mode_loc, mode );
        gl.glBindTexture( GL_TEXTURE_2D, atlas.texture( r.first.second ) );
        const bool cube = mode == sprite_cube;
        gl.glBindVertexArray( cube ? cube_vao : quad_vao );
        gl.glBindBuffer( GL_ARRAY_BUFFER, sprite_vbo );
        const GLsizei stride = sizeof( sprite_instance );
        const size_t base = r.second.first * sizeof( sprite_instance );
        for( GLuint a = 0; a < 4; ++a ) {
            gl.glVertexAttribPointer( 3 + a, 4, GL_FLOAT, GL_FALSE, stride,
                                      reinterpret_cast<void *>( base + a * 4 * sizeof( float ) ) );
        }
        gl.glDrawArraysInstanced( GL_TRIANGLES, 0, cube ? 30 : 6, static_cast<GLsizei>( r.second.second ) );
    }
    gl.glBindVertexArray( 0 );
}

void voxel_renderer::draw_sprite_shadows( const mat4 &light_vp, const vec3 &sun_dir )
{
    if( sprite_ranges.empty() ) {
        return;
    }
    voxel_gl_api &gl = voxel_gl();
    gl.glUseProgram( sprite_shadow_program );
    const auto loc = [&gl, this]( const char *name ) {
        return gl.glGetUniformLocation( sprite_shadow_program, name );
    };
    const vec3 right = normalize( cross( sun_dir, vec3{ 0.0f, 1.0f, 0.0f } ) );
    gl.glUniformMatrix4fv( loc( "u_vp" ), 1, GL_FALSE, light_vp.m.data() );
    gl.glUniform3f( loc( "u_right" ), right.x, right.y, right.z );
    gl.glUniform3f( loc( "u_up" ), 0.0f, 1.0f, 0.0f );
    gl.glUniform3f( loc( "u_toward" ), 0.0f, 0.0f, 0.0f );
    gl.glUniform1i( loc( "u_tex" ), 0 );
    draw_sprite_ranges( sprite_shadow_program, true );
}

void voxel_renderer::draw_sprites( const mat4 &view, const mat4 &view_proj, const mat4 &light_vp,
                                   const vec3 &sun_dir, float daylight, bool use_shadow, const vec3 &focus, float fog_far,
                                   const vec3 &fog )
{
    if( sprite_ranges.empty() ) {
        return;
    }
    voxel_gl_api &gl = voxel_gl();
    gl.glUseProgram( sprite_program );
    const auto loc = [&gl, this]( const char *name ) {
        return gl.glGetUniformLocation( sprite_program, name );
    };
    gl.glUniformMatrix4fv( loc( "u_vp" ), 1, GL_FALSE, view_proj.m.data() );
    gl.glUniformMatrix4fv( loc( "u_light_vp" ), 1, GL_FALSE, light_vp.m.data() );
    gl.glUniform3f( loc( "u_right" ), view.m[0], view.m[4], view.m[8] );
    gl.glUniform3f( loc( "u_up" ), view.m[1], view.m[5], view.m[9] );
    gl.glUniform3f( loc( "u_toward" ), view.m[2], view.m[6], view.m[10] );
    gl.glUniform3f( loc( "u_sun" ), sun_dir.x, sun_dir.y, sun_dir.z );
    gl.glUniform1f( loc( "u_daylight" ), daylight );
    gl.glUniform1f( loc( "u_use_shadow" ), use_shadow ? 1.0f : 0.0f );
    gl.glUniform3f( loc( "u_focus" ), focus.x, focus.y, focus.z );
    gl.glUniform1f( loc( "u_fog_far" ), fog_far );
    gl.glUniform3f( loc( "u_fog_color" ), fog.x, fog.y, fog.z );
    upload_lights( sprite_program );
    gl.glActiveTexture( GL_TEXTURE1 );
    gl.glBindTexture( GL_TEXTURE_2D, shadow_tex );
    gl.glUniform1i( loc( "u_shadow" ), 1 );
    gl.glUniform1i( loc( "u_tex" ), 0 );
    gl.glDisable( GL_CULL_FACE );
    draw_sprite_ranges( sprite_program, false );
    gl.glEnable( GL_CULL_FACE );
}

bool voxel_renderer::ensure_targets( int w, int h )
{
    if( w == target_w && h == target_h && msaa_fbo != 0 ) {
        return true;
    }
    voxel_gl_api &gl = voxel_gl();
    if( msaa_fbo ) {
        gl.glDeleteFramebuffers( 1, &msaa_fbo );
        gl.glDeleteRenderbuffers( 1, &msaa_color );
        gl.glDeleteRenderbuffers( 1, &msaa_depth );
        gl.glDeleteFramebuffers( 1, &resolve_fbo );
        gl.glDeleteRenderbuffers( 1, &resolve_color );
    }
    gl.glGenFramebuffers( 1, &msaa_fbo );
    gl.glGenRenderbuffers( 1, &msaa_color );
    gl.glGenRenderbuffers( 1, &msaa_depth );
    gl.glBindFramebuffer( GL_FRAMEBUFFER, msaa_fbo );
    gl.glBindRenderbuffer( GL_RENDERBUFFER, msaa_color );
    gl.glRenderbufferStorageMultisample( GL_RENDERBUFFER, samples, GL_RGBA8, w, h );
    gl.glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaa_color );
    gl.glBindRenderbuffer( GL_RENDERBUFFER, msaa_depth );
    gl.glRenderbufferStorageMultisample( GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h );
    gl.glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaa_depth );
    const bool msaa_ok = gl.glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;

    gl.glGenFramebuffers( 1, &resolve_fbo );
    gl.glGenRenderbuffers( 1, &resolve_color );
    gl.glBindFramebuffer( GL_FRAMEBUFFER, resolve_fbo );
    gl.glBindRenderbuffer( GL_RENDERBUFFER, resolve_color );
    gl.glRenderbufferStorage( GL_RENDERBUFFER, GL_RGBA8, w, h );
    gl.glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                  resolve_color );
    const bool resolve_ok = gl.glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
    gl.glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    target_w = w;
    target_h = h;
    return msaa_ok && resolve_ok;
}

int voxel_renderer::upload_model( const voxel_mesh &mesh, bool emissive )
{
    voxel_gl_api &gl = voxel_gl();
    gpu_model model;
    model.emissive = emissive;
    const auto upload_mesh = [&gl]( const std::vector<voxel_vertex> &verts, gpu_mesh & out ) {
        if( verts.empty() ) {
            return;
        }
        gl.glGenVertexArrays( 1, &out.vao );
        gl.glGenBuffers( 1, &out.vbo );
        gl.glBindVertexArray( out.vao );
        gl.glBindBuffer( GL_ARRAY_BUFFER, out.vbo );
        gl.glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( verts.size() * sizeof( voxel_vertex ) ),
                         verts.data(), GL_STATIC_DRAW );
        const GLsizei stride = sizeof( voxel_vertex );
        gl.glEnableVertexAttribArray( 0 );
        gl.glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, stride, nullptr );
        gl.glEnableVertexAttribArray( 1 );
        gl.glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<void *>( 3 * sizeof( float ) ) );
        gl.glEnableVertexAttribArray( 2 );
        gl.glVertexAttribPointer( 2, 4, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<void *>( 6 * sizeof( float ) ) );
        for( GLuint loc = 3; loc <= 5; ++loc ) {
            gl.glEnableVertexAttribArray( loc );
            gl.glVertexAttribDivisor( loc, 1 );
        }
        gl.glBindVertexArray( 0 );
        out.count = static_cast<GLsizei>( verts.size() );
    };
    upload_mesh( mesh.opaque, model.opaque );
    upload_mesh( mesh.glass, model.glass );
    models.push_back( model );
    return static_cast<int>( models.size() ) - 1;
}

int voxel_renderer::model_for( const vm_key &key )
{
    const uint64_t packed = key.pack();
    const auto it = model_lookup.find( packed );
    if( it != model_lookup.end() ) {
        return it->second;
    }
    const bool emissive = vm_is_emissive( key.kind ) || ( key.kind == vm_kind::window && key.variant == 5 );
    const int idx = upload_model( build_voxel_mesh( make_voxel_model( key ) ), emissive );
    model_lookup.emplace( packed, idx );
    return idx;
}

void voxel_renderer::add( instance_lists &lists, const vm_key &key, const tripoint_bub_ms &p,
                          float lift, float yaw, float light, float desat, float scale, const voxel_rgb &tint )
{
    add_at( lists, key, vec3{ static_cast<float>( p.x() ), static_cast<float>( p.z() ), static_cast<float>( p.y() ) },
            lift, yaw, light, desat, scale, tint );
}

vec3 voxel_renderer::animated( const void *id, const tripoint_bub_ms &p )
{
    constexpr float duration = 140.0f;
    map &here = get_map();
    const tripoint_abs_ms abs = here.get_abs( p );
    motion &m = motions[id];
    if( m.start == 0 || m.to != abs ) {
        const tripoint_rel_ms jump = abs - m.to;
        const bool near = m.start != 0 && std::abs( jump.x() ) <= 2 && std::abs( jump.y() ) <= 2 &&
                          jump.z() == 0;
        if( near ) {
            const float t = std::clamp( static_cast<float>( now_ms - m.start ) / duration, 0.0f, 1.0f );
            m.from = t >= 1.0f ? m.to : ( t < 0.5f ? m.from : m.to );
        } else {
            m.from = abs;
        }
        m.to = abs;
        m.start = now_ms;
    }
    m.seen = true;
    float t = std::clamp( static_cast<float>( now_ms - m.start ) / duration, 0.0f, 1.0f );
    if( t < 1.0f && m.from != m.to ) {
        animating = true;
    }
    const float bob = m.from != m.to ? std::sin( t * pi ) * 0.07f / level_height : 0.0f;
    t = t * t * ( 3.0f - 2.0f * t );
    const tripoint_rel_ms d = m.to - m.from;
    return vec3{ static_cast<float>( p.x() ) - d.x() * ( 1.0f - t ), static_cast<float>( p.z() ) + bob,
                 static_cast<float>( p.y() ) - d.y() * ( 1.0f - t ) };
}

void voxel_renderer::reset_models()
{
    voxel_gl_api &gl = voxel_gl();
    for( gpu_model &m : models ) {
        for( gpu_mesh *mesh : { &m.opaque, &m.glass } ) {
            if( mesh->vao ) {
                gl.glDeleteVertexArrays( 1, &mesh->vao );
                gl.glDeleteBuffers( 1, &mesh->vbo );
            }
        }
    }
    models.clear();
    model_lookup.clear();
    custom_lookup.clear();
    ter_styles.clear();
    furn_styles.clear();
    main_lists.per_model.clear();
    main_lists.touched.clear();
    shadow_lists.per_model.clear();
    shadow_lists.touched.clear();
}

void voxel_renderer::sync_voxelset()
{
    const int gen = active_voxelset_generation();
    if( gen != vset_generation ) {
        reset_models();
        vset_generation = gen;
    }
    vset = active_voxelset();
}

int voxel_renderer::custom_model( const voxelset_entry &entry, uint32_t pick )
{
    const std::string &file = entry.files[pick % entry.files.size()];
    const std::string path = vset->dir + "/" + file;
    const auto it = custom_lookup.find( path );
    if( it != custom_lookup.end() ) {
        return it->second;
    }
    std::string error;
    const std::optional<voxel_grid> grid = load_vox_file( path, vset->voxels_per_tile, error );
    int idx = -1;
    if( grid ) {
        voxel_grid g = *grid;
        g.ox -= entry.offset[0];
        g.oz -= entry.offset[2];
        const voxel_mesh mesh = build_voxel_mesh( g, 1.0f / static_cast<float>( vset->voxels_per_tile ) );
        idx = upload_model( mesh, entry.emissive );
    } else {
        DebugLog( D_WARNING, D_SDL ) << "voxel tileset model failed: " << error;
    }
    custom_lookup.emplace( path, idx );
    return idx;
}

void voxel_renderer::add_at( instance_lists &lists, const vm_key &key, const vec3 &pos, float lift,
                             float yaw, float light, float desat, float scale, const voxel_rgb &tint,
                             const voxelset_entry *custom, uint32_t pick )
{
    const voxelset_entry *entry = custom;
    if( entry == nullptr && vset != nullptr ) {
        entry = vset->find_builtin( key.kind, key.material );
    }
    int m = -1;
    voxel_rgb final_tint = tint;
    if( entry != nullptr ) {
        m = custom_model( *entry, pick );
        if( m >= 0 ) {
            if( !entry->rotates ) {
                yaw = 0.0f;
            }
            if( entry->random_rotation ) {
                yaw = static_cast<float>( ( pick >> 3 ) % 4 ) * pi * 0.5f;
            }
            scale *= entry->scale;
            lift += static_cast<float>( entry->offset[1] ) / static_cast<float>( vset->voxels_per_tile );
            if( entry->tint ) {
                final_tint = key.color;
            }
        }
    }
    if( m < 0 ) {
        m = model_for( key );
    }
    const voxel_rgb &tint_used = final_tint;
    instance inst{};
    inst.x = pos.x;
    inst.y = pos.y * level_height + lift;
    inst.z = pos.z;
    inst.yaw = yaw;
    inst.r = tint_used.r / 255.0f;
    inst.g = tint_used.g / 255.0f;
    inst.b = tint_used.b / 255.0f;
    inst.light = light;
    inst.scale = scale;
    inst.emissive = models[m].emissive ? 1.0f : 0.0f;
    inst.desat = desat;
    inst.pad = key.kind == vm_kind::water ? 1.0f : 0.0f;
    if( key.kind == vm_kind::water || key.kind == vm_kind::fire ) {
        ambient = true;
    }
    lists.add( m, inst );
}

const tile_style &voxel_renderer::terrain_style( const ter_id &t )
{
    const auto it = ter_styles.find( t.to_i() );
    if( it != ter_styles.end() ) {
        return it->second;
    }
    tile_style st = classify_terrain( t.obj() );
    st.custom = find_chain( vset, t.id().str(), map_looks_like );
    if( st.custom ) {
        st.draw = true;
    }
    return ter_styles.emplace( t.to_i(), st ).first->second;
}

const tile_style &voxel_renderer::furniture_style( const furn_id &f )
{
    const auto it = furn_styles.find( f.to_i() );
    if( it != furn_styles.end() ) {
        return it->second;
    }
    tile_style st = classify_furniture( f.obj() );
    st.custom = find_chain( vset, f.id().str(), map_looks_like );
    if( st.custom ) {
        st.draw = true;
    }
    return furn_styles.emplace( f.to_i(), st ).first->second;
}

bool voxel_renderer::wall_like_at( const tripoint_bub_ms &p )
{
    map &here = get_map();
    if( !here.inbounds( p ) ) {
        return false;
    }
    const ter_t &t = here.ter( p ).obj();
    return terrain_style( here.ter( p ) ).wall_like ||
           t.has_flag( ter_furn_flag::TFLAG_CONNECT_WITH_WALL );
}

bool voxel_renderer::fence_like_at( const tripoint_bub_ms &p )
{
    map &here = get_map();
    if( !here.inbounds( p ) ) {
        return false;
    }
    if( terrain_style( here.ter( p ) ).fence || wall_like_at( p ) ) {
        return true;
    }
    return here.has_furn( p ) && furniture_style( here.furn( p ) ).fence;
}

void voxel_renderer::add_style( const tile_style &st, const tripoint_bub_ms &p, float light,
                                float desat, bool furniture )
{
    if( !st.draw ) {
        return;
    }
    const uint32_t h = voxel_hash( p.x(), p.y(), p.z(), 1337u );
    instance_lists &lists = drawing_shadow ? shadow_lists : main_lists;
    if( st.overlay ) {
        vm_key fk;
        fk.kind = vm_kind::floor;
        fk.material = st.floor_material;
        fk.color = st.floor_color;
        fk.variant = static_cast<uint8_t>( h % 4 );
        add( lists, fk, p, 0.0f, static_cast<float>( ( h >> 8 ) % 4 ) * pi * 0.5f, light, desat );
    }
    vm_key k;
    k.kind = st.kind;
    k.material = st.material;
    k.color = st.color;
    k.color2 = st.color2;
    k.variant = st.variants > 1 ? static_cast<uint8_t>( st.variant + h % st.variants ) : st.variant;
    k.param = st.param;
    if( st.oriented ) {
        k.param = orientation_at( p );
    }
    if( st.fence ) {
        uint8_t mask = 0;
        mask |= fence_like_at( p + tripoint_rel_ms::north ) ? 1 : 0;
        mask |= fence_like_at( p + tripoint_rel_ms::east ) ? 2 : 0;
        mask |= fence_like_at( p + tripoint_rel_ms::south ) ? 4 : 0;
        mask |= fence_like_at( p + tripoint_rel_ms::west ) ? 8 : 0;
        k.param = mask;
    }
    if( st.stripes ) {
        map &here = get_map();
        const ter_id self = here.ter( p );
        const bool ns = ( here.inbounds( p + tripoint_rel_ms::north ) &&
                          here.ter( p + tripoint_rel_ms::north ) == self ) ||
                        ( here.inbounds( p + tripoint_rel_ms::south ) && here.ter( p + tripoint_rel_ms::south ) == self );
        k.param = ns ? 1 : 2;
    }
    const float yaw = st.spin ? static_cast<float>( ( h >> 4 ) % 4 ) * pi * 0.5f : 0.0f;
    float lift = furniture ? voxel_size : 0.0f;
    if( st.kind == vm_kind::fire ) {
        lift = voxel_size;
    }
    const voxelset_entry *custom = st.custom;
    if( !drawing_shadow && cut_here( p ) ) {
        if( st.kind == vm_kind::wall || st.kind == vm_kind::window || st.kind == vm_kind::door ||
            st.kind == vm_kind::door_open ) {
            k.kind = vm_kind::wall;
            k.param = 1;
            k.variant = 0;
            if( st.kind != vm_kind::wall ) {
                k.material = vm_material::generic;
                k.color = st.kind == vm_kind::window ? st.color : st.color2;
            }
            custom = nullptr;
        } else if( st.kind == vm_kind::tree || st.kind == vm_kind::pine || st.kind == vm_kind::dead_tree ||
                   st.kind == vm_kind::young_tree || st.kind == vm_kind::cabinet || st.kind == vm_kind::lamp ||
                   st.kind == vm_kind::pole || st.kind == vm_kind::hedge ) {
            k.kind = vm_kind::stump;
            k.color = st.color2;
            k.variant = 0;
            k.param = 0;
            custom = nullptr;
        }
    }
    add_at( lists, k, vec3{ static_cast<float>( p.x() ), static_cast<float>( p.z() ), static_cast<float>( p.y() ) },
            lift, yaw, light, desat, 1.0f, { 255, 255, 255 }, custom, h );
    if( night && k.kind == vm_kind::window && k.variant != 3 && desat == 0.0f && light > 0.55f ) {
        vm_key glow = k;
        glow.variant = 5;
        glow.color = rgb( 255, 205, 130 );
        glow.color2 = rgb( 255, 205, 130 );
        add_at( lists, glow, vec3{ static_cast<float>( p.x() ), static_cast<float>( p.z() ), static_cast<float>( p.y() ) },
                lift, yaw, light, 0.0f );
    }
}

bool voxel_renderer::cut_here( const tripoint_bub_ms &p ) const
{
    if( !cutaway || p.z() != cut_center.z() ) {
        return false;
    }
    const float dx = static_cast<float>( p.x() - cut_center.x() );
    const float dz = static_cast<float>( p.y() - cut_center.y() );
    const float cx = std::sin( cam.yaw );
    const float cz = std::cos( cam.yaw );
    const float along = dx * cx + dz * cz;
    const float lateral = std::fabs( dx * cz - dz * cx );
    const float reach = 3.0f + 9.0f * std::cos( cam.pitch );
    return along > 0.3f && along < reach && lateral < 1.6f + along * 0.45f;
}

uint8_t voxel_renderer::orientation_at( const tripoint_bub_ms &p )
{
    const bool n = wall_like_at( p + tripoint_rel_ms::north );
    const bool s = wall_like_at( p + tripoint_rel_ms::south );
    const bool e = wall_like_at( p + tripoint_rel_ms::east );
    const bool w = wall_like_at( p + tripoint_rel_ms::west );
    if( e && w ) {
        return 0;
    }
    if( n && s ) {
        return 1;
    }
    if( ( n || s ) && !( e || w ) ) {
        return 1;
    }
    return 0;
}

float light_value( float ambient, lit_level ll )
{
    float b = 0.3f + 0.8f * std::clamp( std::sqrt( std::max( ambient, 0.0f ) / 80.0f ), 0.0f, 1.0f );
    if( ll == lit_level::LOW ) {
        b = std::min( b, 0.42f );
    } else if( ll == lit_level::BRIGHT ) {
        b = std::max( b, 0.95f );
    }
    return std::clamp( b, 0.3f, 1.1f );
}

void voxel_renderer::collect( const tripoint_bub_ms &center, int radius, float daylight )
{
    map &here = get_map();
    avatar &you = get_avatar();
    const visibility_variables &cache = here.get_visibility_variables_cache();
    sync_voxelset();
    cutaway = get_option<bool>( "VOXEL_CUTAWAY" );
    night = daylight < 0.25f;
    cut_center = get_avatar().pos_bub( get_map() );
    sprite_style = get_option<std::string>( "VOXEL_STYLE" ) == "sprites";
    if( sprite_style ) {
        const std::string tileset = get_option<std::string>( "TILES" );
        if( atlas.loaded_id != tileset ) {
            atlas.load( tileset );
        }
        sprite_style = atlas.ok;
    }
    for( std::pair<const std::pair<int, int>, std::vector<sprite_instance>> &b : sprite_batches ) {
        b.second.clear();
    }
    main_lists.clear();
    shadow_lists.clear();
    drawing_shadow = false;
    now_ms = SDL_GetTicks();
    animating = false;
    ambient = false;
    light_pos.clear();
    light_col.clear();
    for( std::pair<const void *const, motion> &m : motions ) {
        m.second.seen = false;
    }

    const int x0 = std::max( 0, center.x() - radius );
    const int x1 = std::min( MAPSIZE_X - 1, center.x() + radius );
    const int y0 = std::max( 0, center.y() - radius );
    const int y1 = std::min( MAPSIZE_Y - 1, center.y() + radius );
    const int min_z = std::max( -OVERMAP_DEPTH, center.z() - 3 );
    const float memory_light = 0.42f;
    const float memory_desat = 0.75f;
    const int columns_w = x1 - x0 + 1;
    std::vector<bool> known_column( static_cast<size_t>( columns_w ) * ( y1 - y0 + 1 ), false );

    for( int y = y0; y <= y1; ++y ) {
        for( int x = x0; x <= x1; ++x ) {
            for( int z = center.z(); z >= min_z; --z ) {
                const tripoint_bub_ms p( x, y, z );
                const level_cache &lc = here.access_cache( z );
                const lit_level ll = lc.visibility_cache[x][y];
                const bool seen = here.get_visibility( ll, cache ) == visibility_type::CLEAR;
                const ter_id ter = here.ter( p );
                const tile_style &ts = terrain_style( ter );
                if( z == center.z() && ( seen || you.has_memory_at( here.get_abs( p ) ) ) ) {
                    known_column[static_cast<size_t>( y - y0 ) * columns_w + ( x - x0 )] = true;
                }
                if( seen ) {
                    const float light = light_value( here.ambient_light_at( p ), ll );
                    if( !sprite_style || !sprite_terrain( p, ter.id().str(), ts, light, 0.0f ) ) {
                        add_style( ts, p, light, 0.0f, false );
                    }
                    if( here.has_furn( p ) ) {
                        const furn_id fu = here.furn( p );
                        const tile_style &fs = furniture_style( fu );
                        if( !sprite_style || !sprite_furniture( p, fu.id().str(), fs, light, 0.0f ) ) {
                            add_style( fs, p, light, 0.0f, true );
                        }
                    }
                    const optional_vpart_position ovp = here.veh_at( p );
                    if( ovp ) {
                        const vehicle &veh = ovp->vehicle();
                        const float veh_yaw = static_cast<float>( units::to_radians( veh.face.dir() ) ) + pi * 0.5f;
                        bool vehicle_sprite = false;
                        const sprite_ref *veh_sprite = nullptr;
                        if( sprite_style ) {
                            const vpart_display svd = veh.get_display_of_tile( ovp->mount_pos() );
                            if( !svd.id.is_null() ) {
                                const sprite_ref *vs = atlas.fg( svd.get_tileset_id() );
                                if( vs == nullptr ) {
                                    vs = find_sprite( "vp_" + svd.id.str(), vpart_looks_like );
                                }
                                if( vs ) {
                                    veh_sprite = vs;
                                    vehicle_sprite = true;
                                }
                            }
                        }
                        vm_key vk;
                        vk.kind = vm_kind::vehicle;
                        vk.color2 = rgb( 70, 60, 55 );
                        bool have_body_color = false;
                        bool any = false;
                        for( const int idx : veh.parts_at_relative( ovp->mount_pos(), true ) ) {
                            const vehicle_part &vp = veh.part( idx );
                            if( vp.removed ) {
                                continue;
                            }
                            any = true;
                            const vpart_info &vi = vp.info();
                            const voxel_rgb pc = soften( to_rgb( vi.color ) );
                            if( vp.is_broken() ) {
                                vk.variant |= vmx_broken;
                            }
                            if( vi.has_flag( VPFLAG_WHEEL ) ) {
                                vk.param |= vmv_wheel;
                            }
                            if( vi.has_flag( VPFLAG_WINDOW ) ) {
                                vk.param |= vmv_window;
                            }
                            if( vi.has_flag( VPFLAG_OPENABLE ) && !vi.has_flag( VPFLAG_WINDOW ) ) {
                                vk.variant |= vp.open ? vmx_door_open : vmx_door;
                                if( !have_body_color ) {
                                    vk.color = pc;
                                    have_body_color = true;
                                }
                            } else if( vi.has_flag( VPFLAG_OPAQUE ) && vi.has_flag( VPFLAG_OBSTACLE ) ) {
                                vk.param |= vmv_board;
                                vk.color = pc;
                                have_body_color = true;
                            }
                            if( vi.has_flag( "SEAT" ) || vi.has_flag( "BED" ) ) {
                                vk.param |= vmv_seat;
                                vk.color2 = voxel_shade( pc, 0.8f );
                            }
                            if( vi.has_flag( VPFLAG_ROOF ) ) {
                                vk.param |= vmv_roof;
                                if( !have_body_color ) {
                                    vk.color = pc;
                                }
                            }
                            if( vi.has_flag( VPFLAG_ENGINE ) ) {
                                vk.param |= vmv_engine;
                            }
                            if( vi.has_flag( VPFLAG_CONE_LIGHT ) || vi.has_flag( VPFLAG_WIDE_CONE_LIGHT ) ||
                                vi.has_flag( VPFLAG_HALF_CIRCLE_LIGHT ) ) {
                                vk.param |= vmv_light;
                                if( vp.enabled && !vp.is_broken() ) {
                                    add_light( p, 0.7f, 9.0f, 0.95f, 0.9f, 0.75f );
                                }
                            }
                            if( ( vi.has_flag( VPFLAG_DOME_LIGHT ) || vi.has_flag( VPFLAG_AISLE_LIGHT ) ||
                                  vi.has_flag( VPFLAG_CIRCLE_LIGHT ) ) && vp.enabled && !vp.is_broken() ) {
                                add_light( p, 1.2f, 6.0f, 0.9f, 0.88f, 0.8f );
                            }
                            if( vi.has_flag( "TURRET" ) ) {
                                vk.param |= vmv_turret;
                            }
                            if( vi.has_flag( VPFLAG_CARGO ) && !vi.has_flag( VPFLAG_WHEEL ) ) {
                                vk.variant |= vmx_cargo;
                            }
                            if( vi.has_flag( VPFLAG_FLUIDTANK ) ) {
                                vk.variant |= vmx_tank;
                            }
                            if( vi.has_flag( VPFLAG_SOLAR_PANEL ) ) {
                                vk.variant |= vmx_solar;
                            }
                            if( !have_body_color && vi.location == vpart_location_id( "structure" ) ) {
                                vk.color = pc;
                            }
                        }
                        if( any && vehicle_sprite ) {
                            vk.param &= static_cast<uint8_t>( ~( vmv_seat | vmv_engine | vmv_turret | vmv_light ) );
                            vk.variant &= static_cast<uint8_t>( ~( vmx_cargo | vmx_tank | vmx_solar ) );
                            const bool shell = ( vk.param & ( vmv_board | vmv_window ) ) || ( vk.variant & vmx_door );
                            const float top = ( vk.param & vmv_roof ) ? 1.51f : shell ? 1.385f : 0.51f;
                            add_sprite( sprite_flat, *veh_sprite, vec3{ static_cast<float>( p.x() ), static_cast<float>( p.z() ), static_cast<float>( p.y() ) }, veh_yaw, top, light,
                                        0.0f );
                        }
                        if( any ) {
                            if( vk.color.r == 0 && vk.color.g == 0 && vk.color.b == 0 ) {
                                vk.color = rgb( 120, 122, 128 );
                            }
                            if( vk.variant & vmx_cargo && vk.param & vmv_seat ) {
                                vk.variant &= ~vmx_cargo;
                            }
                            const float yaw = static_cast<float>( units::to_radians( veh.face.dir() ) ) + pi * 0.5f;
                            const vpart_display vd = veh.get_display_of_tile( ovp->mount_pos() );
                            const voxelset_entry *ve = vd.id.is_null() ? nullptr : find_chain( vset, "vp_" + vd.id.str(),
                                                       vpart_looks_like );
                            add_at( main_lists, vk, vec3{ static_cast<float>( p.x() ), static_cast<float>( p.z() ), static_cast<float>( p.y() ) }, 0.0f, yaw, light, 0.0f, 1.0f, { 255, 255, 255 }, ve );
                        }
                    }
                    const sprite_ref *trap_sprite = nullptr;
                    if( sprite_style && here.can_see_trap_at( p, you ) ) {
                        trap_sprite = atlas.fg( here.tr_at( p ).id.str() );
                        if( trap_sprite ) {
                            add_sprite( sprite_flat, *trap_sprite, vec3{ static_cast<float>( p.x() ), static_cast<float>( p.z() ), static_cast<float>( p.y() ) }, 0.0f, 0.016f, light, 0.0f );
                        }
                    }
                    if( trap_sprite == nullptr && here.can_see_trap_at( p, you ) ) {
                        const trap &tr = here.tr_at( p );
                        vm_key tk;
                        tk.kind = vm_kind::trap;
                        tk.color = soften( to_rgb( tr.color ) );
                        add_at( main_lists, tk, vec3{ static_cast<float>( p.x() ), static_cast<float>( p.z() ), static_cast<float>( p.y() ) }, voxel_size, 0.0f, light, 0.0f, 1.0f, { 255, 255, 255 },
                                find_chain( vset, tr.id.str(), no_looks_like ) );
                    }
                    if( here.has_items( p ) ) {
                        map_stack stack = here.i_at( p );
                        int count = 0;
                        const item *top = nullptr;
                        for( const item &it : stack ) {
                            if( count == 0 ) {
                                top = &it;
                            }
                            ++count;
                        }
                        const float lift = ovp ? voxel_size * 4.0f : here.has_furn( p ) &&
                                           furniture_style( here.furn( p ) ).kind == vm_kind::table ? voxel_size * 6.0f : voxel_size;
                        const uint32_t ih = voxel_hash( x, y, z, 5 );
                        const sprite_ref *item_sprite = sprite_style && top ? find_sprite( top->typeId().str(),
                                                        item_looks_like ) : nullptr;
                        if( item_sprite ) {
                            add_sprite( sprite_flat, *item_sprite, vec3{ static_cast<float>( p.x() ), static_cast<float>( p.z() ), static_cast<float>( p.y() ) }, 0.0f, lift + 0.02f, light, 0.0f );
                        } else if( top ) {
                            vm_key ik;
                            ik.kind = item_kind( *top );
                            ik.color = soften( to_rgb( top->color() ) );
                            if( ik.kind == vm_kind::item_corpse ) {
                                ik.color = voxel_mix( ik.color, rgb( 140, 120, 100 ), 0.5f );
                            }
                            const voxelset_entry *ie = find_chain( vset, top->typeId().str(), item_looks_like );
                            if( ie ) {
                                add_at( main_lists, ik, vec3{ static_cast<float>( p.x() ), static_cast<float>( p.z() ), static_cast<float>( p.y() ) }, lift, static_cast<float>( ih % 8 ) * pi * 0.25f, light, 0.0f,
                                        1.0f, { 255, 255, 255 }, ie, ih );
                            } else if( ik.kind == vm_kind::item_pile ) {
                                ik.variant = static_cast<uint8_t>( std::min( count, 3 ) - 1 + 3 * ( ih % 3 ) );
                                add( main_lists, ik, p, lift, 0.0f, light, 0.0f, 1.0f, ik.color );
                            } else {
                                ik.variant = static_cast<uint8_t>( ih % 2 );
                                add( main_lists, ik, p, lift, static_cast<float>( ih % 8 ) * pi * 0.25f, light, 0.0f );
                            }
                        }
                        if( count > 1 && !item_sprite ) {
                            vm_key pk;
                            pk.kind = vm_kind::item_pile;
                            pk.variant = static_cast<uint8_t>( std::min( count - 1, 3 ) - 1 + 3 * ( ( ih >> 4 ) % 3 ) );
                            add( main_lists, pk, p, lift, pi * 0.5f, light, 0.0f, 0.8f, rgb( 200, 190, 170 ) );
                        }
                    }
                    const field &fld = here.field_at( p );
                    const field_type_id ftype = fld.displayed_field_type();
                    if( ftype ) {
                        const field_entry *fe = fld.find_field( ftype );
                        if( fe ) {
                            const std::string fid = ftype.id().str();
                            const int intensity = std::clamp( fe->get_field_intensity(), 1, 3 );
                            vm_key fk;
                            fk.color = soften( to_rgb( fe->color() ) );
                            float lift = voxel_size;
                            bool draw = true;
                            if( has_any( fid, "fire", "flame", "incendiary" ) ) {
                                fk.kind = vm_kind::fire;
                                fk.variant = static_cast<uint8_t>( intensity - 1 );
                                add_light( p, 0.6f, 3.0f + 2.0f * intensity, 1.0f * ( 0.5f + 0.25f * intensity ),
                                           0.55f * ( 0.5f + 0.25f * intensity ), 0.2f * ( 0.5f + 0.25f * intensity ) );
                            } else if( has_any( fid, "blood", "bile", "gibs", "acid", "slime", "sap", "sludge", "oil",
                                                "juice", "goo", "mess" ) ) {
                                fk.kind = vm_kind::splat;
                                fk.variant = static_cast<uint8_t>( voxel_hash( x, y, z, 9 ) % 4 );
                            } else if( has_any( fid, "smoke", "gas", "fog", "mist", "haze", "cloud", "toxic", "tear",
                                                "miasma", "relax", "nuke", "fume", "steam" ) ) {
                                fk.kind = vm_kind::cloud;
                                fk.variant = static_cast<uint8_t>( intensity );
                                lift = 0.0f;
                            } else {
                                draw = find_chain( vset, fid, no_looks_like ) != nullptr;
                            }
                            const sprite_ref *field_sprite = sprite_style ? atlas.fg( fid ) : nullptr;
                            if( field_sprite ) {
                                add_sprite( fk.kind == vm_kind::fire ? sprite_billboard : sprite_flat, *field_sprite, vec3{ static_cast<float>( p.x() ), static_cast<float>( p.z() ), static_cast<float>( p.y() ) }, 0.0f,
                                            fk.kind == vm_kind::fire ? voxel_size : 0.02f, light, 0.0f );
                            } else if( draw ) {
                                add_at( main_lists, fk, vec3{ static_cast<float>( p.x() ), static_cast<float>( p.z() ), static_cast<float>( p.y() ) }, lift, 0.0f, light, 0.0f, 1.0f, { 255, 255, 255 },
                                        find_chain( vset, fid, no_looks_like ), voxel_hash( x, y, z, 11 ) );
                            }
                        }
                    }
                } else {
                    const tripoint_abs_ms abs = here.get_abs( p );
                    if( you.has_memory_at( abs ) ) {
                        const memorized_tile &mt = you.get_memorized_tile( abs );
                        const ter_str_id tid( mt.get_ter_id() );
                        if( !mt.get_ter_id().empty() && tid.is_valid() ) {
                            const tile_style &mts = terrain_style( tid.id() );
                            if( !sprite_style || !sprite_terrain( p, mt.get_ter_id(), mts, memory_light, memory_desat ) ) {
                                add_style( mts, p, memory_light, memory_desat, false );
                            }
                        }
                        const std::string &dec = mt.get_dec_id();
                        if( dec.compare( 0, 2, "f_" ) == 0 ) {
                            const furn_str_id fid( dec );
                            if( fid.is_valid() ) {
                                const tile_style &mfs = furniture_style( fid.id() );
                                if( !sprite_style || !sprite_furniture( p, dec, mfs, memory_light, memory_desat ) ) {
                                    add_style( mfs, p, memory_light, memory_desat, true );
                                }
                            }
                        } else if( dec.compare( 0, 3, "vp_" ) == 0 ) {
                            vm_key vk;
                            vk.kind = vm_kind::veh_frame;
                            vk.color = rgb( 120, 120, 125 );
                            add( main_lists, vk, p, 0.0f, 0.0f, memory_light, memory_desat );
                        }
                    }
                }
                if( !ts.no_floor ) {
                    break;
                }
            }
        }
    }

    if( daylight > 0.02f && center.z() < OVERMAP_HEIGHT ) {
        drawing_shadow = true;
        const int sr = radius + 6;
        for( int up = 1; up <= 2; ++up ) {
            const int z = center.z() + up;
            if( z > OVERMAP_HEIGHT ) {
                break;
            }
            for( int y = std::max( 0, center.y() - sr ); y <= std::min( MAPSIZE_Y - 1, center.y() + sr ); ++y ) {
                for( int x = std::max( 0, center.x() - sr ); x <= std::min( MAPSIZE_X - 1, center.x() + sr ); ++x ) {
                    const tripoint_bub_ms p( x, y, z );
                    const tile_style &ts = terrain_style( here.ter( p ) );
                    if( ts.no_floor ) {
                        continue;
                    }
                    if( ts.kind == vm_kind::wall || ts.kind == vm_kind::window || ts.kind == vm_kind::door ) {
                        vm_key k;
                        k.kind = vm_kind::wall;
                        add( shadow_lists, k, p, 0.0f, 0.0f, 1.0f, 0.0f );
                    } else {
                        vm_key k;
                        k.kind = vm_kind::floor;
                        add( shadow_lists, k, p, 0.0f, 0.0f, 1.0f, 0.0f );
                    }
                }
            }
        }
        drawing_shadow = false;
    }

    const tripoint_bub_ms you_pos = you.pos_bub( here );
    if( you_pos != last_avatar_pos ) {
        const point d = ( you_pos.xy() - last_avatar_pos.xy() ).raw();
        if( d != point::zero && std::abs( d.x ) <= 1 && std::abs( d.y ) <= 1 ) {
            avatar_yaw = std::atan2( static_cast<float>( d.x ), static_cast<float>( -d.y ) );
        }
        last_avatar_pos = you_pos;
    }

    const auto in_view = [&]( const tripoint_bub_ms & p ) {
        return p.z() <= center.z() && p.z() >= min_z && std::abs( p.x() - center.x() ) <= radius &&
               std::abs( p.y() - center.y() ) <= radius;
    };
    const auto face_towards = []( const tripoint_bub_ms & from, const tripoint_bub_ms & to ) {
        const point d = ( to.xy() - from.xy() ).raw();
        if( d == point::zero ) {
            return 0.0f;
        }
        return std::atan2( static_cast<float>( d.x ), static_cast<float>( -d.y ) );
    };
    const auto creature_light = [&]( const tripoint_bub_ms & p ) {
        const lit_level ll = here.access_cache( p.z() ).visibility_cache[p.x()][p.y()];
        return std::max( light_value( here.ambient_light_at( p ), ll ), 0.5f );
    };

    const auto add_character = [&]( const Character & ch, const tripoint_bub_ms & p, const vm_key & k,
    float yaw ) {
        const float light = creature_light( p );
        const float scale = size_scale( ch.get_size() );
        const vec3 pos = animated( &ch, p );
        if( &ch == &you ) {
            avatar_display = pos;
        }
        if( sprite_style && sprite_character( ch, pos, light ) ) {
            return;
        }
        const bool is_avatar = &ch == &you;
        const std::string base = is_avatar ? "player" : "npc";
        const voxelset_entry *ce = vset ? vset->find_id( base + ( ch.male ? "_male" : "_female" ) ) : nullptr;
        if( ce == nullptr && vset ) {
            ce = vset->find_id( base );
        }
        add_at( main_lists, k, pos, voxel_size, yaw, light, 0.0f, scale, flash_tint( ch ), ce );
        if( const std::optional<vm_key> hk = held_key( ch ) ) {
            add_at( main_lists, *hk, pos, voxel_size, yaw, light, 0.0f, scale );
        }
    };
    const float own_light = you.active_light();
    if( own_light > 0.0f ) {
        add_light( you_pos, 1.0f, std::clamp( std::sqrt( own_light ) * 2.0f, 3.0f, 14.0f ), 0.85f, 0.78f,
                   0.6f );
    }
    if( get_option<bool>( "VOXEL_UPPER_FLOORS" ) && here.is_outside( you_pos ) ) {
        const float cx = std::sin( cam.yaw );
        const float cz = std::cos( cam.yaw );
        for( int up = 1; up <= 2; ++up ) {
            const int z = center.z() + up;
            if( z > OVERMAP_HEIGHT ) {
                break;
            }
            for( int y = y0; y <= y1; ++y ) {
                for( int x = x0; x <= x1; ++x ) {
                    if( !known_column[static_cast<size_t>( y - y0 ) * columns_w + ( x - x0 )] ) {
                        continue;
                    }
                    const float dx = static_cast<float>( x - you_pos.x() );
                    const float dy = static_cast<float>( y - you_pos.y() );
                    if( dx * dx + dy * dy < 20.0f ) {
                        continue;
                    }
                    const float along = dx * cx + dy * cz;
                    const float lateral = std::fabs( dx * cz - dy * cx );
                    if( along > 0.0f && along < 16.0f && lateral < 2.5f + along * 0.5f ) {
                        continue;
                    }
                    const tripoint_bub_ms p( x, y, z );
                    const ter_id ter = here.ter( p );
                    const tile_style &ts = terrain_style( ter );
                    if( ts.no_floor || !ts.draw ) {
                        continue;
                    }
                    const float light = light_value( here.ambient_light_at( p ), lit_level::LIT );
                    if( !sprite_style || !sprite_terrain( p, ter.id().str(), ts, light, 0.0f ) ) {
                        add_style( ts, p, light, 0.0f, false );
                    }
                }
            }
        }
    }
    avatar_display.reset();
    if( in_view( you_pos ) ) {
        add_character( you, you_pos, character_key( you, rgb( 226, 184, 146 ), rgb( 58, 98, 168 ), 0,
                       false ), avatar_yaw );
    }
    for( const npc &guy : g->all_npcs() ) {
        const tripoint_bub_ms p = guy.pos_bub( here );
        if( !in_view( p ) || !you.sees( here, guy ) ) {
            continue;
        }
        const uint32_t h = string_hash( guy.get_name() );
        add_character( guy, p, character_key( guy, palette_pick( skin_palette(), h ),
                                              palette_pick( shirt_palette(), h >> 5 ), static_cast<uint8_t>( h % 3 ), false ),
                       face_towards( p, you_pos ) );
    }
    for( const monster &mon : g->all_monsters() ) {
        const tripoint_bub_ms p = mon.pos_bub( here );
        if( !in_view( p ) || !you.sees( here, mon ) ) {
            continue;
        }
        const vec3 mpos = animated( &mon, p );
        if( sprite_style ) {
            if( const sprite_ref *ms = find_sprite( mon.type->id.str(), monster_looks_like ) ) {
                add_sprite( sprite_billboard, *ms, mpos, 0.0f, voxel_size, creature_light( p ), 0.0f,
                            mon.facing == FacingDirection::LEFT );
                continue;
            }
        }
        add_at( main_lists, monster_key( mon ), mpos, voxel_size, face_towards( p, you_pos ),
                creature_light( p ), 0.0f, size_scale( mon.get_size() ), flash_tint( mon ),
                find_chain( vset, mon.type->id.str(), monster_looks_like ) );
    }

    for( auto it = motions.begin(); it != motions.end(); ) {
        if( it->second.seen ) {
            ++it;
        } else {
            hp_track.erase( it->first );
            it = motions.erase( it );
        }
    }

    const weather_animation_t &anim = get_weather().weather_id->weather_animation;
    if( anim.symbol != NULL_UNICODE && center.z() >= 0 && get_option<bool>( "ANIMATION_RAIN" ) ) {
        vm_key wk;
        const voxel_rgb anim_color = to_rgb( anim.color );
        const bool snowy = anim.symbol == U'*' || ( anim_color.r > 200 && anim_color.g > 200 &&
                           anim_color.b > 200 );
        wk.kind = snowy ? vm_kind::snow : vm_kind::rain;
        wk.color = snowy ? voxel_mix( anim_color, rgb( 240, 244, 252 ), 0.5f ) : voxel_mix( anim_color, rgb( 150,
                   175, 215 ), 0.6f );
        const int drops = std::clamp( static_cast<int>( anim.factor * radius * radius * 12.0f ), 60, 1200 );
        const uint32_t frame_seed = static_cast<uint32_t>( to_turns<int>( calendar::turn -
                                    calendar::turn_zero ) ) * 7u + ++weather_frame;
        for( int i = 0; i < drops; ++i ) {
            const int dx = static_cast<int>( voxel_hash( i, 1, 0, frame_seed ) % ( radius * 2 + 1 ) ) - radius;
            const int dy = static_cast<int>( voxel_hash( i, 2, 0, frame_seed ) % ( radius * 2 + 1 ) ) - radius;
            const tripoint_bub_ms wp( center.x() + dx, center.y() + dy, center.z() );
            if( !here.inbounds( wp ) || !here.is_outside( wp ) ) {
                continue;
            }
            const float h = voxel_noise( i, 3, 0, frame_seed ) * level_height * 1.6f;
            add( main_lists, wk, wp, h, 0.0f, 1.0f, 0.0f );
        }
    }

    if( center != you_pos && overlay_state.cursors.empty() ) {
        vm_key k;
        k.kind = vm_kind::cursor;
        k.color = rgb( 255, 220, 60 );
        add( main_lists, k, center, 0.0f, 0.0f, 1.0f, 0.0f );
    }
    for( const tripoint_bub_ms &c : overlay_state.cursors ) {
        vm_key k;
        k.kind = vm_kind::cursor;
        k.color = rgb( 255, 220, 60 );
        add( main_lists, k, c, 0.0f, 0.0f, 1.0f, 0.0f );
    }
    for( const tripoint_bub_ms &c : overlay_state.highlights ) {
        vm_key k;
        k.kind = vm_kind::marker;
        k.color = rgb( 120, 220, 255 );
        add( main_lists, k, c, voxel_size * 1.5f, 0.0f, 1.0f, 0.0f );
    }
    for( size_t i = 0; i < overlay_state.line.size(); ++i ) {
        vm_key k;
        k.kind = vm_kind::marker;
        k.color = i + 1 == overlay_state.line.size() ? rgb( 255, 80, 60 ) : rgb( 255, 200, 70 );
        add( main_lists, k, overlay_state.line[i], voxel_size * 1.5f, 0.0f, 1.0f, 0.0f );
    }
    if( overlay_state.bullet ) {
        vm_key k;
        k.kind = vm_kind::bolt;
        k.color = rgb( 255, 230, 140 );
        add( main_lists, k, overlay_state.bullet_pos, voxel_size * 4.0f, 0.0f, 1.0f, 0.0f );
    }
    const auto add_blast = [&]( const tripoint_bub_ms & bp, uint8_t power ) {
        vm_key k;
        k.kind = vm_kind::fire;
        k.variant = power;
        add( main_lists, k, bp, voxel_size, static_cast<float>( voxel_hash( bp.x(), bp.y(), 0,
                3 ) % 4 ) * pi * 0.5f, 1.0f, 0.0f, 1.1f );
    };
    if( overlay_state.explosion ) {
        const int r = overlay_state.explosion_radius;
        for( int dy = -r; dy <= r; ++dy ) {
            for( int dx = -r; dx <= r; ++dx ) {
                if( dx * dx + dy * dy <= r * r ) {
                    add_blast( overlay_state.explosion_center + tripoint_rel_ms( dx, dy, 0 ), 2 );
                }
            }
        }
    }
    for( const tripoint_bub_ms &bp : overlay_state.custom_explosion ) {
        add_blast( bp, 1 );
    }
}

void voxel_renderer::upload_instances( instance_lists &main, instance_lists &extra )
{
    voxel_gl_api &gl = voxel_gl();
    upload.clear();
    main_ranges.assign( models.size(), { 0, 0 } );
    extra_ranges.assign( models.size(), { 0, 0 } );
    for( int m : main.touched ) {
        main_ranges[m] = { upload.size(), main.per_model[m].size() };
        upload.insert( upload.end(), main.per_model[m].begin(), main.per_model[m].end() );
    }
    for( int m : extra.touched ) {
        extra_ranges[m] = { upload.size(), extra.per_model[m].size() };
        upload.insert( upload.end(), extra.per_model[m].begin(), extra.per_model[m].end() );
    }
    gl.glBindBuffer( GL_ARRAY_BUFFER, instance_vbo );
    gl.glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( upload.size() * sizeof( instance ) ),
                     upload.empty() ? nullptr : upload.data(), GL_STREAM_DRAW );
}

void voxel_renderer::draw_pass( instance_lists &lists, bool glass, GLuint program )
{
    voxel_gl_api &gl = voxel_gl();
    gl.glUseProgram( program );
    const auto draw_range = [&]( int m, const std::pair<size_t, size_t> &range ) {
        if( range.second == 0 ) {
            return;
        }
        const gpu_mesh &mesh = glass ? models[m].glass : models[m].opaque;
        if( mesh.count == 0 ) {
            return;
        }
        gl.glBindVertexArray( mesh.vao );
        gl.glBindBuffer( GL_ARRAY_BUFFER, instance_vbo );
        const GLsizei stride = sizeof( instance );
        const size_t base = range.first * sizeof( instance );
        gl.glVertexAttribPointer( 3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>( base ) );
        gl.glVertexAttribPointer( 4, 4, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<void *>( base + 4 * sizeof( float ) ) );
        gl.glVertexAttribPointer( 5, 4, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<void *>( base + 8 * sizeof( float ) ) );
        gl.glDrawArraysInstanced( GL_TRIANGLES, 0, mesh.count, static_cast<GLsizei>( range.second ) );
    };
    const bool shadow = &lists == &shadow_lists;
    for( int m : lists.touched ) {
        draw_range( m, shadow ? extra_ranges[m] : main_ranges[m] );
    }
    if( shadow ) {
        for( int m : main_lists.touched ) {
            draw_range( m, main_ranges[m] );
        }
    }
    gl.glBindVertexArray( 0 );
}

voxel_rgb voxel_renderer::flash_tint( const Creature &c )
{
    const int hp = c.get_hp();
    std::pair<int, uint64_t> &e = hp_track[&c];
    if( e.second == 0 && e.first == 0 ) {
        e.first = hp;
        e.second = 1;
    }
    if( hp < e.first ) {
        e.second = now_ms;
    }
    e.first = hp;
    if( e.second > 1 && now_ms - e.second < 260 ) {
        animating = true;
        return { 255, 90, 80 };
    }
    return { 255, 255, 255 };
}

void voxel_renderer::add_light( const tripoint_bub_ms &p, float lift, float radius, float r, float g,
                                float b )
{
    light_pos.push_back( { p.x() + 0.5f, p.z() * level_height + lift, p.y() + 0.5f, radius } );
    light_col.push_back( { r, g, b } );
}

void voxel_renderer::upload_lights( GLuint program )
{
    voxel_gl_api &gl = voxel_gl();
    const int count = static_cast<int>( std::min<size_t>( light_pos.size(), 24 ) );
    gl.glUniform1i( gl.glGetUniformLocation( program, "u_light_count" ), count );
    if( count > 0 ) {
        gl.glUniform4fv( gl.glGetUniformLocation( program, "u_lights" ), count, light_pos[0].data() );
        gl.glUniform3fv( gl.glGetUniformLocation( program, "u_light_colors" ), count, light_col[0].data() );
    }
    gl.glUniform1f( gl.glGetUniformLocation( program, "u_time" ),
                    static_cast<float>( now_ms % 1000000 ) / 1000.0f );
    gl.glUniform1f( gl.glGetUniformLocation( program, "u_shadow_texel" ), 1.0f / shadow_size );
}

void voxel_renderer::apply_quality()
{
    voxel_gl_api &gl = voxel_gl();
    const std::string q = get_option<std::string>( "VOXEL_SHADOW_QUALITY" );
    const int wanted_shadow = q == "low" ? 1024 : q == "high" ? 4096 : 2048;
    if( wanted_shadow != shadow_size ) {
        shadow_size = wanted_shadow;
        gl.glBindTexture( GL_TEXTURE_2D, shadow_tex );
        gl.glTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadow_size, shadow_size, 0,
                         GL_DEPTH_COMPONENT, GL_FLOAT, nullptr );
    }
    wanted_samples = std::stoi( get_option<std::string>( "VOXEL_MSAA" ) );
    const int s = std::clamp( std::min( max_samples, wanted_samples ), 1, 8 );
    if( s != samples ) {
        samples = s;
        target_w = 0;
    }
    if( !cam.initialized ) {
        cam.pitch = static_cast<float>( get_option<int>( "VOXEL_CAMERA_ANGLE" ) ) * pi / 180.0f;
        cam.initialized = true;
    }
}

bool voxel_renderer::render( const tripoint_bub_ms &center, int width, int height,
                             std::vector<uint8_t> &pixels )
{
    if( failed || width <= 0 || height <= 0 ) {
        return false;
    }
    SDL_Window *prev_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext prev_context = SDL_GL_GetCurrentContext();
    if( !initialized ) {
        initialized = true;
        if( !init() ) {
            failed = true;
            if( prev_context ) {
                SDL_GL_MakeCurrent( prev_window, prev_context );
            }
            return false;
        }
    } else if( !SDL_GL_MakeCurrent( window, context ) ) {
        failed = true;
        return false;
    }
    voxel_gl_api &gl = voxel_gl();
    apply_quality();
    if( !ensure_targets( width, height ) ) {
        failed = true;
        if( prev_context ) {
            SDL_GL_MakeCurrent( prev_window, prev_context );
        }
        return false;
    }

    const float zoom = static_cast<float>( std::max( uistate.tileset_zoom, 1 ) );
    const float distance = std::clamp( 24.0f * 16.0f / zoom, 6.0f, 110.0f );
    const float reach = static_cast<float>( get_option<int>( "VOXEL_DRAW_DISTANCE" ) ) / 100.0f;
    const int radius = std::clamp( static_cast<int>( ( distance * 1.25f + 10.0f ) * reach ), 12, 64 );

    const float hour = to_hours<float>( time_past_midnight( calendar::turn ) );
    const float natural = g->natural_light_level( center.z() );
    const float daylight = center.z() >= 0 ? std::clamp( ( natural - 15.0f ) / 70.0f, 0.0f, 1.0f ) : 0.0f;
    const float sun_angle = std::clamp( ( hour - 6.0f ) / 12.0f, 0.08f, 0.92f ) * pi;
    const vec3 sun_dir = normalize( { std::cos( sun_angle ), std::sin( sun_angle ) * 1.3f + 0.25f, 0.45f } );

    collect( center, radius, daylight );
    upload_instances( main_lists, shadow_lists );
    upload_sprites();

    vec3 focus = { center.x() + 0.5f, center.z() * level_height + 0.6f, center.y() + 0.5f };
    if( avatar_display && center == get_avatar().pos_bub() ) {
        focus.x = avatar_display->x + 0.5f;
        focus.z = avatar_display->z + 0.5f;
    }
    if( !have_focus_y ) {
        focus_y = focus.y;
        have_focus_y = true;
    } else if( std::fabs( focus.y - focus_y ) > 0.02f ) {
        focus_y += ( focus.y - focus_y ) * 0.3f;
        animating = true;
    } else {
        focus_y = focus.y;
    }
    focus.y = focus_y;
    if( overlay_state.explosion ) {
        focus.x += ( voxel_noise( static_cast<int>( now_ms / 30 ), 1, 0, 7 ) - 0.5f ) * 0.3f;
        focus.z += ( voxel_noise( static_cast<int>( now_ms / 30 ), 2, 0, 7 ) - 0.5f ) * 0.3f;
    }
    std::vector<size_t> order( light_pos.size() );
    for( size_t i = 0; i < order.size(); ++i ) {
        order[i] = i;
    }
    std::sort( order.begin(), order.end(), [&]( size_t a, size_t b ) {
        const auto d = [&]( size_t i ) {
            const float dx = light_pos[i][0] - focus.x;
            const float dz = light_pos[i][2] - focus.z;
            return dx * dx + dz * dz;
        };
        return d( a ) < d( b );
    } );
    std::vector<std::array<float, 4>> sorted_pos;
    std::vector<std::array<float, 3>> sorted_col;
    for( size_t i = 0; i < order.size() && i < 24; ++i ) {
        sorted_pos.push_back( light_pos[order[i]] );
        sorted_col.push_back( light_col[order[i]] );
    }
    light_pos = sorted_pos;
    light_col = sorted_col;
    const vec3 offset = { std::sin( cam.yaw ) * std::cos( cam.pitch ), std::sin( cam.pitch ), std::cos( cam.yaw ) * std::cos( cam.pitch ) };
    const vec3 eye = focus + offset * distance;
    const mat4 proj = perspective( 0.7f, static_cast<float>( width ) / static_cast<float>( height ), 0.5f,
                                   distance * 4.0f + 80.0f );
    const mat4 view = look_at( eye, focus, { 0.0f, 1.0f, 0.0f } );
    const mat4 view_proj = proj * view;
    mat4 flip = mat4::identity();
    flip.m[5] = -1.0f;
    const mat4 flipped = flip * view_proj;
    cam.view_proj = view_proj;
    cam.width = width;
    cam.height = height;
    cam.center_z = center.z();
    cam.valid = true;

    const float extent = static_cast<float>( radius ) + 6.0f;
    const vec3 sun_up = std::fabs( sun_dir.y ) > 0.95f ? vec3{ 0.0f, 0.0f, 1.0f } : vec3{ 0.0f, 1.0f, 0.0f };
    const mat4 light_vp = ortho( -extent, extent, -extent, extent, 1.0f, 200.0f ) *
                          look_at( focus + sun_dir * 90.0f, focus, sun_up );
    const bool use_shadow = daylight > 0.02f && get_option<bool>( "VOXEL_SHADOWS" );

    gl.glEnable( GL_DEPTH_TEST );
    gl.glDepthFunc( GL_LEQUAL );
    gl.glEnable( GL_CULL_FACE );
    gl.glCullFace( GL_BACK );

    if( use_shadow ) {
        gl.glBindFramebuffer( GL_FRAMEBUFFER, shadow_fbo );
        gl.glViewport( 0, 0, shadow_size, shadow_size );
        gl.glClear( GL_DEPTH_BUFFER_BIT );
        gl.glFrontFace( GL_CCW );
        gl.glEnable( GL_POLYGON_OFFSET_FILL );
        gl.glPolygonOffset( 1.5f, 3.0f );
        gl.glUseProgram( shadow_program );
        gl.glUniformMatrix4fv( gl.glGetUniformLocation( shadow_program, "u_light_vp" ), 1, GL_FALSE,
                               light_vp.m.data() );
        gl.glDisable( GL_CULL_FACE );
        draw_pass( shadow_lists, false, shadow_program );
        draw_sprite_shadows( light_vp, sun_dir );
        gl.glEnable( GL_CULL_FACE );
        gl.glDisable( GL_POLYGON_OFFSET_FILL );
    }

    gl.glBindFramebuffer( GL_FRAMEBUFFER, msaa_fbo );
    gl.glViewport( 0, 0, width, height );
    const vec3 fog = center.z() < 0 ? vec3{ 0.0f, 0.0f, 0.0f } : vec3{ 0.02f + 0.16f * daylight, 0.025f + 0.2f * daylight, 0.04f + 0.25f * daylight };
    gl.glClearColor( fog.x, fog.y, fog.z, 1.0f );
    gl.glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    gl.glFrontFace( GL_CW );

    gl.glUseProgram( main_program );
    gl.glUniformMatrix4fv( gl.glGetUniformLocation( main_program, "u_vp" ), 1, GL_FALSE,
                           flipped.m.data() );
    gl.glUniformMatrix4fv( gl.glGetUniformLocation( main_program, "u_light_vp" ), 1, GL_FALSE,
                           light_vp.m.data() );
    gl.glUniform3f( gl.glGetUniformLocation( main_program, "u_sun" ), sun_dir.x, sun_dir.y, sun_dir.z );
    gl.glUniform1f( gl.glGetUniformLocation( main_program, "u_daylight" ), daylight );
    gl.glUniform1f( gl.glGetUniformLocation( main_program, "u_use_shadow" ), use_shadow ? 1.0f : 0.0f );
    gl.glUniform3f( gl.glGetUniformLocation( main_program, "u_focus" ), focus.x, focus.y, focus.z );
    gl.glUniform1f( gl.glGetUniformLocation( main_program, "u_fog_far" ), static_cast<float>( radius ) );
    gl.glUniform3f( gl.glGetUniformLocation( main_program, "u_fog_color" ), fog.x, fog.y, fog.z );
    gl.glUniform1f( gl.glGetUniformLocation( main_program, "u_alpha" ), 1.0f );
    upload_lights( main_program );
    gl.glActiveTexture( GL_TEXTURE0 );
    gl.glBindTexture( GL_TEXTURE_2D, shadow_tex );
    gl.glUniform1i( gl.glGetUniformLocation( main_program, "u_shadow" ), 0 );

    draw_pass( main_lists, false, main_program );
    draw_sprites( view, flipped, light_vp, sun_dir, daylight, use_shadow, focus, static_cast<float>( radius ),
                  fog );
    gl.glUseProgram( main_program );

    gl.glEnable( GL_BLEND );
    gl.glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    gl.glDepthMask( GL_FALSE );
    gl.glDisable( GL_CULL_FACE );
    gl.glUniform1f( gl.glGetUniformLocation( main_program, "u_alpha" ), 0.45f );
    draw_pass( main_lists, true, main_program );
    gl.glDepthMask( GL_TRUE );
    gl.glDisable( GL_BLEND );

    gl.glBindFramebuffer( GL_READ_FRAMEBUFFER, msaa_fbo );
    gl.glBindFramebuffer( GL_DRAW_FRAMEBUFFER, resolve_fbo );
    gl.glBlitFramebuffer( 0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST );
    gl.glBindFramebuffer( GL_FRAMEBUFFER, resolve_fbo );
    gl.glReadBuffer( GL_COLOR_ATTACHMENT0 );
    pixels.resize( static_cast<size_t>( width ) * height * 4 );
    gl.glPixelStorei( GL_PACK_ALIGNMENT, 1 );
    gl.glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
    gl.glBindFramebuffer( GL_FRAMEBUFFER, 0 );

    if( prev_context ) {
        SDL_GL_MakeCurrent( prev_window, prev_context );
    }
    return true;
}

void voxel_renderer::shutdown()
{
    if( context ) {
        SDL_GL_DestroyContext( context );
        context = nullptr;
    }
    if( window ) {
        SDL_DestroyWindow( window );
        window = nullptr;
    }
    models.clear();
    model_lookup.clear();
    initialized = false;
    target_w = 0;
    target_h = 0;
    msaa_fbo = 0;
}

voxel_renderer &renderer()
{
    static voxel_renderer r;
    return r;
}

int yaw_octant()
{
    const float steps = cam.yaw / ( pi / 4.0f );
    int k = static_cast<int>( std::lround( steps ) ) % 8;
    if( k < 0 ) {
        k += 8;
    }
    return k;
}

point_rel_ms rotate_octants( const point_rel_ms &d, int k )
{
    static const std::array<point, 8> ring = { {
            point::north, point::north_east, point::east, point::south_east,
            point::south, point::south_west, point::west, point::north_west
        }
    };
    for( int i = 0; i < 8; ++i ) {
        if( ring[i] == d.raw() ) {
            return point_rel_ms( ring[( i + k + 8 ) % 8] );
        }
    }
    return d;
}

} // namespace

namespace voxel_view
{

void set_overlays( const overlays &state )
{
    overlay_state = state;
}

bool animating()
{
    return active() && renderer().animating;
}

int animation_interval()
{
    if( !active() ) {
        return 0;
    }
    if( renderer().animating ) {
        return 16;
    }
    if( renderer().ambient && get_option<bool>( "VOXEL_AMBIENT_ANIMATION" ) ) {
        return 70;
    }
    return 0;
}

bool active()
{
    return use_tiles && get_option<bool>( "VOXEL_VIEW" ) && !renderer().failed;
}

bool render( const tripoint_bub_ms &center, int width, int height, std::vector<uint8_t> &pixels )
{
    return renderer().render( center, width, height, pixels );
}

void orbit( float dx, float dy )
{
    cam.yaw -= dx * 0.008f;
    cam.pitch = std::clamp( cam.pitch + dy * 0.006f, 0.35f, 1.5f );
}

void rotate_step( int steps )
{
    const float quarter = pi / 4.0f;
    cam.yaw = std::round( cam.yaw / quarter ) * quarter + steps * quarter;
}

void tilt_step( int steps )
{
    cam.pitch = std::clamp( cam.pitch + steps * 0.15f, 0.35f, 1.5f );
}

void reset_camera()
{
    cam.yaw = 0.0f;
    cam.pitch = 0.95f;
}

std::optional<point> project( const tripoint_bub_ms &p, float lift )
{
    if( !cam.valid ) {
        return std::nullopt;
    }
    const std::array<float, 4> v = { p.x() + 0.5f, p.z() * level_height + lift, p.y() + 0.5f, 1.0f };
    std::array<float, 4> c = { 0.0f, 0.0f, 0.0f, 0.0f };
    for( int row = 0; row < 4; ++row ) {
        for( int k = 0; k < 4; ++k ) {
            c[row] += cam.view_proj.m[k * 4 + row] * v[k];
        }
    }
    if( c[3] <= 0.0f ) {
        return std::nullopt;
    }
    const float nx = c[0] / c[3];
    const float ny = c[1] / c[3];
    if( nx < -1.0f || nx > 1.0f || ny < -1.0f || ny > 1.0f ) {
        return std::nullopt;
    }
    return point( static_cast<int>( ( nx * 0.5f + 0.5f ) * cam.width ),
                  static_cast<int>( ( 0.5f - ny * 0.5f ) * cam.height ) );
}

std::optional<tripoint_bub_ms> pick( const point &pixel, const point &size )
{
    if( !cam.valid || size.x <= 0 || size.y <= 0 ) {
        return std::nullopt;
    }
    mat4 inv;
    if( !invert( cam.view_proj, inv ) ) {
        return std::nullopt;
    }
    const float nx = 2.0f * ( pixel.x + 0.5f ) / size.x - 1.0f;
    const float ny = 1.0f - 2.0f * ( pixel.y + 0.5f ) / size.y;
    const auto unproject = [&inv]( float x, float y, float z ) {
        const std::array<float, 4> v = { x, y, z, 1.0f };
        std::array<float, 4> r = { 0.0f, 0.0f, 0.0f, 0.0f };
        for( int row = 0; row < 4; ++row ) {
            for( int k = 0; k < 4; ++k ) {
                r[row] += inv.m[k * 4 + row] * v[k];
            }
        }
        return vec3{ r[0] / r[3], r[1] / r[3], r[2] / r[3] };
    };
    const vec3 a = unproject( nx, ny, -1.0f );
    const vec3 b = unproject( nx, ny, 1.0f );
    const vec3 dir = b - a;
    const float plane = cam.center_z * level_height + voxel_size;
    if( dir.y > -1e-6f ) {
        return std::nullopt;
    }
    const float t_floor = ( plane - a.y ) / dir.y;
    const float t_top = std::max( 0.0f, ( plane + level_height - a.y ) / dir.y );
    if( t_floor < 0.0f ) {
        return std::nullopt;
    }
    map &here = get_map();
    constexpr int samples = 160;
    for( int i = 0; i <= samples; ++i ) {
        const float t = t_top + ( t_floor - t_top ) * static_cast<float>( i ) / samples;
        const vec3 pt = a + dir * t;
        const tripoint_bub_ms tp( static_cast<int>( std::floor( pt.x ) ), static_cast<int>( std::floor(
                                      pt.z ) ), cam.center_z );
        if( !here.inbounds( tp ) ) {
            continue;
        }
        const ter_t &ter = here.ter( tp ).obj();
        const bool tall = ter.has_flag( ter_furn_flag::TFLAG_WALL ) ||
                          ter.has_flag( ter_furn_flag::TFLAG_DOOR ) || ter.has_flag( ter_furn_flag::TFLAG_WINDOW );
        if( tall && pt.y <= plane + level_height ) {
            return tp;
        }
    }
    const vec3 hit = a + dir * t_floor;
    return tripoint_bub_ms( static_cast<int>( std::floor( hit.x ) ), static_cast<int>( std::floor(
                                hit.z ) ), cam.center_z );
}

point_rel_ms screen_to_map( const point_rel_ms &d )
{
    return rotate_octants( d, -yaw_octant() );
}

point_rel_ms map_to_screen( const point_rel_ms &d )
{
    return rotate_octants( d, yaw_octant() );
}

void shutdown()
{
    renderer().shutdown();
}

} // namespace voxel_view

#else

namespace voxel_view
{

void set_overlays( const overlays & )
{
}

bool animating()
{
    return false;
}

int animation_interval()
{
    return 0;
}

bool active()
{
    return false;
}

bool render( const tripoint_bub_ms &, int, int, std::vector<uint8_t> & )
{
    return false;
}

void orbit( float, float )
{
}

void rotate_step( int )
{
}

void tilt_step( int )
{
}

void reset_camera()
{
}

std::optional<point> project( const tripoint_bub_ms &, float )
{
    return std::nullopt;
}

std::optional<tripoint_bub_ms> pick( const point &, const point & )
{
    return std::nullopt;
}

point_rel_ms screen_to_map( const point_rel_ms &d )
{
    return d;
}

point_rel_ms map_to_screen( const point_rel_ms &d )
{
    return d;
}

void shutdown()
{
}

} // namespace voxel_view

#endif
