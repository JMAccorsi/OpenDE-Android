#include "render/span_buffer.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace dx {
    namespace {
        inline uint64_t maskFrom(int32_t bit)
        {
            return ~0ull << static_cast<unsigned>(bit);
        }

        inline uint64_t maskToInclusive(int32_t bit)
        {
            return ~0ull >> static_cast<unsigned>(63 - bit);
        }

        void setBitRange(uint64_t* rowBits, int32_t x0, int32_t x1)
        {
            const int32_t w0 = x0 >> 6;
            const int32_t w1 = (x1 - 1) >> 6;
            const uint64_t m0 = maskFrom(x0 & 63);
            const uint64_t m1 = maskToInclusive((x1 - 1) & 63);
            if (w0 == w1) {
                rowBits[w0] |= (m0 & m1);
                return;
            }
            rowBits[w0] |= m0;
            for (int32_t w = w0 + 1; w < w1; ++w) {
                rowBits[w] = ~0ull;
            }
            rowBits[w1] |= m1;
        }
    }

    void SpanBuffer::reset(int32_t width, int32_t height)
    {
        columns = std::max(width, 0);
        rowCount = std::max(height, 0);
        wordsPerRow = (columns + 63) >> 6;
        bits.assign(static_cast<size_t>(rowCount * wordsPerRow), 0ull);
        rowOpen.assign(static_cast<size_t>(rowCount), 0u);
        const int32_t rowWords = (rowCount + 63) >> 6;
        rowOpenMask.assign(static_cast<size_t>(rowWords), 0ull);
        if (columns <= 0 || rowCount <= 0) {
            openRows = 0;
            anyCovered = true;
            return;
        }
        const uint64_t lastMask = maskToInclusive((columns - 1) & 63);
        for (int32_t y = 0; y < rowCount; ++y) {
            uint64_t* rowBits = bits.data() + static_cast<size_t>(y * wordsPerRow);
            for (int32_t w = 0; w + 1 < wordsPerRow; ++w) {
                rowBits[w] = ~0ull;
            }
            rowBits[wordsPerRow - 1] = lastMask;
            rowOpen[static_cast<size_t>(y)] = 1u;
        }
        for (int32_t rw = 0; rw + 1 < rowWords; ++rw) {
            rowOpenMask[static_cast<size_t>(rw)] = ~0ull;
        }
        rowOpenMask[static_cast<size_t>(rowWords - 1)] = maskToInclusive((rowCount - 1) & 63);
        openRows = rowCount;
        anyCovered = false;
    }

    void SpanBuffer::reset(int32_t width, int32_t height, const std::vector<ScreenSpan>& open)
    {
        columns = std::max(width, 0);
        rowCount = std::max(height, 0);
        wordsPerRow = (columns + 63) >> 6;
        bits.assign(static_cast<size_t>(rowCount * wordsPerRow), 0ull);
        rowOpen.assign(static_cast<size_t>(rowCount), 0u);
        rowOpenMask.assign(static_cast<size_t>((rowCount + 63) >> 6), 0ull);
        openRows = 0;
        anyCovered = true;
        if (columns <= 0 || rowCount <= 0) {
            return;
        }
        for (const ScreenSpan& span : open) {
            const int32_t x0 = std::max(span.x0, 0);
            const int32_t x1 = std::min(span.x1, columns);
            if (span.y < 0 || span.y >= rowCount || x0 >= x1) {
                continue;
            }
            uint64_t* rowBits = bits.data() + static_cast<size_t>(span.y * wordsPerRow);
            setBitRange(rowBits, x0, x1);
            if (!rowOpen[static_cast<size_t>(span.y)]) {
                rowOpen[static_cast<size_t>(span.y)] = 1u;
                rowOpenMask[static_cast<size_t>(span.y >> 6)] |= (1ull << static_cast<unsigned>(span.y & 63));
                openRows++;
            }
        }
    }

    bool SpanBuffer::full() const
    {
        return openRows <= 0;
    }

    bool SpanBuffer::rangeHasOpen(const uint64_t* rowBits, int32_t x0, int32_t x1) const
    {
        const int32_t w0 = x0 >> 6;
        const int32_t w1 = (x1 - 1) >> 6;
        const uint64_t m0 = maskFrom(x0 & 63);
        const uint64_t m1 = maskToInclusive((x1 - 1) & 63);
        if (w0 == w1) {
            return (rowBits[w0] & (m0 & m1)) != 0ull;
        }
        if ((rowBits[w0] & m0) != 0ull) {
            return true;
        }
        for (int32_t w = w0 + 1; w < w1; ++w) {
            if (rowBits[w] != 0ull) {
                return true;
            }
        }
        return (rowBits[w1] & m1) != 0ull;
    }

    bool SpanBuffer::rectVisible(int32_t x0, int32_t y0, int32_t x1, int32_t y1) const
    {
        x0 = std::max(x0, 0);
        x1 = std::min(x1, columns);
        y0 = std::max(y0, 0);
        y1 = std::min(y1, rowCount);
        if (x0 >= x1 || y0 >= y1 || openRows <= 0) {
            return false;
        }
        const int32_t w0 = x0 >> 6;
        const int32_t w1 = (x1 - 1) >> 6;
        const uint64_t m0 = maskFrom(x0 & 63);
        const uint64_t m1 = maskToInclusive((x1 - 1) & 63);
        const int32_t rw0 = y0 >> 6;
        const int32_t rw1 = (y1 - 1) >> 6;
        const uint64_t rm0 = maskFrom(y0 & 63);
        const uint64_t rm1 = maskToInclusive((y1 - 1) & 63);
        if (w0 == w1) {
            const uint64_t mask = m0 & m1;
            const uint64_t* base = bits.data() + w0;
            for (int32_t rw = rw0; rw <= rw1; ++rw) {
                uint64_t rword = rowOpenMask[static_cast<size_t>(rw)];
                if (rw == rw0) {
                    rword &= rm0;
                }
                if (rw == rw1) {
                    rword &= rm1;
                }
                while (rword) {
                    const int32_t y = (rw << 6) + std::countr_zero(rword);
                    rword &= rword - 1;
                    if ((base[static_cast<size_t>(y * wordsPerRow)] & mask) != 0ull) {
                        return true;
                    }
                }
            }
            return false;
        }
        for (int32_t rw = rw0; rw <= rw1; ++rw) {
            uint64_t rword = rowOpenMask[static_cast<size_t>(rw)];
            if (rw == rw0) {
                rword &= rm0;
            }
            if (rw == rw1) {
                rword &= rm1;
            }
            while (rword) {
                const int32_t y = (rw << 6) + std::countr_zero(rword);
                rword &= rword - 1;
                const uint64_t* rowBits = bits.data() + static_cast<size_t>(y * wordsPerRow);
                if ((rowBits[w0] & m0) != 0ull) {
                    return true;
                }
                for (int32_t w = w0 + 1; w < w1; ++w) {
                    if (rowBits[w] != 0ull) {
                        return true;
                    }
                }
                if ((rowBits[w1] & m1) != 0ull) {
                    return true;
                }
            }
        }
        return false;
    }

    bool SpanBuffer::polygonVisible(const ScreenPoint* points, int32_t count, std::vector<ScreenSpan>* visible) const
    {
        if (count <= 0 || openRows <= 0) {
            return false;
        }
        float top = points[0].y;
        float bottom = points[0].y;
        float minX = points[0].x;
        float maxX = points[0].x;
        for (int32_t i = 1; i < count; i++) {
            top = std::min(top, points[i].y);
            bottom = std::max(bottom, points[i].y);
            minX = std::min(minX, points[i].x);
            maxX = std::max(maxX, points[i].x);
        }
        int32_t first = std::max(static_cast<int32_t>(std::floor(top)) - 1, 0);
        int32_t last = std::min(static_cast<int32_t>(std::ceil(bottom)) + 1, rowCount);
        int32_t polyX0 = std::max(static_cast<int32_t>(std::floor(minX)) - 1, 0);
        int32_t polyX1 = std::min(static_cast<int32_t>(std::ceil(maxX)) + 1, columns);
        if (first >= last || polyX0 >= polyX1) {
            return false;
        }
        if (!visible && !anyCovered) {
            return true;
        }
        if (!rectVisible(polyX0, first, polyX1, last)) {
            return false;
        }
        struct PolyEdge {
            float aX = 0;
            float aY = 0;
            float low = 0;
            float high = 0;
            float minX = 0;
            float maxX = 0;
            float slope = 0;
            bool flat = false;
        };
        PolyEdge edges[64];
        int32_t edgeCount = std::min<int32_t>(count, 64);
        for (int32_t i = 0, j = edgeCount - 1; i < edgeCount; j = i++) {
            const ScreenPoint& a = points[j];
            const ScreenPoint& b = points[i];
            PolyEdge& edge = edges[i];
            edge.aX = a.x;
            edge.aY = a.y;
            edge.low = std::min(a.y, b.y);
            edge.high = std::max(a.y, b.y);
            edge.minX = std::min(a.x, b.x);
            edge.maxX = std::max(a.x, b.x);
            edge.flat = edge.high == edge.low;
            edge.slope = edge.flat ? 0.0f : (b.x - a.x) / (b.y - a.y);
        }
        bool found = false;
        for (int32_t y = first; y < last; y++) {
            if (!rowOpen[static_cast<size_t>(y)]) {
                continue;
            }
            const uint64_t* rowBits = bits.data() + static_cast<size_t>(y * wordsPerRow);
            if (!rangeHasOpen(rowBits, polyX0, polyX1)) {
                continue;
            }
            float low = std::clamp(static_cast<float>(y), top, bottom);
            float high = std::clamp(static_cast<float>(y + 1), top, bottom);
            float left = std::numeric_limits<float>::max();
            float right = std::numeric_limits<float>::lowest();
            for (int32_t i = 0; i < edgeCount; i++) {
                const PolyEdge& edge = edges[i];
                if (edge.high < low || edge.low > high) {
                    continue;
                }
                if (edge.flat) {
                    left = std::min(left, edge.minX);
                    right = std::max(right, edge.maxX);
                    continue;
                }
                float start = edge.aX + (std::max(low, edge.low) - edge.aY) * edge.slope;
                float end = edge.aX + (std::min(high, edge.high) - edge.aY) * edge.slope;
                if (start < end) {
                    if (start < left) {
                        left = start;
                    }
                    if (end > right) {
                        right = end;
                    }
                } else {
                    if (end < left) {
                        left = end;
                    }
                    if (start > right) {
                        right = start;
                    }
                }
            }
            if (left > right) {
                continue;
            }
            int32_t x0 = std::max(static_cast<int32_t>(std::floor(left)) - 1, 0);
            int32_t x1 = std::min(static_cast<int32_t>(std::ceil(right)) + 1, columns);
            if (x0 >= x1) {
                continue;
            }
            if (!visible) {
                if (rangeHasOpen(rowBits, x0, x1)) {
                    return true;
                }
                continue;
            }
            // Emit exact contiguous open runs within [x0, x1).
            const int32_t w0 = x0 >> 6;
            const int32_t w1 = (x1 - 1) >> 6;
            const uint64_t m0 = maskFrom(x0 & 63);
            const uint64_t m1 = maskToInclusive((x1 - 1) & 63);
            int32_t runStart = -1;
            for (int32_t w = w0; w <= w1; ++w) {
                uint64_t word = rowBits[w];
                if (w == w0) {
                    word &= m0;
                }
                if (w == w1) {
                    word &= m1;
                }
                if (word == ~0ull && w > w0 && w < w1) {
                    if (runStart < 0) {
                        runStart = w << 6;
                    }
                    continue;
                }
                if (word == 0ull) {
                    if (runStart >= 0) {
                        visible->push_back({ y, runStart, w << 6 });
                        found = true;
                        runStart = -1;
                    }
                    continue;
                }
                int32_t bit = 0;
                const int32_t base = w << 6;
                while (bit < 64) {
                    uint64_t shifted = word >> static_cast<unsigned>(bit);
                    if (runStart < 0) {
                        if (shifted == 0ull) {
                            break;
                        }
                        int32_t zeros = std::countr_zero(shifted);
                        bit += zeros;
                        runStart = base + bit;
                    } else {
                        uint64_t inv = ~shifted;
                        if (inv == 0ull) {
                            break;
                        }
                        int32_t ones = std::countr_zero(inv);
                        bit += ones;
                        visible->push_back({ y, runStart, base + bit });
                        found = true;
                        runStart = -1;
                    }
                }
            }
            if (runStart >= 0) {
                visible->push_back({ y, runStart, x1 });
                found = true;
            }
        }
        return found;
    }

    void SpanBuffer::occlude(const ScreenPoint* points, int32_t count)
    {
        if (count <= 0 || openRows <= 0) {
            return;
        }
        float top = points[0].y;
        float bottom = points[0].y;
        float minX = points[0].x;
        float maxX = points[0].x;
        for (int32_t i = 1; i < count; i++) {
            top = std::min(top, points[i].y);
            bottom = std::max(bottom, points[i].y);
            minX = std::min(minX, points[i].x);
            maxX = std::max(maxX, points[i].x);
        }
        int32_t first = std::max(static_cast<int32_t>(std::ceil(top - 0.5f)), 0);
        int32_t last = std::min(static_cast<int32_t>(std::ceil(bottom - 0.5f)), rowCount);
        int32_t polyX0 = std::max(static_cast<int32_t>(std::ceil(minX - 0.5f)), 0);
        int32_t polyX1 = std::min(static_cast<int32_t>(std::ceil(maxX - 0.5f)), columns);
        if (first >= last || polyX0 >= polyX1 || !rectVisible(polyX0, first, polyX1, last)) {
            return;
        }
        struct OccludeEdge {
            float lowerX = 0;
            float lowerY = 0;
            float upperY = 0;
            float slope = 0;
        };
        OccludeEdge edges[64];
        int32_t activeEdges = 0;
        int32_t limit = std::min<int32_t>(count, 64);
        for (int32_t i = 0, j = limit - 1; i < limit; j = i++) {
            const ScreenPoint& lower = points[j].y <= points[i].y ? points[j] : points[i];
            const ScreenPoint& upper = points[j].y <= points[i].y ? points[i] : points[j];
            if (lower.y >= upper.y) {
                continue;
            }
            edges[activeEdges++] = { lower.x, lower.y, upper.y, (upper.x - lower.x) / (upper.y - lower.y) };
        }
        for (int32_t y = first; y < last; y++) {
            if (!rowOpen[static_cast<size_t>(y)]) {
                continue;
            }
            const uint64_t* rowBits = bits.data() + static_cast<size_t>(y * wordsPerRow);
            if (!rangeHasOpen(rowBits, polyX0, polyX1)) {
                continue;
            }
            float center = static_cast<float>(y) + 0.5f;
            float left = std::numeric_limits<float>::max();
            float right = std::numeric_limits<float>::lowest();
            for (int32_t i = 0; i < activeEdges; i++) {
                const OccludeEdge& edge = edges[i];
                if (center < edge.lowerY || center >= edge.upperY) {
                    continue;
                }
                float x = edge.lowerX + (center - edge.lowerY) * edge.slope;
                left = std::min(left, x);
                right = std::max(right, x);
            }
            if (left > right) {
                continue;
            }
            int32_t x0 = std::max(static_cast<int32_t>(std::ceil(left - 0.5f)), 0);
            int32_t x1 = std::min(static_cast<int32_t>(std::ceil(right - 0.5f)), columns);
            if (x0 < x1) {
                cover(y, x0, x1);
            }
        }
    }

    void SpanBuffer::cover(int32_t y, int32_t x0, int32_t x1)
    {
        if (!rowOpen[static_cast<size_t>(y)]) {
            return;
        }
        anyCovered = true;
        uint64_t* rowBits = bits.data() + static_cast<size_t>(y * wordsPerRow);
        const int32_t w0 = x0 >> 6;
        const int32_t w1 = (x1 - 1) >> 6;
        const uint64_t m0 = maskFrom(x0 & 63);
        const uint64_t m1 = maskToInclusive((x1 - 1) & 63);
        if (w0 == w1) {
            rowBits[w0] &= ~(m0 & m1);
        } else {
            rowBits[w0] &= ~m0;
            for (int32_t w = w0 + 1; w < w1; ++w) {
                rowBits[w] = 0ull;
            }
            rowBits[w1] &= ~m1;
        }
        uint64_t any = 0ull;
        for (int32_t w = 0; w < wordsPerRow; ++w) {
            any |= rowBits[w];
        }
        if (any == 0ull) {
            rowOpen[static_cast<size_t>(y)] = 0u;
            rowOpenMask[static_cast<size_t>(y >> 6)] &= ~(1ull << static_cast<unsigned>(y & 63));
            openRows--;
        }
    }
}
