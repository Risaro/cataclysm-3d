#include "voxel_tileset.h"

#include <filesystem>
#include <memory>
#include <system_error>

#include "cata_path.h"
#include "cata_utility.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "options.h"
#include "path_info.h"

namespace
{

std::vector<std::filesystem::path> voxelset_roots()
{
    return { PATH_INFO::user_gfx().get_unrelative_path() / "voxel",
             PATH_INFO::gfxdir().get_unrelative_path() / "voxel" };
}

bool read_voxelset( const std::filesystem::path &dir, voxelset &out )
{
    const std::filesystem::path file = dir / "voxelset.json";
    std::error_code ec;
    if( !std::filesystem::is_regular_file( file, ec ) ) {
        return false;
    }
    out.dir = dir.generic_u8string();
    out.id = dir.filename().generic_u8string();
    out.name = out.id;
    return read_from_file_json( cata_path( cata_path::root_path::unknown, file ),
    [&out]( const JsonValue & jv ) {
        const JsonObject jo = jv.get_object();
        jo.allow_omitted_members();
        out.name = jo.get_string( "name", out.id );
        out.voxels_per_tile = std::max( 1, jo.get_int( "voxels_per_tile", 8 ) );
        if( !jo.has_array( "models" ) ) {
            return;
        }
        for( const JsonObject entry : jo.get_array( "models" ) ) {
            entry.allow_omitted_members();
            voxelset_entry e;
            if( entry.has_array( "files" ) ) {
                e.files = entry.get_string_array( "files" );
            } else if( entry.has_string( "file" ) ) {
                e.files.push_back( entry.get_string( "file" ) );
            }
            if( e.files.empty() ) {
                continue;
            }
            if( entry.has_array( "offset" ) ) {
                const JsonArray off = entry.get_array( "offset" );
                for( size_t i = 0; i < 3 && i < off.size(); ++i ) {
                    e.offset[i] = off.get_int( i );
                }
            }
            e.scale = static_cast<float>( entry.get_float( "scale", 1.0 ) );
            e.random_rotation = entry.get_bool( "random_rotation", false );
            e.rotates = entry.get_bool( "rotates", true );
            e.tint = entry.get_bool( "tint", false );
            e.emissive = entry.get_bool( "emissive", false );
            out.entries.push_back( e );
            const int idx = static_cast<int>( out.entries.size() ) - 1;
            if( entry.has_member( "id" ) ) {
                for( const std::string &gid : entry.get_as_string_array( "id" ) ) {
                    out.add_id( gid, idx );
                }
            }
            if( entry.has_member( "builtin" ) ) {
                const std::string material = entry.get_string( "material", "" );
                for( const std::string &kind : entry.get_as_string_array( "builtin" ) ) {
                    out.add_builtin( material.empty() ? kind : kind + ":" + material, idx );
                }
            }
        }
    } );
}

std::unique_ptr<voxelset> loaded;
std::string loaded_option;
int generation = 0;

} // namespace

const voxelset_entry *voxelset::find_id( const std::string &game_id ) const
{
    const auto it = by_id.find( game_id );
    return it == by_id.end() ? nullptr : &entries[it->second];
}

const voxelset_entry *voxelset::find_builtin( vm_kind kind, vm_material material ) const
{
    const std::string k = vm_kind_name( kind );
    const auto exact = by_builtin.find( k + ":" + vm_material_name( material ) );
    if( exact != by_builtin.end() ) {
        return &entries[exact->second];
    }
    const auto it = by_builtin.find( k );
    return it == by_builtin.end() ? nullptr : &entries[it->second];
}

void voxelset::add_id( const std::string &game_id, int entry )
{
    by_id[game_id] = entry;
}

void voxelset::add_builtin( const std::string &key, int entry )
{
    by_builtin[key] = entry;
}

std::vector<std::pair<std::string, std::string>> list_voxelsets()
{
    std::vector<std::pair<std::string, std::string>> result;
    for( const std::filesystem::path &root : voxelset_roots() ) {
        std::error_code ec;
        if( !std::filesystem::is_directory( root, ec ) ) {
            continue;
        }
        for( const std::filesystem::directory_entry &d : std::filesystem::directory_iterator( root, ec ) ) {
            voxelset vs;
            if( d.is_directory( ec ) && read_voxelset( d.path(), vs ) ) {
                bool dup = false;
                for( const std::pair<std::string, std::string> &r : result ) {
                    dup = dup || r.first == vs.id;
                }
                if( !dup ) {
                    result.emplace_back( vs.id, vs.name );
                }
            }
        }
    }
    return result;
}

const voxelset *active_voxelset()
{
    const std::string wanted = get_option<std::string>( "VOXEL_TILESET" );
    if( wanted == loaded_option ) {
        return loaded.get();
    }
    loaded_option = wanted;
    loaded.reset();
    ++generation;
    if( wanted.empty() || wanted == "builtin" ) {
        return nullptr;
    }
    for( const std::filesystem::path &root : voxelset_roots() ) {
        std::unique_ptr<voxelset> vs = std::make_unique<voxelset>();
        if( read_voxelset( root / wanted, *vs ) ) {
            loaded = std::move( vs );
            break;
        }
    }
    if( !loaded ) {
        DebugLog( D_WARNING, D_SDL ) << "voxel tileset " << wanted << " not found";
    }
    return loaded.get();
}

int active_voxelset_generation()
{
    active_voxelset();
    return generation;
}
