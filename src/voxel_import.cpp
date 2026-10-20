#include "voxel_import.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

namespace
{

struct vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct texture_data {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;
};

struct material {
    voxel_rgb color{ 200, 200, 200 };
    int texture = -1;
};

struct corner {
    int v = -1;
    int t = -1;
};

struct face {
    std::array<corner, 3> c;
    int material = -1;
};

std::string directory_of( const std::string &path )
{
    const size_t slash = path.find_last_of( "/\\" );
    return slash == std::string::npos ? std::string() : path.substr( 0, slash + 1 );
}

std::string trim( const std::string &s )
{
    const size_t a = s.find_first_not_of( " \t\r\n" );
    if( a == std::string::npos ) {
        return std::string();
    }
    const size_t b = s.find_last_not_of( " \t\r\n" );
    return s.substr( a, b - a + 1 );
}

int load_texture( const std::string &path, std::vector<texture_data> &textures )
{
    SDL_Surface *raw = IMG_Load( path.c_str() );
    if( raw == nullptr ) {
        return -1;
    }
    SDL_Surface *surf = SDL_ConvertSurface( raw, SDL_PIXELFORMAT_RGBA32 );
    SDL_DestroySurface( raw );
    if( surf == nullptr ) {
        return -1;
    }
    texture_data t;
    t.w = surf->w;
    t.h = surf->h;
    t.rgba.resize( static_cast<size_t>( t.w ) * t.h * 4 );
    for( int y = 0; y < t.h; ++y ) {
        const uint8_t *row = static_cast<const uint8_t *>( surf->pixels ) + static_cast<size_t>
                             ( y ) * surf->pitch;
        std::copy( row, row + static_cast<size_t>( t.w ) * 4, t.rgba.begin() + static_cast<size_t>( y ) * t.w * 4 );
    }
    SDL_DestroySurface( surf );
    textures.push_back( std::move( t ) );
    return static_cast<int>( textures.size() ) - 1;
}

void load_mtl( const std::string &path, std::map<std::string, material> &materials,
               std::vector<texture_data> &textures )
{
    std::ifstream f( path );
    std::string line;
    material *current = nullptr;
    while( std::getline( f, line ) ) {
        std::istringstream ss( line );
        std::string key;
        ss >> key;
        if( key == "newmtl" ) {
            std::string name;
            ss >> name;
            current = &materials[name];
        } else if( key == "Kd" && current ) {
            float r = 0.8f;
            float g = 0.8f;
            float b = 0.8f;
            ss >> r >> g >> b;
            current->color = { static_cast<uint8_t>( std::clamp( r, 0.0f, 1.0f ) * 255.0f ),
                               static_cast<uint8_t>( std::clamp( g, 0.0f, 1.0f ) * 255.0f ),
                               static_cast<uint8_t>( std::clamp( b, 0.0f, 1.0f ) * 255.0f )
                             };
        } else if( key == "map_Kd" && current ) {
            std::string rest;
            std::getline( ss, rest );
            rest = trim( rest );
            const size_t space = rest.find_last_of( ' ' );
            if( space != std::string::npos && rest.compare( 0, 1, "-" ) == 0 ) {
                rest = rest.substr( space + 1 );
            }
            current->texture = load_texture( directory_of( path ) + rest, textures );
        }
    }
}

corner parse_corner( const std::string &token, int nv, int nt )
{
    corner c;
    std::istringstream ss( token );
    std::string part;
    int index = 0;
    while( std::getline( ss, part, '/' ) ) {
        if( !part.empty() ) {
            const int value = std::stoi( part );
            const int resolved = value < 0 ? ( index == 0 ? nv : nt ) + value : value - 1;
            if( index == 0 ) {
                c.v = resolved;
            } else if( index == 1 ) {
                c.t = resolved;
            }
        }
        ++index;
    }
    return c;
}

} // namespace

bool is_mesh_file( const std::string &path )
{
    return path.size() > 4 && ( path.compare( path.size() - 4, 4, ".obj" ) == 0 ||
                                path.compare( path.size() - 4, 4, ".OBJ" ) == 0 );
}

