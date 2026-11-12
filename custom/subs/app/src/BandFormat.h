// Subtitle band geometry and pixel conversion (no D3D device needed: unit tested).
#pragma once

#include <cstdint>

#include <dxgiformat.h>

#include "crispplay/subs/ocr/BandImage.h"

namespace crispplay::subs::app {

// The part of the picture that is read, as fractions of the frame.
struct Region {
    float left = 0.14f, top = 0.62f, right = 0.86f, bottom = 0.97f;
    bool operator==(const Region&) const = default;
};
// Clamped to sane values (inside the frame, at least 10 % wide and 5 % high).
Region sanitized(Region region);

struct BandBox {
    uint32_t x = 0, y = 0, width = 0, height = 0;
    bool empty() const { return width == 0 || height == 0; }
};
// Pixel rectangle of `region` in a frame; even origin and size, inside the frame.
BandBox bandBox(const Region& region, uint32_t frameWidth, uint32_t frameHeight);

// Bytes per pixel of the formats the band can be read back in; 0 = unsupported.
uint32_t bytesPerPixel(DXGI_FORMAT format);

// Read-back rows -> 8-bit BGRA for OCR. hdrMode: 0 SDR, 1 scRGB, 2 HDR10 (PQ).
// HDR is tone mapped around a 203 nit paper white. Returns an empty image for
// an unsupported format.
ocr::BandImage convertBand(const void* data, uint32_t rowPitch, uint32_t width, uint32_t height, DXGI_FORMAT format,
                           uint32_t hdrMode);
// Halves the image until it is no wider than the OCR limit (1400 px); returns
// the factor applied (1, 2 or 4).
int downscaleForOcr(ocr::BandImage& image);

// 10-bit PQ code -> 8-bit sRGB-like value (per channel, gamut ignored): what
// convertBand uses for HDR10. Exposed for tests.
uint8_t pqCodeToSdr(uint32_t code10);

// Cheap content fingerprint (sub-sampled); equal for an unchanged band.
uint64_t fingerprint(const ocr::BandImage& image);

} // namespace crispplay::subs::app
