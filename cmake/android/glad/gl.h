#pragma once

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#ifdef __cplusplus
#include <cstring>
#endif

typedef void (*GLADapiproc)(void);
typedef GLADapiproc (*GLADloadfunc)(const char* name);

inline int gladLoadGL(GLADloadfunc)
{
    return 1;
}

#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#endif

#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#endif

#ifndef GL_CLIP_DISTANCE0
#define GL_CLIP_DISTANCE0 0x3000
#endif

#ifndef GL_CLIP_DISTANCE1
#define GL_CLIP_DISTANCE1 0x3001
#endif

inline bool dxHasGlExtension(const char* name)
{
    GLint numExtensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
    for (GLint i = 0; i < numExtensions; i++) {
        const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (ext && std::strcmp(ext, name) == 0) {
            return true;
        }
    }
    return false;
}

inline bool dxHasAnisotropy()
{
    static const bool supported = dxHasGlExtension("GL_EXT_texture_filter_anisotropic");
    return supported;
}

inline bool dxHasClipCullDistance()
{
    static const bool supported = []() {
        if (!dxHasGlExtension("GL_EXT_clip_cull_distance")) {
            return false;
        }
        GLint maxClips = 0;
        glGetIntegerv(GL_MAX_CLIP_DISTANCES_EXT, &maxClips);
        return maxClips >= 8;
    }();
    return supported;
}

using PFNGLCOLORMASKIDXPROC = void (GL_APIENTRYP)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean);

inline PFNGLCOLORMASKIDXPROC dxResolveColorMaski()
{
    static const PFNGLCOLORMASKIDXPROC fn = []() -> PFNGLCOLORMASKIDXPROC {
        if (auto p = reinterpret_cast<PFNGLCOLORMASKIDXPROC>(eglGetProcAddress("glColorMaski"))) {
            return p;
        }
        if (auto p = reinterpret_cast<PFNGLCOLORMASKIDXPROC>(eglGetProcAddress("glColorMaskiEXT"))) {
            return p;
        }
        return reinterpret_cast<PFNGLCOLORMASKIDXPROC>(eglGetProcAddress("glColorMaskiOES"));
    }();
    return fn;
}

inline bool dxHasColorMaski()
{
    return dxResolveColorMaski() != nullptr;
}

inline void glColorMaski(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    if (auto fn = dxResolveColorMaski()) {
        fn(index, r, g, b, a);
    }
}

#define GLAD_GL_ARB_texture_filter_anisotropic false
#define GLAD_GL_EXT_texture_filter_anisotropic dxHasAnisotropy()

inline void glClearDepth(double depth)
{
    glClearDepthf(static_cast<float>(depth));
}

inline void glDepthRange(double nearVal, double farVal)
{
    glDepthRangef(static_cast<float>(nearVal), static_cast<float>(farVal));
}

inline void glMultiDrawArrays(GLenum mode, const GLint* first, const GLsizei* count, GLsizei drawcount)
{
    for (GLsizei i = 0; i < drawcount; i++) {
        if (count[i] > 0) {
            glDrawArrays(mode, first[i], count[i]);
        }
    }
}
