// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QImage>

#include <cstdint>
#include <span>
#include <type_traits>

namespace ariadshot::render {

// A bounds-checked view of the pixels of a canonical image (QImage::Format_ARGB32_Premultiplied), one scan line or one
// pixel at a time. This is the audited pixel module: it is the only code that reads the image's memory through
// pointers; every other pixel loop works on the spans and pixels handed out here (and on its own scratch buffers).
//
// A pixel is a 32-bit 0xAARRGGBB value with premultiplied colour channels. The view is empty for a null image or any
// other format. A writable view detaches the image first, so it never changes a shared copy. A view stays valid while
// the image it was made from is alive and is neither resized nor detached. Thread: any (one thread per image).
template <typename Pixel> class BasicPixelSpan final {
  public:
    using Image = std::conditional_t<std::is_const_v<Pixel>, const QImage, QImage>;

    explicit BasicPixelSpan(Image& image) noexcept;

    [[nodiscard]] bool isEmpty() const noexcept { return m_first == nullptr; }
    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }

    // Scan line y as exactly width() pixels; an empty span when y is outside [0, height()). Indexing the span is
    // checked only where the standard library's assertions are on, so a loop over a row stays within the span's size.
    [[nodiscard]] std::span<Pixel> row(std::int64_t y) const noexcept;

    // The pixel at column x of row y, or nullptr when x is outside [0, width()) or y outside [0, height()). Both
    // coordinates are checked in every build, for any 64-bit values. Use it for a pixel at a computed position.
    [[nodiscard]] Pixel* pixel(std::int64_t x, std::int64_t y) const noexcept;

  private:
    using Byte = std::conditional_t<std::is_const_v<Pixel>, const unsigned char, unsigned char>;

    Byte* m_first = nullptr;
    qsizetype m_stride = 0;
    int m_width = 0;
    int m_height = 0;
};

using PixelSpan = BasicPixelSpan<std::uint32_t>;
using ConstPixelSpan = BasicPixelSpan<const std::uint32_t>;

extern template class BasicPixelSpan<std::uint32_t>;
extern template class BasicPixelSpan<const std::uint32_t>;

// Whether a view can be made of `image`: it is not null and is QImage::Format_ARGB32_Premultiplied. Code outside this
// module asks it instead of making a view, which only this module does.
[[nodiscard]] bool canViewPixels(const QImage& image) noexcept;

} // namespace ariadshot::render
