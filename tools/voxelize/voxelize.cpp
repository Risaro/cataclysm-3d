#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "voxel_import.h"
#include "voxel_vox.h"

int main( int argc, char **argv )
{
    if( argc < 3 ) {
        std::fprintf( stderr,
                      "usage: %s <input.obj> <output.vox> [--voxels-per-tile N] [--height VOXELS] [--scale S]\n",
                      argv[0] );
        return 1;
    }
    voxel_import_options options;
    for( int i = 3; i + 1 < argc; i += 2 ) {
        if( std::strcmp( argv[i], "--voxels-per-tile" ) == 0 ) {
            options.voxels_per_tile = std::atoi( argv[i + 1] );
        } else if( std::strcmp( argv[i], "--height" ) == 0 ) {
            options.height_voxels = static_cast<float>( std::atof( argv[i + 1] ) );
        } else if( std::strcmp( argv[i], "--scale" ) == 0 ) {
            options.scale = static_cast<float>( std::atof( argv[i + 1] ) );
        }
    }
    std::string error;
    const auto grid = voxelize_obj( argv[1], options, error );
    if( !grid ) {
        std::fprintf( stderr, "%s\n", error.c_str() );
        return 1;
    }
    if( !save_vox_file( argv[2], *grid, error ) ) {
        std::fprintf( stderr, "%s\n", error.c_str() );
        return 1;
    }
    std::printf( "%s: %dx%dx%d voxels\n", argv[2], grid->sx, grid->sy, grid->sz );
    return 0;
}
