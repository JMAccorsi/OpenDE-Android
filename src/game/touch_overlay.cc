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

        constexpr std::array<ControlSpec, kTouchControlCount> kSpecs {{
            { "MoveStick", "MOVE", "MOVER", dx::IK_None, true, 0.135f, 0.695f, 1.00f, 0.145f },
            { "Fire", "FIRE", "ATIRAR", dx::IK_LeftMouse, true, 0.875f, 0.625f, 1.15f, 0.072f },
            { "Use", "USE", "USAR", dx::IK_RightMouse, true, 0.745f, 0.565f, 0.95f, 0.062f },
            { "Jump", "JUMP", "PULAR", dx::IK_Space, true, 0.885f, 0.420f, 0.92f, 0.058f },
            { "Crouch", "DUCK", "AGACH.", kKeyX, true, 0.755f, 0.755f, 0.88f, 0.056f },
            { "Reload", "REL.", "RECAR.", dx::IK_Semicolon, true, 0.640f, 0.720f, 0.85f, 0.052f },
            { "Scope", "AIM", "MIRA", dx::IK_LeftBracket, true, 0.770f, 0.355f, 0.85f, 0.052f },
            { "LeanLeft", "LN L", "ESQ.", kKeyQ, true, 0.070f, 0.425f, 0.82f, 0.048f },
            { "LeanRight", "LN R", "DIR.", kKeyE, true, 0.195f, 0.425f, 0.82f, 0.048f },
            { "Inventory", "INV", "INV.", kKeyI, true, 0.130f, 0.075f, 0.78f, 0.046f },
            { "Goals", "GOALS", "OBJ.", kKeyG, true, 0.220f, 0.075f, 0.78f, 0.046f },
            { "Menu", "MENU", "MENU", dx::IK_Escape, true, 0.500f, 0.060f, 0.82f, 0.046f },
            { "BeltPrev", "<BELT", "<CINTO", dx::IK_MouseWheelUp, false, 0.345f, 0.850f, 0.80f, 0.046f },
            { "BeltNext", "BELT>", "CINTO>", dx::IK_MouseWheelDown, false, 0.405f, 0.850f, 0.80f, 0.046f },
            { "SwitchAmmo", "AMMO", "MUN.", dx::IK_SingleQuote, false, 0.640f, 0.565f, 0.82f, 0.048f },
            { "Laser", "LASER", "LASER", dx::IK_RightBracket, false, 0.885f, 0.235f, 0.78f, 0.046f },
            { "AugLight", "LIGHT", "LUZ", kKeyF12, false, 0.310f, 0.075f, 0.78f, 0.046f },
            { "QuickSave", "Q.SAVE", "SALVAR", dx::IK_GreyPlus, true, 0.605f, 0.060f, 0.78f, 0.046f },
            { "QuickLoad", "Q.LOAD", "CARREG.", dx::IK_GreySlash, true, 0.695f, 0.060f, 0.78f, 0.046f },
            { "AugsMenu", "AUGS", "AUGS", dx::IK_None, true, 0.310f, 0.075f, 0.78f, 0.046f },
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
            constexpr int kSlices = 14;
            const float r2 = radius * radius;
            float prevY = -radius;
            for (int i = -kSlices + 1; i <= kSlices; ++i) {
                float nextY = (static_cast<float>(i) / static_cast<float>(kSlices)) * radius;
                if (i < kSlices) {
                    nextY = std::round(cy + nextY) - cy;
                }
                float h = nextY - prevY;
                if (h > 0.0f) {
                    float ym = 0.5f * (prevY + nextY);
                    float halfW = std::sqrt(std::max(0.0f, r2 - ym * ym));
                    if (halfW > 0.5f) {
                        canvas.drawRect(cx - halfW, cy + prevY, halfW * 2.0f, h, color, dx::BlendStyle::Normal);
                    }
                }
                prevY = nextY;
            }
        }

        void drawCircleOutline(dx::Canvas2D& canvas, float cx, float cy, float radius, float thickness, const dx::LinearColor& color)
        {
            if (radius <= 1.0f || color.a <= 0.005f) {
                return;
            }
            constexpr int kSegs = 28;
            const float twoPi = 6.2831853f;
            int rings = std::max(1, static_cast<int>(std::round(thickness)));
            for (int ring = 0; ring < rings; ++ring) {
                float r = radius - static_cast<float>(ring);
                if (r <= 1.0f) {
                    break;
                }
                for (int i = 0; i < kSegs; ++i) {
                    float a0 = (static_cast<float>(i) / static_cast<float>(kSegs)) * twoPi;
                    float a1 = (static_cast<float>(i + 1) / static_cast<float>(kSegs)) * twoPi;
                    canvas.drawLine(
                        cx + std::cos(a0) * r,
                        cy + std::sin(a0) * r,
                        cx + std::cos(a1) * r,
                        cy + std::sin(a1) * r,
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
        // Upgrade from older layout where QuickSave defaulted to disabled.
        if (!dx::config::system().get(kTouchLayoutSection, kSpecs[static_cast<size_t>(TouchControlId::QuickLoad)].iniKey)) {
            layouts_[static_cast<size_t>(TouchControlId::QuickSave)].enabled = true;
        }
    }

    void TouchOverlayManager::saveLayout() const
    {
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
            // Minimalist: Move, Fire, Use, Jump, Crouch, Menu
            for (size_t i = 0; i < kTouchControlCount; ++i) {
                TouchControlId id = static_cast<TouchControlId>(i);
                bool keep = (id == TouchControlId::MoveStick || id == TouchControlId::Fire ||
                             id == TouchControlId::Use || id == TouchControlId::Jump ||
                             id == TouchControlId::Crouch || id == TouchControlId::Menu);
                layouts_[i].enabled = keep;
            }
            return;
        }
        // Preset 2: Claw / Full tactical layout (higher leans/scope + belt/light/ammo enabled)
        for (size_t i = 0; i < kTouchControlCount; ++i) {
            TouchControlId id = static_cast<TouchControlId>(i);
            layouts_[i].enabled = (id != TouchControlId::Laser && id != TouchControlId::AugLight);
        }
        layouts_[static_cast<size_t>(TouchControlId::Scope)].cx = 0.720f;
        layouts_[static_cast<size_t>(TouchControlId::Scope)].cy = 0.240f;
        layouts_[static_cast<size_t>(TouchControlId::LeanLeft)].cx = 0.080f;
        layouts_[static_cast<size_t>(TouchControlId::LeanLeft)].cy = 0.360f;
        layouts_[static_cast<size_t>(TouchControlId::LeanRight)].cx = 0.195f;
        layouts_[static_cast<size_t>(TouchControlId::LeanRight)].cy = 0.360f;
        layouts_[static_cast<size_t>(TouchControlId::SwitchAmmo)].enabled = true;
        layouts_[static_cast<size_t>(TouchControlId::BeltPrev)].enabled = true;
        layouts_[static_cast<size_t>(TouchControlId::BeltNext)].enabled = true;
        layouts_[static_cast<size_t>(TouchControlId::AugsMenu)].enabled = true;
        layouts_[static_cast<size_t>(TouchControlId::QuickSave)].enabled = true;
        layouts_[static_cast<size_t>(TouchControlId::QuickLoad)].enabled = true;
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

    bool TouchOverlayManager::isActionToggle(TouchControlId id) const
    {
        return id == TouchControlId::Crouch;
    }

    int TouchOverlayManager::hitTestControl(float xNorm, float yNorm, int drawWidth, int drawHeight, bool includeDisabled) const
    {
        float px = xNorm * static_cast<float>(drawWidth);
        float py = yNorm * static_cast<float>(drawHeight);
        int bestIdx = -1;
        float bestDistSq = 1e12f;
        for (size_t i = 0; i < kTouchControlCount; ++i) {
            if (!includeDisabled && !layouts_[i].enabled) {
                continue;
            }
            TouchControlId id = static_cast<TouchControlId>(i);
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

    void TouchOverlayManager::getToolbarRect(int width, int height, float& outX, float& outY, float& outW, float& outH) const
    {
        float w = static_cast<float>(width);
        float h = static_cast<float>(height);
        outW = std::min(w * 0.78f, h * 1.40f);
        outH = std::max(120.0f, h * 0.175f);
        outX = (w - outW) * 0.5f;
        outY = toolbarAtBottom_ ? (h - outH - h * 0.025f) : (h * 0.025f);
    }

    bool TouchOverlayManager::handleEditorTouch(const SDL_Event& event, int drawWidth, int drawHeight)
    {
        float px = event.tfinger.x * static_cast<float>(drawWidth);
        float py = event.tfinger.y * static_cast<float>(drawHeight);
        SDL_FingerID fid = event.tfinger.fingerID;

        float tx = 0.0f;
        float ty = 0.0f;
        float tw = 0.0f;
        float th = 0.0f;
        getToolbarRect(drawWidth, drawHeight, tx, ty, tw, th);

        // Header strip + 7 bottom tool buttons + 1 flip button in top-right of header
        float headerH = th * 0.36f;
        float flipW = std::max(44.0f, tw * 0.075f);
        float flipX = tx + tw - flipW - 8.0f;
        float flipY = ty + 6.0f;
        float flipH = headerH - 10.0f;

        constexpr int kToolCount = 7;
        float rowY = ty + headerH + 6.0f;
        float rowH = th - headerH - 12.0f;
        float gap = 8.0f;
        float btnW = (tw - gap * static_cast<float>(kToolCount + 1)) / static_cast<float>(kToolCount);

        auto hitToolbarItem = [&](float x, float y) -> int {
            if (x >= flipX && x <= flipX + flipW && y >= flipY && y <= flipY + flipH) {
                return 100; // flip toolbar position
            }
            if (x >= tx && x <= tx + tw && y >= rowY && y <= rowY + rowH) {
                for (int i = 0; i < kToolCount; ++i) {
                    float bx = tx + gap + static_cast<float>(i) * (btnW + gap);
                    if (x >= bx && x <= bx + btnW) {
                        return i;
                    }
                }
            }
            if (x >= tx && x <= tx + tw && y >= ty && y <= ty + th) {
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
            cur.cx = std::clamp(event.tfinger.x + editorDragOffsetX_, 0.04f, 0.96f);
            cur.cy = std::clamp(event.tfinger.y + editorDragOffsetY_, 0.05f, 0.95f);
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
                    if (id == TouchControlId::AugsMenu) {
                        augDrawerOpen_ = !augDrawerOpen_;
                        buttons_[idx].fingerDown = true;
                        buttons_[idx].finger = fid;
                        return true;
                    }
                    if (isActionToggle(id)) {
                        crouchToggled_ = !crouchToggled_;
                        setButtonPressed(id, crouchToggled_, cb);
                        buttons_[idx].fingerDown = true;
                        buttons_[idx].finger = fid;
                        return true;
                    }
                    if (!buttons_[idx].active) {
                        buttons_[idx].finger = fid;
                        buttons_[idx].dragAimMoved = false;
                        buttons_[idx].prevX = x;
                        buttons_[idx].prevY = y;
                        if (id == TouchControlId::Jump && crouchToggled_) {
                            crouchToggled_ = false;
                            setButtonPressed(TouchControlId::Crouch, false, cb);
                        }
                        setButtonPressed(id, true, cb);
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
            if (!hasHudTapFinger_ && cb.hitTestHudKey) {
                uint8_t hudKey = cb.hitTestHudKey(x * static_cast<float>(drawWidth), y * static_cast<float>(drawHeight));
                if (hudKey != 0) {
                    hasHudTapFinger_ = true;
                    hudTapFinger_ = fid;
                    hudTapKey_ = hudKey;
                    if (cb.onKey) {
                        cb.onKey(hudKey, true);
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

        // Pass 1: All triangle-based geometry (filled circles + bitmap text) -> 1 GL draw call.
        float stickKx = 0.0f;
        float stickKy = 0.0f;
        float stickR = 0.0f;
        float stickCx = 0.0f;
        float stickCy = 0.0f;
        bool drawStick = false;

        for (size_t i = 0; i < kTouchControlCount; ++i) {
            if (!layouts_[i].enabled) {
                continue;
            }
            TouchControlId id = static_cast<TouchControlId>(i);
            float cx = layouts_[i].cx * fw;
            float cy = layouts_[i].cy * fh;
            float r = controlRadiusPx(id, h);

            if (id == TouchControlId::MoveStick) {
                drawStick = true;
                stickCx = cx;
                stickCy = cy;
                stickR = r;
                dx::LinearColor baseFill { 0.04f, 0.09f, 0.14f, alpha * 0.42f };
                dx::LinearColor knobFill { 0.35f, 0.85f, 1.00f, alpha * (hasStickFinger_ ? 0.85f : 0.55f) };
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

            const bool pressed = buttons_[i].active
                || (id == TouchControlId::Crouch && crouchToggled_)
                || (id == TouchControlId::AugsMenu && augDrawerOpen_);
            dx::LinearColor fill = pressed
                ? dx::LinearColor { 0.12f, 0.42f, 0.56f, std::min(0.95f, alpha * 1.15f) }
                : dx::LinearColor { 0.04f, 0.09f, 0.14f, alpha * 0.62f };
            dx::LinearColor fg = pressed
                ? dx::LinearColor { 1.00f, 1.00f, 1.00f, 1.00f }
                : dx::LinearColor { 0.86f, 0.95f, 1.00f, std::min(1.00f, alpha + 0.38f) };

            drawFilledCircle(canvas, cx, cy, r, fill);

            float textScale = std::max(2.0f, r * 0.085f);
            float textY = (id == TouchControlId::Fire || id == TouchControlId::Jump || id == TouchControlId::Crouch ||
                           id == TouchControlId::Scope || id == TouchControlId::Menu)
                ? (cy + r * 0.22f)
                : cy;
            drawText(canvas, controlLabel(id, ptBr), cx, textY, textScale, fg, r * 1.62f);
        }

        // Pass 2: All line-based geometry (circle outlines + stick notches + icon accents) -> 1 GL draw call.
        if (drawStick) {
            dx::LinearColor ringCol { 0.35f, 0.80f, 0.98f, alpha * 0.85f };
            dx::LinearColor knobRing { 0.85f, 0.96f, 1.00f, alpha * 0.95f };
            drawCircleOutline(canvas, stickCx, stickCy, stickR, 2.5f, ringCol);
            canvas.drawLine(stickCx - stickR * 0.25f, stickCy, stickCx + stickR * 0.25f, stickCy, { 0.35f, 0.80f, 0.98f, alpha * 0.40f });
            canvas.drawLine(stickCx, stickCy - stickR * 0.25f, stickCx, stickCy + stickR * 0.25f, { 0.35f, 0.80f, 0.98f, alpha * 0.40f });
            drawCircleOutline(canvas, stickKx, stickKy, stickR * 0.36f, 2.0f, knobRing);
        }

        for (size_t i = 0; i < kTouchControlCount; ++i) {
            if (!layouts_[i].enabled) {
                continue;
            }
            TouchControlId id = static_cast<TouchControlId>(i);
            if (id == TouchControlId::MoveStick) {
                continue;
            }
            float cx = layouts_[i].cx * fw;
            float cy = layouts_[i].cy * fh;
            float r = controlRadiusPx(id, h);

            const bool pressed = buttons_[i].active
                || (id == TouchControlId::Crouch && crouchToggled_)
                || (id == TouchControlId::AugsMenu && augDrawerOpen_);
            dx::LinearColor rim = pressed
                ? dx::LinearColor { 0.72f, 0.96f, 1.00f, 0.96f }
                : dx::LinearColor { 0.36f, 0.82f, 0.98f, std::min(0.95f, alpha + 0.25f) };
            dx::LinearColor fg = pressed
                ? dx::LinearColor { 1.00f, 1.00f, 1.00f, 1.00f }
                : dx::LinearColor { 0.86f, 0.95f, 1.00f, std::min(1.00f, alpha + 0.38f) };

            if (id == TouchControlId::Fire) {
                rim = pressed ? dx::LinearColor { 1.00f, 0.88f, 0.62f, 0.98f } : dx::LinearColor { 0.96f, 0.58f, 0.28f, std::min(0.95f, alpha + 0.28f) };
            }

            drawCircleOutline(canvas, cx, cy, r, pressed ? 3.0f : 2.0f, rim);
            drawIconAccent(canvas, id, cx, cy, r, fg);
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
            canvas.drawRect(x0, y0, boxW, boxH, { 0.03f, 0.07f, 0.12f, alpha * 0.85f }, dx::BlendStyle::Normal);
            drawRectOutline(canvas, x0, y0, boxW, boxH, 2.0f, { 0.36f, 0.82f, 0.98f, 0.85f });
            drawText(canvas, ptBr ? "SEM AUGS ATIVOS" : "NO ACTIVE AUGS", x0 + boxW * 0.5f, y0 + boxH * 0.5f,
                     std::max(2.0f, boxH * 0.055f), { 0.85f, 0.95f, 1.00f, 0.95f }, boxW * 0.88f);
            return;
        }

        AugDrawerMetrics m = computeAugDrawerMetrics(augEntries_.size(), fw, fh);
        const int rows = static_cast<int>((augEntries_.size() + static_cast<size_t>(m.cols) - 1) / static_cast<size_t>(m.cols));
        const float pad = 8.0f;
        const float panelW = static_cast<float>(m.cols) * m.chipW + static_cast<float>(m.cols - 1) * m.gapX + pad * 2.0f;
        const float panelH = static_cast<float>(rows) * m.chipH + static_cast<float>(rows - 1) * m.gapY + pad * 2.0f;
        canvas.drawRect(m.startX - pad, m.startY - pad, panelW, panelH, { 0.02f, 0.05f, 0.09f, std::min(0.88f, alpha + 0.15f) }, dx::BlendStyle::Normal);
        drawRectOutline(canvas, m.startX - pad, m.startY - pad, panelW, panelH, 1.5f, { 0.30f, 0.72f, 0.92f, 0.70f });

        for (size_t i = 0; i < augEntries_.size(); ++i) {
            const TouchAugEntry& entry = augEntries_[i];
            int col = static_cast<int>(i) % m.cols;
            int row = static_cast<int>(i) / m.cols;
            float x0 = m.startX + static_cast<float>(col) * (m.chipW + m.gapX);
            float y0 = m.startY + static_cast<float>(row) * (m.chipH + m.gapY);

            dx::LinearColor fill = entry.active
                ? dx::LinearColor { 0.12f, 0.48f, 0.62f, std::min(0.95f, alpha + 0.25f) }
                : dx::LinearColor { 0.05f, 0.10f, 0.16f, std::min(0.88f, alpha + 0.10f) };
            dx::LinearColor rim = entry.active
                ? dx::LinearColor { 0.65f, 0.98f, 1.00f, 0.98f }
                : dx::LinearColor { 0.32f, 0.72f, 0.90f, 0.75f };
            dx::LinearColor fg = entry.active
                ? dx::LinearColor { 1.00f, 1.00f, 1.00f, 1.00f }
                : dx::LinearColor { 0.84f, 0.94f, 1.00f, 0.92f };

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

        // Translucent backdrop + alignment grid
        canvas.drawRect(0.0f, 0.0f, fw, fh, { 0.02f, 0.04f, 0.07f, 0.68f }, dx::BlendStyle::Normal);
        for (int gx = 1; gx < 10; ++gx) {
            float lx = fw * (static_cast<float>(gx) * 0.1f);
            dx::LinearColor gCol = (gx == 5) ? dx::LinearColor { 0.35f, 0.82f, 0.98f, 0.28f } : dx::LinearColor { 0.50f, 0.65f, 0.78f, 0.10f };
            canvas.drawLine(lx, 0.0f, lx, fh, gCol);
        }
        for (int gy = 1; gy < 10; ++gy) {
            float ly = fh * (static_cast<float>(gy) * 0.1f);
            dx::LinearColor gCol = (gy == 5) ? dx::LinearColor { 0.35f, 0.82f, 0.98f, 0.28f } : dx::LinearColor { 0.50f, 0.65f, 0.78f, 0.10f };
            canvas.drawLine(0.0f, ly, fw, ly, gCol);
        }

        // Draw all controls (enabled and disabled)
        for (size_t i = 0; i < kTouchControlCount; ++i) {
            TouchControlId id = static_cast<TouchControlId>(i);
            const TouchControlLayout& layout = layouts_[i];
            float cx = layout.cx * fw;
            float cy = layout.cy * fh;
            float r = controlRadiusPx(id, h);
            const bool isSel = (static_cast<int>(i) == selectedControl_);

            dx::LinearColor fill = layout.enabled
                ? dx::LinearColor { 0.08f, 0.22f, 0.32f, 0.72f }
                : dx::LinearColor { 0.14f, 0.14f, 0.16f, 0.42f };
            dx::LinearColor rim = isSel
                ? dx::LinearColor { 1.00f, 0.85f, 0.24f, 1.00f }
                : (layout.enabled ? dx::LinearColor { 0.36f, 0.85f, 1.00f, 0.90f } : dx::LinearColor { 0.55f, 0.58f, 0.62f, 0.65f });
            dx::LinearColor fg = layout.enabled
                ? dx::LinearColor { 0.95f, 0.98f, 1.00f, 1.00f }
                : dx::LinearColor { 0.72f, 0.74f, 0.78f, 0.75f };

            drawFilledCircle(canvas, cx, cy, r, fill);
            drawCircleOutline(canvas, cx, cy, r, isSel ? 3.5f : 2.0f, rim);
            if (isSel) {
                drawCircleOutline(canvas, cx, cy, r + 6.0f, 1.5f, { 1.00f, 0.85f, 0.24f, 0.65f });
            }
            float textScale = std::max(2.0f, r * 0.082f);
            drawText(canvas, controlLabel(id, ptBr), cx, layout.enabled ? cy : (cy - r * 0.16f), textScale, fg, r * 1.62f);
            if (!layout.enabled) {
                drawText(canvas, "OFF", cx, cy + r * 0.24f, std::max(2.0f, textScale * 0.85f), { 0.95f, 0.45f, 0.45f, 0.90f }, r * 1.40f);
            }
        }

        // Draw floating toolbar
        float tx = 0.0f;
        float ty = 0.0f;
        float tw = 0.0f;
        float th = 0.0f;
        getToolbarRect(w, h, tx, ty, tw, th);

        canvas.drawRect(tx, ty, tw, th, { 0.04f, 0.07f, 0.11f, 0.93f }, dx::BlendStyle::Normal);
        drawRectOutline(canvas, tx, ty, tw, th, 2.5f, { 0.35f, 0.82f, 0.98f, 0.95f });

        float headerH = th * 0.36f;
        const TouchControlLayout& sel = layouts_[static_cast<size_t>(selectedControl_)];
        int pct = static_cast<int>(std::round(sel.scale * 100.0f));
        char titleBuf[96];
        std::snprintf(
            titleBuf,
            sizeof(titleBuf),
            "%s: %s (%d%%) - %s",
            ptBr ? "ARRASTE OS CONTROLES" : "DRAG CONTROLS",
            controlLabel(static_cast<TouchControlId>(selectedControl_), ptBr),
            pct,
            ptBr ? (activePreset_ == 0 ? "PADRAO" : (activePreset_ == 1 ? "MINIMO" : "GARRA")) : (activePreset_ == 0 ? "DEFAULT" : (activePreset_ == 1 ? "MINIMAL" : "CLAW")));

        float flipW = std::max(44.0f, tw * 0.075f);
        float flipX = tx + tw - flipW - 8.0f;
        float flipY = ty + 6.0f;
        float flipH = headerH - 10.0f;

        float headerScale = std::max(2.0f, fh * 0.0024f);
        drawText(canvas, titleBuf, tx + (tw - flipW - 12.0f) * 0.5f, ty + headerH * 0.5f, headerScale, { 0.92f, 0.97f, 1.00f, 1.00f }, tw - flipW - 24.0f);

        // Flip toolbar button in header top-right
        canvas.drawRect(flipX, flipY, flipW, flipH, { 0.12f, 0.24f, 0.36f, 0.92f }, dx::BlendStyle::Normal);
        drawRectOutline(canvas, flipX, flipY, flipW, flipH, 1.5f, { 0.55f, 0.88f, 1.00f, 0.90f });
        drawText(
            canvas,
            ptBr ? (toolbarAtBottom_ ? "^ BARRA" : "v BARRA") : (toolbarAtBottom_ ? "^ BAR" : "v BAR"),
            flipX + flipW * 0.5f,
            flipY + flipH * 0.5f,
            headerScale,
            { 1.0f, 1.0f, 1.0f, 1.0f },
            flipW * 0.86f);

        constexpr int kToolCount = 7;
        float rowY = ty + headerH + 6.0f;
        float rowH = th - headerH - 12.0f;
        float gap = 8.0f;
        float btnW = (tw - gap * static_cast<float>(kToolCount + 1)) / static_cast<float>(kToolCount);
        float btnTextScale = std::max(1.8f, fh * 0.0022f);

        const char* labelsPt[kToolCount] = {
            "TAM -",
            "TAM +",
            sel.enabled ? "VIS: SIM" : "VIS: NAO",
            "MODO",
            "PADRAO",
            "CANCELAR",
            "SALVAR",
        };
        const char* labelsEn[kToolCount] = {
            "SIZE -",
            "SIZE +",
            sel.enabled ? "SHOW: ON" : "SHOW: OFF",
            "PRESET",
            "RESET",
            "CANCEL",
            "SAVE",
        };

        for (int i = 0; i < kToolCount; ++i) {
            float bx = tx + gap + static_cast<float>(i) * (btnW + gap);
            dx::LinearColor bFill { 0.10f, 0.18f, 0.28f, 0.92f };
            dx::LinearColor bRim { 0.42f, 0.84f, 1.00f, 0.85f };
            if (i == 2 && !sel.enabled) {
                bFill = { 0.24f, 0.14f, 0.16f, 0.92f };
                bRim = { 0.95f, 0.48f, 0.48f, 0.90f };
            } else if (i == 5) {
                bFill = { 0.30f, 0.12f, 0.14f, 0.92f };
                bRim = { 0.95f, 0.45f, 0.45f, 0.95f };
            } else if (i == 6) {
                bFill = { 0.10f, 0.34f, 0.22f, 0.95f };
                bRim = { 0.38f, 0.94f, 0.58f, 1.00f };
            }
            canvas.drawRect(bx, rowY, btnW, rowH, bFill, dx::BlendStyle::Normal);
            drawRectOutline(canvas, bx, rowY, btnW, rowH, 2.0f, bRim);
            drawText(canvas, ptBr ? labelsPt[i] : labelsEn[i], bx + btnW * 0.5f, rowY + rowH * 0.5f, btnTextScale, { 0.96f, 0.98f, 1.00f, 1.00f }, btnW * 0.88f);
        }
    }
} // namespace game
