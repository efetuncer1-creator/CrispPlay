#include "OverlayRender.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QRawFont>
#include <QTextLayout>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace crispplay::subs::app {
namespace {

RenderedBox gLastBox;

// Fonts Windows ships that Qt may fall back to for a character the main
// family lacks. A character none of them covers is not drawn at all.
const std::vector<QRawFont>& fallbackFonts(const QString& family, qreal pixelSize) {
    static QString cachedFamily;
    static std::vector<QRawFont> fonts;
    if (cachedFamily != family || fonts.empty()) {
        fonts.clear();
        cachedFamily = family;
        for (const QString& name : {family, QStringLiteral("Segoe UI"), QStringLiteral("Segoe UI Symbol"), QStringLiteral("Arial"),
                                    QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Yu Gothic UI"), QStringLiteral("Malgun Gothic")}) {
            if (!QFontDatabase::hasFamily(name)) continue;
            QFont font(name);
            font.setPixelSize(int(pixelSize));
            QRawFont raw = QRawFont::fromFont(font);
            if (raw.isValid()) fonts.push_back(std::move(raw));
        }
    }
    return fonts;
}

QFont overlayFont(const QString& family, double pixelSize) {
    QFont font(family);
    // Qt looks for a missing character in these, in this order (displayable() checks the same list).
    font.setFamilies({family, QStringLiteral("Segoe UI"), QStringLiteral("Segoe UI Symbol"), QStringLiteral("Arial"),
                      QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Yu Gothic UI"), QStringLiteral("Malgun Gothic")});
    font.setPixelSize(std::max(8, int(std::lround(pixelSize))));
    font.setWeight(QFont::DemiBold);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

uint32_t quantized(uint32_t argb, int shift) {
    if (!shift) return argb;
    const uint32_t mask = 0xFFu >> shift << shift;
    uint32_t out = 0;
    for (int c = 0; c < 4; ++c) {
        uint32_t v = (argb >> (c * 8)) & 0xFFu;
        v = std::min(255u, v + (1u << shift >> 1)) & mask;
        out |= v << (c * 8);
    }
    // Premultiplied: a colour channel may not exceed alpha.
    const uint32_t a = out >> 24;
    for (int c = 0; c < 3; ++c) {
        const uint32_t v = (out >> (c * 8)) & 0xFFu;
        if (v > a) out = (out & ~(0xFFu << (c * 8))) | (a << (c * 8));
    }
    return out;
}

// ARGB32 premultiplied picture -> palette (<= 256 entries) + runs.
bool encode(const QImage& image, veyra::engine::SubtitleBitmap& out) {
    const int w = image.width(), h = image.height();
    for (int shift = 0; shift <= 5; ++shift) {
        std::unordered_map<uint32_t, uint32_t> index;
        std::vector<uint32_t> runs;
        out.palette.fill(0);
        bool fits = true;
        uint32_t current = 0, count = 0;
        bool have = false;
        for (int y = 0; y < h && fits; ++y) {
            const auto* row = reinterpret_cast<const uint32_t*>(image.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const uint32_t colour = quantized(row[x], shift);
                auto it = index.find(colour);
                if (it == index.end()) {
                    if (index.size() >= 256) { fits = false; break; }
                    const uint32_t id = uint32_t(index.size());
                    out.palette[id] = colour;
                    it = index.emplace(colour, id).first;
                }
                if (have && it->second == current && count < 0xFFFFFFu) { ++count; continue; }
                if (have) runs.push_back(count << 8 | current);
                current = it->second; count = 1; have = true;
            }
        }
        if (!fits) continue;
        if (have) runs.push_back(count << 8 | current);
        out.runs = std::move(runs);
        out.width = w; out.height = h;
        return true;
    }
    return false;
}

} // namespace

QString displayable(const QString& text, const QString& family) {
    const auto& fonts = fallbackFonts(family, 32);
    QString out;
    out.reserve(text.size());
    const auto codes = text.toUcs4();
    for (const char32_t code : codes) {
        if (code == 0x2047 || code == 0xFFFD || code == 0xFEFF) continue;
        if (code == U'\n' || code == U'\t' || code == 0x00A0) { out += QChar(u' '); continue; }
        if (code < 0x20 || (code >= 0x7F && code < 0xA0)) continue;
        if (code >= 0xE000 && code <= 0xF8FF) continue;   // private use: never a real glyph
        const bool covered = std::any_of(fonts.begin(), fonts.end(), [code](const QRawFont& font) { return font.supportsCharacter(uint(code)); });
        if (!covered) continue;
        out += QString::fromUcs4(&code, 1);
    }
    return out.simplified();
}

RenderedBox lastRenderedBox() { return gLastBox; }

std::shared_ptr<const veyra::engine::SubtitleBitmapFrame> renderLine(const ShownLine& line, int canvasWidth, int canvasHeight,
                                                                     const OverlayStyle& style) {
    gLastBox = {};
    if (canvasWidth < 32 || canvasHeight < 32) return nullptr;
    const QString speaker = displayable(QString::fromStdString(line.speaker), style.family);
    const QString body = displayable(QString::fromStdString(line.text), style.family);
    if (body.isEmpty()) return nullptr;
    const QString prefix = speaker.isEmpty() ? QString() : speaker + QStringLiteral(": ");
    const QString text = prefix + body;

    // The English subtitle's place on the canvas.
    const double left = std::clamp(double(line.frameLeft), 0.0, 1.0) * canvasWidth, right = std::clamp(double(line.frameRight), 0.0, 1.0) * canvasWidth;
    const double top = std::clamp(double(line.frameTop), 0.0, 1.0) * canvasHeight, bottom = std::clamp(double(line.frameBottom), 0.0, 1.0) * canvasHeight;
    QRectF cover(left, top, std::max(0.0, right - left), std::max(0.0, bottom - top));
    const bool placed = cover.width() >= 8 && cover.height() >= 4;
    if (!placed) cover = QRectF(canvasWidth * 0.5, canvasHeight * 0.88, 0, 0);   // no geometry: bottom centre

    const double pixelSize = std::clamp(canvasHeight * 0.036 * style.scale, 10.0, 220.0);
    const QFont font = overlayFont(style.family, pixelSize);
    const double padX = pixelSize * 0.55, padY = pixelSize * 0.22;
    const double wrapWidth = std::max(cover.width() - 2 * padX, canvasWidth * 0.72);

    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WordWrap);
    option.setAlignment(Qt::AlignHCenter);
    layout.setTextOption(option);
    if (!prefix.isEmpty()) {
        QTextLayout::FormatRange range;
        range.start = 0; range.length = int(prefix.size());
        range.format.setForeground(QColor::fromRgba(style.speakerColor));
        layout.setFormats({range});
    }
    double textHeight = 0, textWidth = 0;
    layout.beginLayout();
    for (;;) {
        QTextLine row = layout.createLine();
        if (!row.isValid()) break;
        row.setLineWidth(wrapWidth);
        row.setPosition(QPointF(0, textHeight));
        textHeight += row.height();
        textWidth = std::max(textWidth, double(row.naturalTextWidth()));
    }
    layout.endLayout();
    if (textHeight <= 0) return nullptr;

    // The box: at least the English subtitle, grown around its centre to hold the text.
    const QPointF centre = cover.center();
    QRectF textBox(centre.x() - textWidth / 2 - padX, centre.y() - textHeight / 2 - padY, textWidth + 2 * padX, textHeight + 2 * padY);
    QRectF box = placed ? cover.united(textBox) : textBox;
    // Keep it on the picture; the cover part always stays inside.
    if (box.width() > canvasWidth) box.setWidth(canvasWidth);
    if (box.height() > canvasHeight) box.setHeight(canvasHeight);
    if (box.left() < 0) box.moveLeft(0);
    if (box.top() < 0) box.moveTop(0);
    if (box.right() > canvasWidth) box.moveRight(canvasWidth);
    if (box.bottom() > canvasHeight) box.moveBottom(canvasHeight);
    const QRect pixels = box.toAlignedRect().intersected(QRect(0, 0, canvasWidth, canvasHeight));
    if (pixels.width() < 2 || pixels.height() < 2) return nullptr;

    QImage image(pixels.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        QColor fill = QColor::fromRgba(style.boxColor);
        fill.setAlphaF(std::clamp(style.opacity, 0, 100) / 100.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        const double radius = std::min(pixelSize * 0.28, pixels.height() / 2.0);
        painter.drawRoundedRect(QRectF(0, 0, pixels.width(), pixels.height()), radius, radius);
        painter.setPen(QColor::fromRgba(style.textColor));
        // Rows are centred inside wrapWidth; centre that span in the box.
        const QPointF origin((pixels.width() - wrapWidth) / 2.0, (pixels.height() - textHeight) / 2.0);
        layout.draw(&painter, origin);
    }

    auto frame = std::make_shared<veyra::engine::SubtitleBitmapFrame>();
    frame->width = canvasWidth; frame->height = canvasHeight;
    veyra::engine::SubtitleBitmap bitmap;
    if (!encode(image, bitmap)) return nullptr;
    bitmap.x = pixels.x(); bitmap.y = pixels.y();
    frame->images.push_back(std::move(bitmap));
    gLastBox = {pixels.x(), pixels.y(), pixels.width(), pixels.height()};
    return frame;
}

} // namespace crispplay::subs::app

namespace crispplay::subs::app {

std::shared_ptr<const veyra::engine::SubtitleBitmapFrame> renderOutline(const Region& requested, int canvasWidth, int canvasHeight) {
    if (canvasWidth < 32 || canvasHeight < 32) return nullptr;
    const Region region = sanitized(requested);
    // Whole pixels, kept inside the canvas so all four sides show.
    const int left = std::clamp(int(std::lround(region.left * canvasWidth)), 0, canvasWidth - 8);
    const int top = std::clamp(int(std::lround(region.top * canvasHeight)), 0, canvasHeight - 8);
    const int right = std::clamp(int(std::lround(region.right * canvasWidth)), left + 8, canvasWidth);
    const int bottom = std::clamp(int(std::lround(region.bottom * canvasHeight)), top + 8, canvasHeight);
    const int w = right - left, h = bottom - top;
    const int line = std::max(2, canvasHeight / 360);

    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, false);
        // Dark rim around the accent line: visible on bright and dark pictures alike.
        painter.fillRect(QRect(0, 0, w, h), QColor(0, 0, 0, 150));
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(QRect(line * 2, line * 2, w - line * 4, h - line * 4), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        const QColor accent(0xFF, 0x8A, 0x3D);   // Theme.accent
        painter.fillRect(QRect(line / 2, line / 2, w - line, line), accent);
        painter.fillRect(QRect(line / 2, h - line - line / 2, w - line, line), accent);
        painter.fillRect(QRect(line / 2, line / 2, line, h - line), accent);
        painter.fillRect(QRect(w - line - line / 2, line / 2, line, h - line), accent);
    }
    auto frame = std::make_shared<veyra::engine::SubtitleBitmapFrame>();
    frame->width = canvasWidth; frame->height = canvasHeight;
    veyra::engine::SubtitleBitmap bitmap;
    if (!encode(image, bitmap)) return nullptr;
    bitmap.x = left; bitmap.y = top;
    frame->images.push_back(std::move(bitmap));
    return frame;
}

} // namespace crispplay::subs::app
