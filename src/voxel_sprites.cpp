#include "voxel_sprites.h"

#if defined(TILES)

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>

#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

#include "cata_path.h"
#include "cata_utility.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "options.h"

namespace
{

int first_sprite( const JsonValue &v )
{
    if( v.test_int() ) {
        return v.get_int();
    }
    if( v.test_array() ) {
        const JsonArray arr = v.get_array();
        return arr.empty() ? -1 : first_sprite( arr[0] );
    }
    if( v.test_object() ) {
        const JsonObject o = v.get_object();
        o.allow_omitted_members();
        return o.has_member( "sprite" ) ? first_sprite( o.get_member( "sprite" ) ) : -1;
    }
    return -1;
}

std::string config_name( const std::filesystem::path &dir )
{
    std::ifstream f( dir / "tileset.txt" );
    std::string line;
    while( std::getline( f, line ) ) {
        if( line.compare( 0, 5, "JSON:" ) == 0 ) {
            std::string name = line.substr( 5 );
            const size_t a = name.find_first_not_of( " \t\r" );
            const size_t b = name.find_last_not_of( " \t\r" );
            if( a != std::string::npos ) {
                return name.substr( a, b - a + 1 );
            }
        }
    }
    return "tile_config.json";
}

} // namespace

void sprite_atlas::release()
{
    voxel_gl_api &gl = voxel_gl();
    for( GLuint t : textures ) {
        gl.glDeleteTextures( 1, &t );
    }
    textures.clear();
    sprites.clear();
    tiles.clear();
    subtiles.clear();
    ok = false;
}

bool sprite_atlas::load( const std::string &tileset_id )
{
    release();
    loaded_id = tileset_id;
    const auto it = TILESETS.find( tileset_id );
    if( it == TILESETS.end() ) {
        return false;
    }
    const std::filesystem::path dir = it->second.get_unrelative_path();
    const std::filesystem::path config = dir / config_name( dir );
    voxel_gl_api &gl = voxel_gl();

    int tile_w = 32;
    int tile_h = 32;
    const bool parsed = read_from_file_json( cata_path( cata_path::root_path::unknown, config ),
    [&]( const JsonValue & jv ) {
        const JsonObject root = jv.get_object();
        root.allow_omitted_members();
        if( root.has_array( "tile_info" ) ) {
            for( const JsonObject info : root.get_array( "tile_info" ) ) {
                info.allow_omitted_members();
                tile_w = info.get_int( "width", 32 );
                tile_h = info.get_int( "height", 32 );
            }
        }
        if( !root.has_array( "tiles-new" ) ) {
            return;
        }
        for( const JsonObject sheet : root.get_array( "tiles-new" ) ) {
            sheet.allow_omitted_members();
            const std::string file = sheet.get_string( "file", "" );
            const int sw = sheet.get_int( "sprite_width", tile_w );
            const int sh = sheet.get_int( "sprite_height", tile_h );
            const int ox = sheet.get_int( "sprite_offset_x", 0 );
            const int oy = sheet.get_int( "sprite_offset_y", 0 );
            SDL_Surface *raw = file.empty() ? nullptr : IMG_Load( ( dir / file ).string().c_str() );
            if( raw == nullptr ) {
                continue;
            }
            SDL_Surface *surf = SDL_ConvertSurface( raw, SDL_PIXELFORMAT_RGBA32 );
            SDL_DestroySurface( raw );
            if( surf == nullptr ) {
                continue;
            }
            GLuint tex = 0;
            gl.glGenTextures( 1, &tex );
            gl.glBindTexture( GL_TEXTURE_2D, tex );
            gl.glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
            gl.glPixelStorei( GL_UNPACK_ROW_LENGTH, surf->pitch / 4 );
            gl.glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, surf->w, surf->h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                             surf->pixels );
            gl.glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
            gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
            gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
            gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
            gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
            textures.push_back( tex );
            const int sheet_index = static_cast<int>( textures.size() ) - 1;
            const int cols = std::max( 1, surf->w / sw );
            const int rows = std::max( 1, surf->h / sh );
            const float iw = 1.0f / static_cast<float>( surf->w );
            const float ih = 1.0f / static_cast<float>( surf->h );
            for( int r = 0; r < rows; ++r ) {
                for( int c = 0; c < cols; ++c ) {
                    sprite_ref s;
                    s.sheet = sheet_index;
                    s.u0 = ( c * sw + 0.02f ) * iw;
                    s.u1 = ( ( c + 1 ) * sw - 0.02f ) * iw;
                    s.v0 = ( r * sh + 0.02f ) * ih;
                    s.v1 = ( ( r + 1 ) * sh - 0.02f ) * ih;
                    s.w = static_cast<float>( sw ) / tile_w;
                    s.h = static_cast<float>( sh ) / tile_w;
                    s.shift_x = ( ox + sw * 0.5f - tile_w * 0.5f ) / tile_w;
                    s.shift_y = static_cast<float>( tile_h - ( oy + sh ) ) / tile_w;
                    sprites.push_back( s );
                }
            }
            SDL_DestroySurface( surf );
            if( !sheet.has_array( "tiles" ) ) {
                continue;
            }
            const auto register_tile = [&]( const JsonObject & t, const std::string & suffix ) {
                tile_entry e;
                if( t.has_member( "fg" ) ) {
                    const int f = first_sprite( t.get_member( "fg" ) );
                    e.fg = f >= 0 ? f : -1;
                }
                if( t.has_member( "bg" ) ) {
                    const int b = first_sprite( t.get_member( "bg" ) );
                    e.bg = b >= 0 ? b : -1;
                }
                for( const std::string &tid : t.get_as_string_array( "id" ) ) {
                    tiles[tid + suffix] = e;
                }
            };
            for( const JsonObject t : sheet.get_array( "tiles" ) ) {
                t.allow_omitted_members();
                register_tile( t, "" );
                if( t.has_array( "additional_tiles" ) ) {
                    for( const JsonObject sub : t.get_array( "additional_tiles" ) ) {
                        sub.allow_omitted_members();
                        const std::string sub_id = sub.get_string( "id", "" );
                        if( sub.has_member( "fg" ) ) {
                            std::vector<int> list;
                            const JsonValue fgv = sub.get_member( "fg" );
                            if( fgv.test_array() ) {
                                for( const JsonValue elem : fgv.get_array() ) {
                                    list.push_back( first_sprite( elem ) );
                                }
                            } else {
                                list.push_back( first_sprite( fgv ) );
                            }
                            for( const std::string &tid : t.get_as_string_array( "id" ) ) {
                                subtiles[sub_id + "#" + tid] = list;
                            }
                        }
                        if( sub_id == "unconnected" || sub_id == "center" ) {
                            tile_entry e;
                            if( sub.has_member( "fg" ) ) {
                                e.fg = first_sprite( sub.get_member( "fg" ) );
                            }
                            if( sub.has_member( "bg" ) ) {
                                e.bg = first_sprite( sub.get_member( "bg" ) );
                            }
                            for( const std::string &tid : t.get_as_string_array( "id" ) ) {
                                tiles["#" + sub_id + "#" + tid] = e;
                            }
                        }
                    }
                }
            }
        }
    } );
    ok = parsed && !sprites.empty();
    if( !ok ) {
        DebugLog( D_WARNING, D_SDL ) << "voxel sprite atlas failed for tileset " << tileset_id;
    }
    return ok;
}

