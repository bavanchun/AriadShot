// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/pixel/PixelSpan.h"

#include <cstddef>

namespace ariadshot::render {

template <typename Pixel> BasicPixelSpan<Pixel>::BasicPixelSpan(Image& image) noexcept {
    if (image.format() != QImage::Format_ARGB32_Premultiplied) {
        return;
    }
    Byte* first = nullptr;
    if constexpr (std::is_const_v<Pixel>) {
        first = image.constBits();
    } else {
        first = image.bits();
    }
    constexpr qsizetype kBytesPerPixel = sizeof(std::uint32_t);
    const qsizetype stride = image.bytesPerLine();
    const bool aligned =
        reinterpret_cast<std::uintptr_t>(first) % alignof(std::uint32_t) == 0 && stride % kBytesPerPixel == 0;
    if (first == nullptr || !aligned || stride < image.width() * kBytesPerPixel) {
        return;
    }
    m_first = first;
    m_stride = stride;
    m_width = image.width();
    m_height = image.height();
}

template <typename Pixel> std::span<Pixel> BasicPixelSpan<Pixel>::row(std::int64_t y) const noexcept {
    if (m_first == nullptr || y < 0 || y >= m_height) {
        return {};
    }
    return {reinterpret_cast<Pixel*>(m_first + m_stride * y), static_cast<std::size_t>(m_width)};
}

template <typename Pixel> Pixel* BasicPixelSpan<Pixel>::pixel(std::int64_t x, std::int64_t y) const noexcept {
    if (x < 0 || x >= m_width) {
        return nullptr;
    }
    const std::span<Pixel> line = row(y);
    return line.empty() ? nullptr : &line[static_cast<std::size_t>(x)];
}

template class BasicPixelSpan<std::uint32_t>;
template class BasicPixelSpan<const std::uint32_t>;

} // namespace ariadshot::render