std::optional<voxel_grid> voxelize_obj( const std::string &path, const voxel_import_options &options,
                                        std::string &error )
{
    std::ifstream f( path );
    if( !f ) {
        error = "cannot open " + path;
        return std::nullopt;
    }
    std::vector<vec3f> verts;
    std::vector<std::array<float, 2>> uvs;
    std::vector<face> faces;
    std::map<std::string, material> materials;
    std::vector<texture_data> textures;
    std::vector<std::string> material_names;
    int current = -1;
    std::string line;
    while( std::getline( f, line ) ) {
        std::istringstream ss( line );
        std::string key;
        ss >> key;
        if( key == "v" ) {
            vec3f v;
            ss >> v.x >> v.y >> v.z;
            verts.push_back( v );
        } else if( key == "vt" ) {
            std::array<float, 2> t = { 0.0f, 0.0f };
            ss >> t[0] >> t[1];
            uvs.push_back( t );
        } else if( key == "mtllib" ) {
            std::string rest;
            std::getline( ss, rest );
            load_mtl( directory_of( path ) + trim( rest ), materials, textures );
        } else if( key == "usemtl" ) {
            std::string name;
            ss >> name;
            const auto it = std::find( material_names.begin(), material_names.end(), name );
            if( it == material_names.end() ) {
                material_names.push_back( name );
                current = static_cast<int>( material_names.size() ) - 1;
            } else {
                current = static_cast<int>( it - material_names.begin() );
            }
        } else if( key == "f" ) {
            std::vector<corner> poly;
            std::string token;
            while( ss >> token ) {
                poly.push_back( parse_corner( token, static_cast<int>( verts.size() ),
                                              static_cast<int>( uvs.size() ) ) );
            }
            for( size_t i = 1; i + 1 < poly.size(); ++i ) {
                faces.push_back( { { poly[0], poly[i], poly[i + 1] }, current } );
            }
        }
    }
    if( verts.empty() || faces.empty() ) {
        error = path + " has no faces";
        return std::nullopt;
    }

    vec3f lo = verts[0];
    vec3f hi = verts[0];
    for( const vec3f &v : verts ) {
        lo = { std::min( lo.x, v.x ), std::min( lo.y, v.y ), std::min( lo.z, v.z ) };
        hi = { std::max( hi.x, v.x ), std::max( hi.y, v.y ), std::max( hi.z, v.z ) };
    }
    float per_unit = static_cast<float>( options.voxels_per_tile ) * options.scale;
    if( options.height_voxels > 0.0f && hi.y > lo.y ) {
        per_unit = options.height_voxels / ( hi.y - lo.y );
    }
    const int sx = std::clamp( static_cast<int>( std::ceil( ( hi.x - lo.x ) * per_unit - 0.001f ) ), 1, 256 );
    const int sy = std::clamp( static_cast<int>( std::ceil( ( hi.y - lo.y ) * per_unit - 0.001f ) ), 1, 256 );
    const int sz = std::clamp( static_cast<int>( std::ceil( ( hi.z - lo.z ) * per_unit - 0.001f ) ), 1, 256 );
    voxel_grid grid( sx, sy, sz, ( sx - options.voxels_per_tile ) / 2, ( sz - options.voxels_per_tile ) / 2 );

    const auto to_grid = [&]( const vec3f & v ) {
        return vec3f{ ( v.x - lo.x ) * per_unit, ( v.y - lo.y ) * per_unit, ( v.z - lo.z ) * per_unit };
    };
    const auto sample = [&]( const face & fc, float a, float b ) -> voxel_rgb {
        material mat;
        if( fc.material >= 0 )
        {
            const auto it = materials.find( material_names[fc.material] );
            if( it != materials.end() ) {
                mat = it->second;
            }
        }
        if( mat.texture < 0 || fc.c[0].t < 0 || fc.c[1].t < 0 || fc.c[2].t < 0 )
        {
            return mat.color;
        }
        const auto &t0 = uvs[fc.c[0].t];
        const auto &t1 = uvs[fc.c[1].t];
        const auto &t2 = uvs[fc.c[2].t];
        float u = t0[0] + ( t1[0] - t0[0] ) * a + ( t2[0] - t0[0] ) * b;
        float v = t0[1] + ( t1[1] - t0[1] ) * a + ( t2[1] - t0[1] ) * b;
        u -= std::floor( u );
        v -= std::floor( v );
        const texture_data &tex = textures[mat.texture];
        const int px = std::clamp( static_cast<int>( u * tex.w ), 0, tex.w - 1 );
        const int py = std::clamp( static_cast<int>( ( 1.0f - v ) * tex.h ), 0, tex.h - 1 );
        const uint8_t *p = &tex.rgba[( static_cast<size_t>( py ) * tex.w + px ) * 4];
        return voxel_rgb{ p[0], p[1], p[2] };
    };

    for( const face &fc : faces ) {
        if( fc.c[0].v < 0 || fc.c[1].v < 0 || fc.c[2].v < 0 ||
            fc.c[0].v >= static_cast<int>( verts.size() ) || fc.c[1].v >= static_cast<int>( verts.size() ) ||
            fc.c[2].v >= static_cast<int>( verts.size() ) ) {
            continue;
        }
        const vec3f a = to_grid( verts[fc.c[0].v] );
        const vec3f b = to_grid( verts[fc.c[1].v] );
        const vec3f c = to_grid( verts[fc.c[2].v] );
        const auto len = []( const vec3f & p, const vec3f & q ) {
            return std::sqrt( ( p.x - q.x ) * ( p.x - q.x ) + ( p.y - q.y ) * ( p.y - q.y ) + ( p.z - q.z ) *
                              ( p.z - q.z ) );
        };
        const int steps = std::max( 1, static_cast<int>( std::ceil( std::max( { len( a, b ), len( a, c ), len( b, c ) } ) * 2.0f ) ) );
        for( int i = 0; i <= steps; ++i ) {
            for( int j = 0; i + j <= steps; ++j ) {
                const float u = static_cast<float>( i ) / steps;
                const float v = static_cast<float>( j ) / steps;
                const vec3f p{ a.x + ( b.x - a.x ) * u + ( c.x - a.x ) * v, a.y + ( b.y - a.y ) * u + ( c.y - a.y ) * v,
                               a.z + ( b.z - a.z ) * u + ( c.z - a.z ) * v };
                const int x = std::clamp( static_cast<int>( p.x ), 0, sx - 1 );
                const int y = std::clamp( static_cast<int>( p.y ), 0, sy - 1 );
                const int z = std::clamp( static_cast<int>( p.z ), 0, sz - 1 );
                if( !grid.solid( x, y, z ) ) {
                    grid.set( x, y, z, sample( fc, u, v ) );
                }
            }
        }
    }
    return grid;
}
