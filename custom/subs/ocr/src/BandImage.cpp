#include "crispplay/subs/ocr/BandImage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace crispplay::subs::ocr {

BandImage BandImage::make(int width, int height) {
    BandImage img;
    img.width = std::max(0, width);
    img.height = std::max(0, height);
    img.stride = img.width * 4;
    img.bgra.assign(size_t(img.stride) * size_t(img.height), 0);
    return img;
}

namespace {

template <class PixelFn>
BandImage convert(const void* src, int width, int height, int srcStride, PixelFn fn) {
    BandImage out = BandImage::make(width, height);
    if (!src || out.empty()) return out;
    const auto* base = static_cast<const uint8_t*>(src);
    for (int y = 0; y < height; ++y) fn(base + size_t(y) * size_t(srcStride), out.row(y), width);
    return out;
}

// Linear [0,1] -> sRGB 8-bit, 4096-entry table.
const std::array<uint8_t, 4097>& srgbTable() {
    static const std::array<uint8_t, 4097> table = [] {
        std::array<uint8_t, 4097> t{};
        for (int i = 0; i <= 4096; ++i) {
            const double v = i / 4096.0;
            const double s = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
            t[i] = uint8_t(std::clamp(int(std::lround(s * 255.0)), 0, 255));
        }
        return t;
    }();
    return table;
}

inline uint8_t encodeSrgb(float v) {
    if (!(v > 0.f)) return 0;  // also catches NaN
    if (v >= 1.f) return 255;
    return srgbTable()[size_t(v * 4096.f + 0.5f)];
}

}  // namespace

BandImage fromBgra8(const void* src, int width, int height, int srcStride) {
    return convert(src, width, height, srcStride, [](const uint8_t* s, uint8_t* d, int w) {
        std::memcpy(d, s, size_t(w) * 4);
        for (int x = 0; x < w; ++x) d[4 * x + 3] = 255;
    });
}

BandImage fromRgba8(const void* src, int width, int height, int srcStride) {
    return convert(src, width, height, srcStride, [](const uint8_t* s, uint8_t* d, int w) {
        for (int x = 0; x < w; ++x, s += 4, d += 4) {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = 255;
        }
    });
}

BandImage fromR10G10B10A2(const void* src, int width, int height, int srcStride) {
    return convert(src, width, height, srcStride, [](const uint8_t* s, uint8_t* d, int w) {
        for (int x = 0; x < w; ++x, s += 4, d += 4) {
            uint32_t p;
            std::memcpy(&p, s, 4);
            const auto to8 = [](uint32_t v) { return uint8_t((v * 255u + 511u) / 1023u); };
            d[2] = to8(p & 0x3FFu);
            d[1] = to8((p >> 10) & 0x3FFu);
            d[0] = to8((p >> 20) & 0x3FFu);
            d[3] = 255;
        }
    });
}

