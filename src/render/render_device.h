#pragma once

#include "engine/canvas.h"
#include "render/gl_math.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace dx {
    class UTexture;

    struct GLVertex {
        float x = 0;
        float y = 0;
        float z = 0;
        float u = 0;
        float v = 0;
        float lu = 0;
        float lv = 0;
        uint8_t r = 255;
        uint8_t g = 255;
        uint8_t b = 255;
        uint8_t a = 255;
        int8_t nx = 0;
        int8_t ny = 0;
        int8_t nz = 127;
        int8_t nPad = 0;
        uint8_t fogR = 0;
        uint8_t fogG = 0;
        uint8_t fogB = 0;
        uint8_t fogPad = 0;

        static inline uint8_t packUnorm(float value)
        {
            const float clamped = std::clamp(value, 0.0f, 1.0f);
            return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
        }

        static inline int8_t packSnorm(float value)
        {
            const float clamped = std::clamp(value, -1.0f, 1.0f);
            return static_cast<int8_t>(clamped >= 0.0f ? (clamped * 127.0f + 0.5f) : (clamped * 127.0f - 0.5f));
        }

        inline void setColor(float cr, float cg, float cb, float ca = 1.0f)
        {
            r = packUnorm(cr);
            g = packUnorm(cg);
            b = packUnorm(cb);
            a = packUnorm(ca);
        }

        inline void setNormal(float xVal, float yVal, float zVal)
        {
            nx = packSnorm(xVal);
            ny = packSnorm(yVal);
            nz = packSnorm(zVal);
        }

        inline void setFog(float fr, float fg, float fb)
        {
            fogR = packUnorm(fr);
            fogG = packUnorm(fg);
            fogB = packUnorm(fb);
        }
    };
    static_assert(sizeof(GLVertex) == 40);

    struct DynamicLight {
        Vector location;
        float radius = 0;
        Vector color;
        Vector direction;
        float cone = 2;
        bool specialLit = false;
    };

    struct GLTextureInfo {
        uint32_t id = 0;
        uint32_t width = 1;
        uint32_t height = 1;
        bool masked = false;
        bool procedural = false;
        bool smooth = false;
        int32_t levels = 1;
    };

    constexpr int32_t SHADER_Textured = 1;
    constexpr int32_t SHADER_Lightmap = 2;
    constexpr int32_t SHADER_AlphaTest = 4;
    constexpr int32_t SHADER_Dynamic = 16;
    constexpr int32_t SHADER_Detail = 32;
    constexpr int32_t SHADER_Fog = 64;
    constexpr int32_t SHADER_Fullbright = 128;
    constexpr size_t MaxDynamicLights = 32;

    enum class BlendMode : uint8_t {
        Opaque,
        Masked,
        Translucent,
        Modulated,
        Alpha,
        Highlight,
    };

    enum class Primitive : uint8_t {
        Triangles,
        Lines,
    };

    enum class DepthFunc : uint8_t {
        LessEqual,
        Equal,
        Always,
    };

    enum class OcclusionQuality : uint8_t {
        Off,
        Low,
        Medium,
        High,
    };

    enum class OcclusionSource : uint8_t {
        None,
        World,
        Mesh,
    };

    class RenderDevice {
    public:
        virtual ~RenderDevice() = default;

        virtual bool drawingToWindow() = 0;
        virtual void bindGammaTarget(int width, int height) = 0;
        virtual void resolveGamma(int width, int height, float exponent) = 0;
        virtual void applyGamma(int width, int height, float exponent) = 0;
        virtual void setGamma(float exponent) = 0;
        virtual void beginScene(int width, int height, int samples, OcclusionQuality ambientOcclusion) = 0;
        virtual void endScene(int width, int height, bool fxaa) = 0;
        virtual void beginOcclusion() = 0;
        virtual void setOcclusionSource(OcclusionSource source) = 0;
        virtual void applyOcclusion(float nearPlane, float farPlane, float tanHalfWidth, float tanHalfHeight) = 0;
        virtual void readPixels(int width, int height, uint8_t* rgba) = 0;

        virtual void setViewport(int x, int y, int width, int height) = 0;
        virtual void enableScissor(int x, int y, int width, int height) = 0;
        virtual void disableScissor() = 0;
        virtual void clear(bool color, bool depth, float depthValue) = 0;
        virtual void setDepthTest(bool enabled) = 0;
        virtual void setDepthWrite(bool enabled) = 0;
        virtual void setDepthFunc(DepthFunc func) = 0;
        virtual void setDepthRange(float nearValue, float farValue) = 0;
        virtual void setColorWrite(bool enabled) = 0;
        virtual void setCulling(bool enabled) = 0;
        virtual void setFrontFace(bool clockwise) = 0;
        virtual void setWireframe(bool enabled) = 0;
        virtual void setPolygonOffset(bool enabled, float factor, float units) = 0;
        virtual void setBlend(BlendMode mode) = 0;
        virtual void setClipPlanes(const float* planes, int32_t count) = 0;

        virtual void useShader() = 0;
        virtual void setViewProj(const Mat4& matrix) = 0;
        virtual void setFlags(int32_t flags) = 0;
        virtual void setQuality(bool smoothLightmap, float detailDistance) = 0;
        virtual void setPan(float u, float v) = 0;
        virtual void setScale(float u, float v) = 0;
        virtual void setTint(float r, float g, float b, float a) = 0;
        virtual void setEye(const Vector& eye, const Vector& forward) = 0;
        virtual void setDynamicLights(const std::vector<DynamicLight>& lights) = 0;
        virtual void bindTexture(uint32_t texture) = 0;
        virtual void bindLightmap(uint32_t texture) = 0;

        virtual void draw(const std::vector<GLVertex>& vertices, Primitive primitive) = 0;
        virtual void upload(const std::vector<GLVertex>& vertices) = 0;
        virtual GLVertex* beginUpload(size_t vertexCount) { (void)vertexCount; return nullptr; }
        virtual void endUpload() {}
        virtual void drawRange(int32_t first, int32_t count) = 0;
        virtual uint32_t createStatic(const std::vector<GLVertex>& vertices) = 0;
        virtual void deleteStatic(uint32_t geometry) = 0;
        virtual void bindStatic(uint32_t geometry) = 0;
        virtual void drawStatic(uint32_t geometry, int32_t first, int32_t count) = 0;
        virtual void drawStaticRanges(uint32_t geometry, const int32_t* firsts, const int32_t* counts, int32_t ranges) = 0;
        virtual uint32_t* beginStaticIndexUpload(uint32_t geometry, size_t indexCount, size_t& outByteOffset)
        {
            (void)geometry;
            (void)indexCount;
            (void)outByteOffset;
            return nullptr;
        }
        virtual void endStaticIndexUpload() {}
        virtual void drawStaticIndexed(uint32_t geometry, size_t byteOffset, int32_t count)
        {
            (void)geometry;
            (void)byteOffset;
            (void)count;
        }

        virtual uint32_t createQuery() = 0;
        virtual void deleteQuery(uint32_t query) = 0;
        virtual void beginQuery(uint32_t query) = 0;
        virtual void endQuery() = 0;
        virtual bool queryResult(uint32_t query, bool& visible) = 0;

        virtual void beginTextureFrame(double time) = 0;
        virtual UTexture* animated(UTexture* texture) = 0;
        virtual const GLTextureInfo* texture(UTexture* texture) = 0;
        virtual uint32_t whiteTexture() = 0;
        virtual void setTextureOptions(float anisotropy, bool trilinear, float rate) = 0;
        virtual void clearTextures() = 0;

        virtual uint32_t createLightmap(int32_t size, const uint8_t* rgba) = 0;
        virtual void updateLightmap(uint32_t texture, int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t* rgba) = 0;
        virtual void deleteLightmaps(const std::vector<uint32_t>& ids) = 0;

        virtual Canvas2D& canvas() = 0;
        virtual void beginCanvas(int width, int height) = 0;
        virtual void flushCanvas() = 0;
        virtual void frameCounters(int32_t& drawCalls, int32_t& triangles, int32_t& lightmapUploads) const = 0;
    };
}
