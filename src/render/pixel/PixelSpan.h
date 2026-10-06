// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QImage>

#include <cstdint>
#include <span>
#include <type_traits>

namespace ariadshot::render {

// A bounds-checked view of the pixels of a canonical image (QImage::Format_ARGB32_Premultiplied), one scan line at a
// time. This is the audited pixel module: it is the only code that reads the image's memory through pointers; every
// other pixel loop works on the spans handed out here (and on its own scratch buffers).
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

    // Scan line y as exactly width() pixels; an empty span when y is outside [0, height()).
    [[nodiscard]] std::span<Pixel> row(int y) const noexcept;

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

} // namespace ariadshot::render
