#include "render/span_buffer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dx {
    void SpanBuffer::reset(int32_t width, int32_t height)
    {
        columns = std::max(width, 0);
        rows.resize(static_cast<size_t>(std::max(height, 0)));
        for (std::vector<Interval>& row : rows) {
            row.clear();
            if (columns > 0) {
                row.push_back({ 0, columns });
            }
        }
        openRows = columns > 0 ? static_cast<int32_t>(rows.size()) : 0;
    }

    void SpanBuffer::reset(int32_t width, int32_t height, const std::vector<ScreenSpan>& open)
    {
        columns = std::max(width, 0);
        rows.resize(static_cast<size_t>(std::max(height, 0)));
        for (std::vector<Interval>& row : rows) {
            row.clear();
        }
        for (const ScreenSpan& span : open) {
            int32_t x0 = std::max(span.x0, 0);
            int32_t x1 = std::min(span.x1, columns);
            if (span.y < 0 || static_cast<size_t>(span.y) >= rows.size() || x0 >= x1) {
                continue;
            }
            rows[static_cast<size_t>(span.y)].push_back({ x0, x1 });
        }
        openRows = 0;
        for (std::vector<Interval>& row : rows) {
            if (row.empty()) {
                continue;
            }
            std::sort(row.begin(), row.end(), [](const Interval& a, const Interval& b) {
                return a.x0 < b.x0;
            });
            size_t kept = 0;
            for (size_t i = 1; i < row.size(); i++) {
                if (row[i].x0 <= row[kept].x1) {
                    row[kept].x1 = std::max(row[kept].x1, row[i].x1);
                } else {
                    row[++kept] = row[i];
                }
            }
            row.resize(kept + 1);
            openRows++;
        }
    }

    bool SpanBuffer::full() const
    {
        return openRows <= 0;
    }

    bool SpanBuffer::rectVisible(int32_t x0, int32_t y0, int32_t x1, int32_t y1) const
    {
        x0 = std::max(x0, 0);
        x1 = std::min(x1, columns);
        y0 = std::max(y0, 0);
        y1 = std::min(y1, static_cast<int32_t>(rows.size()));
        if (x0 >= x1) {
            return false;
        }
        for (int32_t y = y0; y < y1; y++) {
            for (const Interval& gap : rows[static_cast<size_t>(y)]) {
                if (gap.x0 >= x1) {
                    break;
                }
                if (gap.x1 > x0) {
                    return true;
                }
            }
        }
        return false;
    }

    bool SpanBuffer::polygonVisible(const ScreenPoint* points, int32_t count, std::vector<ScreenSpan>* visible) const
    {
        if (count <= 0) {
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
        int32_t last = std::min(static_cast<int32_t>(std::ceil(bottom)) + 1, static_cast<int32_t>(rows.size()));
        int32_t polyX0 = std::max(static_cast<int32_t>(std::floor(minX)) - 1, 0);
        int32_t polyX1 = std::min(static_cast<int32_t>(std::ceil(maxX)) + 1, columns);
        if (first >= last || polyX0 >= polyX1) {
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
            const std::vector<Interval>& row = rows[static_cast<size_t>(y)];
            if (row.empty() || row.front().x0 >= polyX1 || row.back().x1 <= polyX0) {
                continue;
            }
            bool anyOverlap = false;
            for (const Interval& gap : row) {
                if (gap.x0 >= polyX1) {
                    break;
                }
                if (gap.x1 > polyX0) {
                    anyOverlap = true;
                    break;
                }
            }
            if (!anyOverlap) {
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
                left = std::min({ left, start, end });
                right = std::max({ right, start, end });
            }
            if (left > right) {
                continue;
            }
            int32_t x0 = std::max(static_cast<int32_t>(std::floor(left)) - 1, 0);
            int32_t x1 = std::min(static_cast<int32_t>(std::ceil(right)) + 1, columns);
            for (const Interval& gap : row) {
                if (gap.x0 >= x1) {
                    break;
                }
                if (gap.x1 <= x0) {
                    continue;
                }
                if (!visible) {
                    return true;
                }
                found = true;
                visible->push_back({ y, std::max(gap.x0, x0), std::min(gap.x1, x1) });
            }
        }
        return found;
    }

    void SpanBuffer::occlude(const ScreenPoint* points, int32_t count)
    {
        if (count <= 0) {
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
        int32_t last = std::min(static_cast<int32_t>(std::ceil(bottom - 0.5f)), static_cast<int32_t>(rows.size()));
        int32_t polyX0 = std::max(static_cast<int32_t>(std::ceil(minX - 0.5f)), 0);
        int32_t polyX1 = std::min(static_cast<int32_t>(std::ceil(maxX - 0.5f)), columns);
        if (first >= last || polyX0 >= polyX1) {
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
            const std::vector<Interval>& row = rows[static_cast<size_t>(y)];
            if (row.empty() || row.front().x0 >= polyX1 || row.back().x1 <= polyX0) {
                continue;
            }
            bool anyOverlap = false;
            for (const Interval& gap : row) {
                if (gap.x0 >= polyX1) {
                    break;
                }
                if (gap.x1 > polyX0) {
                    anyOverlap = true;
                    break;
                }
            }
            if (!anyOverlap) {
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
        std::vector<Interval>& row = rows[static_cast<size_t>(y)];
        for (size_t i = 0; i < row.size(); i++) {
            Interval gap = row[i];
            if (gap.x0 >= x1) {
                break;
            }
            if (gap.x1 <= x0) {
                continue;
            }
            if (gap.x0 < x0 && gap.x1 > x1) {
                row[i].x1 = x0;
                row.insert(row.begin() + static_cast<std::ptrdiff_t>(i) + 1, Interval { x1, gap.x1 });
                return;
            }
            if (gap.x0 < x0) {
                row[i].x1 = x0;
                continue;
            }
            if (gap.x1 > x1) {
                row[i].x0 = x1;
                return;
            }
            row.erase(row.begin() + static_cast<std::ptrdiff_t>(i));
            i--;
        }
        if (row.empty()) {
            openRows--;
        }
    }
}
