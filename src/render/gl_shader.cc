#include "render/gl_shader.h"

#include <glad/gl.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>

#ifdef __ANDROID__
#include <SDL3/SDL_log.h>
#endif

namespace dx {
    namespace {
        const char* vertexSource = R"(
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec2 aLightUV;
layout(location = 3) in vec4 aColor;
layout(location = 4) in vec3 aNormal;
layout(location = 5) in vec3 aFog;
uniform mat4 uViewProj;
uniform vec2 uPan;
uniform vec2 uScale;
uniform int uFlags;
uniform vec3 uEye;
uniform vec3 uForward;
uniform vec4 uClipPlanes[8];
uniform int uClipCount;
uniform float uDetailDistance;
#ifdef CLIP_PLANES
#ifdef HARDWARE_CLIP
#ifndef ES_IMPLICIT_CLIP
out float gl_ClipDistance[8];
#endif
#else
out vec4 vClip0;
out vec4 vClip1;
#endif
#endif
centroid out vec2 vUV;
centroid out vec2 vLightUV;
out vec4 vColor;
out vec3 vWorld;
out vec3 vNormal;
out vec3 vFog;
void main()
{
    vFog = aFog;
    vWorld = aPosition;
    vNormal = aNormal;
    vUV = aUV * uScale + uPan;
    vLightUV = aLightUV;
    vColor = aColor;
    if ((uFlags & 32) != 0)
    {
        float depth = dot(aPosition - uEye, uForward);
        float fade = roundEven((1.0 - clamp(depth, 0.0, uDetailDistance) / uDetailDistance) * 128.0) / 255.0;
        vColor = vec4(vec3(128.0 / 255.0), fade);
    }
#ifdef CLIP_PLANES
#ifdef HARDWARE_CLIP
    gl_ClipDistance[0] = 0 < uClipCount ? dot(uClipPlanes[0], vec4(aPosition, 1.0)) : 1.0;
    gl_ClipDistance[1] = 1 < uClipCount ? dot(uClipPlanes[1], vec4(aPosition, 1.0)) : 1.0;
    gl_ClipDistance[2] = 2 < uClipCount ? dot(uClipPlanes[2], vec4(aPosition, 1.0)) : 1.0;
    gl_ClipDistance[3] = 3 < uClipCount ? dot(uClipPlanes[3], vec4(aPosition, 1.0)) : 1.0;
    gl_ClipDistance[4] = 4 < uClipCount ? dot(uClipPlanes[4], vec4(aPosition, 1.0)) : 1.0;
    gl_ClipDistance[5] = 5 < uClipCount ? dot(uClipPlanes[5], vec4(aPosition, 1.0)) : 1.0;
    gl_ClipDistance[6] = 6 < uClipCount ? dot(uClipPlanes[6], vec4(aPosition, 1.0)) : 1.0;
    gl_ClipDistance[7] = 7 < uClipCount ? dot(uClipPlanes[7], vec4(aPosition, 1.0)) : 1.0;
#else
    float clip[8];
    for (int i = 0; i < 8; i++)
    {
        clip[i] = i < uClipCount ? dot(uClipPlanes[i], vec4(aPosition, 1.0)) : 1.0;
    }
    vClip0 = vec4(clip[0], clip[1], clip[2], clip[3]);
    vClip1 = vec4(clip[4], clip[5], clip[6], clip[7]);
#endif
#endif
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
)";

        const char* fragmentSource = R"(
centroid in vec2 vUV;
centroid in vec2 vLightUV;
in vec4 vColor;
in vec3 vWorld;
in vec3 vNormal;
in vec3 vFog;
#if defined(CLIP_PLANES) && !defined(HARDWARE_CLIP)
in vec4 vClip0;
in vec4 vClip1;
#endif
uniform sampler2D uTexture;
uniform sampler2D uLightmap;
uniform int uFlags;
uniform vec4 uTint;
uniform int uLightCount;
uniform vec4 uLightPosition[8];
uniform vec4 uLightColor[8];
uniform vec4 uLightDirection[8];
uniform float uOcclusion;
uniform float uGamma;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 occlusionMask;
mediump vec3 lightmap()
{
#if defined(SMOOTH_LIGHTMAP) && !defined(CLIP_PLANES)
    vec2 size = vec2(textureSize(uLightmap, 0));
    vec2 invSize = 1.0 / size;
    vec2 texel = vLightUV * size - 0.5;
    vec2 f = fract(texel);
    texel -= f;
    mediump vec2 mf = f;
    mediump vec2 f2 = mf * mf;
    mediump vec2 f3 = f2 * mf;
    mediump vec2 w0 = (1.0 - 3.0 * mf + 3.0 * f2 - f3) * (1.0 / 6.0);
    mediump vec2 w1 = (4.0 - 6.0 * f2 + 3.0 * f3) * (1.0 / 6.0);
    mediump vec2 w3 = f3 * (1.0 / 6.0);
    mediump vec2 s0 = w0 + w1;
    mediump vec2 s1 = 1.0 - s0;
    vec2 c0 = (texel - 0.5 + w1 / s0) * invSize;
    vec2 c1 = (texel + 1.5 + w3 / s1) * invSize;
    mediump vec3 top = mix(texture(uLightmap, vec2(c1.x, c0.y)).rgb, texture(uLightmap, vec2(c0.x, c0.y)).rgb, s0.x);
    mediump vec3 bottom = mix(texture(uLightmap, vec2(c1.x, c1.y)).rgb, texture(uLightmap, vec2(c0.x, c1.y)).rgb, s0.x);
    return mix(bottom, top, s0.y);
#else
    return texture(uLightmap, vLightUV).rgb;
#endif
}
mediump vec3 dynamicLight()
{
    mediump vec3 total = vec3(0.0);
    vec3 normal = normalize(vNormal);
    for (int i = 0; i < uLightCount; i++)
    {
        vec3 delta = uLightPosition[i].xyz - vWorld;
        float radius = uLightPosition[i].w;
        float index = floor(dot(delta, delta) * 4093.0 / (radius * radius) + 0.5);
        if (index >= 4096.0)
        {
            continue;
        }
        float v = sqrt((index + 1.0) / 4096.0);
        mediump float falloff = 1.0 + 2.0 * v * v * v - 3.0 * v * v;
        vec3 direction = normalize(delta);
        mediump float attenuation = falloff * abs(dot(direction, normal));
        if (uLightDirection[i].w < 1.0)
        {
            float cosine = dot(direction, uLightDirection[i].xyz);
            if (cosine <= uLightDirection[i].w)
            {
                continue;
            }
            mediump float spot = (cosine - uLightDirection[i].w) / (1.0 - uLightDirection[i].w);
            attenuation *= spot * spot;
        }
        mediump vec3 lightRgb = uLightColor[i].rgb;
        total += lightRgb * attenuation;
    }
    return total;
}
void main()
{
#if defined(CLIP_PLANES) && !defined(HARDWARE_CLIP)
    if (any(lessThan(vClip0, vec4(0.0))) || any(lessThan(vClip1, vec4(0.0))))
    {
        discard;
    }
#endif
    occlusionMask = vec4(uOcclusion);
    if ((uFlags & 32) != 0)
    {
        mediump vec3 vc = vColor.rgb;
        mediump float va = vColor.a;
        fragColor = vec4(mix(vc, texture(uTexture, vUV).rgb, va), 1.0);
        return;
    }
    if ((uFlags & 64) != 0)
    {
        fragColor = texture(uLightmap, vLightUV);
        return;
    }
    mediump vec4 color = (uFlags & 128) != 0 ? vec4(uTint.rgb, vColor.a * uTint.a) : vColor * uTint;
    mediump vec4 texel = vec4(1.0);
    if ((uFlags & 1) != 0)
    {
        texel = texture(uTexture, vUV);
        color *= texel;
    }
#ifdef ALPHA_TEST
    if ((uFlags & 4) != 0 && color.a < 0.5)
    {
        discard;
    }
#endif
    mediump vec3 dynamic = vec3(0.0);
    if ((uFlags & 16) != 0)
    {
        dynamic = dynamicLight();
    }
    if ((uFlags & 2) != 0)
    {
        color.rgb *= min(lightmap() + dynamic, vec3(254.0 / 255.0)) * 2.0;
    }
    else if ((uFlags & 16) != 0)
    {
        color.rgb += texel.rgb * uTint.rgb * dynamic * 2.0;
    }
    mediump vec3 finalRgb = clamp(color.rgb + vFog, 0.0, 1.0);
    if (uGamma != 1.0)
    {
        finalRgb = pow(max(finalRgb, vec3(1e-5)), vec3(uGamma));
    }
    fragColor = vec4(finalRgb, color.a);
}
)";

