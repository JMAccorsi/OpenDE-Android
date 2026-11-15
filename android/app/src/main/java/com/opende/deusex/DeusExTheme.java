package com.opende.deusex;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorFilter;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.Shader;
import android.graphics.drawable.Drawable;
import android.util.TypedValue;
import android.view.View;

final class DeusExTheme {
    private DeusExTheme() {}

    static int dp(Context ctx, float v) {
        return Math.round(TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP, v, ctx.getResources().getDisplayMetrics()));
    }

    static Path makeChamferPath(Rect b, float c) {
        float l = b.left;
        float t = b.top;
        float r = b.right;
        float bt = b.bottom;
        Path p = new Path();
        p.moveTo(l + c, t);
        p.lineTo(r - c, t);
        p.lineTo(r, t + c);
        p.lineTo(r, bt - c);
        p.lineTo(r - c, bt);
        p.lineTo(l + c, bt);
        p.lineTo(l, bt - c);
        p.lineTo(l, t + c);
        p.close();
        return p;
    }

    static final class BackdropDrawable extends Drawable {
        private final float dpScale;
        private final Paint bgPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint tracePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint nodePaint = new Paint(Paint.ANTI_ALIAS_FLAG);

        BackdropDrawable(Context ctx) {
            this.dpScale = ctx.getResources().getDisplayMetrics().density;
            tracePaint.setStyle(Paint.Style.STROKE);
            tracePaint.setStrokeWidth(Math.max(1f, 1.1f * dpScale));
            tracePaint.setColor(Color.parseColor("#2A2F37"));
            nodePaint.setStyle(Paint.Style.FILL);
            nodePaint.setColor(Color.parseColor("#3E4550"));
        }

        @Override
        public void draw(Canvas canvas) {
            Rect b = getBounds();
            float w = b.width();
            float h = b.height();
            bgPaint.setShader(new LinearGradient(
                0, 0, 0, h,
                new int[] {Color.parseColor("#0A0C0F"), Color.parseColor("#111419"), Color.parseColor("#080A0D")},
                new float[] {0f, 0.5f, 1f},
                Shader.TileMode.CLAMP));
            canvas.drawRect(b, bgPaint);

            float m = 18f * dpScale;
            float m2 = 28f * dpScale;
            float ch = 28f * dpScale;
            float ch2 = 22f * dpScale;

            Path outer = new Path();
            outer.moveTo(m + ch, m);
            outer.lineTo(w - m - ch, m);
            outer.lineTo(w - m, m + ch);
            outer.lineTo(w - m, h - m - ch);
            outer.lineTo(w - m - ch, h - m);
            outer.lineTo(m + ch, h - m);
            outer.lineTo(m, h - m - ch);
            outer.lineTo(m, m + ch);
            outer.close();
            canvas.drawPath(outer, tracePaint);

            Path inner = new Path();
            inner.moveTo(m2 + ch2, m2);
            inner.lineTo(w - m2 - ch2, m2);
            inner.lineTo(w - m2, m2 + ch2);
            inner.lineTo(w - m2, h - m2 - ch2);
            inner.lineTo(w - m2 - ch2, h - m2);
            inner.lineTo(m2 + ch2, h - m2);
            inner.lineTo(m2, h - m2 - ch2);
            inner.lineTo(m2, m2 + ch2);
            inner.close();
            canvas.drawPath(inner, tracePaint);

            // Subtle corner PCB trace ticks & nodes
            float[] nx = {m + ch, w - m - ch, m2 + ch2, w - m2 - ch2, m, w - m};
            float[] ny = {m, m, m2, m2, h - m - ch, h - m - ch};
            float ns = 2.2f * dpScale;
            for (int i = 0; i < nx.length; i++) {
                canvas.drawRect(nx[i] - ns, ny[i] - ns, nx[i] + ns, ny[i] + ns, nodePaint);
            }
            float tick = 10f * dpScale;
            canvas.drawLine(w * 0.25f, m, w * 0.25f + tick, m2, tracePaint);
            canvas.drawLine(w * 0.75f, m, w * 0.75f - tick, m2, tracePaint);
            canvas.drawLine(w * 0.25f, h - m, w * 0.25f + tick, h - m2, tracePaint);
            canvas.drawLine(w * 0.75f, h - m, w * 0.75f - tick, h - m2, tracePaint);
        }

        @Override public void setAlpha(int alpha) {}
        @Override public void setColorFilter(ColorFilter colorFilter) {}
        @Override public int getOpacity() { return PixelFormat.OPAQUE; }
    }

    static final class ChamferDrawable extends Drawable {
        private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint strokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final float chamferPx;
        private final int normalFill;
        private final int focusedFill;
        private final int normalStroke;
        private final int focusedStroke;
        private boolean active = false;

        ChamferDrawable(int fillColor, int focusedFillColor, int strokeColor, int focusedStrokeColor, float strokePx, float chamferPx) {
            this.normalFill = fillColor;
            this.focusedFill = focusedFillColor;
            this.normalStroke = strokeColor;
            this.focusedStroke = focusedStrokeColor;
            this.chamferPx = chamferPx;
            fillPaint.setStyle(Paint.Style.FILL);
            strokePaint.setStyle(Paint.Style.STROKE);
            strokePaint.setStrokeWidth(strokePx);
        }

        @Override
        public boolean isStateful() {
            return true;
        }

        @Override
        protected boolean onStateChange(int[] stateSet) {
            boolean nowActive = false;
            for (int st : stateSet) {
                if (st == android.R.attr.state_pressed || st == android.R.attr.state_focused || st == android.R.attr.state_selected) {
                    nowActive = true;
                    break;
                }
            }
            if (nowActive != active) {
                active = nowActive;
                invalidateSelf();
                return true;
            }
            return false;
        }

        @Override
        public void draw(Canvas canvas) {
            Rect b = new Rect(getBounds());
            int inset = Math.max(1, Math.round(strokePaint.getStrokeWidth() * 0.5f));
            b.inset(inset, inset);
            Path p = makeChamferPath(b, chamferPx);
            fillPaint.setColor(active ? focusedFill : normalFill);
            strokePaint.setColor(active ? focusedStroke : normalStroke);
            canvas.drawPath(p, fillPaint);
            canvas.drawPath(p, strokePaint);
        }

        @Override public void setAlpha(int alpha) {}
        @Override public void setColorFilter(ColorFilter colorFilter) {}
        @Override public int getOpacity() { return PixelFormat.TRANSLUCENT; }
    }

    static final class SegmentedBarView extends View {
        private final Paint bgPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint segOffPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint segOnPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final float dpScale;
        private int progress = 0; // 0..1000
        private boolean indeterminate = false;
        private float animPhase = 0f;

        SegmentedBarView(Context ctx) {
            super(ctx);
            dpScale = ctx.getResources().getDisplayMetrics().density;
            bgPaint.setStyle(Paint.Style.FILL);
            bgPaint.setColor(Color.parseColor("#111418"));
            segOffPaint.setStyle(Paint.Style.FILL);
            segOffPaint.setColor(Color.parseColor("#2A2F37"));
            segOnPaint.setStyle(Paint.Style.FILL);
            segOnPaint.setColor(Color.parseColor("#B8C0C8"));
        }

        void setProgressPermille(int permille) {
            indeterminate = false;
            progress = Math.max(0, Math.min(1000, permille));
            invalidate();
        }

        void setIndeterminateMode(boolean enable) {
            indeterminate = enable;
            if (enable) {
                postInvalidateOnAnimation();
            } else {
                invalidate();
            }
        }

        @Override
        protected void onDraw(Canvas canvas) {
            int w = getWidth();
            int h = getHeight();
            canvas.drawRect(0, 0, w, h, bgPaint);
            int count = 36;
            float gap = 2f * dpScale;
            float segW = (w - gap * (count + 1)) / count;
            if (segW <= 1f) return;

            int activeCount = Math.round((progress / 1000f) * count);
            int pulseStart = -1;
            if (indeterminate) {
                animPhase = (animPhase + 0.03f) % 1.0f;
                pulseStart = Math.round(animPhase * count);
            }

            for (int i = 0; i < count; i++) {
                float x = gap + i * (segW + gap);
                boolean lit;
                if (indeterminate) {
                    int dist = (i - pulseStart + count) % count;
                    lit = dist < 6;
                } else {
                    lit = i < activeCount;
                }
                canvas.drawRect(x, 1.5f * dpScale, x + segW, h - 1.5f * dpScale, lit ? segOnPaint : segOffPaint);
            }
            if (indeterminate) {
                postInvalidateDelayed(33);
            }
        }
    }
}
