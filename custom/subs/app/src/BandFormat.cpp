#include "BandFormat.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace crispplay::subs::app {
namespace {
constexpr float kPaperWhiteNits = 203.0f;   // BT.2408 reference white

float srgbEncode(float linear) {
    linear = std::clamp(linear, 0.0f, 1.0f);
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

// SMPTE ST 2084 EOTF: code (0..1) -> nits.
float pqToNits(float code) {
    constexpr float m1 = 0.1593017578125f, m2 = 78.84375f, c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
    const float p = std::pow(std::clamp(code, 0.0f, 1.0f), 1.0f / m2);
    const float num = std::max(p - c1, 0.0f), den = c2 - c3 * p;
    return den > 0 ? 10000.0f * std::pow(num / den, 1.0f / m1) : 10000.0f;
}

const std::array<uint8_t, 1024>& pqTable() {
    static const std::array<uint8_t, 1024> table = [] {
        std::array<uint8_t, 1024> t{};
        constexpr float knee = 0.8f;
        for (uint32_t i = 0; i < 1024; ++i) {
            float v = pqToNits(float(i) / 1023.0f) / kPaperWhiteNits;
            // Same shape as the OCR library's scRGB path: linear to the knee,
            // then a Reinhard shoulder into the rest of the range.
            if (v > knee) { const float over = v - knee; v = knee + (1.0f - knee) * over / (over + (1.0f - knee)); }
            t[i] = uint8_t(std::lround(srgbEncode(v) * 255.0f));
        }
        return t;
    }();
    return table;
}
} // namespace

Region sanitized(Region r) {
    auto fix = [](float v, float fallback) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : fallback; };
    r.left = fix(r.left, 0.14f); r.right = fix(r.right, 0.86f); r.top = fix(r.top, 0.62f); r.bottom = fix(r.bottom, 0.97f);
    if (r.right < r.left) std::swap(r.left, r.right);
    if (r.bottom < r.top) std::swap(r.top, r.bottom);
    if (r.right - r.left < 0.10f) { r.left = std::min(r.left, 0.90f); r.right = r.left + 0.10f; }
    if (r.bottom - r.top < 0.05f) { r.top = std::min(r.top, 0.95f); r.bottom = r.top + 0.05f; }
    return r;
}

BandBox bandBox(const Region& requested, uint32_t frameWidth, uint32_t frameHeight) {
    if (frameWidth < 16 || frameHeight < 16) return {};
    const Region r = sanitized(requested);
    auto even = [](double v) { return uint32_t(std::lround(v)) & ~1u; };
    BandBox box;
    box.x = std::min(even(r.left * frameWidth), frameWidth - 2);
    box.y = std::min(even(r.top * frameHeight), frameHeight - 2);
    const uint32_t right = std::min(even(r.right * frameWidth), frameWidth & ~1u);
    const uint32_t bottom = std::min(even(r.bottom * frameHeight), frameHeight & ~1u);
    box.width = right > box.x ? right - box.x : 0;
    box.height = bottom > box.y ? bottom - box.y : 0;
    return box;
}

uint32_t bytesPerPixel(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return 4;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return 8;
    default:
        return 0;
    }
}

uint8_t pqCodeToSdr(uint32_t code10) { return pqTable()[code10 & 1023u]; }

ocr::BandImage convertBand(const void* data, uint32_t rowPitch, uint32_t width, uint32_t height, DXGI_FORMAT format,
                           uint32_t hdrMode) {
    if (!data || !width || !height) return {};
    const int w = int(width), h = int(height), stride = int(rowPitch);
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return ocr::fromRgba8(data, w, h, stride);
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        return ocr::fromBgra8(data, w, h, stride);
    case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_TYPELESS: {
        if (hdrMode != 2) return ocr::fromR10G10B10A2(data, w, h, stride);
        const auto& table = pqTable();
        ocr::BandImage out = ocr::BandImage::make(w, h);
        for (int y = 0; y < h; ++y) {
            const auto* src = static_cast<const uint8_t*>(data) + size_t(y) * rowPitch;
            uint8_t* dst = out.row(y);
            for (int x = 0; x < w; ++x, src += 4, dst += 4) {
                uint32_t v; std::memcpy(&v, src, 4);
                dst[2] = table[v & 1023u]; dst[1] = table[(v >> 10) & 1023u]; dst[0] = table[(v >> 20) & 1023u]; dst[3] = 255;
            }
        }
        return out;
    }
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS: {
        ocr::HdrToneMap map;
        map.paperWhite = hdrMode == 1 ? kPaperWhiteNits / 80.0f : 1.0f;
        return ocr::fromRgba16f(data, w, h, stride, map);
    }
    default:
        return {};
    }
}

int downscaleForOcr(ocr::BandImage& image) {
    int factor = 1;
    while (factor < 4 && ocr::needsDownscale(image)) { image = ocr::downscale2x(image); factor *= 2; }
    return factor;
}

uint64_t fingerprint(const ocr::BandImage& image) {
    if (image.empty()) return 0;
    uint64_t hash = 1469598103934665603ull ^ (uint64_t(image.width) << 32 | uint32_t(image.height));
    const int stepY = std::max(1, image.height / 96), stepX = std::max(1, image.width / 256);
    for (int y = 0; y < image.height; y += stepY) {
        const uint8_t* row = image.row(y);
        for (int x = 0; x < image.width; x += stepX) {
            uint32_t v; std::memcpy(&v, row + size_t(x) * 4, 4);
            hash = (hash ^ v) * 1099511628211ull;
        }
    }
    return hash ? hash : 1;
}

} // namespace crispplay::subs::app