        const char* gammaVertexSource = R"(#version 330 core
void main()
{
    vec2 position = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
)";

        const char* gammaFragmentSource = R"(#version 330 core
uniform sampler2D uFrame;
uniform float uExponent;
out vec4 fragColor;
void main()
{
    mediump vec3 color = texelFetch(uFrame, ivec2(gl_FragCoord.xy), 0).rgb;
    mediump float exponent = uExponent;
    fragColor = vec4(pow(color, vec3(exponent)), 1.0);
}
)";

        const char* fxaaFragmentSource = R"(#version 330 core
uniform sampler2D uFrame;
uniform vec2 uInverseSize;
out vec4 fragColor;
void main()
{
    vec2 uv = gl_FragCoord.xy * uInverseSize;
    mediump vec3 rgbNW = texture(uFrame, uv + vec2(-1.0, -1.0) * uInverseSize).rgb;
    mediump vec3 rgbNE = texture(uFrame, uv + vec2(1.0, -1.0) * uInverseSize).rgb;
    mediump vec3 rgbSW = texture(uFrame, uv + vec2(-1.0, 1.0) * uInverseSize).rgb;
    mediump vec3 rgbSE = texture(uFrame, uv + vec2(1.0, 1.0) * uInverseSize).rgb;
    mediump vec3 rgbM = texture(uFrame, uv).rgb;
    mediump vec3 weights = vec3(0.299, 0.587, 0.114);
    mediump float lumaNW = dot(rgbNW, weights);
    mediump float lumaNE = dot(rgbNE, weights);
    mediump float lumaSW = dot(rgbSW, weights);
    mediump float lumaSE = dot(rgbSE, weights);
    mediump float lumaM = dot(rgbM, weights);
    mediump float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    mediump float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    vec2 direction = vec2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)), (lumaNW + lumaSW) - (lumaNE + lumaSE));
    float reduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 / 8.0), 1.0 / 128.0);
    float scale = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduce);
    direction = clamp(direction * scale, vec2(-8.0), vec2(8.0)) * uInverseSize;
    mediump vec3 rgbA = 0.5 * (texture(uFrame, uv + direction * (1.0 / 3.0 - 0.5)).rgb + texture(uFrame, uv + direction * (2.0 / 3.0 - 0.5)).rgb);
    mediump vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uFrame, uv - direction * 0.5).rgb + texture(uFrame, uv + direction * 0.5).rgb);
    mediump float lumaB = dot(rgbB, weights);
    fragColor = vec4((lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB, 1.0);
}
)";

        const char* prepareFragmentSource = R"(#version 330 core
uniform highp sampler2D uDepth;
uniform highp sampler2D uMask;
uniform vec3 uLinearize;
uniform int uDivisor;
out uint packedDepth;
void main()
{
    ivec2 source = ivec2(gl_FragCoord.xy) * uDivisor;
    float depth = uLinearize.x / (uLinearize.y - texelFetch(uDepth, source, 0).r * uLinearize.z);
    uint mask = uint(texelFetch(uMask, source, 0).r * 255.0 + 0.5);
    packedDepth = (uint(min(depth * 256.0, 16777215.0)) << 8) | mask;
}
)";

        const char* occlusionFragmentSource = R"(
uniform highp usampler2D uPacked;
uniform vec4 uUnproject;
out uint packedOcclusion;
const float radius = 40.0;
const float invRadiusSq = 1.0 / (radius * radius);
const mat2 turn = mat2(-0.7373689, 0.6754903, -0.6754903, -0.7373689);
vec3 viewPosition(ivec2 pixel, uint value)
{
    float z = float(value >> 8) / 256.0;
    return vec3((vec2(pixel) * uUnproject.xy + uUnproject.zw) * z, z);
}
void main()
{
    ivec2 size = textureSize(uPacked, 0);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    uint center = texelFetch(uPacked, pixel, 0).r;
    uint receiver = center & 255u;
    if (receiver < 3u || (center >> 8) == 0u)
    {
        packedOcclusion = 255u;
        return;
    }
    float reach = radius * 256.0 / (float(center >> 8) * uUnproject.y);
    if (reach < 1.0)
    {
        packedOcclusion = 255u;
        return;
    }
    vec3 position = viewPosition(pixel, center);
    int cDepth = int(center >> 8);
    bool hasR = pixel.x + 1 < size.x;
    bool hasL = pixel.x > 0;
    vec3 dx = vec3(0.0);
    if (hasR && hasL)
    {
        uint rVal = texelFetch(uPacked, pixel + ivec2(1, 0), 0).r;
        uint lVal = texelFetch(uPacked, pixel - ivec2(1, 0), 0).r;
        if (abs(cDepth - int(lVal >> 8)) < abs(int(rVal >> 8) - cDepth))
        {
            dx = position - viewPosition(pixel - ivec2(1, 0), lVal);
        }
        else
        {
            dx = viewPosition(pixel + ivec2(1, 0), rVal) - position;
        }
    }
    else if (hasR)
    {
        dx = viewPosition(pixel + ivec2(1, 0), texelFetch(uPacked, pixel + ivec2(1, 0), 0).r) - position;
    }
    else if (hasL)
    {
        dx = position - viewPosition(pixel - ivec2(1, 0), texelFetch(uPacked, pixel - ivec2(1, 0), 0).r);
    }
    bool hasU = pixel.y + 1 < size.y;
    bool hasD = pixel.y > 0;
    vec3 dy = vec3(0.0);
    if (hasU && hasD)
    {
        uint uVal = texelFetch(uPacked, pixel + ivec2(0, 1), 0).r;
        uint dVal = texelFetch(uPacked, pixel - ivec2(0, 1), 0).r;
        if (abs(cDepth - int(dVal >> 8)) < abs(int(uVal >> 8) - cDepth))
        {
            dy = position - viewPosition(pixel - ivec2(0, 1), dVal);
        }
        else
        {
            dy = viewPosition(pixel + ivec2(0, 1), uVal) - position;
        }
    }
    else if (hasU)
    {
        dy = viewPosition(pixel + ivec2(0, 1), texelFetch(uPacked, pixel + ivec2(0, 1), 0).r) - position;
    }
    else if (hasD)
    {
        dy = position - viewPosition(pixel - ivec2(0, 1), texelFetch(uPacked, pixel - ivec2(0, 1), 0).r);
    }
    vec3 normal = normalize(cross(dx, dy));
    if (dot(normal, position) > 0.0)
    {
        normal = -normal;
    }
    reach = min(reach, 0.15 * float(size.y)) / float(SAMPLES);
    float angle = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    vec2 direction = vec2(cos(angle), sin(angle));
    mediump float occlusion = 0.0;
    mediump float count = 0.0;
    for (int i = 0; i < SAMPLES; i++)
    {
        ivec2 tap = pixel + ivec2(floor(direction * (reach * (float(i) + 0.5)) + 0.5));
        direction = turn * direction;
        ivec2 inside = clamp(tap, ivec2(0), size - 1);
        mediump float valid = tap == inside ? 1.0 : 0.0;
        uint value = texelFetch(uPacked, inside, 0).r;
        vec3 offset = viewPosition(inside, value) - position;
        float distanceSquared = dot(offset, offset);
        mediump float cosine = (dot(offset, normal) - 2.0) * inversesqrt(distanceSquared + 0.0001);
        occlusion += valid * max(cosine, 0.0) * max(1.0 - distanceSquared * invRadiusSq, 0.0) * float(value & 255u);
        count += valid;
    }
    mediump float strength = receiver > 128u ? 0.5 : 1.0;
    mediump float visibility = 1.0 - occlusion * strength * 3.0 / (255.0 * max(count, 1.0));
    packedOcclusion = (center & 0xFFFFFF00u) | uint(clamp(visibility, 0.0, 1.0) * 255.0 + 0.5);
}
)";

        const char* blurFragmentSource = R"(
