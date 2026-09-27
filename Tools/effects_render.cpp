// effects_render — standalone visual proof of the crown-jewel UI effects.
// Mirrors Brain/lib/born2flap/ui/effects.rb (Telemetry -> material params) but
// applies them as pixel operations so they can be *seen*. No dependencies.
//
// Renders a 2x2 canvas: BASE | FLAP_GLOW | PROGRESSIVE_BLUR | SPEED_BLUR+SHIMMER
// Writes effects_render.bmp (convert with: sips -s format png in.bmp --out out.png)
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

static const double PI = 3.14159265358979323846;

struct Img {
    int w = 0, h = 0;
    std::vector<uint8_t> p; // RGB, row-major
    uint8_t &at(int x, int y, int c) { return p[(y * w + x) * 3 + c]; }
    const uint8_t &at(int x, int y, int c) const { return p[(y * w + x) * 3 + c]; }
    Img(int w_, int h_) : w(w_), h(h_), p(w_ * h_ * 3, 0) {}
};

static float clampf(float v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// ---- base "HUD" scene: glowing wingbeat core + progress bar + title line ----
static Img make_base(int w, int h, float glow, float warmth) {
    Img img(w, h);
    const float cx = w * 0.5f, cy = h * 0.46f;
    const float barTop = h * 0.72f, barBot = h * 0.80f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // vertical navy gradient background
            float t = (float)y / h;
            float r = 30 + (10 - 30) * t;
            float g = 40 + (12 - 40) * t;
            float b = 70 + (25 - 70) * t;

            // faint grid
            if ((x % 40) < 1 || (y % 40) < 1) { r += 6; g += 8; b += 10; }

            // wingbeat core (cyan) + halo scaled by glow
            float dx = x - cx, dy = y - cy;
            float d2 = dx * dx + dy * dy;
            float core = std::exp(-d2 / (60.0f * 60.0f));
            float halo = std::exp(-d2 / (150.0f * 150.0f));
            r += (40 * core + 55 * halo) * glow + warmth * halo * glow;
            g += (255 * core + 140 * halo) * glow;
            b += (255 * core + 200 * halo) * glow;

            // progress bar (amber)
            if (y >= barTop && y <= barBot && x >= w * 0.18f && x <= w * 0.82f) {
                float bt = (float)(y - barTop) / (barBot - barTop);
                r = 255 * (0.6f + 0.4f * glow);
                g = 180 - 120 * bt;
                b = 40 - 30 * bt;
            }

            // title line (bright)
            if (y >= h * 0.10f && y <= h * 0.13f && x >= w * 0.20f && x <= w * 0.80f) {
                r = 200; g = 220; b = 255;
            }

            img.at(x, y, 0) = (uint8_t)clampf(r);
            img.at(x, y, 1) = (uint8_t)clampf(g);
            img.at(x, y, 2) = (uint8_t)clampf(b);
        }
    }
    return img;
}

static Img box_blur_h(const Img &s, int k) {
    Img d(s.w, s.h);
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            float r = 0, g = 0, b = 0; int n = 0;
            for (int dx = -k; dx <= k; ++dx) {
                int xx = x + dx; if (xx < 0 || xx >= s.w) continue;
                r += s.at(xx, y, 0); g += s.at(xx, y, 1); b += s.at(xx, y, 2); ++n;
            }
            d.at(x, y, 0) = (uint8_t)(r / n); d.at(x, y, 1) = (uint8_t)(g / n); d.at(x, y, 2) = (uint8_t)(b / n);
        }
    return d;
}

// progressive blur: radius grows with y (focus at bottom -> far edge at top melts)
static Img progressive_blur(const Img &s, float maxR) {
    Img d(s.w, s.h);
    for (int x = 0; x < s.w; ++x)
        for (int y = 0; y < s.h; ++y) {
            int k = (int)(maxR * ((float)y / (s.h - 1)));
            float r = 0, g = 0, b = 0; int n = 0;
            for (int dy = -k; dy <= k; ++dy) {
                int yy = y + dy; if (yy < 0 || yy >= s.h) continue;
                r += s.at(x, yy, 0); g += s.at(x, yy, 1); b += s.at(x, yy, 2); ++n;
            }
            d.at(x, y, 0) = (uint8_t)(r / n); d.at(x, y, 1) = (uint8_t)(g / n); d.at(x, y, 2) = (uint8_t)(b / n);
        }
    return d;
}

// thermal shimmer: vertical displacement via travelling sine wave
static Img shimmer(const Img &s, float A, float lambda, float phase) {
    Img d(s.w, s.h);
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            int yy = y + (int)(A * std::sin(2 * PI * x / lambda + phase));
            if (yy < 0) yy = 0; if (yy >= s.h) yy = s.h - 1;
            d.at(x, y, 0) = s.at(x, yy, 0);
            d.at(x, y, 1) = s.at(x, yy, 1);
            d.at(x, y, 2) = s.at(x, yy, 2);
        }
    return d;
}

