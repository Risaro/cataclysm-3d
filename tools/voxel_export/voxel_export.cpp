#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "voxel_model.h"
#include "voxel_models.h"
#include "voxel_vox.h"

namespace
{

struct export_item {
    vm_key key;
    std::string file;
    bool tint = false;
    bool with_material = false;
};

constexpr voxel_rgb neutral{ 232, 232, 228 };

voxel_rgb default_color( vm_kind kind, vm_material material )
{
    switch( material ) {
        case vm_material::grass:
            return { 80, 128, 52 };
        case vm_material::dirt:
            return { 112, 84, 56 };
        case vm_material::sand:
            return { 205, 185, 130 };
        case vm_material::pavement:
            return { 62, 62, 66 };
        case vm_material::sidewalk:
            return { 165, 163, 156 };
        case vm_material::concrete:
            return { 158, 156, 150 };
        case vm_material::wood:
        case vm_material::log:
            return kind == vm_kind::floor ? voxel_rgb{ 140, 104, 68 } : voxel_rgb{ 135, 98, 62 };
        case vm_material::tile:
            return { 215, 215, 210 };
        case vm_material::carpet:
            return { 150, 70, 70 };
        case vm_material::rock:
            return { 108, 102, 96 };
        case vm_material::metal:
            return { 125, 130, 136 };
        case vm_material::brick:
            return { 150, 72, 55 };
        case vm_material::glass:
            return { 150, 200, 230 };
        case vm_material::snow:
            return { 232, 236, 242 };
        case vm_material::generic:
            break;
    }
    switch( kind ) {
        case vm_kind::wall:
        case vm_kind::window:
            return { 200, 190, 172 };
        case vm_kind::door:
        case vm_kind::door_open:
            return { 128, 88, 54 };
        case vm_kind::tree:
        case vm_kind::young_tree:
        case vm_kind::shrub:
            return { 70, 122, 46 };
        case vm_kind::pine:
            return { 38, 88, 50 };
        case vm_kind::dead_tree:
        case vm_kind::stump:
            return { 92, 68, 48 };
        case vm_kind::tall_grass:
        case vm_kind::hedge:
            return { 92, 140, 58 };
        case vm_kind::flower:
            return { 220, 200, 60 };
        case vm_kind::water:
            return { 62, 112, 162 };
        case vm_kind::fence:
            return { 120, 88, 56 };
        case vm_kind::humanoid:
            return { 226, 184, 146 };
        case vm_kind::zombie:
            return { 128, 146, 112 };
        case vm_kind::quadruped:
            return { 140, 110, 80 };
        case vm_kind::bird:
            return { 120, 110, 100 };
        case vm_kind::bug:
            return { 70, 60, 50 };
        case vm_kind::robot:
            return { 150, 155, 160 };
        case vm_kind::blob:
            return { 110, 160, 90 };
        case vm_kind::fish:
            return { 120, 140, 160 };
        case vm_kind::plant_creature:
            return { 160, 90, 140 };
        case vm_kind::vehicle:
        case vm_kind::veh_frame:
        case vm_kind::veh_wheel:
        case vm_kind::veh_body:
        case vm_kind::veh_window:
        case vm_kind::veh_seat:
        case vm_kind::veh_part:
            return { 150, 60, 50 };
        case vm_kind::fire:
            return { 240, 120, 40 };
        case vm_kind::cursor:
        case vm_kind::marker:
            return { 255, 220, 60 };
        case vm_kind::rain:
            return { 150, 175, 215 };
        case vm_kind::snow:
            return { 240, 244, 252 };
        case vm_kind::bolt:
            return { 255, 230, 140 };
        case vm_kind::pillar:
        case vm_kind::grave:
        case vm_kind::boulder:
        case vm_kind::rubble:
            return { 150, 148, 142 };
        case vm_kind::anvil:
            return { 60, 62, 66 };
        case vm_kind::stairs_up:
        case vm_kind::stairs_down:
        case vm_kind::hole:
        case vm_kind::ramp:
            return { 140, 120, 100 };
        default:
            break;
    }
    return neutral;
}

bool tinted( vm_kind kind )
{
    switch( kind ) {
        case vm_kind::bed:
        case vm_kind::table:
        case vm_kind::chair:
        case vm_kind::sofa:
        case vm_kind::counter:
        case vm_kind::cabinet:
        case vm_kind::crate:
        case vm_kind::barrel:
        case vm_kind::dumpster:
        case vm_kind::sign:
        case vm_kind::furniture:
        case vm_kind::machine:
        case vm_kind::stack:
        case vm_kind::mound:
        case vm_kind::mat:
        case vm_kind::panel:
        case vm_kind::trap:
        case vm_kind::item_pile:
        case vm_kind::item_gun:
        case vm_kind::item_blade:
        case vm_kind::item_tool:
        case vm_kind::item_clothing:
        case vm_kind::item_food:
        case vm_kind::item_bottle:
        case vm_kind::item_book:
        case vm_kind::item_ammo:
        case vm_kind::splat:
        case vm_kind::cloud:
            return true;
        default:
            return false;
    }
}

std::string json_escape( const std::string &s )
{
    std::string out;
    for( const char c : s ) {
        if( c == '"' || c == '\\' ) {
            out += '\\';
        }
        out += c;
    }
    return out;
}

} // namespace