uniform highp usampler2D uOcclusion;
uniform ivec2 uDirection;
#ifdef RESOLVE
out vec4 fragColor;
#else
out uint packedOcclusion;
#endif
#ifndef BLUR_RADIUS
#define BLUR_RADIUS 4
#endif
const float kernel[5] = float[5](1.0, 0.8824969, 0.6065307, 0.3246525, 0.1353353);
void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 limit = textureSize(uOcclusion, 0) - 1;
    uint center = texelFetch(uOcclusion, pixel, 0).r;
    float depth = float(center >> 8);
    mediump float result = 255.0;
    if (depth > 0.0)
    {
        float tolerance = 1.0 / (depth * 0.05);
        mediump float total = float(center & 255u);
        mediump float weights = 1.0;
        for (int i = 1; i <= BLUR_RADIUS; i++)
        {
            ivec2 stepOffset = uDirection * i;
            uint tapP = texelFetch(uOcclusion, min(pixel + stepOffset, limit), 0).r;
            uint tapN = texelFetch(uOcclusion, max(pixel - stepOffset, ivec2(0)), 0).r;
            mediump float wP = kernel[i] * max(0.0, 1.0 - abs(float(tapP >> 8) - depth) * tolerance);
            mediump float wN = kernel[i] * max(0.0, 1.0 - abs(float(tapN >> 8) - depth) * tolerance);
            total += float(tapP & 255u) * wP + float(tapN & 255u) * wN;
            weights += wP + wN;
        }
        result = total / weights;
    }
#ifdef RESOLVE
    fragColor = vec4(result / 255.0);
#else
    packedOcclusion = (center & 0xFFFFFF00u) | uint(result + 0.5);
#endif
}
)";

        const char* compositeFragmentSource = R"(#version 330 core
uniform sampler2D uOcclusion;
uniform highp sampler2D uMask;
uniform vec4 uMapping;
out vec4 fragColor;
void main()
{
    float occlusion = texture(uOcclusion, gl_FragCoord.xy * uMapping.xy + uMapping.zw).r;
    float receiver = texelFetch(uMask, ivec2(gl_FragCoord.xy), 0).r;
    fragColor = vec4(vec3(receiver < 0.01 ? 1.0 : occlusion), 1.0);
}
)";

        GLuint compile(GLenum type, const char* source, const char* prefix = "", const char* esVersion = "#version 300 es\n")
        {
            GLuint shader = glCreateShader(type);
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
            std::string header = "#version 330 core\n";
            std::string esHeader = esVersion;
            std::string esPrecision = "precision highp float;\nprecision highp int;\n";
            std::string esPrefix = prefix;
            std::string esSource = source;
            if (esPrefix.starts_with(header)) {
                size_t pos = header.size();
                while (esPrefix.compare(pos, 10, "#extension") == 0) {
                    size_t nl = esPrefix.find('\n', pos);
                    pos = nl == std::string::npos ? esPrefix.size() : nl + 1;
                }
                esPrefix.insert(pos, esPrecision);
                esPrefix.replace(0, header.size(), esHeader);
            } else if (esSource.starts_with(header)) {
                esSource.replace(0, header.size(), esHeader + esPrecision);
            }
            prefix = esPrefix.c_str();
            source = esSource.c_str();
#else
            static_cast<void>(esVersion);
#endif
            const char* sources[2] = { prefix, source };
            glShaderSource(shader, 2, sources, nullptr);
            glCompileShader(shader);
            GLint ok = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[2048] = {};
                glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                glDeleteShader(shader);
#ifdef __ANDROID__
                SDL_Log("OpenDE shader compile failed: %s", log);
#endif
                throw std::runtime_error(std::string("shader compile failed: ") + log);
            }
            return shader;
        }

        GLuint linkFullscreen(const char* source, const char* prefix = "")
        {
            GLuint vertex = compile(GL_VERTEX_SHADER, gammaVertexSource);
            GLuint fragment = 0;
            try {
                fragment = compile(GL_FRAGMENT_SHADER, source, prefix);
            } catch (...) {
                glDeleteShader(vertex);
                throw;
            }
            GLuint program = glCreateProgram();
            glAttachShader(program, vertex);
            glAttachShader(program, fragment);
            glLinkProgram(program);
            glDeleteShader(vertex);
            glDeleteShader(fragment);
            GLint ok = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &ok);
            if (!ok) {
                char log[2048] = {};
                glGetProgramInfoLog(program, sizeof(log), nullptr, log);
                glDeleteProgram(program);
#ifdef __ANDROID__
                SDL_Log("OpenDE shader link failed: %s", log);
#endif
                throw std::runtime_error(std::string("shader link failed: ") + log);
            }
            return program;
        }
    }

    UberShader::UberShader()
    {
        static_assert(GL_TRIANGLES == 0x0004);
        auto buildProgram = [&](int32_t index, bool useHwClip) -> GLuint {
            std::string vertexPrefix = "#version 330 core\n";
            std::string fragmentPrefix = "#version 330 core\n";
            const char* esVersion = "#version 300 es\n";
            if (index & 1) {
                fragmentPrefix += "#define ALPHA_TEST\n";
            }
            if (index & 2) {
                if (useHwClip) {
#ifdef __ANDROID__
                    esVersion = "#version 320 es\n";
                    vertexPrefix += "#extension GL_EXT_clip_cull_distance : enable\n#define ES_IMPLICIT_CLIP\n#define HARDWARE_CLIP\n";
#else
                    vertexPrefix += "#define HARDWARE_CLIP\n";
#endif
                    fragmentPrefix += "#define HARDWARE_CLIP\n";
                }
                vertexPrefix += "#define CLIP_PLANES\n";
                fragmentPrefix += "#define CLIP_PLANES\n";
            }
            if (index & 4) {
                fragmentPrefix += "#define SMOOTH_LIGHTMAP\n";
            }
            GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource, vertexPrefix.c_str(), esVersion);
            GLuint fragment = 0;
            try {
                fragment = compile(GL_FRAGMENT_SHADER, fragmentSource, fragmentPrefix.c_str(), esVersion);
            } catch (...) {
                glDeleteShader(vertex);
                throw;
            }
            GLuint program = glCreateProgram();
            glAttachShader(program, vertex);
            glAttachShader(program, fragment);
            glLinkProgram(program);
            glDeleteShader(vertex);
            glDeleteShader(fragment);
            GLint ok = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &ok);
            if (!ok) {
                char log[2048] = {};
                glGetProgramInfoLog(program, sizeof(log), nullptr, log);
                glDeleteProgram(program);
#ifdef __ANDROID__
                SDL_Log("OpenDE shader link failed: %s", log);
#endif
                throw std::runtime_error(std::string("shader link failed: ") + log);
            }
            return program;
        };

        programs[0] = buildProgram(0, false);
        programs[1] = buildProgram(1, false);
#if defined(__EMSCRIPTEN__)
        bool tryHwClip = false;
#elif defined(__ANDROID__)
        bool tryHwClip = dxHasClipCullDistance();
#else
        bool tryHwClip = true;
