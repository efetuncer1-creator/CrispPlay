#pragma once
// The subtitle band handed to the OCR readers: 8-bit BGRA, alpha always 255,
// rows `stride` bytes apart. Converters below turn the pixel formats the
// presenter can output (RGBA8/BGRA8, R10G10B10A2, RGBA16F scRGB) into it.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace crispplay::subs::ocr {

struct BandImage {
    int width = 0;
    int height = 0;
    int stride = 0;              // bytes per row, >= 4 * width
    std::vector<uint8_t> bgra;   // stride * height bytes

    bool empty() const { return width <= 0 || height <= 0 || bgra.size() < size_t(stride) * size_t(height); }
    const uint8_t* row(int y) const { return bgra.data() + size_t(y) * size_t(stride); }
    uint8_t* row(int y) { return bgra.data() + size_t(y) * size_t(stride); }
    // Packed (stride = 4 * width), zero-filled image.
    static BandImage make(int width, int height);
};

// `src` points at the first pixel, `srcStride` is in bytes. All converters
// return a packed image and force alpha to 255.
BandImage fromBgra8(const void* src, int width, int height, int srcStride);
BandImage fromRgba8(const void* src, int width, int height, int srcStride);
// DXGI_FORMAT_R10G10B10A2_UNORM, treated as SDR (each channel scaled to 8 bits).
BandImage fromR10G10B10A2(const void* src, int width, int height, int srcStride);

struct HdrToneMap {
    // scRGB value that becomes display white before the shoulder (1.0 = 80 nits).
    // Callers can pass the SDR/paper white level / 80 for HDR output.
    float paperWhite = 1.0f;
    // Below this (after dividing by paperWhite) values stay linear; above it a
    // Reinhard shoulder compresses everything up to infinity into the rest.
    float knee = 0.8f;
};
// DXGI_FORMAT_R16G16B16A16_FLOAT, linear scRGB. Tone maps the max channel (so
// yellow subtitles keep their hue), then encodes sRGB.
BandImage fromRgba16f(const void* src, int width, int height, int srcStride, HdrToneMap map = {});

// Bands wider than this are halved before OCR (spike: the cost is linear in
// pixels and the 1080p band is ~1383 px wide).
inline constexpr int kDownscaleAboveWidth = 1400;
inline bool needsDownscale(const BandImage& img) { return img.width > kDownscaleAboveWidth; }
// 2x2 box filter; odd last row/column is dropped.
BandImage downscale2x(const BandImage& img);

// "White text only" (fallback preprocessing): bright pixels (max channel >= 200)
// with a dark pixel (luma < 90) within `radius` become black on white.
BandImage whiteTextOnly(const BandImage& img, int radius = 5);

// Copy of a rectangle (clamped to the image).
BandImage crop(const BandImage& img, int x0, int y0, int x1, int y1);

// Bilinear resize with OpenCV INTER_LINEAR geometry (half-pixel centres,
// clamped edges, no anti-aliasing), rounded back to 8 bits.
BandImage resizeBilinear(const BandImage& img, int width, int height);

// IEEE half -> float (exposed for tests).
float halfToFloat(uint16_t h);

}  // namespace crispplay::subs::ocr
