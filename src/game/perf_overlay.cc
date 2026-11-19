#include "game/perf_overlay.h"

#include "engine/canvas.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace game {
    namespace {
        struct GlyphRows {
            char ch;
            uint8_t rows[7];
        };

        constexpr GlyphRows kFont[] = {
            { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
            { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
            { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
            { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
            { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
            { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
            { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
            { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
            { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
            { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
            { 'J', { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C } },
            { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
            { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
            { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
            { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
            { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
            { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
            { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
            { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
            { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
            { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
            { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
            { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
            { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 } },
            { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
            { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
            { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
            { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
            { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
            { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
            { '3', { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E } },
            { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
            { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
            { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
            { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
            { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
            { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
            { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
            { ':', { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 } },
            { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
            { '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } },
            { '%', { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 } },
            { '/', { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 } },
            { '(', { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } },
            { ')', { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
            { '|', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
        };

        const uint8_t* lookupGlyph(char ch)
        {
            const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            for (const GlyphRows& g : kFont) {
                if (g.ch == upper) {
                    return g.rows;
                }
            }
            return kFont[0].rows;
        }

        void drawTextLeft(dx::Canvas2D& canvas, const char* text, float x, float y, float step, const dx::LinearColor& color)
        {
            if (!text || !text[0]) {
                return;
            }
            const float charAdvance = 6.0f * step;
            float cx = std::floor(x);
            const float cy = std::floor(y);
            for (const char* p = text; *p; ++p) {
                const uint8_t* rows = lookupGlyph(*p);
                for (int r = 0; r < 7; ++r) {
                    const uint8_t bits = rows[r];
                    if (!bits) {
                        continue;
                    }
                    // Coalesce horizontal runs of set pixels into single drawRect calls.
                    int c = 0;
                    while (c < 5) {
                        if (!(bits & (1u << (4 - c)))) {
                            ++c;
                            continue;
                        }
                        int start = c;
                        while (c < 5 && (bits & (1u << (4 - c)))) {
                            ++c;
                        }
                        canvas.drawRect(
                            cx + static_cast<float>(start) * step,
                            cy + static_cast<float>(r) * step,
                            static_cast<float>(c - start) * step,
                            step,
                            color,
                            dx::BlendStyle::Normal);
                    }
                }
                cx += charAdvance;
            }
        }

        dx::LinearColor fpsColor(float fps)
        {
            if (fps >= 58.0f) {
                return { 0.30f, 0.95f, 0.45f, 0.98f };
            }
            if (fps >= 40.0f) {
                return { 0.98f, 0.82f, 0.25f, 0.98f };
            }
            return { 0.98f, 0.32f, 0.28f, 0.98f };
        }
    }

    void PerfOverlay::setMode(PerfOverlayMode newMode)
    {
        currentMode = newMode;
    }

    void PerfOverlay::pushSample(const PerfOverlaySample& sample)
    {
        if (currentMode == PerfOverlayMode::Off) {
            return;
        }
        const float clampedFrame = std::clamp(sample.frameMs, 0.1f, 500.0f);
        frameHistory[historyHead] = clampedFrame;
        historyHead = (historyHead + 1) % kHistorySize;
        if (historyCount < kHistorySize) {
            historyCount++;
        }

        accumTimeMs += clampedFrame;
        if (accumTimeMs >= 200.0f || dispFps <= 0.0f) {
            accumTimeMs = 0.0f;
            std::array<float, kHistorySize> sorted {};
            float sum = 0.0f;
            for (size_t i = 0; i < historyCount; ++i) {
                sorted[i] = frameHistory[i];
                sum += frameHistory[i];
            }
            const float meanMs = sum / static_cast<float>(historyCount);
            dispFrameMs = meanMs;
            dispFps = meanMs > 0.01f ? (1000.0f / meanMs) : 0.0f;

            std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(historyCount));
            const size_t p99Idx = std::min(historyCount - 1, static_cast<size_t>(static_cast<double>(historyCount - 1) * 0.99 + 0.5));
            const float p99Ms = sorted[p99Idx];
            dispLow1Fps = p99Ms > 0.01f ? (1000.0f / p99Ms) : 0.0f;
            dispSample = sample;
        }
    }

    void PerfOverlay::render(dx::Canvas2D& canvas, int screenWidth, int screenHeight) const
    {
        if (currentMode == PerfOverlayMode::Off || screenWidth <= 0 || screenHeight <= 0 || historyCount == 0) {
            return;
        }

        const float step = std::clamp(std::floor(static_cast<float>(screenHeight) / 420.0f), 1.0f, 4.0f);
        const float lineH = 10.0f * step;
        const float pad = 6.0f * step;
        const float border = std::max(1.0f, step * 0.5f);

        const dx::LinearColor bg { 0.02f, 0.04f, 0.07f, 0.78f };
        const dx::LinearColor edge { 0.25f, 0.70f, 0.92f, 0.65f };
        const dx::LinearColor textMain { 0.88f, 0.95f, 1.00f, 0.96f };
        const dx::LinearColor textSub { 0.62f, 0.80f, 0.92f, 0.92f };
        const dx::LinearColor headColor = fpsColor(dispFps);

        auto drawPanel = [&](float px, float py, float pw, float ph) {
            canvas.drawRect(px, py, pw, ph, bg, dx::BlendStyle::Normal);
            canvas.drawRect(px, py, pw, border, edge, dx::BlendStyle::Normal);
            canvas.drawRect(px, py + ph - border, pw, border, edge, dx::BlendStyle::Normal);
            canvas.drawRect(px, py, border, ph, edge, dx::BlendStyle::Normal);
            canvas.drawRect(px + pw - border, py, border, ph, edge, dx::BlendStyle::Normal);
        };

        // Leave space on the far right for the Deus Ex F12 aug icon (~7% of width).
        const float rightMargin = std::max(12.0f * step, static_cast<float>(screenWidth) * 0.075f);
        const float topMargin = 8.0f * step;

        if (currentMode == PerfOverlayMode::Compact) {
            char line1[64];
            char line2[64];
            std::snprintf(line1, sizeof(line1), "FPS:%5.1f  %5.2f MS", static_cast<double>(dispFps), static_cast<double>(dispFrameMs));
            std::snprintf(line2, sizeof(line2), "1%%:%5.1f  %dX%d", static_cast<double>(dispLow1Fps), dispSample.width, dispSample.height);

            const size_t maxChars = std::max(std::strlen(line1), std::strlen(line2));
            const float panelW = static_cast<float>(maxChars) * 6.0f * step + pad * 2.0f;
            const float panelH = lineH * 2.0f + pad * 2.0f;
            const float px = std::max(pad, static_cast<float>(screenWidth) - rightMargin - panelW);
            const float py = topMargin;

            drawPanel(px, py, panelW, panelH);
            drawTextLeft(canvas, line1, px + pad, py + pad, step, headColor);
            drawTextLeft(canvas, line2, px + pad, py + pad + lineH, step, textSub);
            return;
        }

        // Full Debug mode
        char l0[80];
        char l1[80];
        char l2[80];
        char l3[80];
        char l4[80];
        char l5[80];
        char l6[80];

        std::snprintf(l0, sizeof(l0), "FPS:%5.1f  FT:%5.2fMS  1%%:%5.1f",
            static_cast<double>(dispFps), static_cast<double>(dispFrameMs), static_cast<double>(dispLow1Fps));
        std::snprintf(l1, sizeof(l1), "TICK:%4.2f (ACT:%4.2f AI:%4.2f N:%d)",
            static_cast<double>(dispSample.tickMs), static_cast<double>(dispSample.tickActorMs),
            static_cast<double>(dispSample.tickAiMs), dispSample.tickedActors);
        std::snprintf(l2, sizeof(l2), "RNDR:%4.2f  SWAP:%4.2f  %dX%d",
            static_cast<double>(dispSample.renderMs), static_cast<double>(dispSample.swapMs),
            dispSample.width, dispSample.height);
        std::snprintf(l3, sizeof(l3), "OCC:%4.2f BSP:%4.2f ACT:%4.2f SKY:%4.2f",
            static_cast<double>(dispSample.occludeMs), static_cast<double>(dispSample.bspMs),
            static_cast<double>(dispSample.actorsMs), static_cast<double>(dispSample.skyMs));
        std::snprintf(l4, sizeof(l4), "LGT:%4.2f SSAO:%4.2f FXAA:%4.2f",
            static_cast<double>(dispSample.lightsMs), static_cast<double>(dispSample.ssaoMs),
            static_cast<double>(dispSample.fxaaMs));
        std::snprintf(l5, sizeof(l5), "DC:%3d TRI:%4.1fK BAT:%2d ACTN:%3d",
            dispSample.drawCalls, static_cast<double>(dispSample.triangles) / 1000.0,
            dispSample.actBatches, dispSample.actActors);
        std::snprintf(l6, sizeof(l6), "MESH F:%2d H:%2d R:%2d (E:%4.2f FL:%4.2f)",
            dispSample.meshFast, dispSample.meshFull, dispSample.meshRebuild,
            static_cast<double>(dispSample.actEmitMs), static_cast<double>(dispSample.actFlushMs));

        const char* lines[] = { l0, l1, l2, l3, l4, l5, l6 };
        size_t maxChars = 0;
        for (const char* s : lines) {
            maxChars = std::max(maxChars, std::strlen(s));
        }

        const float graphH = 22.0f * step;
        const float graphGap = 4.0f * step;
        const float panelW = static_cast<float>(maxChars) * 6.0f * step + pad * 2.0f;
        const float panelH = lineH * 7.0f + graphH + graphGap + pad * 2.0f;
        const float px = std::max(pad, static_cast<float>(screenWidth) - rightMargin - panelW);
        const float py = topMargin;

        drawPanel(px, py, panelW, panelH);

        // Line 0: FPS header
        drawTextLeft(canvas, l0, px + pad, py + pad, step, headColor);

        // Mini frame-time sparkline graph (0..33.3 ms scale, 16.67 ms = 60 FPS reference line)
        const float gx = px + pad;
        const float gy = py + pad + lineH;
        const float gw = panelW - pad * 2.0f;
        canvas.drawRect(gx, gy, gw, graphH, { 0.01f, 0.02f, 0.04f, 0.70f }, dx::BlendStyle::Normal);
        const float line60Y = gy + graphH * 0.5f;
        canvas.drawRect(gx, line60Y, gw, std::max(1.0f, step * 0.5f), { 0.30f, 0.85f, 0.45f, 0.35f }, dx::BlendStyle::Normal);

        const size_t bars = std::min<size_t>(historyCount, 80);
        const float barW = gw / 80.0f;
        for (size_t i = 0; i < bars; ++i) {
            const size_t idx = (historyHead + kHistorySize - bars + i) % kHistorySize;
            const float ms = frameHistory[idx];
            const float frac = std::clamp(ms / 33.33f, 0.05f, 1.0f);
            const float bh = frac * graphH;
            const dx::LinearColor barCol = (ms <= 16.8f)
                ? dx::LinearColor { 0.28f, 0.90f, 0.45f, 0.85f }
                : ((ms <= 25.0f) ? dx::LinearColor { 0.96f, 0.78f, 0.22f, 0.85f } : dx::LinearColor { 0.96f, 0.30f, 0.25f, 0.85f });
            canvas.drawRect(gx + static_cast<float>(i) * barW, gy + graphH - bh, std::max(1.0f, barW - 0.5f), bh, barCol, dx::BlendStyle::Normal);
        }

        // Lines 1..6: Detailed telemetry
        float ty = gy + graphH + graphGap;
        drawTextLeft(canvas, l1, px + pad, ty, step, textMain);
        ty += lineH;
        drawTextLeft(canvas, l2, px + pad, ty, step, textMain);
        ty += lineH;
        drawTextLeft(canvas, l3, px + pad, ty, step, textSub);
        ty += lineH;
        drawTextLeft(canvas, l4, px + pad, ty, step, textSub);
        ty += lineH;
        drawTextLeft(canvas, l5, px + pad, ty, step, textMain);
        ty += lineH;
        drawTextLeft(canvas, l6, px + pad, ty, step, textSub);
    }
}