#endif
        if (tryHwClip) {
#ifdef __ANDROID__
            try {
                programs[2] = buildProgram(2, true);
                programs[3] = buildProgram(3, true);
            } catch (const std::exception& err) {
                if (programs[2]) {
                    glDeleteProgram(programs[2]);
                    programs[2] = 0;
                }
                if (programs[3]) {
                    glDeleteProgram(programs[3]);
                    programs[3] = 0;
                }
                SDL_Log("OpenDE: HARDWARE_CLIP fallback to software clip planes: %s", err.what());
                tryHwClip = false;
                programs[2] = buildProgram(2, false);
                programs[3] = buildProgram(3, false);
            }
#else
            programs[2] = buildProgram(2, true);
            programs[3] = buildProgram(3, true);
#endif
        } else {
            programs[2] = buildProgram(2, false);
            programs[3] = buildProgram(3, false);
        }
        hardwareClip = tryHwClip;
        programs[4] = buildProgram(4, false);
        programs[5] = buildProgram(5, false);

        for (int32_t index = 0; index < 6; index++) {
            GLuint program = programs[index];
            Locations& location = locations[index];
            location.viewProj = glGetUniformLocation(program, "uViewProj");
            location.flags = glGetUniformLocation(program, "uFlags");
            location.pan = glGetUniformLocation(program, "uPan");
            location.scale = glGetUniformLocation(program, "uScale");
            location.tint = glGetUniformLocation(program, "uTint");
            location.lightCount = glGetUniformLocation(program, "uLightCount");
            location.lightPosition = glGetUniformLocation(program, "uLightPosition");
            location.lightColor = glGetUniformLocation(program, "uLightColor");
            location.lightDirection = glGetUniformLocation(program, "uLightDirection");
            location.eye = glGetUniformLocation(program, "uEye");
            location.forward = glGetUniformLocation(program, "uForward");
            location.clipPlanes = glGetUniformLocation(program, "uClipPlanes");
            location.clipCount = glGetUniformLocation(program, "uClipCount");
            location.occlusion = glGetUniformLocation(program, "uOcclusion");
            location.gamma = glGetUniformLocation(program, "uGamma");
            glUseProgram(program);
            glUniform1i(glGetUniformLocation(program, "uTexture"), 0);
            glUniform1i(glGetUniformLocation(program, "uLightmap"), 1);
            glUniform1f(glGetUniformLocation(program, "uDetailDistance"), 380.0f);
            glUniform1f(location.gamma, 1.0f);
            current = index;
            sendState();
        }
        current = 0;
        glUseProgram(programs[0]);

        glGenVertexArrays(static_cast<GLsizei>(StreamRingCount), streamVaos);
        glGenBuffers(static_cast<GLsizei>(StreamRingCount), streamVbos);
        glGenBuffers(static_cast<GLsizei>(StreamRingCount), streamEbos);
        for (size_t i = 0; i < StreamRingCount; i++) {
            bindVao(streamVaos[i]);
            glBindBuffer(GL_ARRAY_BUFFER, streamVbos[i]);
            setupAttributes();
        }
    }

    UberShader::~UberShader()
    {
        glDeleteBuffers(static_cast<GLsizei>(StreamRingCount), streamEbos);
        glDeleteBuffers(static_cast<GLsizei>(StreamRingCount), streamVbos);
        glDeleteVertexArrays(static_cast<GLsizei>(StreamRingCount), streamVaos);
        for (GLuint program : programs) {
            glDeleteProgram(program);
        }
    }

    void UberShader::nextFrame()
    {
        streamRingIndex = (streamRingIndex + 1) % StreamRingCount;
        streamOffset = 0;
        streamEboOffset = 0;
        frameDrawCalls = 0;
        frameTriangles = 0;
    }

    void UberShader::setupAttributes()
    {
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GLVertex), reinterpret_cast<const void*>(offsetof(GLVertex, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), reinterpret_cast<const void*>(offsetof(GLVertex, u)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), reinterpret_cast<const void*>(offsetof(GLVertex, lu)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(GLVertex), reinterpret_cast<const void*>(offsetof(GLVertex, r)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(GLVertex), reinterpret_cast<const void*>(offsetof(GLVertex, nx)));
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(GLVertex), reinterpret_cast<const void*>(offsetof(GLVertex, fogR)));
    }

    void UberShader::invalidateTexture()
    {
        boundTexture = ~0u;
    }

    void UberShader::invalidateLightmap()
    {
        boundLightmap = ~0u;
    }

    void UberShader::invalidateBlend()
    {
        blendInitialized = false;
    }

    void UberShader::invalidateVao(uint32_t vao)
    {
        if (vao == ~0u || boundVao == vao) {
            boundVao = ~0u;
        }
    }

    void UberShader::bindVao(uint32_t vao)
    {
        if (boundVao != vao) {
            glBindVertexArray(vao);
            boundVao = vao;
        }
    }

    void UberShader::markOthersStale()
    {
        for (int32_t i = 0; i < 6; i++) {
            if (i != current) {
                stale[i] = true;
            }
        }
    }

    int32_t UberShader::targetIndex() const
    {
        int32_t wanted = ((state.flags & SHADER_AlphaTest) ? 1 : 0) | (state.clipCount > 0 ? 2 : 0);
        if (smoothLightmapActive && (wanted & 2) == 0) {
            wanted |= 4;
        }
        return wanted;
    }

    void UberShader::use()
    {
        boundTexture = ~0u;
        boundLightmap = ~0u;
        boundVao = ~0u;
        blendInitialized = false;
        glActiveTexture(GL_TEXTURE0);
        glUseProgram(programs[current]);
    }

    bool UberShader::select(int32_t index)
    {
        if (index == current) {
            return false;
        }
        current = index;
        glUseProgram(programs[current]);
        if (stale[current]) {
            sendState();
            stale[current] = false;
            return true;
        }
        return false;
    }

    void UberShader::sendState()
    {
        const Locations& location = locations[current];
        glUniformMatrix4fv(location.viewProj, 1, GL_FALSE, state.viewProj.data());
        glUniform1i(location.flags, state.flags);
        glUniform2fv(location.pan, 1, state.pan);
        glUniform2fv(location.scale, 1, state.scale);
        glUniform4fv(location.tint, 1, state.tint);
        glUniform1i(location.lightCount, state.lightCount);
        if (state.lightCount > 0) {
            glUniform4fv(location.lightPosition, state.lightCount, state.lightPositions);
            glUniform4fv(location.lightColor, state.lightCount, state.lightColors);
            glUniform4fv(location.lightDirection, state.lightCount, state.lightDirections);
        }
        glUniform3fv(location.eye, 1, state.eye);
        glUniform3fv(location.forward, 1, state.forward);
        if (state.clipCount > 0) {
            glUniform4fv(location.clipPlanes, state.clipCount, state.clipPlanes);
        }
        glUniform1i(location.clipCount, state.clipCount);
        glUniform1f(location.occlusion, state.occlusion);
        glUniform1f(location.gamma, activeGamma());
    }

    void UberShader::setViewProj(const Mat4& matrix)
    {
        if (hasViewProj && std::equal(matrix.data(), matrix.data() + 16, state.viewProj.data())) {
            return;
        }
        hasViewProj = true;
        state.viewProj = matrix;
        markOthersStale();
        glUniformMatrix4fv(locations[current].viewProj, 1, GL_FALSE, matrix.data());
    }

    void UberShader::setQuality(bool smoothLightmap, float detailDistance)
    {
        smoothLightmapActive = smoothLightmap;
        for (GLuint program : programs) {
            glUseProgram(program);
            glUniform1f(glGetUniformLocation(program, "uDetailDistance"), detailDistance);
        }
        int32_t wanted = targetIndex();
        if (wanted != current) {
            select(wanted);
        } else {
            glUseProgram(programs[current]);
        }
    }

    void UberShader::setFlags(int32_t flags)
    {
        if (flags == state.flags) {
            return;
        }
        state.flags = flags;
        markOthersStale();
        int32_t wanted = targetIndex();
        if (!select(wanted)) {
            glUniform1i(locations[current].flags, flags);
        }
    }

    void UberShader::setPan(float u, float v)
    {
        if (u == state.pan[0] && v == state.pan[1]) {
            return;
        }
        state.pan[0] = u;
        state.pan[1] = v;
        markOthersStale();
        glUniform2f(locations[current].pan, u, v);
    }

    void UberShader::setScale(float u, float v)
    {
        if (u == state.scale[0] && v == state.scale[1]) {
            return;
        }
        state.scale[0] = u;
        state.scale[1] = v;
        markOthersStale();
        glUniform2f(locations[current].scale, u, v);
    }

    void UberShader::setEye(const Vector& eye, const Vector& forward)
    {
        if (hasEye && eye.x == state.eye[0] && eye.y == state.eye[1] && eye.z == state.eye[2] && forward.x == state.forward[0] && forward.y == state.forward[1] && forward.z == state.forward[2]) {
            return;
        }
        hasEye = true;
        state.eye[0] = eye.x;
        state.eye[1] = eye.y;
        state.eye[2] = eye.z;
        state.forward[0] = forward.x;
        state.forward[1] = forward.y;
        state.forward[2] = forward.z;
        markOthersStale();
        glUniform3f(locations[current].eye, eye.x, eye.y, eye.z);
        glUniform3f(locations[current].forward, forward.x, forward.y, forward.z);
    }

    void UberShader::setClipPlanes(const float* planes, int32_t count)
    {
        bool same = count == state.clipCount && (count == 0 || std::equal(planes, planes + count * 4, state.clipPlanes));
        if (same) {
            return;
        }
        if (count > 0) {
            std::copy(planes, planes + count * 4, state.clipPlanes);
        }
        state.clipCount = count;
        markOthersStale();
        int32_t wanted = targetIndex();
        if (!select(wanted)) {
            if (count > 0) {
                glUniform4fv(locations[current].clipPlanes, count, planes);
            }
            glUniform1i(locations[current].clipCount, count);
        }
    }

    void UberShader::setOcclusion(float weight)
    {
        if (weight == state.occlusion) {
            return;
        }
        state.occlusion = weight;
        markOthersStale();
        glUniform1f(locations[current].occlusion, weight);
    }

    float UberShader::activeGamma() const
    {
        if (blendInitialized && (currentBlend == BlendMode::Modulated || currentBlend == BlendMode::Translucent)) {
            return 1.0f;
        }
        return state.gamma;
    }

    void UberShader::setGamma(float exponent)
    {
        if (exponent == state.gamma) {
            return;
        }
        state.gamma = exponent;
        markOthersStale();
        glUniform1f(locations[current].gamma, activeGamma());
    }

    void UberShader::setTint(float r, float g, float b, float a)
    {
        if (r == state.tint[0] && g == state.tint[1] && b == state.tint[2] && a == state.tint[3]) {
            return;
        }
        state.tint[0] = r;
        state.tint[1] = g;
        state.tint[2] = b;
        state.tint[3] = a;
        markOthersStale();
        glUniform4f(locations[current].tint, r, g, b, a);
    }

    void UberShader::setBlend(BlendMode mode)
    {
        if (blendInitialized && mode == currentBlend) {
            return;
        }
        const float prevGamma = blendInitialized ? activeGamma() : -1.0f;
        currentBlend = mode;
        blendInitialized = true;
        const float nextGamma = activeGamma();
        if (nextGamma != prevGamma) {
            markOthersStale();
            glUniform1f(locations[current].gamma, nextGamma);
        }
        switch (mode) {
        case BlendMode::Opaque:
        case BlendMode::Masked:
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            break;
        case BlendMode::Translucent:
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
            glDepthMask(GL_FALSE);
            break;
        case BlendMode::Modulated:
            glEnable(GL_BLEND);
            glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
            glDepthMask(GL_FALSE);
            break;
        case BlendMode::Alpha:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            break;
        case BlendMode::Highlight:
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            break;
        }
    }

    void UberShader::setDynamicLights(const std::vector<DynamicLight>& lights)
    {
        float* positions = state.lightPositions;
        float* colors = state.lightColors;
        float* directions = state.lightDirections;
        std::fill(std::begin(state.lightPositions), std::end(state.lightPositions), 0.0f);
        std::fill(std::begin(state.lightColors), std::end(state.lightColors), 0.0f);
        std::fill(std::begin(state.lightDirections), std::end(state.lightDirections), 0.0f);
        size_t count = std::min(lights.size(), MaxDynamicLights);
        for (size_t i = 0; i < count; i++) {
            const DynamicLight& light = lights[i];
            positions[i * 4 + 0] = light.location.x;
            positions[i * 4 + 1] = light.location.y;
            positions[i * 4 + 2] = light.location.z;
            positions[i * 4 + 3] = light.radius;
            colors[i * 4 + 0] = light.color.x;
            colors[i * 4 + 1] = light.color.y;
            colors[i * 4 + 2] = light.color.z;
            directions[i * 4 + 0] = light.direction.x;
            directions[i * 4 + 1] = light.direction.y;
            directions[i * 4 + 2] = light.direction.z;
            directions[i * 4 + 3] = light.cone;
        }
        state.lightCount = static_cast<int32_t>(count);
        markOthersStale();
        const Locations& location = locations[current];
        glUniform1i(location.lightCount, state.lightCount);
        glUniform4fv(location.lightPosition, static_cast<GLsizei>(MaxDynamicLights), positions);
        glUniform4fv(location.lightColor, static_cast<GLsizei>(MaxDynamicLights), colors);
        glUniform4fv(location.lightDirection, static_cast<GLsizei>(MaxDynamicLights), directions);
    }

    void UberShader::bindTexture(uint32_t texture)
    {
        if (boundTexture == texture) {
            return;
        }
        boundTexture = texture;
        glBindTexture(GL_TEXTURE_2D, texture);
    }

    void UberShader::bindLightmap(uint32_t texture)
    {
        if (boundLightmap == texture) {
            return;
        }
        boundLightmap = texture;
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texture);
        glActiveTexture(GL_TEXTURE0);
    }

    int32_t UberShader::stream(const std::vector<GLVertex>& vertices)
    {
        if (vertices.empty()) {
            return 0;
        }
        size_t bytes = vertices.size() * sizeof(GLVertex);
        uint32_t vao = streamVaos[streamRingIndex];
        uint32_t& vbo = streamVbos[streamRingIndex];
        size_t& capacity = streamCapacities[streamRingIndex];
        bindVao(vao);
        if (bytes > capacity || streamOffset + bytes > capacity) {
            size_t newCapacity = std::max(capacity, std::max(bytes, size_t { 8 } << 20));
            if (streamOffset > 0) {
                if (vbo) {
                    glDeleteBuffers(1, &vbo);
                }
                glGenBuffers(1, &vbo);
                newCapacity = std::max(newCapacity, bytes);
            }
            capacity = newCapacity;
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            setupAttributes();
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity), nullptr, GL_STREAM_DRAW);
            streamOffset = 0;
        } else {
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
        }
