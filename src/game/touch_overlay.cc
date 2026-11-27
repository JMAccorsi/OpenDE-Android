#include "game/touch_overlay.h"

#include "core/config.h"
#include "engine/canvas.h"
#include "engine/input.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace game {
    namespace {
        struct ControlSpec {
            const char* iniKey;
            const char* labelEn;
            const char* labelPt;
            uint8_t key;
            bool defaultEnabled;
            float defaultCx;
            float defaultCy;
            float defaultScale;
            float radiusFrac;
        };

        constexpr uint8_t kKeyE = static_cast<uint8_t>(dx::IK_A + ('E' - 'A'));
        constexpr uint8_t kKeyG = static_cast<uint8_t>(dx::IK_A + ('G' - 'A'));
        constexpr uint8_t kKeyI = static_cast<uint8_t>(dx::IK_A + ('I' - 'A'));
        constexpr uint8_t kKeyQ = static_cast<uint8_t>(dx::IK_A + ('Q' - 'A'));
        constexpr uint8_t kKeyX = static_cast<uint8_t>(dx::IK_A + ('X' - 'A'));
        constexpr uint8_t kKeyF12 = static_cast<uint8_t>(dx::IK_F1 + 11);

        // Increment whenever default control coordinates/visibility change so non-customized
        // users automatically receive ergonomic layout updates.
        constexpr int kTouchLayoutVersion = 3;

        // Neutral dark-gray translucent theme (high legibility on both bright & dark maps).
        struct TouchTheme {
            dx::LinearColor stickBaseRgb = { 0.09f, 0.10f, 0.12f, 1.0f };
            dx::LinearColor stickKnobRgb = { 0.72f, 0.75f, 0.78f, 1.0f };
            dx::LinearColor stickRimRgb = { 0.84f, 0.86f, 0.88f, 1.0f };
            dx::LinearColor btnFillRgb = { 0.10f, 0.11f, 0.13f, 1.0f };
            dx::LinearColor btnPressedRgb = { 0.34f, 0.37f, 0.41f, 1.0f };
            dx::LinearColor btnRimRgb = { 0.84f, 0.86f, 0.88f, 1.0f };
            dx::LinearColor btnRimPressedRgb = { 1.00f, 1.00f, 1.00f, 1.0f };
            dx::LinearColor fireFillRgb = { 0.22f, 0.14f, 0.08f, 1.0f };
            dx::LinearColor firePressedRgb = { 0.52f, 0.30f, 0.12f, 1.0f };
            dx::LinearColor fireRimRgb = { 0.94f, 0.56f, 0.24f, 1.0f };
            dx::LinearColor fireRimPressedRgb = { 1.00f, 0.86f, 0.58f, 1.0f };
            dx::LinearColor textRgb = { 0.94f, 0.95f, 0.97f, 1.0f };
            dx::LinearColor textPressedRgb = { 1.00f, 1.00f, 1.00f, 1.0f };
        };

        constexpr TouchTheme kTheme {};

        constexpr std::array<ControlSpec, kTouchControlCount> kSpecs {{
            { "MoveStick", "MOVE", "MOVER", dx::IK_None, true, 0.125f, 0.700f, 1.00f, 0.135f },
            { "Fire", "FIRE", "ATIRAR", dx::IK_LeftMouse, true, 0.905f, 0.680f, 1.12f, 0.068f },
            { "Use", "USE", "USAR", dx::IK_RightMouse, true, 0.822f, 0.700f, 0.94f, 0.056f },
            { "Jump", "JUMP", "PULAR", dx::IK_Space, true, 0.912f, 0.515f, 0.88f, 0.052f },
            { "Crouch", "DUCK", "AGACH.", kKeyX, true, 0.840f, 0.545f, 0.86f, 0.050f },
            { "Reload", "REL.", "RECAR.", dx::IK_Semicolon, true, 0.775f, 0.585f, 0.82f, 0.046f },
            { "Scope", "ZOOM", "MIRA", dx::IK_LeftBracket, true, 0.755f, 0.715f, 0.84f, 0.048f },
            { "LeanLeft", "< LEAN", "< ESQ.", kKeyQ, true, 0.085f, 0.500f, 0.80f, 0.045f },
            { "LeanRight", "LEAN >", "DIR. >", kKeyE, true, 0.165f, 0.500f, 0.80f, 0.045f },
            { "Inventory", "ITEMS", "INV.", kKeyI, true, 0.670f, 0.075f, 0.78f, 0.045f },
            { "Goals", "GOALS", "OBJ.", kKeyG, true, 0.595f, 0.075f, 0.76f, 0.044f },
            { "Menu", "MENU", "MENU", dx::IK_Escape, true, 0.745f, 0.075f, 0.78f, 0.045f },
            { "BeltPrev", "<BELT", "<CINTO", dx::IK_MouseWheelUp, false, 0.255f, 0.875f, 0.76f, 0.043f },
            { "BeltNext", "BELT>", "CINTO>", dx::IK_MouseWheelDown, false, 0.315f, 0.875f, 0.76f, 0.043f },
            { "SwitchAmmo", "AMMO", "MUN.", dx::IK_SingleQuote, true, 0.700f, 0.590f, 0.80f, 0.045f },
            { "Laser", "LASER", "LASER", dx::IK_RightBracket, true, 0.690f, 0.715f, 0.78f, 0.044f },
            { "AugLight", "LIGHT", "LUZ", kKeyF12, false, 0.275f, 0.610f, 0.78f, 0.044f },
            { "QuickSave", "Q.SAVE", "SALVAR", dx::IK_GreyPlus, true, 0.820f, 0.075f, 0.76f, 0.044f },
            { "QuickLoad", "Q.LOAD", "CARREG.", dx::IK_GreySlash, false, 0.515f, 0.075f, 0.74f, 0.043f },
            { "AugsMenu", "AUGS", "AUGS", dx::IK_None, true, 0.275f, 0.745f, 0.82f, 0.046f },
            { "Drop", "DROP", "SOLTAR", dx::IK_None, true, 0.775f, 0.785f, 0.92f, 0.050f },
            { "Throw", "THROW", "JOGAR", dx::IK_None, true, 0.890f, 0.785f, 1.00f, 0.056f },
        }};

        constexpr const char* kTouchLayoutSection = "OpenDE.TouchLayout";

        const ControlSpec& specOf(TouchControlId id)
        {
            return kSpecs[static_cast<size_t>(id)];
        }

        // Compact 5x7 ASCII bitmap font (35 bits per glyph, row-major MSB first in 7 rows of 5 bits).
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
            { '<', { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 } },
            { '>', { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 } },
            { '%', { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 } },
            { '/', { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 } },
            { '(', { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } },
            { ')', { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
            { '^', { 0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00 } },
            { 'v', { 0x00, 0x00, 0x00, 0x11, 0x0A, 0x04, 0x00 } },
        };

        const uint8_t* lookupGlyph(char ch)
        {
            char upper = (ch == 'v') ? 'v' : static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            for (const GlyphRows& g : kFont) {
                if (g.ch == upper) {
                    return g.rows;
                }
            }
            return kFont[0].rows;
        }

        void drawText(dx::Canvas2D& canvas, const char* text, float centerX, float centerY, float pxScale, const dx::LinearColor& color, float maxWidth = 0.0f)
        {
            if (!text || !text[0]) {
                return;
            }
            const size_t len = std::strlen(text);
            float step = std::max(1.0f, std::floor(pxScale));
            if (maxWidth > 0.0f && len > 0) {
                float unitsW = static_cast<float>(len) * 6.0f - 1.0f;
                step = std::min(step, std::max(1.0f, std::floor(maxWidth / unitsW)));
            }
            const float charW = 5.0f * step;
            const float gap = step;
            const float totalW = static_cast<float>(len) * charW + static_cast<float>(len > 0 ? len - 1 : 0) * gap;
            const float totalH = 7.0f * step;
            float x = std::floor(centerX - totalW * 0.5f);
            float y = std::floor(centerY - totalH * 0.5f);
            for (size_t i = 0; i < len; ++i) {
                const uint8_t* rows = lookupGlyph(text[i]);
                for (int r = 0; r < 7; ++r) {
                    uint8_t bits = rows[r];
                    if (!bits) {
                        continue;
                    }
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
                        canvas.drawRect(x + static_cast<float>(start) * step, y + static_cast<float>(r) * step, static_cast<float>(c - start) * step, step, color, dx::BlendStyle::Normal);
                    }
                }
                x += charW + gap;
            }
        }

        void drawFilledCircle(dx::Canvas2D& canvas, float cx, float cy, float radius, const dx::LinearColor& color)
        {
            if (radius <= 1.0f || color.a <= 0.005f) {
                return;
            }
            constexpr int kSlices = 8;
            constexpr int kBandCount = kSlices * 2;
            struct FillLut {
                float y0[kBandCount];
                float y1[kBandCount];
                float halfW[kBandCount];
                FillLut()
                {
                    for (int idx = 0; idx < kBandCount; ++idx) {
                        float a = static_cast<float>(idx - kSlices) / static_cast<float>(kSlices);
                        float b = static_cast<float>(idx - kSlices + 1) / static_cast<float>(kSlices);
                        float m = 0.5f * (a + b);
                        y0[idx] = a;
                        y1[idx] = b;
                        halfW[idx] = std::sqrt(std::max(0.0f, 1.0f - m * m));
                    }
                }
            };
            static const FillLut lut;
            float prevY = -radius;
            for (int idx = 0; idx < kBandCount; ++idx) {
                float nextY = (idx + 1 < kBandCount) ? (std::round(cy + lut.y1[idx] * radius) - cy) : radius;
                float h = nextY - prevY;
                float halfW = lut.halfW[idx] * radius;
                if (h > 0.0f && halfW > 0.5f) {
                    canvas.drawRect(cx - halfW, cy + prevY, halfW * 2.0f, h, color, dx::BlendStyle::Normal);
                }
                prevY = nextY;
            }
        }

        void drawCircleOutline(dx::Canvas2D& canvas, float cx, float cy, float radius, float thickness, const dx::LinearColor& color)
        {
            if (radius <= 1.0f || color.a <= 0.005f) {
                return;
            }
            constexpr int kSegs = 18;
            struct CircleLut {
                float c[kSegs + 1];
                float s[kSegs + 1];
                CircleLut()
                {
                    constexpr float twoPi = 6.2831853f;
                    for (int i = 0; i <= kSegs; ++i) {
                        float a = (static_cast<float>(i) / static_cast<float>(kSegs)) * twoPi;
                        c[i] = std::cos(a);
                        s[i] = std::sin(a);
                    }
                }
            };
            static const CircleLut lut;
            int rings = std::clamp(static_cast<int>(std::round(thickness)), 1, 2);
            for (int ring = 0; ring < rings; ++ring) {
                float r = radius - static_cast<float>(ring);
                if (r <= 1.0f) {
                    break;
                }
                for (int i = 0; i < kSegs; ++i) {
                    canvas.drawLine(
                        cx + lut.c[i] * r,
                        cy + lut.s[i] * r,
                        cx + lut.c[i + 1] * r,
                        cy + lut.s[i + 1] * r,
                        color);
                }
            }
        }

        void drawRectOutline(dx::Canvas2D& canvas, float x, float y, float w, float h, float t, const dx::LinearColor& color)
        {
            canvas.drawRect(x, y, w, t, color, dx::BlendStyle::Normal);
            canvas.drawRect(x, y + h - t, w, t, color, dx::BlendStyle::Normal);
            canvas.drawRect(x, y + t, t, std::max(0.0f, h - 2.0f * t), color, dx::BlendStyle::Normal);
            canvas.drawRect(x + w - t, y + t, t, std::max(0.0f, h - 2.0f * t), color, dx::BlendStyle::Normal);
        }

        void drawIconAccent(dx::Canvas2D& canvas, TouchControlId id, float cx, float cy, float r, const dx::LinearColor& color)
        {
            const float d = r * 0.44f;
            if (id == TouchControlId::Fire) {
                float gap = r * 0.16f;
                float arm = r * 0.24f;
                canvas.drawLine(cx - gap - arm, cy - d * 0.55f, cx - gap, cy - d * 0.55f, color);
                canvas.drawLine(cx + gap, cy - d * 0.55f, cx + gap + arm, cy - d * 0.55f, color);
                canvas.drawLine(cx, cy - d * 0.55f - gap - arm, cx, cy - d * 0.55f - gap, color);
                canvas.drawLine(cx, cy - d * 0.55f + gap, cx, cy - d * 0.55f + gap + arm * 0.55f, color);
            } else if (id == TouchControlId::Jump) {
                float yTop = cy - r * 0.50f;
                float yBot = cy - r * 0.18f;
                canvas.drawLine(cx, yTop, cx - d * 0.50f, yBot, color);
                canvas.drawLine(cx, yTop, cx + d * 0.50f, yBot, color);
                canvas.drawLine(cx - d * 0.50f, yBot, cx + d * 0.50f, yBot, color);
            } else if (id == TouchControlId::Crouch) {
                float yTop = cy - r * 0.50f;
                float yBot = cy - r * 0.20f;
                canvas.drawLine(cx - d * 0.50f, yTop, cx, yBot, color);
                canvas.drawLine(cx + d * 0.50f, yTop, cx, yBot, color);
            } else if (id == TouchControlId::Scope) {
                drawCircleOutline(canvas, cx, cy - r * 0.34f, r * 0.18f, 1.5f, color);
                canvas.drawLine(cx - r * 0.24f, cy - r * 0.34f, cx + r * 0.24f, cy - r * 0.34f, color);
            } else if (id == TouchControlId::Menu) {
                float mw = r * 0.56f;
                float mh = std::max(2.0f, r * 0.07f);
                canvas.drawRect(cx - mw * 0.5f, cy - r * 0.44f, mw, mh, color, dx::BlendStyle::Normal);
                canvas.drawRect(cx - mw * 0.5f, cy - r * 0.32f, mw, mh, color, dx::BlendStyle::Normal);
            }
        }

        const char* controlLabel(TouchControlId id, bool ptBr)
        {
            const ControlSpec& s = specOf(id);
            return ptBr ? s.labelPt : s.labelEn;
        }
    } // namespace

    TouchOverlayManager::TouchOverlayManager()
    {
        buildDefaultLayouts();
    }

    void TouchOverlayManager::buildDefaultLayouts()
    {
        for (size_t i = 0; i < kTouchControlCount; ++i) {
            layouts_[i] = TouchControlLayout {
                kSpecs[i].defaultEnabled,
                kSpecs[i].defaultCx,
                kSpecs[i].defaultCy,
                kSpecs[i].defaultScale,
            };
        }
        activePreset_ = 0;
    }

    void TouchOverlayManager::load(const std::string& iniSection)
    {
        (void)iniSection;
        buildDefaultLayouts();
        int savedVersion = 0;
        if (std::optional<std::string> verRaw = dx::config::system().get(kTouchLayoutSection, "LayoutVersion")) {
            savedVersion = std::atoi(verRaw->c_str());
        }
        layoutCustomized_ = false;
        if (std::optional<std::string> custRaw = dx::config::system().get(kTouchLayoutSection, "Customized")) {
            layoutCustomized_ = (std::atoi(custRaw->c_str()) != 0);
        }
        // Automatically migrate non-customized users to newer ergonomic defaults.
        if (savedVersion < kTouchLayoutVersion && !layoutCustomized_) {
            return;
        }
        for (size_t i = 0; i < kTouchControlCount; ++i) {
            std::optional<std::string> raw = dx::config::system().get(kTouchLayoutSection, kSpecs[i].iniKey);
            if (!raw) {
                continue;
            }
            int en = layouts_[i].enabled ? 1 : 0;
            float cx = layouts_[i].cx;
            float cy = layouts_[i].cy;
            float sc = layouts_[i].scale;
            if (std::sscanf(raw->c_str(), "%d,%f,%f,%f", &en, &cx, &cy, &sc) == 4) {
                layouts_[i].enabled = (en != 0);
                layouts_[i].cx = std::clamp(cx, 0.04f, 0.96f);
                layouts_[i].cy = std::clamp(cy, 0.05f, 0.95f);
                layouts_[i].scale = std::clamp(sc, 0.60f, 1.80f);
            }
        }
    }

    void TouchOverlayManager::saveLayout() const
    {
        dx::config::system().set(kTouchLayoutSection, "LayoutVersion", std::to_string(kTouchLayoutVersion));
        dx::config::system().set(kTouchLayoutSection, "Customized", layoutCustomized_ ? "1" : "0");
        for (size_t i = 0; i < kTouchControlCount; ++i) {
            char buf[96];
            std::snprintf(
                buf,
                sizeof(buf),
                "%d,%.4f,%.4f,%.2f",
                layouts_[i].enabled ? 1 : 0,
                static_cast<double>(layouts_[i].cx),
                static_cast<double>(layouts_[i].cy),
                static_cast<double>(layouts_[i].scale));
            dx::config::system().set(kTouchLayoutSection, kSpecs[i].iniKey, buf);
        }
        dx::config::system().save();
    }

    void TouchOverlayManager::resetToDefault()
    {
        buildDefaultLayouts();
        layoutCustomized_ = false;
    }

    void TouchOverlayManager::applyPreset(int presetIndex)
    {
        int mode = ((presetIndex % 3) + 3) % 3;
        buildDefaultLayouts();
        activePreset_ = mode;
        if (mode == 0) {
            return;
        }
        if (mode == 1) {
            // Minimalist: Move, Fire, Use, Jump, Crouch, Menu + temporary Drop/Throw
            for (size_t i = 0; i < kTouchControlCount; ++i) {
                TouchControlId id = static_cast<TouchControlId>(i);
                bool keep = (id == TouchControlId::MoveStick || id == TouchControlId::Fire ||
                             id == TouchControlId::Use || id == TouchControlId::Jump ||
                             id == TouchControlId::Crouch || id == TouchControlId::Menu ||
                             id == TouchControlId::Drop || id == TouchControlId::Throw);
                layouts_[i].enabled = keep;
            }
            return;
        }
        // Preset 2: 4-Finger Claw layout (top triggers for Fire/Scope/Jump/Crouch/Lean + wide camera zone)
        for (size_t i = 0; i < kTouchControlCount; ++i) {
            TouchControlId id = static_cast<TouchControlId>(i);
            layouts_[i].enabled = (id != TouchControlId::BeltPrev
                && id != TouchControlId::BeltNext && id != TouchControlId::QuickLoad);
        }
        layouts_[static_cast<size_t>(TouchControlId::MoveStick)] = { true, 0.145f, 0.700f, 1.00f };
        layouts_[static_cast<size_t>(TouchControlId::Fire)] = { true, 0.145f, 0.235f, 1.12f };
        layouts_[static_cast<size_t>(TouchControlId::LeanLeft)] = { true, 0.245f, 0.200f, 0.84f };
        layouts_[static_cast<size_t>(TouchControlId::LeanRight)] = { true, 0.245f, 0.350f, 0.84f };
        layouts_[static_cast<size_t>(TouchControlId::Scope)] = { true, 0.845f, 0.225f, 1.00f };
        layouts_[static_cast<size_t>(TouchControlId::Laser)] = { true, 0.845f, 0.375f, 0.82f };
        layouts_[static_cast<size_t>(TouchControlId::Jump)] = { true, 0.750f, 0.215f, 0.90f };
        layouts_[static_cast<size_t>(TouchControlId::Crouch)] = { true, 0.750f, 0.365f, 0.88f };
        layouts_[static_cast<size_t>(TouchControlId::Use)] = { true, 0.875f, 0.610f, 0.98f };
        layouts_[static_cast<size_t>(TouchControlId::Reload)] = { true, 0.785f, 0.720f, 0.88f };
        layouts_[static_cast<size_t>(TouchControlId::SwitchAmmo)] = { true, 0.885f, 0.755f, 0.80f };
        layouts_[static_cast<size_t>(TouchControlId::Drop)] = { true, 0.775f, 0.785f, 0.90f };
        layouts_[static_cast<size_t>(TouchControlId::Throw)] = { true, 0.890f, 0.785f, 0.98f };
        layouts_[static_cast<size_t>(TouchControlId::AugLight)] = { true, 0.270f, 0.560f, 0.80f };
        layouts_[static_cast<size_t>(TouchControlId::Inventory)] = { true, 0.360f, 0.075f, 0.76f };
        layouts_[static_cast<size_t>(TouchControlId::Goals)] = { true, 0.435f, 0.075f, 0.76f };
        layouts_[static_cast<size_t>(TouchControlId::Menu)] = { true, 0.510f, 0.075f, 0.78f };
        layouts_[static_cast<size_t>(TouchControlId::AugsMenu)] = { true, 0.585f, 0.075f, 0.76f };
        layouts_[static_cast<size_t>(TouchControlId::QuickSave)] = { true, 0.660f, 0.075f, 0.76f };
    }

    void TouchOverlayManager::setSettings(const TouchOverlaySettings& newSettings)
    {
        settings_ = newSettings;
        if (!settings_.enabled) {
            visible_ = false;
        }
    }

    void TouchOverlayManager::openEditor()
    {
        editorBackup_ = layouts_;
        editorOpen_ = true;
        editorDragging_ = false;
        editorPendingTool_ = -1;
        selectedControl_ = static_cast<int>(TouchControlId::Fire);
        toolbarAtBottom_ = false;
    }

    void TouchOverlayManager::closeEditor(bool saveChanges)
    {
        if (!editorOpen_) {
            return;
        }
        if (saveChanges) {
            layoutCustomized_ = true;
            saveLayout();
        } else {
            layouts_ = editorBackup_;
        }
        editorOpen_ = false;
        editorDragging_ = false;
        editorPendingTool_ = -1;
    }

    float TouchOverlayManager::controlRadiusPx(TouchControlId id, int drawHeight) const
    {
        float h = static_cast<float>(std::max(drawHeight, 1));
        const TouchControlLayout& layout = layouts_[static_cast<size_t>(id)];
        return specOf(id).radiusFrac * h * layout.scale * settings_.scale;
    }

    void TouchOverlayManager::setGameContext(const TouchGameContext& ctx)
    {
        ctx_ = ctx;
        const uint64_t now = SDL_GetTicks();
        if (ctx.valid && ctx.frobKind != TouchFrobKind::None) {
            displayFrobKind_ = ctx.frobKind;
            lastFrobTicks_ = now;
        } else if (!ctx.valid || (now - lastFrobTicks_) >= 120) {
            displayFrobKind_ = TouchFrobKind::None;
        }
    }

    bool TouchOverlayManager::isActionToggle(TouchControlId id) const
    {
        if (id != TouchControlId::Crouch) {
            return false;
        }
        if (ctx_.valid && ctx_.gameToggleCrouch) {
            // Game's own bToggleCrouch handles press+release toggling natively.
            return false;
        }
        if (settings_.smartContext && ctx_.valid && (ctx_.isSwimming || ctx_.spyDroneActive)) {
            return false;
        }
        return true;
    }

    bool TouchOverlayManager::isContextualControl(TouchControlId id) const
    {
        return id == TouchControlId::Scope || id == TouchControlId::Laser ||
               id == TouchControlId::Reload || id == TouchControlId::SwitchAmmo ||
               id == TouchControlId::Drop || id == TouchControlId::Throw;
    }

    bool TouchOverlayManager::isControlContextVisible(TouchControlId id) const
    {
        const size_t idx = static_cast<size_t>(id);
        if (!layouts_[idx].enabled) {
            return false;
        }
        if (!settings_.smartContext || !ctx_.valid) {
            if (id == TouchControlId::Drop || id == TouchControlId::Throw) {
                return ctx_.valid && ctx_.carryingObject;
            }
            return true;
        }
        switch (id) {
        case TouchControlId::Drop:
        case TouchControlId::Throw:
            return ctx_.carryingObject;
        case TouchControlId::Fire:
            return !ctx_.carryingObject && ctx_.fireKind != TouchFireKind::None;
        case TouchControlId::Use:
            if (ctx_.spyDroneActive) {
                return false;
            }
            if (ctx_.carryingObject) {
                return displayFrobKind_ != TouchFrobKind::None;
            }
            return true;
        case TouchControlId::Scope:
            return !ctx_.carryingObject && !ctx_.spyDroneActive && ctx_.hasScope;
        case TouchControlId::Laser:
            return !ctx_.carryingObject && !ctx_.spyDroneActive && ctx_.hasLaser;
        case TouchControlId::Reload:
            return !ctx_.carryingObject && !ctx_.spyDroneActive && ctx_.canReload && ctx_.needsReload;
        case TouchControlId::SwitchAmmo:
            return !ctx_.carryingObject && !ctx_.spyDroneActive && ctx_.hasMultiAmmo;
        case TouchControlId::LeanLeft:
        case TouchControlId::LeanRight:
            return ctx_.canLean && !ctx_.isSwimming && !ctx_.onLadder && !ctx_.spyDroneActive;
        case TouchControlId::Crouch:
            return !ctx_.onLadder;
        case TouchControlId::AugsMenu:
            return ctx_.hasAnyAug || augDrawerOpen_;
        case TouchControlId::AugLight:
            return ctx_.hasLightAug;
        case TouchControlId::MoveStick:
        case TouchControlId::Jump:
        case TouchControlId::Inventory:
        case TouchControlId::Goals:
        case TouchControlId::Menu:
        case TouchControlId::BeltPrev:
        case TouchControlId::BeltNext:
        case TouchControlId::QuickSave:
        case TouchControlId::QuickLoad:
        case TouchControlId::Count:
            return true;
        }
        return true;
    }

    const char* TouchOverlayManager::dynamicControlLabel(TouchControlId id, bool ptBr) const
    {
        if (!settings_.smartContext || !ctx_.valid) {
            return controlLabel(id, ptBr);
        }
        if (id == TouchControlId::Use) {
            switch (displayFrobKind_) {
            case TouchFrobKind::Pickup:
                return ptBr ? "PEGAR" : "TAKE";
            case TouchFrobKind::Door:
                return ptBr ? "ABRIR" : "OPEN";
            case TouchFrobKind::DoorLocked:
                return ptBr ? "TRANC." : "LOCKED";
            case TouchFrobKind::Talk:
                return ptBr ? "FALAR" : "TALK";
            case TouchFrobKind::Grab:
                return ptBr ? "PEGAR" : "GRAB";
            case TouchFrobKind::Corpse:
                return ptBr ? "CORPO" : "BODY";
            case TouchFrobKind::Disarm:
                return ptBr ? "DESARM." : "DISARM";
            case TouchFrobKind::Generic:
                return ptBr ? "USAR" : "USE";
            case TouchFrobKind::None:
                if (ctx_.hasItemInHand && !ctx_.carryingObject) {
                    return ptBr ? "GUARD." : "STOW";
                }
                return controlLabel(id, ptBr);
            }
        }
        if (id == TouchControlId::Fire) {
            switch (ctx_.fireKind) {
            case TouchFireKind::Melee:
                return ptBr ? "GOLPE" : "STRIKE";
            case TouchFireKind::Throw:
                return ptBr ? "LANCAR" : "THROW";
            case TouchFireKind::Plant:
                return ptBr ? "PLANTAR" : "PLANT";
            case TouchFireKind::Pick:
                return ptBr ? "ABRIR" : "PICK";
            case TouchFireKind::Hack:
                return ptBr ? "HACK" : "BYPASS";
            case TouchFireKind::UseItem:
                return ptBr ? "USAR" : "USE";
            case TouchFireKind::DeactivateItem:
                return ptBr ? "DESLIG." : "OFF";
            case TouchFireKind::DetonateDrone:
                return ptBr ? "DETONAR" : "EMP";
            case TouchFireKind::Fire:
            case TouchFireKind::None:
                return controlLabel(id, ptBr);
            }
        }
        if (id == TouchControlId::Jump) {
            if (ctx_.isSwimming || ctx_.spyDroneActive) {
                return ptBr ? "SUBIR" : "UP";
            }
            if (ctx_.onLadder) {
                return ptBr ? "SOLTAR" : "OFF";
            }
        }
        if (id == TouchControlId::Crouch) {
            if (ctx_.isSwimming) {
                return ptBr ? "DESCER" : "DIVE";
            }
            if (ctx_.spyDroneActive) {
                return ptBr ? "DESCER" : "DOWN";
            }
        }
        if (id == TouchControlId::Scope && ctx_.isScoped) {
            return ptBr ? "SAIR" : "UNZOOM";
        }
        return controlLabel(id, ptBr);
    }

    int TouchOverlayManager::hitTestControl(float xNorm, float yNorm, int drawWidth, int drawHeight, bool includeDisabled) const
    {
        float px = xNorm * static_cast<float>(drawWidth);
        float py = yNorm * static_cast<float>(drawHeight);
        int bestIdx = -1;
        float bestDistSq = 1e12f;
        for (size_t i = 0; i < kTouchControlCount; ++i) {
            TouchControlId id = static_cast<TouchControlId>(i);
            if (!includeDisabled && !isControlContextVisible(id)) {
                continue;
            }
            float cx = layouts_[i].cx * static_cast<float>(drawWidth);
            float cy = layouts_[i].cy * static_cast<float>(drawHeight);
            float r = controlRadiusPx(id, drawHeight);
            // Slightly generous hit target for finger comfort
            float hitR = r * 1.12f;
            float dx = px - cx;
            float dy = py - cy;
            float distSq = dx * dx + dy * dy;
            if (distSq <= hitR * hitR && distSq < bestDistSq) {
                bestDistSq = distSq;
                bestIdx = static_cast<int>(i);
            }
        }
        return bestIdx;
    }

    void TouchOverlayManager::setButtonPressed(TouchControlId id, bool pressed, const TouchInputCallbacks& cb)
    {
        size_t idx = static_cast<size_t>(id);
        buttons_[idx].active = pressed;
        uint8_t key = kSpecs[idx].key;
        if (pressed) {
            if (id == TouchControlId::Scope && settings_.smartContext && ctx_.valid && ctx_.scopeIsBinoculars) {
                key = dx::IK_LeftMouse;
            }
            buttons_[idx].pressedKey = key;
        } else {
            if (buttons_[idx].pressedKey != 0) {
                key = buttons_[idx].pressedKey;
            }
            buttons_[idx].pressedKey = 0;
        }
        if (key != dx::IK_None && cb.onKey) {
            cb.onKey(key, pressed);
        }
    }

    void TouchOverlayManager::releaseTouchStickOnly(const TouchInputCallbacks& cb)
    {
        hasStickFinger_ = false;
        if (prevEmitStickX_ != 0.0f && cb.onAxis) {
            cb.onAxis(dx::IK_JoyX, 0.0f);
            prevEmitStickX_ = 0.0f;
        }
        if (prevEmitStickY_ != 0.0f && cb.onAxis) {
            cb.onAxis(dx::IK_JoyY, 0.0f);
            prevEmitStickY_ = 0.0f;
        }
    }

    void TouchOverlayManager::releaseAllControls(const TouchInputCallbacks& cb)
    {
        releaseTouchStickOnly(cb);
        hasLookFinger_ = false;
        lookCanQuickClick_ = false;
        for (size_t i = 0; i < kTouchControlCount; ++i) {
            buttons_[i].fingerDown = false;
            if (buttons_[i].active) {
                TouchControlId id = static_cast<TouchControlId>(i);
                setButtonPressed(id, false, cb);
            }
        }
        crouchToggled_ = false;
        crouchPulseEndTicks_ = 0;
        if (hasAugTapFinger_) {
            if (augTapKey_ != 0 && cb.onKey) {
                cb.onKey(augTapKey_, false);
            }
            hasAugTapFinger_ = false;
            augTapKey_ = 0;
        }
        if (hasHudTapFinger_) {
            if (hudTapKey_ != 0 && cb.onKey) {
                cb.onKey(hudTapKey_, false);
            }
            hasHudTapFinger_ = false;
            hudTapKey_ = 0;
        }
        keypadExitDown_ = false;
    }

    void TouchOverlayManager::markTouchActive()
    {
        if (settings_.enabled) {
            visible_ = true;
        }
    }

    void TouchOverlayManager::onPhysicalInput(const TouchInputCallbacks& cb)
    {
        if (editorOpen_) {
            return;
        }
        if (visible_ || hasStickFinger_ || hasLookFinger_ || crouchToggled_ || hasAugTapFinger_ || hasHudTapFinger_ || augDrawerOpen_ || keypadExitDown_) {
            visible_ = false;
            augDrawerOpen_ = false;
            keypadExitDown_ = false;
            releaseAllControls(cb);
        }
    }

    void TouchOverlayManager::releaseGameplayInputs(const TouchInputCallbacks& cb)
    {
        releaseAllControls(cb);
    }

    void TouchOverlayManager::emitAxes(const TouchInputCallbacks& cb)
    {
        if (crouchPulseEndTicks_ != 0 && SDL_GetTicks() >= crouchPulseEndTicks_) {
            const size_t crouchIdx = static_cast<size_t>(TouchControlId::Crouch);
            crouchPulseEndTicks_ = 0;
            if (!buttons_[crouchIdx].fingerDown && buttons_[crouchIdx].active) {
                setButtonPressed(TouchControlId::Crouch, false, cb);
            }
        }
        if (editorOpen_ || !settings_.enabled || !visible_ || !hasStickFinger_) {
            if (prevEmitStickX_ != 0.0f && cb.onAxis) {
                cb.onAxis(dx::IK_JoyX, 0.0f);
                prevEmitStickX_ = 0.0f;
            }
            if (prevEmitStickY_ != 0.0f && cb.onAxis) {
                cb.onAxis(dx::IK_JoyY, 0.0f);
                prevEmitStickY_ = 0.0f;
            }
            return;
        }
        float tx = std::clamp((stickCurX_ - stickOriginX_) / 0.11f, -1.0f, 1.0f) * 1000.0f;
        float ty = std::clamp((stickOriginY_ - stickCurY_) / 0.11f, -1.0f, 1.0f) * 1000.0f;
        if (std::hypot(tx, ty) < 120.0f) {
            tx = 0.0f;
            ty = 0.0f;
        }
        if (cb.onAxis) {
            if (tx != 0.0f || prevEmitStickX_ != 0.0f) {
                cb.onAxis(dx::IK_JoyX, tx);
                prevEmitStickX_ = tx;
            }
            if (ty != 0.0f || prevEmitStickY_ != 0.0f) {
                cb.onAxis(dx::IK_JoyY, ty);
                prevEmitStickY_ = ty;
            }
        }
    }

    namespace {
        struct UiRect {
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;

            bool contains(float px, float py) const
            {
                return px >= x && px <= x + w && py >= y && py <= y + h;
            }
        };

        struct EditorToolbarLayout {
            static constexpr int kToolCount = 7;
            UiRect panel;
            UiRect header;
            UiRect gridBtn;
            UiRect flipBtn;
            std::array<UiRect, kToolCount> tools;
            float divider1X = 0.0f;
            float divider2X = 0.0f;
        };

        EditorToolbarLayout computeEditorToolbarLayout(int width, int height, bool atBottom)
        {
            EditorToolbarLayout l;
            const float fw = static_cast<float>(width);
            const float fh = static_cast<float>(height);
            const float tw = std::clamp(fw * 0.74f, std::min(fw * 0.94f, 640.0f), fh * 1.44f);
            const float th = std::clamp(fh * 0.142f, 98.0f, 152.0f);
            const float tx = (fw - tw) * 0.5f;
            const float ty = atBottom ? (fh - th - fh * 0.022f) : (fh * 0.022f);

            l.panel = { tx, ty, tw, th };
            const float headerH = th * 0.36f;
            l.header = { tx, ty, tw, headerH };

            const float hPad = 8.0f;
            const float pillH = std::max(24.0f, headerH - 12.0f);
            const float pillY = ty + (headerH - pillH) * 0.5f;
            const float flipW = std::clamp(tw * 0.095f, 72.0f, 120.0f);
            const float gridW = std::clamp(tw * 0.125f, 92.0f, 152.0f);

            l.flipBtn = { tx + tw - hPad - flipW, pillY, flipW, pillH };
            l.gridBtn = { l.flipBtn.x - 8.0f - gridW, pillY, gridW, pillH };

            // Proportional tool widths:
            // [0: SIZE -] [1: SIZE +] [2: SHOW] | [3: PRESET] [4: RESET] | [5: CANCEL] [6: SAVE]
            constexpr std::array<float, EditorToolbarLayout::kToolCount> kWeights = {
                0.78f, 0.78f, 1.24f, 1.62f, 1.02f, 1.08f, 1.18f
            };
            constexpr float kTotalWeight = 0.78f + 0.78f + 1.24f + 1.62f + 1.02f + 1.08f + 1.18f;

            const float padX = 10.0f;
            const float gap = 7.0f;
            const float groupGap = 15.0f;
            const float rowY = ty + headerH + 9.0f;
            const float rowH = th - headerH - 18.0f;
            const float usableW = tw - padX * 2.0f - gap * 4.0f - groupGap * 2.0f;

            float curX = tx + padX;
            for (int i = 0; i < EditorToolbarLayout::kToolCount; ++i) {
                float bw = usableW * (kWeights[static_cast<size_t>(i)] / kTotalWeight);
                l.tools[static_cast<size_t>(i)] = { curX, rowY, bw, rowH };
                curX += bw;
                if (i == 2) {
                    l.divider1X = curX + groupGap * 0.5f;
                    curX += groupGap;
                } else if (i == 4) {
                    l.divider2X = curX + groupGap * 0.5f;
                    curX += groupGap;
                } else if (i < EditorToolbarLayout::kToolCount - 1) {
                    curX += gap;
                }
            }
            return l;
        }

        void drawCornerBrackets(dx::Canvas2D& canvas, float x0, float y0, float x1, float y1, float arm, float thick, const dx::LinearColor& col)
        {
            // Top-left
            canvas.drawRect(x0, y0, arm, thick, col, dx::BlendStyle::Normal);
            canvas.drawRect(x0, y0, thick, arm, col, dx::BlendStyle::Normal);
            // Top-right
            canvas.drawRect(x1 - arm, y0, arm, thick, col, dx::BlendStyle::Normal);
            canvas.drawRect(x1 - thick, y0, thick, arm, col, dx::BlendStyle::Normal);
            // Bottom-left
            canvas.drawRect(x0, y1 - thick, arm, thick, col, dx::BlendStyle::Normal);
            canvas.drawRect(x0, y1 - arm, thick, arm, col, dx::BlendStyle::Normal);
            // Bottom-right
            canvas.drawRect(x1 - arm, y1 - thick, arm, thick, col, dx::BlendStyle::Normal);
            canvas.drawRect(x1 - thick, y1 - arm, thick, arm, col, dx::BlendStyle::Normal);
        }
    } // namespace

    void TouchOverlayManager::getToolbarRect(int width, int height, float& outX, float& outY, float& outW, float& outH) const
    {
        EditorToolbarLayout l = computeEditorToolbarLayout(width, height, toolbarAtBottom_);
        outX = l.panel.x;
        outY = l.panel.y;
        outW = l.panel.w;
        outH = l.panel.h;
    }

    bool TouchOverlayManager::handleEditorTouch(const SDL_Event& event, int drawWidth, int drawHeight)
    {
        float px = event.tfinger.x * static_cast<float>(drawWidth);
        float py = event.tfinger.y * static_cast<float>(drawHeight);
        SDL_FingerID fid = event.tfinger.fingerID;

        const EditorToolbarLayout tb = computeEditorToolbarLayout(drawWidth, drawHeight, toolbarAtBottom_);

        auto hitToolbarItem = [&](float x, float y) -> int {
            if (tb.flipBtn.contains(x, y)) {
                return 100; // flip toolbar top/bottom
            }
            if (tb.gridBtn.contains(x, y)) {
                return 101; // toggle snap-to-grid
            }
            for (int i = 0; i < EditorToolbarLayout::kToolCount; ++i) {
                if (tb.tools[static_cast<size_t>(i)].contains(x, y)) {
                    return i;
                }
            }
            if (tb.panel.contains(x, y)) {
                return 99; // inside toolbar body (consume touch)
            }
            return -1;
        };

        if (event.type == SDL_EVENT_FINGER_DOWN) {
            int tool = hitToolbarItem(px, py);
            if (tool >= 0) {
                editorPendingTool_ = tool;
                editorFinger_ = fid;
                editorDragging_ = false;
                return true;
            }
            int hit = hitTestControl(event.tfinger.x, event.tfinger.y, drawWidth, drawHeight, true);
            if (hit >= 0) {
                selectedControl_ = hit;
                editorDragging_ = true;
                editorPendingTool_ = -1;
                editorFinger_ = fid;
                editorDragOffsetX_ = layouts_[static_cast<size_t>(hit)].cx - event.tfinger.x;
                editorDragOffsetY_ = layouts_[static_cast<size_t>(hit)].cy - event.tfinger.y;
            }
            return true;
        }
        if (event.type == SDL_EVENT_FINGER_MOTION && fid == editorFinger_ && editorDragging_) {
            TouchControlLayout& cur = layouts_[static_cast<size_t>(selectedControl_)];
            float nx = std::clamp(event.tfinger.x + editorDragOffsetX_, 0.04f, 0.96f);
            float ny = std::clamp(event.tfinger.y + editorDragOffsetY_, 0.05f, 0.95f);
            if (editorSnapGrid_) {
                constexpr float kStep = 0.025f;
                nx = std::clamp(std::round(nx / kStep) * kStep, 0.04f, 0.96f);
                ny = std::clamp(std::round(ny / kStep) * kStep, 0.05f, 0.95f);
            }
            cur.cx = nx;
            cur.cy = ny;
            return true;
        }
        if (event.type == SDL_EVENT_FINGER_UP && fid == editorFinger_) {
            if (editorDragging_) {
                editorDragging_ = false;
                return true;
            }
            int tool = editorPendingTool_;
            editorPendingTool_ = -1;
            if (tool == hitToolbarItem(px, py)) {
                TouchControlLayout& sel = layouts_[static_cast<size_t>(selectedControl_)];
                switch (tool) {
                case 0: // Size -
                    sel.scale = std::clamp(sel.scale - 0.10f, 0.60f, 1.80f);
                    break;
                case 1: // Size +
                    sel.scale = std::clamp(sel.scale + 0.10f, 0.60f, 1.80f);
                    break;
                case 2: // Toggle On/Off
                    sel.enabled = !sel.enabled;
                    break;
                case 3: // Cycle Preset
                    applyPreset((activePreset_ + 1) % 3);
                    break;
                case 4: // Reset
                    resetToDefault();
                    break;
                case 5: // Cancel
                    closeEditor(false);
                    break;
                case 6: // Save
                    closeEditor(true);
                    break;
                case 100: // Flip Toolbar top/bottom
                    toolbarAtBottom_ = !toolbarAtBottom_;
                    break;
                case 101: // Toggle Snap-to-Grid
                    editorSnapGrid_ = !editorSnapGrid_;
                    break;
                default:
                    break;
                }
            }
            return true;
        }
        return true;
    }

    bool TouchOverlayManager::handleEditorPhysicalKey(SDL_Keycode sym, bool pressed)
    {
        if (!editorOpen_) {
            return false;
        }
        if (pressed && (sym == SDLK_ESCAPE || sym == SDLK_AC_BACK)) {
            closeEditor(false);
        }
        return true;
    }

    bool TouchOverlayManager::handleEditorControllerButton(SDL_GamepadButton button, bool pressed)
    {
        if (!editorOpen_) {
            return false;
        }
        if (pressed && (button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_BACK)) {
            closeEditor(false);
        } else if (pressed && button == SDL_GAMEPAD_BUTTON_START) {
            closeEditor(true);
        }
        return true;
    }

    bool TouchOverlayManager::handleTouchEvent(const SDL_Event& event, int drawWidth, int drawHeight, const TouchInputCallbacks& cb)
    {
        if (drawWidth <= 0 || drawHeight <= 0) {
            return false;
        }
        if (editorOpen_) {
            return handleEditorTouch(event, drawWidth, drawHeight);
        }
        if (!settings_.enabled) {
            return false;
        }

        const SDL_FingerID fid = event.tfinger.fingerID;
        const float x = event.tfinger.x;
        const float y = event.tfinger.y;

        if (event.type == SDL_EVENT_FINGER_DOWN) {
            const bool justWoke = !visible_;
            visible_ = true;

            int hit = hitTestControl(x, y, drawWidth, drawHeight, false);
            if (hit == static_cast<int>(TouchControlId::MoveStick) && !hasStickFinger_) {
                hasStickFinger_ = true;
                stickFinger_ = fid;
                const TouchControlLayout& stickLayout = layouts_[static_cast<size_t>(TouchControlId::MoveStick)];
                stickOriginX_ = stickLayout.cx;
                stickOriginY_ = stickLayout.cy;
                stickCurX_ = x;
                stickCurY_ = y;
                return true;
            }
            if (hit >= 0 && hit != static_cast<int>(TouchControlId::MoveStick)) {
                // On a blind wake touch (before controls were visible), summon the overlay
                // without triggering an accidental weapon fire/quicksave tap on that first frame.
                if (!justWoke) {
                    size_t idx = static_cast<size_t>(hit);
                    TouchControlId id = static_cast<TouchControlId>(idx);
                    if (id == TouchControlId::Drop || id == TouchControlId::Throw) {
                        buttons_[idx].active = true;
                        buttons_[idx].finger = fid;
                        if (cb.onDropCarried) {
                            cb.onDropCarried(id == TouchControlId::Drop);
                        }
                        if (cb.onButtonHaptic) {
                            cb.onButtonHaptic();
                        }
                        return true;
                    }
                    if (id == TouchControlId::AugsMenu) {
                        augDrawerOpen_ = !augDrawerOpen_;
                        buttons_[idx].fingerDown = true;
                        buttons_[idx].finger = fid;
                        if (cb.onButtonHaptic) {
                            cb.onButtonHaptic();
                        }
                        return true;
                    }
                    if (isActionToggle(id)) {
                        crouchToggled_ = !crouchToggled_;
                        setButtonPressed(id, crouchToggled_, cb);
                        buttons_[idx].fingerDown = true;
                        buttons_[idx].finger = fid;
                        if (cb.onButtonHaptic) {
                            cb.onButtonHaptic();
                        }
                        return true;
                    }
                    if (!buttons_[idx].active) {
                        buttons_[idx].fingerDown = true;
                        buttons_[idx].finger = fid;
                        buttons_[idx].dragAimMoved = false;
                        buttons_[idx].prevX = x;
                        buttons_[idx].prevY = y;
                        if (id == TouchControlId::Crouch && ctx_.valid && ctx_.gameToggleCrouch && !ctx_.isSwimming && !ctx_.spyDroneActive) {
                            crouchPulseEndTicks_ = SDL_GetTicks() + 70;
                        }
                        if (id == TouchControlId::Jump && crouchToggled_) {
                            crouchToggled_ = false;
                            setButtonPressed(TouchControlId::Crouch, false, cb);
                        }
                        setButtonPressed(id, true, cb);
                        if (cb.onButtonHaptic) {
                            cb.onButtonHaptic();
                        }
                    }
                    return true;
                }
            }
            if (!justWoke && augDrawerOpen_ && !hasAugTapFinger_) {
                int augIdx = hitTestAugDrawer(x, y, drawWidth, drawHeight);
                if (augIdx >= 0 && static_cast<size_t>(augIdx) < augEntries_.size()) {
                    uint8_t key = augEntries_[static_cast<size_t>(augIdx)].key;
                    if (key != 0 && cb.onKey) {
                        hasAugTapFinger_ = true;
                        augTapFinger_ = fid;
                        augTapKey_ = key;
                        cb.onKey(key, true);
                    }
                    return true;
                }
            }
            if (!hasHudTapFinger_ && cb.hitTestHud) {
                TouchHudAction hudAction = cb.hitTestHud(x * static_cast<float>(drawWidth), y * static_cast<float>(drawHeight));
                if (hudAction.key != 0 || (hudAction.command && hudAction.command[0] != '\0')) {
                    hasHudTapFinger_ = true;
                    hudTapFinger_ = fid;
                    hudTapKey_ = hudAction.key;
                    if (hudAction.key != 0 && cb.onKey) {
                        cb.onKey(hudAction.key, true);
                    } else if (hudAction.command && hudAction.command[0] != '\0' && cb.onCommand) {
                        cb.onCommand(hudAction.command);
                    }
                    return true;
                }
            }
            if (!hasLookFinger_) {
                hasLookFinger_ = true;
                lookCanQuickClick_ = !justWoke;
                lookFinger_ = fid;
                lookStartX_ = x;
                lookStartY_ = y;
                lookMaxDistSq_ = 0.0f;
                lookDownTicks_ = SDL_GetTicks();
            }
            return true;
        }

        if (event.type == SDL_EVENT_FINGER_MOTION) {
            if (hasStickFinger_ && fid == stickFinger_) {
                stickCurX_ = x;
                stickCurY_ = y;
                return true;
            }
            if ((hasAugTapFinger_ && fid == augTapFinger_) || (hasHudTapFinger_ && fid == hudTapFinger_)) {
                return true;
            }
            // Allow smooth aim while holding Fire
            const size_t fireIdx = static_cast<size_t>(TouchControlId::Fire);
            if (buttons_[fireIdx].active && buttons_[fireIdx].finger == fid && cb.onLookDelta) {
                float sens = 0.65f * settings_.lookSensitivity;
                cb.onLookDelta(event.tfinger.dx * static_cast<float>(drawWidth) * sens, -event.tfinger.dy * static_cast<float>(drawHeight) * sens);
                return true;
            }
            if (hasLookFinger_ && fid == lookFinger_) {
                float dxNorm = x - lookStartX_;
                float dyNorm = y - lookStartY_;
                lookMaxDistSq_ = std::max(lookMaxDistSq_, dxNorm * dxNorm + dyNorm * dyNorm);
                if (cb.onLookDelta) {
                    float sens = 0.65f * settings_.lookSensitivity;
                    cb.onLookDelta(event.tfinger.dx * static_cast<float>(drawWidth) * sens, -event.tfinger.dy * static_cast<float>(drawHeight) * sens);
                }
                return true;
            }
            return true;
        }

        if (event.type == SDL_EVENT_FINGER_UP) {
            if (hasStickFinger_ && fid == stickFinger_) {
                hasStickFinger_ = false;
                return true;
            }
            if (hasAugTapFinger_ && fid == augTapFinger_) {
                if (augTapKey_ != 0 && cb.onKey) {
                    cb.onKey(augTapKey_, false);
                }
                hasAugTapFinger_ = false;
                augTapKey_ = 0;
                return true;
            }
            if (hasHudTapFinger_ && fid == hudTapFinger_) {
                if (hudTapKey_ != 0 && cb.onKey) {
                    cb.onKey(hudTapKey_, false);
                }
                hasHudTapFinger_ = false;
                hudTapKey_ = 0;
                return true;
            }
            for (size_t i = 0; i < kTouchControlCount; ++i) {
                if (i == static_cast<size_t>(TouchControlId::MoveStick)) {
                    continue;
                }
                if ((isActionToggle(static_cast<TouchControlId>(i)) || i == static_cast<size_t>(TouchControlId::AugsMenu))
                    && buttons_[i].fingerDown && buttons_[i].finger == fid) {
                    buttons_[i].fingerDown = false;
                    return true;
                }
                if (buttons_[i].active && buttons_[i].finger == fid) {
                    TouchControlId id = static_cast<TouchControlId>(i);
                    buttons_[i].fingerDown = false;
                    if (id == TouchControlId::Crouch && crouchPulseEndTicks_ != 0 && SDL_GetTicks() < crouchPulseEndTicks_) {
                        // Defer release until minimum pulse elapses so game's bToggleCrouch reliably sees the press.
                        return true;
                    }
                    setButtonPressed(id, false, cb);
                    return true;
                }
            }
            if (hasLookFinger_ && fid == lookFinger_) {
                hasLookFinger_ = false;
                if (lookCanQuickClick_ && (SDL_GetTicks() - lookDownTicks_) < 210 && lookMaxDistSq_ < (0.025f * 0.025f) && cb.onQuickRightClick) {
                    cb.onQuickRightClick();
                }
                lookCanQuickClick_ = false;
                return true;
            }
            return true;
        }
        return false;
    }

    void TouchOverlayManager::getKeypadExitRect(float fw, float fh, float kx, float ky, float kw, float kh, float& outX, float& outY, float& outW, float& outH) const
    {
        const float gap = std::clamp(fh * 0.022f, 8.0f, 20.0f);
        outW = std::max(kw, std::clamp(fw * 0.14f, 110.0f, 220.0f));
        outH = std::clamp(fh * 0.075f, 38.0f, 72.0f);
        outX = std::clamp(kx + (kw - outW) * 0.5f, 8.0f, std::max(8.0f, fw - outW - 8.0f));
        outY = ky + kh + gap;
        if (outY + outH > fh - 8.0f) {
            outY = std::max(8.0f, ky - gap - outH);
        }
    }

    bool TouchOverlayManager::handleKeypadExitTouch(const SDL_Event& event, int drawWidth, int drawHeight, float kx, float ky, float kw, float kh, const std::function<void()>& onClose)
    {
        if (!settings_.enabled || !visible_ || editorOpen_ || drawWidth <= 0 || drawHeight <= 0) {
            keypadExitDown_ = false;
            return false;
        }
        float bx = 0.0f;
        float by = 0.0f;
        float bw = 0.0f;
        float bh = 0.0f;
        getKeypadExitRect(static_cast<float>(drawWidth), static_cast<float>(drawHeight), kx, ky, kw, kh, bx, by, bw, bh);
        const float px = event.tfinger.x * static_cast<float>(drawWidth);
        const float py = event.tfinger.y * static_cast<float>(drawHeight);
        const float pad = 6.0f;
        const bool inBox = px >= bx - pad && px <= bx + bw + pad && py >= by - pad && py <= by + bh + pad;
        if (event.type == SDL_EVENT_FINGER_DOWN) {
            if (inBox) {
                keypadExitDown_ = true;
                keypadExitFinger_ = event.tfinger.fingerID;
                return true;
            }
            return false;
        }
        if (event.type == SDL_EVENT_FINGER_MOTION) {
            return keypadExitDown_ && event.tfinger.fingerID == keypadExitFinger_;
        }
        if (event.type == SDL_EVENT_FINGER_UP && keypadExitDown_ && event.tfinger.fingerID == keypadExitFinger_) {
            keypadExitDown_ = false;
            if (inBox && onClose) {
                onClose();
            }
            return true;
        }
        return false;
    }

    void TouchOverlayManager::renderKeypadExit(dx::Canvas2D& canvas, bool ptBr, float kx, float ky, float kw, float kh)
    {
        float bx = 0.0f;
        float by = 0.0f;
        float bw = 0.0f;
        float bh = 0.0f;
        getKeypadExitRect(canvas.width(), canvas.height(), kx, ky, kw, kh, bx, by, bw, bh);
        const dx::LinearColor fill = keypadExitDown_
            ? dx::LinearColor { 0.44f, 0.14f, 0.16f, 0.96f }
            : dx::LinearColor { 0.24f, 0.08f, 0.10f, 0.88f };
        const dx::LinearColor rim = keypadExitDown_
            ? dx::LinearColor { 1.00f, 0.65f, 0.65f, 1.00f }
            : dx::LinearColor { 0.95f, 0.45f, 0.45f, 0.92f };
        canvas.drawRect(bx, by, bw, bh, fill, dx::BlendStyle::Normal);
        drawRectOutline(canvas, bx, by, bw, bh, 2.0f, rim);
        drawText(
            canvas,
            ptBr ? "X SAIR" : "X EXIT",
            bx + bw * 0.5f,
            by + bh * 0.5f,
            std::max(2.0f, bh * 0.065f),
            { 0.98f, 0.96f, 0.96f, 1.00f },
            bw * 0.85f);
    }

    void TouchOverlayManager::render(dx::Canvas2D& canvas, bool ptBr, bool gameplayBlocked, bool showKeypadExit, float kx, float ky, float kw, float kh)
    {
        if (editorOpen_) {
            renderEditor(canvas, ptBr);
            return;
        }
        if (!settings_.enabled || !visible_) {
            keypadExitDown_ = false;
            return;
        }
        if (gameplayBlocked) {
            if (showKeypadExit) {
                renderKeypadExit(canvas, ptBr, kx, ky, kw, kh);
            } else {
                keypadExitDown_ = false;
            }
            return;
        }
        keypadExitDown_ = false;
        renderGameplayOverlays(canvas, ptBr);
    }

    void TouchOverlayManager::renderGameplayOverlays(dx::Canvas2D& canvas, bool ptBr)
    {
        const int w = static_cast<int>(canvas.width());
        const int h = static_cast<int>(canvas.height());
        const float fw = static_cast<float>(w);
        const float fh = static_cast<float>(h);
        const float alpha = std::clamp(settings_.opacity, 0.15f, 0.95f);

        struct CachedBtn {
            TouchControlId id = TouchControlId::MoveStick;
            float cx = 0.0f;
            float cy = 0.0f;
            float r = 0.0f;
            bool pressed = false;
        };
        std::array<CachedBtn, kTouchControlCount> cached {};
        size_t cachedCount = 0;

        // Pass 1: All triangle-based geometry (filled circles + bitmap text) -> 1 GL draw call.
        float stickKx = 0.0f;
        float stickKy = 0.0f;
        float stickR = 0.0f;
        float stickCx = 0.0f;
        float stickCy = 0.0f;
        bool drawStick = false;

        for (size_t i = 0; i < kTouchControlCount; ++i) {
            TouchControlId id = static_cast<TouchControlId>(i);
            if (!isControlContextVisible(id)) {
                continue;
            }
            float cx = layouts_[i].cx * fw;
            float cy = layouts_[i].cy * fh;
            float r = controlRadiusPx(id, h);

            if (id == TouchControlId::MoveStick) {
                drawStick = true;
                stickCx = cx;
                stickCy = cy;
                stickR = r;
                dx::LinearColor baseFill { kTheme.stickBaseRgb.r, kTheme.stickBaseRgb.g, kTheme.stickBaseRgb.b, alpha * 0.52f };
                dx::LinearColor knobFill { kTheme.stickKnobRgb.r, kTheme.stickKnobRgb.g, kTheme.stickKnobRgb.b, alpha * (hasStickFinger_ ? 0.80f : 0.52f) };
                drawFilledCircle(canvas, cx, cy, r, baseFill);

                stickKx = cx;
                stickKy = cy;
                if (hasStickFinger_) {
                    float maxOff = r * 0.62f;
                    float ox = (stickCurX_ - stickOriginX_) * fw;
                    float oy = (stickCurY_ - stickOriginY_) * fh;
                    float dist = std::hypot(ox, oy);
                    if (dist > maxOff && dist > 1e-3f) {
                        ox = (ox / dist) * maxOff;
                        oy = (oy / dist) * maxOff;
                    }
                    stickKx += ox;
                    stickKy += oy;
                }
                drawFilledCircle(canvas, stickKx, stickKy, r * 0.36f, knobFill);
                continue;
            }

            const bool smart = settings_.smartContext && ctx_.valid;
            const bool pressed = buttons_[i].active
                || (id == TouchControlId::Crouch && (smart ? (ctx_.isCrouching || crouchToggled_) : crouchToggled_))
                || (id == TouchControlId::AugsMenu && augDrawerOpen_)
                || (smart && id == TouchControlId::Scope && ctx_.isScoped)
                || (smart && id == TouchControlId::Laser && ctx_.isLaserOn)
                || (smart && id == TouchControlId::AugLight && ctx_.lightAugActive);
            cached[cachedCount++] = { id, cx, cy, r, pressed };

            const bool isFire = (id == TouchControlId::Fire || id == TouchControlId::Throw);
            const bool useHighlight = smart && id == TouchControlId::Use && displayFrobKind_ != TouchFrobKind::None;
            const bool useWarn = useHighlight && (displayFrobKind_ == TouchFrobKind::DoorLocked || displayFrobKind_ == TouchFrobKind::Disarm);
            const bool isDrop = (id == TouchControlId::Drop);
            const bool fireSpecialCyan = smart && id == TouchControlId::Fire && (ctx_.fireKind == TouchFireKind::Pick || ctx_.fireKind == TouchFireKind::Hack || ctx_.fireKind == TouchFireKind::UseItem);
            const bool fireSpecialRed = smart && id == TouchControlId::Fire && ctx_.fireKind == TouchFireKind::DetonateDrone;
            const bool reloadAlert = smart && id == TouchControlId::Reload && ctx_.lowOrEmptyClip;
            const bool laserActive = smart && id == TouchControlId::Laser && ctx_.isLaserOn;
            const bool augActiveGlow = smart && id == TouchControlId::AugsMenu && ctx_.anyAugActive && !augDrawerOpen_;

            dx::LinearColor baseFillRgb = isFire
                ? (pressed ? kTheme.firePressedRgb : kTheme.fireFillRgb)
                : (pressed ? kTheme.btnPressedRgb : kTheme.btnFillRgb);
            if (useHighlight && !pressed) {
                baseFillRgb = useWarn ? dx::LinearColor { 0.22f, 0.16f, 0.08f, 1.0f } : dx::LinearColor { 0.09f, 0.18f, 0.22f, 1.0f };
            } else if (isDrop) {
                baseFillRgb = pressed ? dx::LinearColor { 0.16f, 0.36f, 0.40f, 1.0f } : dx::LinearColor { 0.09f, 0.17f, 0.20f, 1.0f };
            } else if (fireSpecialCyan) {
                baseFillRgb = pressed ? dx::LinearColor { 0.18f, 0.38f, 0.44f, 1.0f } : dx::LinearColor { 0.10f, 0.18f, 0.22f, 1.0f };
            } else if (fireSpecialRed) {
                baseFillRgb = pressed ? dx::LinearColor { 0.56f, 0.16f, 0.16f, 1.0f } : dx::LinearColor { 0.28f, 0.10f, 0.10f, 1.0f };
            } else if (reloadAlert && !pressed) {
                baseFillRgb = { 0.24f, 0.14f, 0.09f, 1.0f };
            } else if (laserActive) {
                baseFillRgb = { 0.30f, 0.12f, 0.12f, 1.0f };
            } else if (augActiveGlow) {
                baseFillRgb = { 0.20f, 0.18f, 0.10f, 1.0f };
            }

            dx::LinearColor fill {
                baseFillRgb.r, baseFillRgb.g, baseFillRgb.b,
                pressed ? std::min(0.92f, alpha * 1.15f) : std::min(0.84f, alpha * (useHighlight ? 0.86f : 0.72f))
            };
            const dx::LinearColor& baseTextRgb = pressed ? kTheme.textPressedRgb : kTheme.textRgb;
            dx::LinearColor fg {
                baseTextRgb.r, baseTextRgb.g, baseTextRgb.b,
                (pressed || useHighlight) ? 1.00f : std::min(1.00f, alpha + 0.38f)
            };

            drawFilledCircle(canvas, cx, cy, r, fill);

            const bool hasIcon = (id == TouchControlId::Fire || id == TouchControlId::Jump || id == TouchControlId::Crouch ||
                                  id == TouchControlId::Scope || id == TouchControlId::Menu);
            float textScale = std::max(2.0f, r * 0.085f);
            float textY = hasIcon ? (cy + r * 0.22f) : cy;
            drawText(canvas, dynamicControlLabel(id, ptBr), cx, textY, textScale, fg, hasIcon ? (r * 1.36f) : (r * 1.48f));
        }

        // Pass 2: All line-based geometry (circle outlines + stick notches + icon accents) -> 1 GL draw call.
        if (drawStick) {
            dx::LinearColor ringCol { kTheme.stickRimRgb.r, kTheme.stickRimRgb.g, kTheme.stickRimRgb.b, std::min(0.92f, alpha + 0.22f) };
            dx::LinearColor knobRing { 0.96f, 0.97f, 0.98f, std::min(0.96f, alpha + 0.32f) };
            drawCircleOutline(canvas, stickCx, stickCy, stickR, 2.0f, ringCol);
            canvas.drawLine(stickCx - stickR * 0.22f, stickCy, stickCx + stickR * 0.22f, stickCy, { kTheme.stickRimRgb.r, kTheme.stickRimRgb.g, kTheme.stickRimRgb.b, alpha * 0.35f });
            canvas.drawLine(stickCx, stickCy - stickR * 0.22f, stickCx, stickCy + stickR * 0.22f, { kTheme.stickRimRgb.r, kTheme.stickRimRgb.g, kTheme.stickRimRgb.b, alpha * 0.35f });
            drawCircleOutline(canvas, stickKx, stickKy, stickR * 0.36f, 1.5f, knobRing);
        }

        const bool smart = settings_.smartContext && ctx_.valid;
        for (size_t idx = 0; idx < cachedCount; ++idx) {
            const CachedBtn& b = cached[idx];
            const bool isFire = (b.id == TouchControlId::Fire || b.id == TouchControlId::Throw);
            const bool useHighlight = smart && b.id == TouchControlId::Use && displayFrobKind_ != TouchFrobKind::None;
            const bool useWarn = useHighlight && (displayFrobKind_ == TouchFrobKind::DoorLocked || displayFrobKind_ == TouchFrobKind::Disarm);
            const bool isDrop = (b.id == TouchControlId::Drop);
            const bool fireSpecialCyan = smart && b.id == TouchControlId::Fire && (ctx_.fireKind == TouchFireKind::Pick || ctx_.fireKind == TouchFireKind::Hack || ctx_.fireKind == TouchFireKind::UseItem);
            const bool fireSpecialRed = smart && b.id == TouchControlId::Fire && ctx_.fireKind == TouchFireKind::DetonateDrone;
            const bool reloadAlert = smart && b.id == TouchControlId::Reload && ctx_.lowOrEmptyClip;
            const bool laserActive = smart && b.id == TouchControlId::Laser && ctx_.isLaserOn;
            const bool scopeActive = smart && b.id == TouchControlId::Scope && ctx_.isScoped;
            const bool augActiveGlow = smart && b.id == TouchControlId::AugsMenu && ctx_.anyAugActive && !augDrawerOpen_;

            dx::LinearColor baseRimRgb = isFire
                ? (b.pressed ? kTheme.fireRimPressedRgb : kTheme.fireRimRgb)
                : (b.pressed ? kTheme.btnRimPressedRgb : kTheme.btnRimRgb);
            if (useHighlight) {
                baseRimRgb = useWarn ? dx::LinearColor { 0.98f, 0.78f, 0.32f, 1.0f } : dx::LinearColor { 0.42f, 0.90f, 1.00f, 1.0f };
            } else if (isDrop) {
                baseRimRgb = b.pressed ? dx::LinearColor { 0.75f, 0.98f, 1.00f, 1.0f } : dx::LinearColor { 0.46f, 0.88f, 0.94f, 1.0f };
            } else if (fireSpecialCyan) {
                baseRimRgb = b.pressed ? dx::LinearColor { 0.78f, 0.98f, 1.00f, 1.0f } : dx::LinearColor { 0.44f, 0.88f, 0.98f, 1.0f };
            } else if (fireSpecialRed) {
                baseRimRgb = { 1.00f, 0.42f, 0.42f, 1.0f };
            } else if (reloadAlert) {
                baseRimRgb = { 0.98f, 0.64f, 0.28f, 1.0f };
            } else if (laserActive) {
                baseRimRgb = { 1.00f, 0.44f, 0.44f, 1.0f };
            } else if (scopeActive || augActiveGlow) {
                baseRimRgb = { 1.00f, 0.88f, 0.52f, 1.0f };
            }

            const bool boldRim = b.pressed || useHighlight || reloadAlert || laserActive || scopeActive || augActiveGlow;
            dx::LinearColor rim {
                baseRimRgb.r, baseRimRgb.g, baseRimRgb.b,
                boldRim ? 0.96f : std::min(0.94f, alpha + 0.28f)
            };
            const dx::LinearColor& baseTextRgb = b.pressed ? kTheme.textPressedRgb : kTheme.textRgb;
            dx::LinearColor fg {
                baseTextRgb.r, baseTextRgb.g, baseTextRgb.b,
                (b.pressed || useHighlight) ? 1.00f : std::min(1.00f, alpha + 0.38f)
            };

            drawCircleOutline(canvas, b.cx, b.cy, b.r, boldRim ? 2.2f : 1.5f, rim);
            drawIconAccent(canvas, b.id, b.cx, b.cy, b.r, fg);
        }

        if (augDrawerOpen_) {
            renderAugDrawer(canvas, ptBr);
        }
    }

    namespace {
        struct AugDrawerMetrics {
            int cols = 0;
            float startX = 0.0f;
            float startY = 0.0f;
            float chipW = 0.0f;
            float chipH = 0.0f;
            float gapX = 0.0f;
            float gapY = 0.0f;
        };

        AugDrawerMetrics computeAugDrawerMetrics(size_t count, float fw, float fh)
        {
            AugDrawerMetrics m;
            if (count == 0) {
                return m;
            }
            m.cols = static_cast<int>(std::min<size_t>(count, 6));
            m.chipW = std::clamp(fw * 0.125f, 110.0f, 180.0f);
            m.chipH = std::clamp(fh * 0.068f, 44.0f, 68.0f);
            m.gapX = fw * 0.008f;
            m.gapY = fh * 0.010f;
            float totalW = static_cast<float>(m.cols) * m.chipW + static_cast<float>(m.cols - 1) * m.gapX;
            m.startX = (fw - totalW) * 0.5f;
            m.startY = fh * 0.145f;
            return m;
        }
    }

    int TouchOverlayManager::hitTestAugDrawer(float xNorm, float yNorm, int drawWidth, int drawHeight) const
    {
        if (!augDrawerOpen_ || augEntries_.empty() || drawWidth <= 0 || drawHeight <= 0) {
            return -1;
        }
        const float fw = static_cast<float>(drawWidth);
        const float fh = static_cast<float>(drawHeight);
        const float px = xNorm * fw;
        const float py = yNorm * fh;
        AugDrawerMetrics m = computeAugDrawerMetrics(augEntries_.size(), fw, fh);
        for (size_t i = 0; i < augEntries_.size(); ++i) {
            int col = static_cast<int>(i) % m.cols;
            int row = static_cast<int>(i) / m.cols;
            float x0 = m.startX + static_cast<float>(col) * (m.chipW + m.gapX);
            float y0 = m.startY + static_cast<float>(row) * (m.chipH + m.gapY);
            if (px >= x0 && px < x0 + m.chipW && py >= y0 && py < y0 + m.chipH) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void TouchOverlayManager::renderAugDrawer(dx::Canvas2D& canvas, bool ptBr)
    {
        const float fw = canvas.width();
        const float fh = canvas.height();
        const float alpha = std::clamp(settings_.opacity, 0.25f, 0.95f);

        if (augEntries_.empty()) {
            const float boxW = std::clamp(fw * 0.26f, 220.0f, 360.0f);
            const float boxH = std::clamp(fh * 0.065f, 42.0f, 64.0f);
            const float x0 = (fw - boxW) * 0.5f;
            const float y0 = fh * 0.145f;
            canvas.drawRect(x0, y0, boxW, boxH, { 0.10f, 0.11f, 0.13f, alpha * 0.86f }, dx::BlendStyle::Normal);
            drawRectOutline(canvas, x0, y0, boxW, boxH, 1.5f, { 0.82f, 0.84f, 0.87f, 0.85f });
            drawText(canvas, ptBr ? "SEM AUGS ATIVOS" : "NO ACTIVE AUGS", x0 + boxW * 0.5f, y0 + boxH * 0.5f,
                     std::max(2.0f, boxH * 0.055f), { 0.94f, 0.95f, 0.97f, 0.95f }, boxW * 0.88f);
            return;
        }

        AugDrawerMetrics m = computeAugDrawerMetrics(augEntries_.size(), fw, fh);
        const int rows = static_cast<int>((augEntries_.size() + static_cast<size_t>(m.cols) - 1) / static_cast<size_t>(m.cols));
        const float pad = 8.0f;
        const float panelW = static_cast<float>(m.cols) * m.chipW + static_cast<float>(m.cols - 1) * m.gapX + pad * 2.0f;
        const float panelH = static_cast<float>(rows) * m.chipH + static_cast<float>(rows - 1) * m.gapY + pad * 2.0f;
        canvas.drawRect(m.startX - pad, m.startY - pad, panelW, panelH, { 0.08f, 0.09f, 0.11f, std::min(0.88f, alpha + 0.18f) }, dx::BlendStyle::Normal);
        drawRectOutline(canvas, m.startX - pad, m.startY - pad, panelW, panelH, 1.5f, { 0.78f, 0.80f, 0.84f, 0.72f });

        for (size_t i = 0; i < augEntries_.size(); ++i) {
            const TouchAugEntry& entry = augEntries_[i];
            int col = static_cast<int>(i) % m.cols;
            int row = static_cast<int>(i) / m.cols;
            float x0 = m.startX + static_cast<float>(col) * (m.chipW + m.gapX);
            float y0 = m.startY + static_cast<float>(row) * (m.chipH + m.gapY);

            dx::LinearColor fill = entry.active
                ? dx::LinearColor { 0.34f, 0.37f, 0.42f, std::min(0.95f, alpha + 0.28f) }
                : dx::LinearColor { 0.12f, 0.13f, 0.15f, std::min(0.88f, alpha + 0.12f) };
            dx::LinearColor rim = entry.active
                ? dx::LinearColor { 1.00f, 0.88f, 0.56f, 0.98f }
                : dx::LinearColor { 0.78f, 0.80f, 0.84f, 0.75f };
            dx::LinearColor fg = entry.active
                ? dx::LinearColor { 1.00f, 1.00f, 1.00f, 1.00f }
                : dx::LinearColor { 0.92f, 0.93f, 0.95f, 0.94f };

            canvas.drawRect(x0, y0, m.chipW, m.chipH, fill, dx::BlendStyle::Normal);
            drawRectOutline(canvas, x0, y0, m.chipW, m.chipH, entry.active ? 2.5f : 1.5f, rim);

            char hotkeyBuf[8];
            std::snprintf(hotkeyBuf, sizeof(hotkeyBuf), "F%d", entry.hotKeyNum);
            float scaleKey = std::max(1.8f, m.chipH * 0.045f);
            float scaleName = std::max(1.8f, m.chipH * 0.050f);
            drawText(canvas, hotkeyBuf, x0 + m.chipW * 0.5f, y0 + m.chipH * 0.28f, scaleKey, rim, m.chipW * 0.85f);
            drawText(canvas, entry.label.c_str(), x0 + m.chipW * 0.5f, y0 + m.chipH * 0.68f, scaleName, fg, m.chipW * 0.88f);
        }
    }

    void TouchOverlayManager::renderEditor(dx::Canvas2D& canvas, bool ptBr)
    {
        const int w = static_cast<int>(canvas.width());
        const int h = static_cast<int>(canvas.height());
        const float fw = static_cast<float>(w);
        const float fh = static_cast<float>(h);

        // DXLauncher-style dark navy-slate backdrop + blueprint grid
        canvas.drawRect(0.0f, 0.0f, fw, fh, { 0.040f, 0.048f, 0.085f, 0.72f }, dx::BlendStyle::Normal);
        for (int gx = 1; gx < 16; ++gx) {
            float lx = fw * (static_cast<float>(gx) / 16.0f);
            dx::LinearColor gCol = (gx == 8)
                ? dx::LinearColor { 0.45f, 0.54f, 0.88f, 0.28f }
                : ((gx % 4 == 0) ? dx::LinearColor { 0.36f, 0.44f, 0.70f, 0.16f } : dx::LinearColor { 0.30f, 0.36f, 0.56f, 0.08f });
            canvas.drawLine(lx, 0.0f, lx, fh, gCol);
        }
        for (int gy = 1; gy < 10; ++gy) {
            float ly = fh * (static_cast<float>(gy) / 10.0f);
            dx::LinearColor gCol = (gy == 5)
                ? dx::LinearColor { 0.45f, 0.54f, 0.88f, 0.28f }
                : dx::LinearColor { 0.30f, 0.36f, 0.56f, 0.10f };
            canvas.drawLine(0.0f, ly, fw, ly, gCol);
        }

        // Subtle Deus Ex HUD safe-zone reference boxes so controls don't overlap HUD elements
        auto drawHudZone = [&](float rx, float ry, float rw, float rh, const char* tag) {
            canvas.drawRect(rx, ry, rw, rh, { 0.20f, 0.26f, 0.44f, 0.12f }, dx::BlendStyle::Normal);
            drawRectOutline(canvas, rx, ry, rw, rh, 1.0f, { 0.46f, 0.54f, 0.80f, 0.28f });
            drawText(canvas, tag, rx + rw * 0.5f, ry + rh * 0.5f, std::max(1.5f, fh * 0.0017f), { 0.62f, 0.70f, 0.92f, 0.42f }, rw * 0.90f);
        };
        drawHudZone(fw * 0.005f, fh * 0.010f, fw * 0.095f, fh * 0.305f, "HUD");
        drawHudZone(fw * 0.935f, fh * 0.010f, fw * 0.060f, fh * 0.115f, "AUG");
        drawHudZone(fw * 0.365f, fh * 0.865f, fw * 0.620f, fh * 0.125f, "HOTBAR");

        // Live alignment guides when dragging a control
        if (editorDragging_ && selectedControl_ >= 0 && selectedControl_ < static_cast<int>(kTouchControlCount)) {
            const TouchControlLayout& cur = layouts_[static_cast<size_t>(selectedControl_)];
            const float sx = cur.cx * fw;
            const float sy = cur.cy * fh;
            const dx::LinearColor guideCol { 0.52f, 0.66f, 1.00f, 0.55f };
            canvas.drawLine(sx, 0.0f, sx, fh, guideCol);
            canvas.drawLine(0.0f, sy, fw, sy, guideCol);
        }

        // Draw all controls in WYSIWYG dark-gray/white style so preview matches gameplay
        for (size_t i = 0; i < kTouchControlCount; ++i) {
            TouchControlId id = static_cast<TouchControlId>(i);
            const TouchControlLayout& layout = layouts_[i];
            const float cx = layout.cx * fw;
            const float cy = layout.cy * fh;
            const float r = controlRadiusPx(id, h);
            const bool isSel = (static_cast<int>(i) == selectedControl_);
            const bool isFire = (id == TouchControlId::Fire);

            dx::LinearColor fill = layout.enabled
                ? (isFire ? dx::LinearColor { kTheme.fireFillRgb.r, kTheme.fireFillRgb.g, kTheme.fireFillRgb.b, 0.78f }
                          : dx::LinearColor { kTheme.btnFillRgb.r, kTheme.btnFillRgb.g, kTheme.btnFillRgb.b, 0.78f })
                : dx::LinearColor { 0.09f, 0.10f, 0.13f, 0.38f };
            dx::LinearColor rim = isSel
                ? dx::LinearColor { 0.68f, 0.78f, 1.00f, 1.00f }
                : (layout.enabled
                    ? (isFire ? dx::LinearColor { kTheme.fireRimRgb.r, kTheme.fireRimRgb.g, kTheme.fireRimRgb.b, 0.92f }
                              : dx::LinearColor { kTheme.btnRimRgb.r, kTheme.btnRimRgb.g, kTheme.btnRimRgb.b, 0.82f })
                    : dx::LinearColor { 0.45f, 0.48f, 0.56f, 0.48f });
            dx::LinearColor fg = layout.enabled
                ? dx::LinearColor { 0.95f, 0.96f, 0.99f, 1.00f }
                : dx::LinearColor { 0.66f, 0.69f, 0.76f, 0.62f };

            drawFilledCircle(canvas, cx, cy, r, fill);
            if (id == TouchControlId::MoveStick) {
                drawFilledCircle(canvas, cx, cy, r * 0.36f, { kTheme.stickKnobRgb.r, kTheme.stickKnobRgb.g, kTheme.stickKnobRgb.b, layout.enabled ? 0.50f : 0.22f });
                drawCircleOutline(canvas, cx, cy, r * 0.36f, 1.5f, rim);
            }
            drawCircleOutline(canvas, cx, cy, r, isSel ? 2.5f : 1.5f, rim);

            if (isSel) {
                const float pad = 7.0f;
                const float arm = std::clamp(r * 0.34f, 10.0f, 22.0f);
                drawCornerBrackets(canvas, cx - r - pad, cy - r - pad, cx + r + pad, cy + r + pad, arm, 2.5f, { 0.92f, 0.95f, 1.00f, 0.98f });
            }

            const bool hasIcon = (id == TouchControlId::Fire || id == TouchControlId::Jump || id == TouchControlId::Crouch ||
                                  id == TouchControlId::Scope || id == TouchControlId::Menu);
            if (layout.enabled && hasIcon) {
                drawIconAccent(canvas, id, cx, cy, r, fg);
            }
            float textScale = (id == TouchControlId::MoveStick)
                ? std::clamp(r * 0.036f, 2.0f, fh * 0.0036f)
                : std::max(2.0f, r * 0.082f);
            float textY = (id == TouchControlId::MoveStick)
                ? (cy + r * 0.56f)
                : (layout.enabled ? (hasIcon ? (cy + r * 0.22f) : cy) : (cy - r * 0.16f));
            drawText(canvas, controlLabel(id, ptBr), cx, textY, textScale, fg, hasIcon ? (r * 1.36f) : (r * 1.48f));
            if (!layout.enabled) {
                drawText(canvas, "OFF", cx, cy + r * 0.26f, std::max(1.8f, textScale * 0.80f), { 0.92f, 0.44f, 0.48f, 0.88f }, r * 1.30f);
            } else if (settings_.smartContext && isContextualControl(id)) {
                drawText(canvas, "AUTO", cx, cy + r * 0.54f, std::max(1.5f, textScale * 0.65f), { 0.48f, 0.86f, 0.98f, 0.90f }, r * 1.20f);
            }
        }

        // DXLauncher-style floating toolbar panel
        const EditorToolbarLayout tb = computeEditorToolbarLayout(w, h, toolbarAtBottom_);

        // Soft drop shadow + main card
        canvas.drawRect(tb.panel.x + 4.0f, tb.panel.y + 5.0f, tb.panel.w, tb.panel.h, { 0.00f, 0.00f, 0.02f, 0.52f }, dx::BlendStyle::Normal);
        canvas.drawRect(tb.panel.x, tb.panel.y, tb.panel.w, tb.panel.h, { 0.075f, 0.088f, 0.135f, 0.96f }, dx::BlendStyle::Normal);

        // DXLauncher signature indigo header bar + top accent line
        canvas.drawRect(tb.header.x, tb.header.y, tb.header.w, tb.header.h, { 0.185f, 0.220f, 0.435f, 0.98f }, dx::BlendStyle::Normal);
        canvas.drawRect(tb.header.x, tb.header.y, tb.header.w, 2.5f, { 0.48f, 0.56f, 0.92f, 0.95f }, dx::BlendStyle::Normal);
        canvas.drawLine(tb.header.x, tb.header.y + tb.header.h, tb.header.x + tb.header.w, tb.header.y + tb.header.h, { 0.42f, 0.49f, 0.74f, 0.80f });
        drawRectOutline(canvas, tb.panel.x, tb.panel.y, tb.panel.w, tb.panel.h, 1.5f, { 0.40f, 0.47f, 0.68f, 0.92f });

        // Header info: selected control name, scale %, visibility status
        const TouchControlLayout& sel = layouts_[static_cast<size_t>(selectedControl_)];
        const int pct = static_cast<int>(std::round(sel.scale * 100.0f));
        char titleBuf[96];
        std::snprintf(
            titleBuf,
            sizeof(titleBuf),
            "%s: %s  (%d%%)  %s",
            ptBr ? "EDITOR" : "EDITOR",
            controlLabel(static_cast<TouchControlId>(selectedControl_), ptBr),
            pct,
            sel.enabled ? (ptBr ? "ATIVO" : "ON") : (ptBr ? "OCULTO" : "OFF"));

        const float headerTextScale = std::max(1.9f, fh * 0.0023f);
        const float titleAreaW = (tb.gridBtn.x - 12.0f) - (tb.header.x + 14.0f);
        drawText(
            canvas,
            titleBuf,
            tb.header.x + 14.0f + titleAreaW * 0.5f,
            tb.header.y + tb.header.h * 0.5f,
            headerTextScale,
            { 0.95f, 0.97f, 1.00f, 1.00f },
            titleAreaW);

        // Header right pills: GRID snap toggle + BAR position flip
        auto drawHeaderPill = [&](const UiRect& r, const char* label, bool active, bool pressed) {
            dx::LinearColor fill = pressed
                ? dx::LinearColor { 0.36f, 0.44f, 0.74f, 0.98f }
                : (active ? dx::LinearColor { 0.24f, 0.30f, 0.56f, 0.95f } : dx::LinearColor { 0.11f, 0.13f, 0.22f, 0.92f });
            dx::LinearColor rim = active
                ? dx::LinearColor { 0.66f, 0.76f, 1.00f, 0.95f }
                : dx::LinearColor { 0.42f, 0.48f, 0.66f, 0.78f };
            canvas.drawRect(r.x, r.y, r.w, r.h, fill, dx::BlendStyle::Normal);
            drawRectOutline(canvas, r.x, r.y, r.w, r.h, 1.5f, rim);
            drawText(canvas, label, r.x + r.w * 0.5f, r.y + r.h * 0.5f, std::max(1.7f, fh * 0.0019f), { 0.95f, 0.97f, 1.00f, 1.00f }, r.w * 0.88f);
        };
        drawHeaderPill(
            tb.gridBtn,
            ptBr ? (editorSnapGrid_ ? "GRADE: ON" : "GRADE: OFF") : (editorSnapGrid_ ? "GRID: ON" : "GRID: OFF"),
            editorSnapGrid_,
            editorPendingTool_ == 101);
        drawHeaderPill(
            tb.flipBtn,
            ptBr ? (toolbarAtBottom_ ? "^ TOPO" : "v BASE") : (toolbarAtBottom_ ? "^ TOP" : "v BOT"),
            false,
            editorPendingTool_ == 100);

        // Subtle group dividers in the action row
        const float divY0 = tb.tools[0].y + 4.0f;
        const float divY1 = tb.tools[0].y + tb.tools[0].h - 4.0f;
        const dx::LinearColor divCol { 0.36f, 0.42f, 0.62f, 0.48f };
        canvas.drawLine(tb.divider1X, divY0, tb.divider1X, divY1, divCol);
        canvas.drawLine(tb.divider2X, divY0, tb.divider2X, divY1, divCol);

        // Action buttons
        char presetLabel[32];
        if (ptBr) {
            std::snprintf(presetLabel, sizeof(presetLabel), "MODO: %s", activePreset_ == 0 ? "PADRAO" : (activePreset_ == 1 ? "MINIMO" : "GARRA"));
        } else {
            std::snprintf(presetLabel, sizeof(presetLabel), "MODE: %s", activePreset_ == 0 ? "DEFAULT" : (activePreset_ == 1 ? "MINIMAL" : "CLAW"));
        }

        const char* labelsPt[EditorToolbarLayout::kToolCount] = {
            "TAM -",
            "TAM +",
            sel.enabled ? "EXIBIR: ON" : "EXIBIR: OFF",
            presetLabel,
            "PADRAO",
            "CANCELAR",
            "SALVAR",
        };
        const char* labelsEn[EditorToolbarLayout::kToolCount] = {
            "SIZE -",
            "SIZE +",
            sel.enabled ? "SHOW: ON" : "SHOW: OFF",
            presetLabel,
            "RESET",
            "CANCEL",
            "SAVE",
        };

        const float btnTextScale = std::max(1.8f, fh * 0.0021f);
        for (int i = 0; i < EditorToolbarLayout::kToolCount; ++i) {
            const UiRect& br = tb.tools[static_cast<size_t>(i)];
            const bool isPressed = (editorPendingTool_ == i);

            // Default DXLauncher slate button
            dx::LinearColor bFill { 0.135f, 0.155f, 0.225f, 0.95f };
            dx::LinearColor bRim { 0.42f, 0.48f, 0.68f, 0.85f };
            dx::LinearColor bText { 0.94f, 0.96f, 1.00f, 1.00f };

            if (i == 2) {
                if (sel.enabled) {
                    bFill = { 0.11f, 0.24f, 0.22f, 0.95f };
                    bRim = { 0.36f, 0.80f, 0.64f, 0.92f };
                } else {
                    bFill = { 0.24f, 0.12f, 0.15f, 0.95f };
                    bRim = { 0.86f, 0.42f, 0.46f, 0.92f };
                }
            } else if (i == 3) {
                bFill = { 0.16f, 0.19f, 0.33f, 0.96f };
                bRim = { 0.52f, 0.60f, 0.92f, 0.92f };
            } else if (i == 5) {
                bFill = { 0.26f, 0.11f, 0.14f, 0.95f };
                bRim = { 0.88f, 0.40f, 0.44f, 0.92f };
            } else if (i == 6) {
                bFill = { 0.12f, 0.33f, 0.24f, 0.98f };
                bRim = { 0.40f, 0.92f, 0.64f, 1.00f };
            }

            if (isPressed) {
                bFill.r = std::min(1.0f, bFill.r + 0.14f);
                bFill.g = std::min(1.0f, bFill.g + 0.14f);
                bFill.b = std::min(1.0f, bFill.b + 0.14f);
                bRim = { 1.00f, 1.00f, 1.00f, 1.00f };
            }

            canvas.drawRect(br.x, br.y, br.w, br.h, bFill, dx::BlendStyle::Normal);
            // Subtle DXLauncher top bevel highlight
            canvas.drawLine(br.x + 1.0f, br.y + 1.0f, br.x + br.w - 1.0f, br.y + 1.0f, { 1.0f, 1.0f, 1.0f, 0.12f });
            drawRectOutline(canvas, br.x, br.y, br.w, br.h, isPressed ? 2.0f : 1.5f, bRim);
            drawText(canvas, ptBr ? labelsPt[i] : labelsEn[i], br.x + br.w * 0.5f, br.y + br.h * 0.5f, btnTextScale, bText, br.w * 0.86f);
        }
    }
} // namespace game