const sprite_ref *sprite_atlas::fg( const std::string &id ) const
{
    for( const char *prefix : { "#unconnected#", "#center#", "" } ) {
        const auto it = tiles.find( prefix + id );
        if( it != tiles.end() && it->second.fg >= 0 && it->second.fg < static_cast<int>( sprites.size() ) ) {
            return &sprites[it->second.fg];
        }
    }
    return nullptr;
}

const sprite_ref *sprite_atlas::bg( const std::string &id ) const
{
    const auto it = tiles.find( id );
    if( it != tiles.end() && it->second.bg >= 0 && it->second.bg < static_cast<int>( sprites.size() ) ) {
        return &sprites[it->second.bg];
    }
    return nullptr;
}

const sprite_ref *sprite_atlas::subtile( const std::string &id, const std::string &sub,
                                        int rotation, int &extra_rotation ) const
{
    extra_rotation = 0;
    const auto it = subtiles.find( sub + "#" + id );
    if( it == subtiles.end() || it->second.empty() ) {
        return nullptr;
    }
    const std::vector<int> &list = it->second;
    int index = 0;
    if( rotation < static_cast<int>( list.size() ) ) {
        index = list[rotation];
    } else {
        index = list[0];
        extra_rotation = rotation;
    }
    if( index < 0 || index >= static_cast<int>( sprites.size() ) ) {
        return nullptr;
    }
    return &sprites[index];
}

GLuint sprite_atlas::texture( int sheet ) const
{
    return sheet >= 0 && sheet < static_cast<int>( textures.size() ) ? textures[sheet] : 0;
}

int sprite_atlas::sheet_count() const
{
    return static_cast<int>( textures.size() );
}

#endif