#ifdef __EMSCRIPTEN__
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(streamOffset), static_cast<GLsizeiptr>(bytes), vertices.data());
#else
        void* target = glMapBufferRange(GL_ARRAY_BUFFER, static_cast<GLintptr>(streamOffset), static_cast<GLsizeiptr>(bytes), GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if (target) {
            std::memcpy(target, vertices.data(), bytes);
            glUnmapBuffer(GL_ARRAY_BUFFER);
        } else {
            glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(streamOffset), static_cast<GLsizeiptr>(bytes), vertices.data());
        }
#endif
        int32_t first = static_cast<int32_t>(streamOffset / sizeof(GLVertex));
        streamOffset += bytes;
        return first;
    }

    void UberShader::draw(const std::vector<GLVertex>& vertices, uint32_t primitive)
    {
        if (vertices.empty()) {
            return;
        }
        int32_t first = stream(vertices);
        glDrawArrays(primitive, first, static_cast<GLsizei>(vertices.size()));
        frameDrawCalls++;
        if (primitive == GL_TRIANGLES) {
            frameTriangles += static_cast<int32_t>(vertices.size() / 3);
        }
    }

    void UberShader::upload(const std::vector<GLVertex>& vertices)
    {
        uploadBase = stream(vertices);
    }

    GLVertex* UberShader::beginUpload(size_t vertexCount)
    {
#ifdef __EMSCRIPTEN__
        (void)vertexCount;
        return nullptr;
#else
        if (vertexCount == 0) {
            return nullptr;
        }
        size_t bytes = vertexCount * sizeof(GLVertex);
        uint32_t vao = streamVaos[streamRingIndex];
        uint32_t& vbo = streamVbos[streamRingIndex];
        size_t& capacity = streamCapacities[streamRingIndex];
        bindVao(vao);
        if (bytes > capacity || streamOffset + bytes > capacity) {
            size_t newCapacity = std::max(capacity, std::max(bytes, size_t { 8 } << 20));
            if (streamOffset > 0) {
                if (vbo) {
                    glDeleteBuffers(1, &vbo);
                }
                glGenBuffers(1, &vbo);
                newCapacity = std::max(newCapacity, bytes);
            }
            capacity = newCapacity;
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            setupAttributes();
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity), nullptr, GL_STREAM_DRAW);
            streamOffset = 0;
        } else {
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
        }
        void* target = glMapBufferRange(GL_ARRAY_BUFFER, static_cast<GLintptr>(streamOffset), static_cast<GLsizeiptr>(bytes), GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if (!target) {
            return nullptr;
        }
        uploadBase = static_cast<int32_t>(streamOffset / sizeof(GLVertex));
        streamOffset += bytes;
        return static_cast<GLVertex*>(target);
#endif
    }

    void UberShader::endUpload()
    {
#ifndef __EMSCRIPTEN__
        glUnmapBuffer(GL_ARRAY_BUFFER);
#endif
    }

    void UberShader::drawRange(int32_t first, int32_t count, uint32_t primitive)
    {
        if (count <= 0) {
            return;
        }
        bindVao(streamVaos[streamRingIndex]);
        glDrawArrays(primitive, uploadBase + first, count);
        frameDrawCalls++;
        if (primitive == GL_TRIANGLES) {
            frameTriangles += count / 3;
        }
    }

    uint32_t UberShader::createStatic(const std::vector<GLVertex>& vertices, uint32_t& buffer)
    {
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &buffer);
        bindVao(vao);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(GLVertex)), vertices.data(), GL_STATIC_DRAW);
        setupAttributes();
        return vao;
    }

    void UberShader::bindStatic(uint32_t vao)
    {
        bindVao(vao);
    }

    void UberShader::drawStatic(uint32_t vao, int32_t first, int32_t count)
    {
        if (count <= 0) {
            return;
        }
        bindVao(vao);
        glDrawArrays(GL_TRIANGLES, first, count);
        frameDrawCalls++;
        frameTriangles += count / 3;
    }

    void UberShader::drawStaticRanges(uint32_t vao, const int32_t* firsts, const int32_t* counts, int32_t ranges)
    {
        if (ranges <= 0) {
            return;
        }
        bindVao(vao);
        if (ranges == 1) {
            if (counts[0] > 0) {
                glDrawArrays(GL_TRIANGLES, firsts[0], counts[0]);
                frameDrawCalls++;
                frameTriangles += counts[0] / 3;
            }
            return;
        }
#ifdef __ANDROID__
        int32_t total = 0;
        for (int32_t i = 0; i < ranges; i++) {
            if (counts[i] > 0) {
                total += counts[i];
            }
        }
        if (total <= 0) {
            return;
        }
        const size_t bytes = static_cast<size_t>(total) * sizeof(uint32_t);
        uint32_t& ebo = streamEbos[streamRingIndex];
        size_t& cap = streamEboCapacities[streamRingIndex];
        if (bytes > cap || streamEboOffset + bytes > cap) {
            size_t newCap = std::max(cap, std::max(bytes, size_t { 1 } << 20));
            if (streamEboOffset > 0) {
                if (ebo) {
                    glDeleteBuffers(1, &ebo);
                }
                glGenBuffers(1, &ebo);
                newCap = std::max(newCap, bytes);
            }
            cap = newCap;
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(cap), nullptr, GL_STREAM_DRAW);
            streamEboOffset = 0;
        } else {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        }
        void* mapped = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>(streamEboOffset), static_cast<GLsizeiptr>(bytes), GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if (mapped) {
            uint32_t* dst = static_cast<uint32_t*>(mapped);
            for (int32_t i = 0; i < ranges; i++) {
                const uint32_t base = static_cast<uint32_t>(firsts[i]);
                const int32_t cnt = counts[i];
                for (int32_t j = 0; j < cnt; j++) {
                    *dst++ = base + static_cast<uint32_t>(j);
                }
            }
            glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
        } else {
            static std::vector<uint32_t> fallbackIndices;
            fallbackIndices.clear();
            fallbackIndices.reserve(static_cast<size_t>(total));
            for (int32_t i = 0; i < ranges; i++) {
                const uint32_t base = static_cast<uint32_t>(firsts[i]);
                const int32_t cnt = counts[i];
                for (int32_t j = 0; j < cnt; j++) {
                    fallbackIndices.push_back(base + static_cast<uint32_t>(j));
                }
            }
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>(streamEboOffset), static_cast<GLsizeiptr>(bytes), fallbackIndices.data());
        }
        glDrawElements(GL_TRIANGLES, total, GL_UNSIGNED_INT, reinterpret_cast<const void*>(streamEboOffset));
        streamEboOffset += bytes;
        frameDrawCalls++;
        frameTriangles += total / 3;