int main( int argc, char **argv )
{
    if( argc < 2 ) {
        std::fprintf( stderr, "usage: %s <output directory> [voxelset name]\n", argv[0] );
        return 1;
    }
    const std::filesystem::path out = argv[1];
    const std::string name = argc > 2 ? argv[2] : "Starter voxel tileset";
    std::filesystem::create_directories( out / "builtin" );

    std::vector<export_item> items;
    const std::set<vm_kind> per_material = { vm_kind::floor, vm_kind::wall, vm_kind::window };
    for( int ki = 0; ki < vm_kind_count; ++ki ) {
        const vm_kind kind = static_cast<vm_kind>( ki );
        if( per_material.count( kind ) ) {
            for( int mi = 0; mi < vm_material_count; ++mi ) {
                const vm_material material = static_cast<vm_material>( mi );
                if( kind == vm_kind::window && material == vm_material::glass ) {
                    continue;
                }
                export_item e;
                e.key.kind = kind;
                e.key.material = material;
                e.key.color = default_color( kind, material );
                e.key.color2 = kind == vm_kind::window ? voxel_rgb{ 230, 230, 225 } : voxel_shade( e.key.color, 0.8f );
                e.file = std::string( "builtin/" ) + vm_kind_name( kind ) + "_" + vm_material_name( material ) +
                         ".vox";
                e.with_material = true;
                items.push_back( e );
            }
            continue;
        }
        export_item e;
        e.key.kind = kind;
        e.tint = tinted( kind );
        e.key.color = e.tint ? neutral : default_color( kind, vm_material::generic );
        e.key.color2 = kind == vm_kind::humanoid || kind == vm_kind::zombie ? voxel_rgb{ 58, 98, 168 } :
                       voxel_shade( e.key.color, 0.8f );
        if( kind == vm_kind::tree || kind == vm_kind::pine || kind == vm_kind::young_tree ) {
            e.key.color2 = { 92, 68, 48 };
        }
        if( kind == vm_kind::fence ) {
            e.key.param = 2 | 8;
        }
        if( kind == vm_kind::vehicle ) {
            e.key.param = vmv_wheel | vmv_board;
        }
        e.file = std::string( "builtin/" ) + vm_kind_name( kind ) + ".vox";
        items.push_back( e );
    }

    std::ofstream json( out / "voxelset.json" );
    json << "{\n";
    json << "  \"name\": \"" << json_escape( name ) << "\",\n";
    json << "  \"voxels_per_tile\": 8,\n";
    json << "  \"models\": [\n";
    int written = 0;
    for( size_t i = 0; i < items.size(); ++i ) {
        const export_item &e = items[i];
        std::string error;
        if( !save_vox_file( ( out / e.file ).string(), make_voxel_model( e.key ), error ) ) {
            std::fprintf( stderr, "%s: %s\n", e.file.c_str(), error.c_str() );
            continue;
        }
        if( written++ > 0 ) {
            json << ",\n";
        }
        json << "    { \"builtin\": \"" << vm_kind_name( e.key.kind ) << "\"";
        if( e.with_material ) {
            json << ", \"material\": \"" << vm_material_name( e.key.material ) << "\"";
        }
        json << ", \"file\": \"" << e.file << "\"";
        if( e.tint ) {
            json << ", \"tint\": true";
        }
        if( vm_is_emissive( e.key.kind ) ) {
            json << ", \"emissive\": true";
        }
        json << " }";
    }
    json << "\n  ]\n}\n";
    std::printf( "exported %d models to %s\n", written, out.string().c_str() );
    return 0;
}
