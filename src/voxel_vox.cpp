#include "voxel_vox.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>

namespace
{

struct reader {
    const std::vector<uint8_t> &data;
    size_t pos = 0;

    bool has( size_t n ) const {
        return pos + n <= data.size();
    }

    uint32_t u32() {
        uint32_t v = 0;
        std::memcpy( &v, data.data() + pos, 4 );
        pos += 4;
        return v;
    }

    std::string tag() {
        std::string t( reinterpret_cast<const char *>( data.data() + pos ), 4 );
        pos += 4;
        return t;
    }

    std::string str() {
        if( !has( 4 ) ) {
            return std::string();
        }
        const uint32_t n = u32();
        if( !has( n ) ) {
            pos = data.size();
            return std::string();
        }
        std::string s( reinterpret_cast<const char *>( data.data() + pos ), n );
        pos += n;
        return s;
    }
};

struct vox_voxel {
    uint8_t x;
    uint8_t y;
    uint8_t z;
    uint8_t index;
};

void put_u32( std::vector<uint8_t> &out, uint32_t v )
{
    for( int i = 0; i < 4; ++i ) {
        out.push_back( static_cast<uint8_t>( ( v >> ( i * 8 ) ) & 0xFF ) );
    }
}

void put_tag( std::vector<uint8_t> &out, const char *tag )
{
    out.insert( out.end(), tag, tag + 4 );
}

void put_str( std::vector<uint8_t> &out, const std::string &s )
{
    put_u32( out, static_cast<uint32_t>( s.size() ) );
    out.insert( out.end(), s.begin(), s.end() );
}

void put_chunk( std::vector<uint8_t> &out, const char *tag, const std::vector<uint8_t> &content )
{
    put_tag( out, tag );
    put_u32( out, static_cast<uint32_t>( content.size() ) );
    put_u32( out, 0 );
    out.insert( out.end(), content.begin(), content.end() );
}

uint32_t pack_color( const voxel_rgb &c, bool glass )
{
    return ( static_cast<uint32_t>( c.r ) << 16 ) | ( static_cast<uint32_t>( c.g ) << 8 ) | c.b |
           ( glass ? 1u << 24 : 0u );
}

} // namespace

std::optional<voxel_grid> load_vox_file( const std::string &path, int voxels_per_tile,
        std::string &error )
{
    std::ifstream f( path, std::ios::binary );
    if( !f ) {
        error = "cannot open " + path;
        return std::nullopt;
    }
    const std::vector<uint8_t> data( ( std::istreambuf_iterator<char>( f ) ),
                                     std::istreambuf_iterator<char>() );
    reader r{ data };
    if( !r.has( 8 ) || r.tag() != "VOX " ) {
        error = path + " is not a MagicaVoxel file";
        return std::nullopt;
    }
    r.u32();

    std::array<int, 3> size = { 0, 0, 0 };
    std::vector<vox_voxel> voxels;
    bool have_model = false;
    std::array<uint32_t, 256> palette{};
    bool have_palette = false;
    std::array<bool, 256> glass{};

    while( r.has( 12 ) ) {
        const std::string id = r.tag();
        const uint32_t content = r.u32();
        r.u32();
        if( id == "MAIN" ) {
            continue;
        }
        if( !r.has( content ) ) {
            break;
        }
        const size_t end = r.pos + content;
        if( id == "SIZE" && !have_model ) {
            size[0] = static_cast<int>( r.u32() );
            size[1] = static_cast<int>( r.u32() );
            size[2] = static_cast<int>( r.u32() );
        } else if( id == "XYZI" && !have_model ) {
            const uint32_t n = r.u32();
            for( uint32_t i = 0; i < n && r.has( 4 ); ++i ) {
                vox_voxel v{};
                v.x = data[r.pos];
                v.y = data[r.pos + 1];
                v.z = data[r.pos + 2];
                v.index = data[r.pos + 3];
                r.pos += 4;
                voxels.push_back( v );
            }
            have_model = true;
        } else if( id == "RGBA" ) {
            for( int i = 0; i < 256 && r.has( 4 ); ++i ) {
                const uint8_t cr = data[r.pos];
                const uint8_t cg = data[r.pos + 1];
                const uint8_t cb = data[r.pos + 2];
                const uint8_t ca = data[r.pos + 3];
                r.pos += 4;
                palette[( i + 1 ) & 0xFF] = ( static_cast<uint32_t>( cr ) << 16 ) |
                                            ( static_cast<uint32_t>( cg ) << 8 ) | cb;
                if( ca < 250 ) {
                    glass[( i + 1 ) & 0xFF] = true;
                }
            }
            have_palette = true;
        } else if( id == "MATL" ) {
            const uint32_t mat = r.u32();
            const uint32_t pairs = r.has( 4 ) ? r.u32() : 0;
            for( uint32_t i = 0; i < pairs && r.pos < end; ++i ) {
                const std::string key = r.str();
                const std::string value = r.str();
                if( key == "_type" && ( value == "_glass" || value == "_blend" ) && mat < 256 ) {
                    glass[mat] = true;
                }
            }
        }
        r.pos = end;
    }

    if( !have_model || size[0] <= 0 || size[1] <= 0 || size[2] <= 0 ) {
        error = path + " has no voxel model";
        return std::nullopt;
    }
    if( !have_palette ) {
        for( int i = 0; i < 256; ++i ) {
            const uint32_t v = static_cast<uint32_t>( 255 - ( i % 64 ) * 3 );
            palette[i] = ( v << 16 ) | ( v << 8 ) | v;
        }
    }

    const int sx = size[0];
    const int sz = size[1];
    const int sy = size[2];
    voxel_grid grid( sx, sy, sz, ( sx - voxels_per_tile ) / 2, ( sz - voxels_per_tile ) / 2 );
    for( const vox_voxel &v : voxels ) {
        const uint32_t c = palette[v.index];
        const voxel_rgb rgb{ static_cast<uint8_t>( ( c >> 16 ) & 0xFF ), static_cast<uint8_t>( ( c >> 8 ) & 0xFF ),
                             static_cast<uint8_t>( c & 0xFF ) };
        grid.set( v.x, v.z, sz - 1 - v.y, rgb, glass[v.index] );
    }
    return grid;
}

