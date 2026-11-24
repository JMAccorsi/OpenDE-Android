#pragma once

#include "engine/resources.h"
#include "render/procedural.h"
#include "render/render_device.h"
#include "render/texture_replacement.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace dx {
    struct TextureAnimation {
        UTexture* current = nullptr;
        double lastUpdate = 0;
        float accumulator = 0;
        int32_t primeCurrent = 0;
    };

    class TextureCache {
    public:
        static UTexture* advance(UTexture* head, TextureAnimation& animation, double now, float constantRate = 0.0f);

        TextureCache();
        ~TextureCache();
        TextureCache(const TextureCache&) = delete;
        TextureCache& operator=(const TextureCache&) = delete;

        void beginFrame(double time);
        UTexture* animated(UTexture* texture);
        const GLTextureInfo* get(UTexture* texture);
        const GLTextureInfo* getMasked(UTexture* texture);
        uint32_t white();
        void clear();
        void resetProcedural(UTexture* texture);
        void setFiltering(float newAnisotropy, bool newTrilinear);
        void setConstantRate(float rate);
        bool takeBoundChanged();

    private:
        bool upload(UTexture* texture, GLTextureInfo& info, bool masked);
        bool uploadReplacement(UTexture* texture, GLTextureInfo& info);

        struct ProceduralRing {
            uint32_t ids[3] = {};
            uint32_t width = 0;
            uint32_t height = 0;
            uint8_t nextSlot = 0;
        };

        std::unordered_map<UTexture*, GLTextureInfo> textures;
        std::unordered_map<UTexture*, GLTextureInfo> maskedTextures;
        std::unordered_map<UTexture*, TextureAnimation> animations;
        std::unordered_map<UTexture*, uint64_t> proceduralFrames;
        std::unordered_map<UTexture*, ProceduralRing> proceduralRings;
        std::vector<uint8_t> proceduralRgba;
        ProceduralTextures procedural;
        TextureReplacements replacements;
        uint32_t whiteTexture = 0;
        double time = 0;
        uint64_t frame = 0;
        float anisotropy = 1;
        float maxAnisotropy = 1;
        bool trilinear = false;
        bool boundChanged = false;
        float constantRate = 0;
    };
}
