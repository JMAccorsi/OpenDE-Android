#pragma once

#include <cstdint>
#include <vector>

namespace dx {
    struct ScreenSpan {
        int32_t y = 0;
        int32_t x0 = 0;
        int32_t x1 = 0;
    };

    struct ScreenPoint {
        float x = 0;
        float y = 0;
    };

    class SpanBuffer {
    public:
        void reset(int32_t width, int32_t height);
        void reset(int32_t width, int32_t height, const std::vector<ScreenSpan>& open);
        bool full() const;
        bool hasCoverage() const { return anyCovered; }
        bool rectVisible(int32_t x0, int32_t y0, int32_t x1, int32_t y1) const;
        bool polygonVisible(const ScreenPoint* points, int32_t count, std::vector<ScreenSpan>* visible) const;
        void occlude(const ScreenPoint* points, int32_t count);

    private:
        void cover(int32_t y, int32_t x0, int32_t x1);
        bool rangeHasOpen(const uint64_t* rowBits, int32_t x0, int32_t x1) const;

        std::vector<uint64_t> bits;
        std::vector<uint8_t> rowOpen;
        std::vector<uint64_t> rowOpenMask;
        int32_t rowCount = 0;
        int32_t wordsPerRow = 0;
        int32_t columns = 0;
        int32_t openRows = 0;
        bool anyCovered = false;
    };
}