bool save_vox_file( const std::string &path, const voxel_grid &grid, std::string &error )
{
    if( grid.sx > 256 || grid.sy > 256 || grid.sz > 256 ) {
        error = "model is larger than 256 voxels";
        return false;
    }
    std::vector<uint32_t> colors;
    for( int y = 0; y < grid.sy; ++y ) {
        for( int z = 0; z < grid.sz; ++z ) {
            for( int x = 0; x < grid.sx; ++x ) {
                if( grid.solid( x, y, z ) ) {
                    colors.push_back( pack_color( grid.color( x, y, z ), grid.glass( x, y, z ) ) );
                }
            }
        }
    }
    int shift = 0;
    std::map<uint32_t, uint8_t> index;
    const auto reduce = [&shift]( uint32_t c ) {
        const uint32_t mask = ( 0xFFu << shift ) & 0xFFu;
        const uint32_t half = shift > 0 ? 1u << ( shift - 1 ) : 0u;
        const uint32_t r = std::min( 255u, ( ( c >> 16 ) & mask ) + half );
        const uint32_t g = std::min( 255u, ( ( c >> 8 ) & mask ) + half );
        const uint32_t b = std::min( 255u, ( c & mask ) + half );
        return ( c & ( 1u << 24 ) ) | ( r << 16 ) | ( g << 8 ) | b;
    };
    while( true ) {
        index.clear();
        for( const uint32_t c : colors ) {
            index.emplace( reduce( c ), 0 );
        }
        if( index.size() <= 255 || shift >= 7 ) {
            break;
        }
        ++shift;
    }
    std::vector<uint32_t> palette;
    for( std::pair<const uint32_t, uint8_t> &e : index ) {
        if( palette.size() >= 255 ) {
            e.second = static_cast<uint8_t>( palette.size() );
            continue;
        }
        palette.push_back( e.first );
        e.second = static_cast<uint8_t>( palette.size() );
    }

    std::vector<uint8_t> size_chunk;
    put_u32( size_chunk, static_cast<uint32_t>( grid.sx ) );
    put_u32( size_chunk, static_cast<uint32_t>( grid.sz ) );
    put_u32( size_chunk, static_cast<uint32_t>( grid.sy ) );

    std::vector<uint8_t> xyzi;
    put_u32( xyzi, static_cast<uint32_t>( colors.size() ) );
    for( int y = 0; y < grid.sy; ++y ) {
        for( int z = 0; z < grid.sz; ++z ) {
            for( int x = 0; x < grid.sx; ++x ) {
                if( !grid.solid( x, y, z ) ) {
                    continue;
                }
                const uint32_t c = reduce( pack_color( grid.color( x, y, z ), grid.glass( x, y, z ) ) );
                xyzi.push_back( static_cast<uint8_t>( x ) );
                xyzi.push_back( static_cast<uint8_t>( grid.sz - 1 - z ) );
                xyzi.push_back( static_cast<uint8_t>( y ) );
                xyzi.push_back( index[c] );
            }
        }
    }

    std::vector<uint8_t> rgba;
    for( int i = 0; i < 256; ++i ) {
        if( i < static_cast<int>( palette.size() ) ) {
            const uint32_t c = palette[i];
            rgba.push_back( static_cast<uint8_t>( ( c >> 16 ) & 0xFF ) );
            rgba.push_back( static_cast<uint8_t>( ( c >> 8 ) & 0xFF ) );
            rgba.push_back( static_cast<uint8_t>( c & 0xFF ) );
            rgba.push_back( 255 );
        } else {
            rgba.insert( rgba.end(), { 0, 0, 0, 255 } );
        }
    }

    std::vector<uint8_t> children;
    put_chunk( children, "SIZE", size_chunk );
    put_chunk( children, "XYZI", xyzi );
    put_chunk( children, "RGBA", rgba );
    for( size_t i = 0; i < palette.size(); ++i ) {
        if( palette[i] & ( 1u << 24 ) ) {
            std::vector<uint8_t> matl;
            put_u32( matl, static_cast<uint32_t>( i + 1 ) );
            put_u32( matl, 2 );
            put_str( matl, "_type" );
            put_str( matl, "_glass" );
            put_str( matl, "_alpha" );
            put_str( matl, "0.5" );
            put_chunk( children, "MATL", matl );
        }
    }

    std::vector<uint8_t> out;
    put_tag( out, "VOX " );
    put_u32( out, 150 );
    put_tag( out, "MAIN" );
    put_u32( out, 0 );
    put_u32( out, static_cast<uint32_t>( children.size() ) );
    out.insert( out.end(), children.begin(), children.end() );

    std::ofstream f( path, std::ios::binary );
    if( !f ) {
        error = "cannot write " + path;
        return false;
    }
    f.write( reinterpret_cast<const char *>( out.data() ), static_cast<std::streamsize>( out.size() ) );
    return static_cast<bool>( f );
}