static void blit(Img &dst, const Img &src, int ox, int oy) {
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x)
            for (int c = 0; c < 3; ++c) dst.at(ox + x, oy + y, c) = src.at(x, y, c);
}

static void border(Img &img, int x0, int y0, int x1, int y1) {
    for (int x = x0; x <= x1; ++x) { img.at(x, y0, 0) = img.at(x, y0, 1) = img.at(x, y0, 2) = 255; img.at(x, y1, 0) = img.at(x, y1, 1) = img.at(x, y1, 2) = 255; }
    for (int y = y0; y <= y1; ++y) { img.at(x0, y, 0) = img.at(x0, y, 1) = img.at(x0, y, 2) = 255; img.at(x1, y, 0) = img.at(x1, y, 1) = img.at(x1, y, 2) = 255; }
}

static void write_bmp(const std::string &path, const Img &img) {
    int row = ((img.w * 3 + 3) / 4) * 4;
    int dataSize = row * img.h;
    int fileSize = 54 + dataSize;
    std::vector<uint8_t> buf(fileSize, 0);

    auto u16 = [&](int o, uint16_t v){ buf[o]=v&0xff; buf[o+1]=(v>>8)&0xff; };
    auto u32 = [&](int o, uint32_t v){ buf[o]=v&0xff; buf[o+1]=(v>>8)&0xff; buf[o+2]=(v>>16)&0xff; buf[o+3]=(v>>24)&0xff; };

    buf[0]='B'; buf[1]='M';
    u32(2, fileSize);       // file size
    u32(10, 54);            // data offset
    u32(14, 40);            // info header size
    u32(18, img.w);         // width
    u32(22, img.h);         // height (positive = bottom-up)
    u16(26, 1);             // planes
    u16(28, 24);            // bpp
    u32(30, 0);             // compression
    u32(34, dataSize);      // image size
    u32(38, 2835);          // x ppm
    u32(42, 2835);          // y ppm
    u32(46, 0); u32(50, 0);

    for (int y = 0; y < img.h; ++y) {
        int srcY = img.h - 1 - y; // bottom-up
        int off = 54 + y * row;
        for (int x = 0; x < img.w; ++x) {
            buf[off + x*3 + 0] = img.at(x, srcY, 2); // B
            buf[off + x*3 + 1] = img.at(x, srcY, 1); // G
            buf[off + x*3 + 2] = img.at(x, srcY, 0); // R
        }
    }
    FILE *f = std::fopen(path.c_str(), "wb");
    std::fwrite(buf.data(), 1, buf.size(), f);
    std::fclose(f);
}

// ASCII preview for the terminal
static std::string ascii_preview(const Img &img, int cw, int ch) {
    static const char *ramp = " .:-=+*#%@";
    std::string out;
    out.reserve((cw + 1) * ch);
    for (int ty = 0; ty < ch; ++ty) {
        for (int tx = 0; tx < cw; ++tx) {
            int x0 = tx * img.w / cw, x1 = (tx + 1) * img.w / cw;
            int y0 = ty * img.h / ch, y1 = (ty + 1) * img.h / ch;
            float L = 0; int n = 0;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    L += (0.299f * img.at(x, y, 0) + 0.587f * img.at(x, y, 1) + 0.114f * img.at(x, y, 2)) / 255.0f;
                    ++n;
                }
            L /= n;
            out += ramp[std::min(9, (int)(L * 9.99f))];
        }
        out += '\n';
    }
    return out;
}

int main() {
    const int cw = 560, chh = 320, m = 24;
    Img canvas(cw * 2 + m * 3, chh * 2 + m * 3);

    // TL: base (sharp)
    blit(canvas, make_base(cw, chh, 0.5f, 0.0f), m, m);
    // TR: flap glow (emissive pulse peak + warmth shift)
    blit(canvas, make_base(cw, chh, 1.0f, 60.0f), cw + m * 2, m);
    // BL: progressive blur (far edge melts into focus at bottom)
    blit(canvas, progressive_blur(make_base(cw, chh, 0.5f, 0.0f), 22.0f), m, chh + m * 2);
    // BR: speed blur (motion) + thermal shimmer (travelling wave)
    blit(canvas, box_blur_h(shimmer(make_base(cw, chh, 0.5f, 0.0f), 7.0f, 40.0f, 0.8f), 16), cw + m * 2, chh + m * 2);

    for (int gy = 0; gy < 2; ++gy)
        for (int gx = 0; gx < 2; ++gx)
            border(canvas, m - 2 + gx * (cw + m), m - 2 + gy * (chh + m),
                   m + cw + 1 + gx * (cw + m), m + chh + 1 + gy * (chh + m));

    write_bmp("effects_render.bmp", canvas);
    std::printf("%s", ascii_preview(canvas, 88, 34).c_str());
    std::printf("wrote effects_render.bmp (%dx%d)\n", canvas.w, canvas.h);
    return 0;
}