#else
        glMultiDrawArrays(GL_TRIANGLES, firsts, counts, ranges);
        frameDrawCalls++;
        for (int32_t i = 0; i < ranges; i++) {
            if (counts[i] > 0) {
                frameTriangles += counts[i] / 3;
            }
        }
#endif
    }

    GammaPass::GammaPass()
    {
        program = linkFullscreen(gammaFragmentSource);
        exponentLocation = glGetUniformLocation(program, "uExponent");
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "uFrame"), 0);
        glGenVertexArrays(1, &vao);
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    GammaPass::~GammaPass()
    {
        if (framebuffer) {
            glDeleteFramebuffers(1, &framebuffer);
            glDeleteRenderbuffers(1, &depthBuffer);
        }
        glDeleteTextures(1, &texture);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(program);
    }

    void GammaPass::resize(int width, int height)
    {
        if (width == textureWidth && height == textureHeight) {
            return;
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        textureWidth = width;
        textureHeight = height;
        if (framebuffer) {
            glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);
        }
    }

    void GammaPass::draw(int width, int height, float exponent)
    {
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, width, height);
        glUseProgram(program);
        glUniform1f(exponentLocation, exponent);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    void GammaPass::apply(int width, int height, float exponent)
    {
        resize(width, height);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
        draw(width, height, exponent);
    }

    void GammaPass::bindTarget(int width, int height)
    {
        if (!framebuffer) {
            glGenFramebuffers(1, &framebuffer);
            glGenRenderbuffers(1, &depthBuffer);
            textureWidth = 0;
            textureHeight = 0;
        }
        resize(width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    }

    void GammaPass::resolve(int width, int height, float exponent)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        draw(width, height, exponent);
    }

    uint32_t GammaPass::targetFramebuffer() const
    {
        return framebuffer;
    }

    ScenePass::ScenePass()
    {
#ifdef __ANDROID__
        colorMaskiSupported = dxHasColorMaski();
#elif !defined(__EMSCRIPTEN__)
        colorMaskiSupported = true;
#endif
        program = linkFullscreen(fxaaFragmentSource);
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        inverseSizeLocation = glGetUniformLocation(program, "uInverseSize");
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "uFrame"), 0);
        glGenVertexArrays(1, &vao);
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    ScenePass::~ScenePass()
    {
        if (framebuffer) {
            glDeleteFramebuffers(1, &framebuffer);
            glDeleteRenderbuffers(1, &colorBuffer);
            glDeleteRenderbuffers(1, &depthBuffer);
        }
        if (fxaaFramebuffer) {
            glDeleteFramebuffers(1, &fxaaFramebuffer);
        }
        releaseMask();
        releaseTextures();
        glDeleteTextures(1, &texture);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(program);
    }

    void ScenePass::prepareColorResolve(int width, int height)
    {
        if (!fxaaFramebuffer) {
            glGenFramebuffers(1, &fxaaFramebuffer);
            fxaaTextureAttached = false;
        }
        if (width != textureWidth || height != textureHeight || !fxaaTextureAttached) {
            GLint prevActive = 0;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
            glActiveTexture(GL_TEXTURE0);
            GLint bound = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
            glBindTexture(GL_TEXTURE_2D, texture);
            if (width != textureWidth || height != textureHeight) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                textureWidth = width;
                textureHeight = height;
            }
            GLint prevDraw = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
            glBindFramebuffer(GL_FRAMEBUFFER, fxaaFramebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
            fxaaTextureAttached = true;
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevDraw));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(bound));
            glActiveTexture(static_cast<GLenum>(prevActive));
        }
    }

    uint32_t ScenePass::resolveColorForOcclusion(int width, int height)
    {
        if (!offscreen || bufferSamples == 0) {
            return 0;
        }
        prepareColorResolve(width, height);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fxaaFramebuffer);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        colorResolved = true;
        return fxaaFramebuffer;
    }

    void ScenePass::begin(int width, int height, int samples, bool occlusion, int32_t targetFbo)
    {
#ifdef __ANDROID__
        samples = std::min(samples, 4);
#endif
        samples = std::min(samples, static_cast<int>(maxSamples));
        offscreen = samples > 1 || occlusion;
        occlusionActive = occlusion;
        occlusionCapture = false;
        blendWritesMask = false;
        maskWrites = false;
        maskClean = false;
        colorResolved = false;
        if (!offscreen) {
            if (framebuffer) {
                glDeleteFramebuffers(1, &framebuffer);
                glDeleteRenderbuffers(1, &colorBuffer);
                glDeleteRenderbuffers(1, &depthBuffer);
                releaseTextures();
                releaseMask();
                framebuffer = 0;
                colorBuffer = 0;
                depthBuffer = 0;
                bufferWidth = 0;
            }
            return;
        }
        if (samples < 2) {
            samples = 0;
        }
        if (targetFbo >= 0) {
            target = targetFbo;
        } else {
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
        }
        if (samples > 0 && occlusion) {
            prepareColorResolve(width, height);
        }
        if (!framebuffer) {
            glGenFramebuffers(1, &framebuffer);
            glGenRenderbuffers(1, &colorBuffer);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        if (width != bufferWidth || height != bufferHeight || samples != bufferSamples || occlusion != bufferOcclusion) {
            glBindRenderbuffer(GL_RENDERBUFFER, colorBuffer);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorBuffer);
            if (occlusion && samples == 0) {
                if (!depthTexture) {
                    glGenTextures(1, &depthTexture);
                    glGenTextures(1, &maskTexture);
                }
                GLint bound = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
                glBindTexture(GL_TEXTURE_2D, depthTexture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glBindTexture(GL_TEXTURE_2D, maskTexture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(bound));
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, maskTexture, 0);
                glDeleteRenderbuffers(1, &depthBuffer);
                depthBuffer = 0;
                releaseMask();
            } else {
                if (!depthBuffer) {
                    glGenRenderbuffers(1, &depthBuffer);
                }
                glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
                glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);
                if (occlusion) {
                    if (!maskBuffer) {
                        glGenRenderbuffers(1, &maskBuffer);
                    }
                    glBindRenderbuffer(GL_RENDERBUFFER, maskBuffer);
                    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_R8, width, height);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, maskBuffer);
                } else {
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, 0);
                    GLenum buffer = GL_COLOR_ATTACHMENT0;
                    glDrawBuffers(1, &buffer);
                    releaseMask();
                }
                releaseTextures();
            }
            if (occlusion) {
                GLenum buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
                glDrawBuffers(2, buffers);
            }
            bufferWidth = width;
            bufferHeight = height;
            bufferSamples = samples;
            bufferOcclusion = occlusion;
        } else if (occlusion && !colorMaskiSupported) {
            GLenum buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
            glDrawBuffers(2, buffers);
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, width, height);
        glDepthMask(GL_TRUE);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        if (occlusion) {
            maskClean = true;
            applyMaskWriteMask();
        }
    }

    void ScenePass::end(int width, int height, bool fxaa)
    {
        if (offscreen) {
            if (colorMaskiSupported) {
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            }
            if (fxaa) {
                if (!colorResolved) {
                    prepareColorResolve(width, height);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
                    glReadBuffer(GL_COLOR_ATTACHMENT0);
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fxaaFramebuffer);
                    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
#ifdef __ANDROID__
                    const GLenum attachments[3] = { GL_COLOR_ATTACHMENT0, GL_DEPTH_STENCIL_ATTACHMENT, GL_COLOR_ATTACHMENT1 };
                    glInvalidateFramebuffer(GL_READ_FRAMEBUFFER, bufferOcclusion ? 3 : 2, attachments);
#endif
                }
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, texture);
                glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(target));
                glDisable(GL_SCISSOR_TEST);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthMask(GL_TRUE);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
                offscreen = false;
                occlusionActive = false;
                occlusionCapture = false;
                colorResolved = false;
            } else {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, colorResolved ? fxaaFramebuffer : framebuffer);
                glReadBuffer(GL_COLOR_ATTACHMENT0);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(target));
                glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
