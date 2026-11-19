#include "render/gl_canvas.h"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>

namespace dx {
    GLCanvas::GLCanvas(UberShader& shaderRef, TextureCache& textureRef)
        : shader(shaderRef)
        , textures(textureRef)
    {
        glGenSamplers(1, &nearestSampler);
        glSamplerParameteri(nearestSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glSamplerParameteri(nearestSampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glSamplerParameteri(nearestSampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glSamplerParameteri(nearestSampler, GL_TEXTURE_WRAP_T, GL_REPEAT);
        batches.reserve(256);
        vertices.reserve(4096);
    }

    GLCanvas::~GLCanvas()
    {
        glDeleteSamplers(1, &nearestSampler);
    }

    void GLCanvas::begin(int w, int h)
    {
        viewWidth = std::max(w, 1);
        viewHeight = std::max(h, 1);
        vertices.clear();
        batches.clear();
        clipping = false;
    }

    float GLCanvas::width() const
    {
        return static_cast<float>(viewWidth);
    }

    float GLCanvas::height() const
    {
        return static_cast<float>(viewHeight);
    }

    void GLCanvas::flush()
    {
        if (vertices.empty() || batches.empty()) {
            vertices.clear();
            batches.clear();
            return;
        }
        shader.use();
        shader.upload(vertices);
        shader.setViewProj(orthoMatrix(static_cast<float>(viewWidth), static_cast<float>(viewHeight)));
        shader.setPan(0, 0);
        shader.setScale(1, 1);
        shader.setTint(1, 1, 1, 1);
        shader.setClipPlanes(nullptr, 0);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        const uint32_t whiteTex = textures.white();
        bool scissorKnown = false;
        bool scissorActive = false;
        bool hasScissorRect = false;
        int lastClipRect[4] = {};
        uint32_t activeSampler = 0;

        for (const Batch& batch : batches) {
            if (batch.count <= 0) {
                continue;
            }
            if (batch.clipping) {
                if (!scissorKnown || !scissorActive) {
                    glEnable(GL_SCISSOR_TEST);
                    scissorActive = true;
                    scissorKnown = true;
                }
                if (!hasScissorRect || !std::equal(batch.clipRect, batch.clipRect + 4, lastClipRect)) {
                    glScissor(batch.clipRect[0], viewHeight - batch.clipRect[1] - batch.clipRect[3], batch.clipRect[2], batch.clipRect[3]);
                    std::copy(batch.clipRect, batch.clipRect + 4, lastClipRect);
                    hasScissorRect = true;
                }
            } else if (!scissorKnown || scissorActive) {
                glDisable(GL_SCISSOR_TEST);
                scissorActive = false;
                scissorKnown = true;
            }

            int32_t flags = SHADER_Textured;
            BlendMode mode = BlendMode::Alpha;
            switch (batch.style) {
            case BlendStyle::Masked:
                flags |= SHADER_AlphaTest;
                mode = BlendMode::Masked;
                break;
            case BlendStyle::Translucent:
                mode = BlendMode::Translucent;
                break;
            case BlendStyle::Modulated:
                mode = BlendMode::Modulated;
                if (batch.masked) {
                    flags |= SHADER_AlphaTest;
                }
                break;
            case BlendStyle::None:
            case BlendStyle::Normal:
                mode = batch.masked ? BlendMode::Masked : (batch.texture == whiteTex ? BlendMode::Alpha : BlendMode::Opaque);
                if (batch.masked) {
                    flags |= SHADER_AlphaTest;
                }
                break;
            }

            uint32_t wantedSampler = batch.smooth ? 0 : nearestSampler;
            if (wantedSampler != activeSampler) {
                glBindSampler(0, wantedSampler);
                activeSampler = wantedSampler;
            }

            shader.setFlags(flags);
            shader.setBlend(mode);
            shader.bindTexture(batch.texture);
            shader.drawRange(batch.first, batch.count, batch.primitive);
        }

        if (activeSampler != 0) {
            glBindSampler(0, 0);
        }
        if (scissorActive) {
            glDisable(GL_SCISSOR_TEST);
        }
        shader.setBlend(BlendMode::Opaque);
        vertices.clear();
        batches.clear();
    }

    void GLCanvas::setClip(float x, float y, float w, float h)
    {
        int rect[4] = { static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), static_cast<int>(std::ceil(w)), static_cast<int>(std::ceil(h)) };
        rect[2] = std::max(rect[2], 0);
        rect[3] = std::max(rect[3], 0);
        clipping = true;
        std::copy(rect, rect + 4, clipRect);
    }

    void GLCanvas::clearClip()
    {
        clipping = false;
    }

    void GLCanvas::prepare(uint32_t texture, BlendStyle style, bool masked, bool smooth, uint32_t primitive)
    {
        if (!batches.empty()) {
            const Batch& last = batches.back();
            bool sameClip = last.clipping == clipping && (!clipping || std::equal(last.clipRect, last.clipRect + 4, clipRect));
            if (sameClip && last.texture == texture && last.style == style && last.masked == masked && last.smooth == smooth && last.primitive == primitive) {
                return;
            }
        }
        Batch batch;
        batch.texture = texture;
        batch.style = style;
        batch.masked = masked;
        batch.smooth = smooth;
        batch.primitive = primitive;
        batch.clipping = clipping;
        if (clipping) {
            std::copy(clipRect, clipRect + 4, batch.clipRect);
        }
        batch.first = static_cast<int32_t>(vertices.size());
        batch.count = 0;
        batches.push_back(batch);
    }

    void GLCanvas::drawTile(UTexture* texture, float x, float y, float w, float h, float u, float v, float ul, float vl, LinearColor color, BlendStyle style, bool smooth)
    {
        if (style == BlendStyle::None) {
            return;
        }
        uint32_t id = textures.white();
        float texWidth = 1;
        float texHeight = 1;
        bool masked = false;
        if (texture) {
            UTexture* frame = textures.animated(texture);
            const GLTextureInfo* info = style == BlendStyle::Masked ? textures.getMasked(frame) : textures.get(frame);
            if (!info) {
                return;
            }
            id = info->id;
            texWidth = static_cast<float>(info->width);
            texHeight = static_cast<float>(info->height);
            masked = info->masked;
        }
        prepare(id, style, masked, smooth, GL_TRIANGLES);
        if (style == BlendStyle::Modulated) {
            color = { 1, 1, 1, 1 };
        }
        float u0 = u / texWidth;
        float v0 = v / texHeight;
        float u1 = (u + ul) / texWidth;
        float v1 = (v + vl) / texHeight;
        GLVertex corners[4];
        float xs[4] = { x, x + w, x + w, x };
        float ys[4] = { y, y, y + h, y + h };
        float us[4] = { u0, u1, u1, u0 };
        float vs[4] = { v0, v0, v1, v1 };
        for (int i = 0; i < 4; i++) {
            corners[i].x = xs[i];
            corners[i].y = ys[i];
            corners[i].u = us[i];
            corners[i].v = vs[i];
            corners[i].r = color.r;
            corners[i].g = color.g;
            corners[i].b = color.b;
            corners[i].a = color.a;
        }
        const size_t base = vertices.size();
        vertices.resize(base + 6);
        GLVertex* out = vertices.data() + base;
        out[0] = corners[0];
        out[1] = corners[1];
        out[2] = corners[2];
        out[3] = corners[0];
        out[4] = corners[2];
        out[5] = corners[3];
        batches.back().count += 6;
    }

    void GLCanvas::drawRect(float x, float y, float w, float h, LinearColor color, BlendStyle style)
    {
        drawTile(nullptr, x, y, w, h, 0, 0, 1, 1, color, style == BlendStyle::Masked ? BlendStyle::Normal : style, false);
    }

    void GLCanvas::drawLine(float x1, float y1, float x2, float y2, LinearColor color)
    {
        prepare(textures.white(), BlendStyle::Normal, false, true, GL_LINES);
        GLVertex a;
        a.x = x1 + 0.5f;
        a.y = y1 + 0.5f;
        a.r = color.r;
        a.g = color.g;
        a.b = color.b;
        a.a = color.a;
        GLVertex b = a;
        b.x = x2 + 0.5f;
        b.y = y2 + 0.5f;
        vertices.push_back(a);
        vertices.push_back(b);
        batches.back().count += 2;
    }
}
