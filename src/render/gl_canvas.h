#pragma once

#include "engine/canvas.h"
#include "render/gl_shader.h"
#include "render/texture_cache.h"

#include <vector>

namespace dx {
    class GLCanvas : public Canvas2D {
    public:
        GLCanvas(UberShader& shader, TextureCache& textures);
        ~GLCanvas() override;

        void begin(int width, int height);
        void flush();

        float width() const override;
        float height() const override;
        void setClip(float x, float y, float w, float h) override;
        void clearClip() override;
        void drawTile(UTexture* texture, float x, float y, float w, float h, float u, float v, float ul, float vl, LinearColor color, BlendStyle style, bool smooth) override;
        void drawRect(float x, float y, float w, float h, LinearColor color, BlendStyle style) override;
        void drawLine(float x1, float y1, float x2, float y2, LinearColor color) override;

    private:
        struct Batch {
            uint32_t texture = 0;
            BlendStyle style = BlendStyle::Normal;
            bool masked = false;
            bool smooth = true;
            uint32_t primitive = 0;
            bool clipping = false;
            int clipRect[4] = {};
            int32_t first = 0;
            int32_t count = 0;
        };

        void prepare(uint32_t texture, BlendStyle style, bool masked, bool smooth, uint32_t primitive);

        UberShader& shader;
        TextureCache& textures;
        int viewWidth = 1;
        int viewHeight = 1;
        bool clipping = false;
        int clipRect[4] = {};
        std::vector<Batch> batches;
        std::vector<GLVertex> vertices;
        uint32_t nearestSampler = 0;
    };
}