#ifdef __ANDROID__
                if (!colorResolved) {
                    const GLenum attachments[3] = { GL_COLOR_ATTACHMENT0, GL_DEPTH_STENCIL_ATTACHMENT, GL_COLOR_ATTACHMENT1 };
                    glInvalidateFramebuffer(GL_READ_FRAMEBUFFER, bufferOcclusion ? 3 : 2, attachments);
                }
#endif
                glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(target));
                offscreen = false;
                occlusionActive = false;
                occlusionCapture = false;
                colorResolved = false;
                return;
            }
        } else if (fxaa) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);
            if (width != textureWidth || height != textureHeight) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                textureWidth = width;
                textureHeight = height;
                fxaaTextureAttached = false;
            }
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
        } else {
            return;
        }
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, width, height);
        glUseProgram(program);
        glUniform2f(inverseSizeLocation, 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    bool ScenePass::occluding() const
    {
        return occlusionActive;
    }

    bool ScenePass::supportsColorMaski() const
    {
        return colorMaskiSupported;
    }

    uint32_t ScenePass::sceneFramebuffer() const
    {
        return framebuffer;
    }

    uint32_t ScenePass::sceneDepth() const
    {
        return bufferOcclusion && bufferSamples == 0 ? depthTexture : 0;
    }

    uint32_t ScenePass::sceneMask() const
    {
        return bufferOcclusion && bufferSamples == 0 ? maskTexture : 0;
    }

    void ScenePass::releaseTextures()
    {
        glDeleteTextures(1, &depthTexture);
        glDeleteTextures(1, &maskTexture);
        depthTexture = 0;
        maskTexture = 0;
    }

    void ScenePass::releaseMask()
    {
        glDeleteRenderbuffers(1, &maskBuffer);
        maskBuffer = 0;
    }

    void ScenePass::beginOcclusion()
    {
        clearMask();
    }

    void ScenePass::endOcclusion()
    {
        if (!occlusionActive) {
            return;
        }
        occlusionCapture = false;
        setMaskWrites(false);
    }

    void ScenePass::setBlendWritesMask(bool enabled)
    {
        if (blendWritesMask == enabled) {
            return;
        }
        blendWritesMask = enabled;
        if (occlusionActive && occlusionCapture) {
            setMaskWrites(enabled);
        }
    }

    void ScenePass::applyMaskWriteMask()
    {
        if (!occlusionActive) {
            return;
        }
        if (colorMaskiSupported) {
            GLboolean write = maskWrites ? GL_TRUE : GL_FALSE;
            glColorMaski(1, write, write, write, write);
        } else {
            GLenum buffers[2] = { GL_COLOR_ATTACHMENT0, maskWrites ? static_cast<GLenum>(GL_COLOR_ATTACHMENT1) : static_cast<GLenum>(GL_NONE) };
            glDrawBuffers(2, buffers);
        }
    }

    void ScenePass::setMaskWrites(bool enabled)
    {
        if (!occlusionActive || enabled == maskWrites) {
            return;
        }
        maskWrites = enabled;
        applyMaskWriteMask();
    }

    void ScenePass::clearMask()
    {
        if (!occlusionActive) {
            return;
        }
        occlusionCapture = true;
        blendWritesMask = true;
        if (!maskClean) {
            setMaskWrites(true);
            const GLfloat zero[4] = {};
            glClearBufferfv(GL_COLOR, 1, zero);
            maskClean = true;
            return;
        }
        setMaskWrites(true);
    }

    OcclusionPass::OcclusionPass()
    {
        Program* programs[7] = { &prepare, &occlusion[0], &occlusion[1], &occlusion[2], &blur, &blurResolve, &composite };
        const char* sources[7] = { prepareFragmentSource, occlusionFragmentSource, occlusionFragmentSource, occlusionFragmentSource, blurFragmentSource, blurFragmentSource, compositeFragmentSource };
#ifdef __ANDROID__
        const char* prefixes[7] = { "", "#version 330 core\n#define SAMPLES 6\n", "#version 330 core\n#define SAMPLES 8\n", "#version 330 core\n#define SAMPLES 12\n", "#version 330 core\n#define BLUR_RADIUS 3\n", "#version 330 core\n#define RESOLVE\n#define BLUR_RADIUS 3\n", "" };
#else
        const char* prefixes[7] = { "", "#version 330 core\n#define SAMPLES 8\n", "#version 330 core\n#define SAMPLES 12\n", "#version 330 core\n#define SAMPLES 16\n", "#version 330 core\n", "#version 330 core\n#define RESOLVE\n", "" };
#endif
        for (size_t i = 0; i < 7; i++) {
            Program& program = *programs[i];
            program.id = linkFullscreen(sources[i], prefixes[i]);
            program.linearize = glGetUniformLocation(program.id, "uLinearize");
            program.divisor = glGetUniformLocation(program.id, "uDivisor");
            program.unproject = glGetUniformLocation(program.id, "uUnproject");
            program.direction = glGetUniformLocation(program.id, "uDirection");
            program.mapping = glGetUniformLocation(program.id, "uMapping");
            glUseProgram(program.id);
            glUniform1i(glGetUniformLocation(program.id, "uPacked"), 0);
            glUniform1i(glGetUniformLocation(program.id, "uOcclusion"), 0);
            glUniform1i(glGetUniformLocation(program.id, "uDepth"), 1);
            glUniform1i(glGetUniformLocation(program.id, "uMask"), 2);
        }
        glGenVertexArrays(1, &vao);
        glGenFramebuffers(3, packedFramebuffers);
        glGenFramebuffers(1, &resultFramebuffer);
        glGenTextures(3, packedTextures);
        glGenTextures(1, &resultTexture);
        uint32_t textures[4] = { packedTextures[0], packedTextures[1], packedTextures[2], resultTexture };
        for (uint32_t texture : textures) {
            GLint filter = texture == resultTexture ? GL_LINEAR : GL_NEAREST;
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
    }

    OcclusionPass::~OcclusionPass()
    {
        releaseResolve();
        glDeleteTextures(1, &resultTexture);
        glDeleteTextures(3, packedTextures);
        glDeleteFramebuffers(1, &resultFramebuffer);
        glDeleteFramebuffers(3, packedFramebuffers);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(prepare.id);
        for (const Program& program : occlusion) {
            glDeleteProgram(program.id);
        }
        glDeleteProgram(blur.id);
        glDeleteProgram(blurResolve.id);
        glDeleteProgram(composite.id);
    }

    void OcclusionPass::release()
    {
        releaseResolve();
        if (textureWidth == 0) {
            return;
        }
        textureWidth = 0;
        textureHeight = 0;
        textureDivisor = 0;
        GLint bound = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
        uint32_t textures[4] = { packedTextures[0], packedTextures[1], packedTextures[2], resultTexture };
        for (uint32_t texture : textures) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 0, 0, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        }
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(bound));
    }

    void OcclusionPass::releaseResolve()
    {
        if (!resolveFramebuffer) {
            return;
        }
        glDeleteFramebuffers(1, &resolveFramebuffer);
        glDeleteTextures(1, &depthTexture);
        glDeleteTextures(1, &maskTexture);
        resolveFramebuffer = 0;
        depthTexture = 0;
        maskTexture = 0;
        resolveWidth = 0;
        resolveHeight = 0;
    }

    void OcclusionPass::resize(int width, int height, int divisor)
    {
        if (width == textureWidth && height == textureHeight && divisor == textureDivisor) {
            return;
        }
        textureWidth = width;
        textureHeight = height;
        textureDivisor = divisor;
        packedWidth = (width + divisor - 1) / divisor;
        packedHeight = (height + divisor - 1) / divisor;
        for (size_t i = 0; i < 3; i++) {
            glBindTexture(GL_TEXTURE_2D, packedTextures[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, packedWidth, packedHeight, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
            glBindFramebuffer(GL_FRAMEBUFFER, packedFramebuffers[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, packedTextures[i], 0);
        }
        glBindTexture(GL_TEXTURE_2D, resultTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, packedWidth, packedHeight, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, resultFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resultTexture, 0);
    }

    void OcclusionPass::resolve(uint32_t sceneFramebuffer, int width, int height)
    {
        if (!resolveFramebuffer) {
            glGenFramebuffers(1, &resolveFramebuffer);
            glGenTextures(1, &depthTexture);
            glGenTextures(1, &maskTexture);
        }
        if (width != resolveWidth || height != resolveHeight) {
            resolveWidth = width;
            resolveHeight = height;
            glBindTexture(GL_TEXTURE_2D, depthTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glBindTexture(GL_TEXTURE_2D, maskTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, resolveFramebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, maskTexture, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFramebuffer);
        glReadBuffer(GL_COLOR_ATTACHMENT1);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFramebuffer);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    void OcclusionPass::run(const Program& program, uint32_t target, int width, int height, uint32_t texture, bool invalidate)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        static_cast<void>(invalidate);
        glViewport(0, 0, width, height);
        glUseProgram(program.id);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    void OcclusionPass::apply(uint32_t sceneFramebuffer, uint32_t sceneDepth, uint32_t sceneMask, uint32_t resolveColorFbo, int width, int height, OcclusionQuality quality, float nearPlane, float farPlane, float tanHalfWidth, float tanHalfHeight, bool colorMaskiSupported)
    {
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glDepthMask(GL_FALSE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

#ifdef __ANDROID__
        int divisor = quality == OcclusionQuality::Low ? 3 : 2;
#else
        int divisor = quality == OcclusionQuality::High ? 1 : 2;
#endif
        resize(width, height, divisor);
        bool attached = sceneDepth != 0;
        if (!attached) {
            resolve(sceneFramebuffer, width, height);
#ifdef __ANDROID__
            glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFramebuffer);
            const GLenum attachments[3] = { GL_COLOR_ATTACHMENT0, GL_DEPTH_STENCIL_ATTACHMENT, GL_COLOR_ATTACHMENT1 };
            glInvalidateFramebuffer(GL_READ_FRAMEBUFFER, 3, attachments);
#endif
            sceneDepth = depthTexture;
            sceneMask = maskTexture;
        } else {
            releaseResolve();
        }
        glBindVertexArray(vao);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sceneDepth);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, sceneMask);
        float scale = static_cast<float>(divisor);
        const Program& estimator = occlusion[static_cast<size_t>(quality) - 1];
        glUseProgram(prepare.id);
        glUniform3f(prepare.linearize, nearPlane * farPlane, farPlane, farPlane - nearPlane);
        glUniform1i(prepare.divisor, divisor);
        run(prepare, packedFramebuffers[0], packedWidth, packedHeight, 0, true);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(estimator.id);
        glUniform4f(estimator.unproject, 2.0f * scale * tanHalfWidth / static_cast<float>(width), 2.0f * scale * tanHalfHeight / static_cast<float>(height), (1.0f / static_cast<float>(width) - 1.0f) * tanHalfWidth, (1.0f / static_cast<float>(height) - 1.0f) * tanHalfHeight);
        run(estimator, packedFramebuffers[1], packedWidth, packedHeight, packedTextures[0], true);
        glUseProgram(blur.id);
        glUniform2i(blur.direction, 1, 0);
        run(blur, packedFramebuffers[2], packedWidth, packedHeight, packedTextures[1], true);
        glUseProgram(blurResolve.id);
        glUniform2i(blurResolve.direction, 0, 1);
        run(blurResolve, resultFramebuffer, packedWidth, packedHeight, packedTextures[2], true);
        uint32_t compositeTarget = (!attached && resolveColorFbo) ? resolveColorFbo : sceneFramebuffer;
        glBindFramebuffer(GL_FRAMEBUFFER, compositeTarget);
        if (attached) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
        }
        if (compositeTarget == sceneFramebuffer) {
            if (colorMaskiSupported) {
                glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            } else {
                GLenum colorOnly = GL_COLOR_ATTACHMENT0;
                glDrawBuffers(1, &colorOnly);
            }
        }
        glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO, GL_SRC_COLOR);
        glUseProgram(composite.id);
        glUniform4f(composite.mapping, 1.0f / (scale * static_cast<float>(packedWidth)), 1.0f / (scale * static_cast<float>(packedHeight)), (0.5f - 0.5f / scale) / static_cast<float>(packedWidth), (0.5f - 0.5f / scale) / static_cast<float>(packedHeight));
        run(composite, compositeTarget, width, height, resultTexture, false);
        if (attached) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, sceneMask, 0);
        }
        if (compositeTarget == sceneFramebuffer) {
            if (colorMaskiSupported) {
                glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            } else {
                GLenum restoredBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
                glDrawBuffers(2, restoredBuffers);
            }
        }
        glDisable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ZERO);
        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}
