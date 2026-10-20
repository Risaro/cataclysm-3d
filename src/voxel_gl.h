#pragma once
#ifndef CATA_SRC_VOXEL_GL_H
#define CATA_SRC_VOXEL_GL_H

#if defined(TILES)

#define GL_GLEXT_PROTOTYPES 1
#include <SDL3/SDL_opengl.h>

#define VOXEL_GL_FUNCTIONS( X ) \
    X( glActiveTexture ) \
    X( glAttachShader ) \
    X( glBindBuffer ) \
    X( glBindFramebuffer ) \
    X( glBindRenderbuffer ) \
    X( glBindTexture ) \
    X( glBindVertexArray ) \
    X( glBlendFunc ) \
    X( glBlitFramebuffer ) \
    X( glBufferData ) \
    X( glCheckFramebufferStatus ) \
    X( glClear ) \
    X( glClearColor ) \
    X( glCompileShader ) \
    X( glCreateProgram ) \
    X( glCreateShader ) \
    X( glCullFace ) \
    X( glDeleteBuffers ) \
    X( glDeleteFramebuffers ) \
    X( glDeleteProgram ) \
    X( glDeleteRenderbuffers ) \
    X( glDeleteShader ) \
    X( glDeleteTextures ) \
    X( glDeleteVertexArrays ) \
    X( glDepthFunc ) \
    X( glDepthMask ) \
    X( glDisable ) \
    X( glDrawArraysInstanced ) \
    X( glEnable ) \
    X( glEnableVertexAttribArray ) \
    X( glFinish ) \
    X( glFramebufferRenderbuffer ) \
    X( glFramebufferTexture2D ) \
    X( glFrontFace ) \
    X( glGenBuffers ) \
    X( glGenFramebuffers ) \
    X( glGenRenderbuffers ) \
    X( glGenTextures ) \
    X( glGenVertexArrays ) \
    X( glGetError ) \
    X( glGetIntegerv ) \
    X( glGetProgramInfoLog ) \
    X( glGetProgramiv ) \
    X( glGetShaderInfoLog ) \
    X( glGetShaderiv ) \
    X( glGetUniformLocation ) \
    X( glLinkProgram ) \
    X( glPixelStorei ) \
    X( glPolygonOffset ) \
    X( glReadBuffer ) \
    X( glReadPixels ) \
    X( glRenderbufferStorage ) \
    X( glRenderbufferStorageMultisample ) \
    X( glShaderSource ) \
    X( glTexImage2D ) \
    X( glTexParameteri ) \
    X( glUniform1f ) \
    X( glUniform1i ) \
    X( glUniform3f ) \
    X( glUniformMatrix4fv ) \
    X( glUseProgram ) \
    X( glVertexAttribDivisor ) \
    X( glVertexAttribPointer ) \
    X( glViewport )

struct voxel_gl_api {
#define VOXEL_GL_DECLARE( name ) decltype( &::name ) name = nullptr;
    VOXEL_GL_FUNCTIONS( VOXEL_GL_DECLARE )
#undef VOXEL_GL_DECLARE

    bool load();
};

voxel_gl_api &voxel_gl();

#endif

#endif // CATA_SRC_VOXEL_GL_H
