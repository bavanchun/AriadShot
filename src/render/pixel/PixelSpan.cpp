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

template <typename Pixel> std::span<Pixel> BasicPixelSpan<Pixel>::row(int y) const noexcept {
    if (m_first == nullptr || y < 0 || y >= m_height) {
        return {};
    }
    return {reinterpret_cast<Pixel*>(m_first + m_stride * y), static_cast<std::size_t>(m_width)};
}

template class BasicPixelSpan<std::uint32_t>;
template class BasicPixelSpan<const std::uint32_t>;

} // namespace ariadshot::render