float halfToFloat(uint16_t h) {
    const uint32_t sign = uint32_t(h & 0x8000u) << 16;
    const uint32_t exp = (h >> 10) & 0x1Fu;
    const uint32_t man = h & 0x3FFu;
    uint32_t bits;
    if (exp == 0) {
        if (man == 0) {
            bits = sign;
        } else {  // subnormal: renormalise
            int e = -1;
            uint32_t m = man;
            do {
                ++e;
                m <<= 1;
            } while ((m & 0x400u) == 0);
            bits = sign | uint32_t(127 - 15 - e) << 23 | (m & 0x3FFu) << 13;
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | man << 13;
    } else {
        bits = sign | (exp + 127 - 15) << 23 | man << 13;
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

BandImage fromRgba16f(const void* src, int width, int height, int srcStride, HdrToneMap map) {
    const float invWhite = map.paperWhite > 0.f ? 1.f / map.paperWhite : 1.f;
    const float knee = std::clamp(map.knee, 0.f, 0.99f);
    const float room = 1.f - knee;
    return convert(src, width, height, srcStride, [&](const uint8_t* s, uint8_t* d, int w) {
        for (int x = 0; x < w; ++x, s += 8, d += 4) {
            uint16_t h[3];
            std::memcpy(h, s, 6);
            float c[3];
            for (int i = 0; i < 3; ++i) {
                const float v = halfToFloat(h[i]) * invWhite;
                c[i] = (v > 0.f && std::isfinite(v)) ? v : (v > 0.f ? 1e6f : 0.f);
            }
            // Reinhard shoulder on the max channel; scale all channels by the same
            // factor so the hue (yellow vs white text) survives.
            const float m = std::max({c[0], c[1], c[2]});
            if (m > knee) {
                const float t = (m - knee) / room;
                const float mapped = knee + room * (t / (1.f + t));
                const float k = mapped / m;
                for (float& v : c) v *= k;
            }
            d[2] = encodeSrgb(c[0]);
            d[1] = encodeSrgb(c[1]);
            d[0] = encodeSrgb(c[2]);
            d[3] = 255;
        }
    });
}

BandImage downscale2x(const BandImage& img) {
    BandImage out = BandImage::make(img.width / 2, img.height / 2);
    if (out.empty() || img.empty()) return out;
    for (int y = 0; y < out.height; ++y) {
        const uint8_t* a = img.row(2 * y);
        const uint8_t* b = img.row(2 * y + 1);
        uint8_t* d = out.row(y);
        for (int x = 0; x < out.width; ++x, a += 8, b += 8, d += 4) {
            for (int c = 0; c < 3; ++c) d[c] = uint8_t((a[c] + a[c + 4] + b[c] + b[c + 4] + 2) / 4);
            d[3] = 255;
        }
    }
    return out;
}

BandImage whiteTextOnly(const BandImage& img, int radius) {
    const int w = img.width, h = img.height;
    BandImage out = BandImage::make(w, h);
    if (img.empty()) return out;
    radius = std::max(0, radius);
    // dark mask, then a square dilation done as two separable running-max passes.
    std::vector<uint8_t> dark(size_t(w) * h), tmp(size_t(w) * h), near(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const uint8_t* p = img.row(y);
        for (int x = 0; x < w; ++x, p += 4) {
            // cv2 BGR2GRAY weights.
            const int luma = (p[0] * 1868 + p[1] * 9617 + p[2] * 4899 + 8192) >> 14;
            dark[size_t(y) * w + x] = luma < 90;
        }
    }
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = &dark[size_t(y) * w];
        uint8_t* dst = &tmp[size_t(y) * w];
        int last = -1000000;  // last dark x seen
        for (int x = 0; x < w; ++x) {
            if (src[x]) last = x;
            dst[x] = x - last <= radius;
        }
        last = 1000000;
        for (int x = w - 1; x >= 0; --x) {
            if (src[x]) last = x;
            dst[x] |= uint8_t(last - x <= radius);
        }
    }
    for (int x = 0; x < w; ++x) {
        int last = -1000000;
        for (int y = 0; y < h; ++y) {
            if (tmp[size_t(y) * w + x]) last = y;
            near[size_t(y) * w + x] = y - last <= radius;
        }
        last = 1000000;
        for (int y = h - 1; y >= 0; --y) {
            if (tmp[size_t(y) * w + x]) last = y;
            near[size_t(y) * w + x] |= uint8_t(last - y <= radius);
        }
    }
    for (int y = 0; y < h; ++y) {
        const uint8_t* p = img.row(y);
        uint8_t* d = out.row(y);
        for (int x = 0; x < w; ++x, p += 4, d += 4) {
            const bool text = std::max({p[0], p[1], p[2]}) >= 200 && near[size_t(y) * w + x];
            d[0] = d[1] = d[2] = text ? 0 : 255;
            d[3] = 255;
        }
    }
    return out;
}

BandImage resizeBilinear(const BandImage& img, int width, int height) {
    BandImage out = BandImage::make(width, height);
    if (out.empty() || img.empty()) return out;
    if (width == img.width && height == img.height) {
        for (int y = 0; y < height; ++y) std::memcpy(out.row(y), img.row(y), size_t(width) * 4);
        return out;
    }
    struct Tap { int i0, i1; float f; };
    const auto taps = [](int dst, int src) {
        std::vector<Tap> t(static_cast<size_t>(dst));
        const double scale = double(src) / dst;
        for (int d = 0; d < dst; ++d) {
            double f = (d + 0.5) * scale - 0.5;
            int s = int(std::floor(f));
            f -= s;
            if (s < 0) { s = 0; f = 0; }
            if (s >= src - 1) { s = src - 1; f = 0; }
            t[size_t(d)] = {s, std::min(s + 1, src - 1), float(f)};
        }
        return t;
    };
    const auto tx = taps(width, img.width);
    const auto ty = taps(height, img.height);
    std::vector<float> rowA(size_t(width) * 3), rowB(size_t(width) * 3);
    const auto horiz = [&](int sy, std::vector<float>& dst) {
        const uint8_t* s = img.row(sy);
        for (int x = 0; x < width; ++x) {
            const Tap& t = tx[size_t(x)];
            for (int c = 0; c < 3; ++c)
                dst[size_t(x) * 3 + c] = s[t.i0 * 4 + c] * (1.f - t.f) + s[t.i1 * 4 + c] * t.f;
        }
    };
    int cachedA = -1, cachedB = -1;
    for (int y = 0; y < height; ++y) {
        const Tap& t = ty[size_t(y)];
        if (cachedA != t.i0) {
            if (cachedB == t.i0) std::swap(rowA, rowB), std::swap(cachedA, cachedB);
            else horiz(t.i0, rowA), cachedA = t.i0;
        }
        if (cachedB != t.i1) horiz(t.i1, rowB), cachedB = t.i1;
        uint8_t* d = out.row(y);
        for (int x = 0; x < width; ++x, d += 4) {
            for (int c = 0; c < 3; ++c) {
                const float v = rowA[size_t(x) * 3 + c] * (1.f - t.f) + rowB[size_t(x) * 3 + c] * t.f;
                d[c] = uint8_t(std::clamp(int(v + 0.5f), 0, 255));
            }
            d[3] = 255;
        }
    }
    return out;
}

BandImage crop(const BandImage& img, int x0, int y0, int x1, int y1) {
    x0 = std::clamp(x0, 0, img.width);
    x1 = std::clamp(x1, x0, img.width);
    y0 = std::clamp(y0, 0, img.height);
    y1 = std::clamp(y1, y0, img.height);
    BandImage out = BandImage::make(x1 - x0, y1 - y0);
    for (int y = 0; y < out.height; ++y) std::memcpy(out.row(y), img.row(y0 + y) + size_t(x0) * 4, size_t(out.width) * 4);
    return out;
}

}  // namespace crispplay::subs::ocr
