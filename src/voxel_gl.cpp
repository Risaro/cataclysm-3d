#include "voxel_gl.h"

#if defined(TILES)

#include <SDL3/SDL_video.h>

bool voxel_gl_api::load()
{
    bool ok = true;
#define VOXEL_GL_LOAD( fn ) \
    fn = reinterpret_cast<decltype( fn )>( SDL_GL_GetProcAddress( #fn ) ); \
    ok = ok && fn != nullptr;
    VOXEL_GL_FUNCTIONS( VOXEL_GL_LOAD )
#undef VOXEL_GL_LOAD
    return ok;
}

voxel_gl_api &voxel_gl()
{
    static voxel_gl_api api;
    return api;
}

#endif
