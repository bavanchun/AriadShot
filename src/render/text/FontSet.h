// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/Expected.h"

#include <QFont>
#include <QList>
#include <QString>
#include <QStringList>

namespace ariadshot::render {

enum class FontError {
    GuiApplicationUnavailable,
    ResourceUnavailable,
    RegistrationFailed,
    FamilyMismatch,
};

[[nodiscard]] QString describe(FontError error);

// Owns the bundled Linux font registrations. On macOS, the system font and Apple Color Emoji remain in use.
// Keep this instance alive while its text fonts are in use. Thread: GUI.
class FontSet final {
  public:
    FontSet() = default;
    ~FontSet();

    FontSet(const FontSet&) = delete;
    FontSet& operator=(const FontSet&) = delete;
    FontSet(FontSet&&) = delete;
    FontSet& operator=(FontSet&&) = delete;

    [[nodiscard]] Expected<void, FontError> registerFonts();
    [[nodiscard]] QStringList registeredFamilies() const;
    [[nodiscard]] QString defaultFamily() const;

    // A missing family falls back to the platform default, as MacShot does for an unavailable font-picker choice
    // (macshot/macshot/Model/Annotation.swift:1679-1684@b4d4f3a).
    // Call registerFonts() first and check its result. Thread: GUI.
    [[nodiscard]] QFont textFont(const QString& requestedFamily, qreal pointSize, bool bold = false,
                                 bool italic = false) const;

  private:
    void removeRegisteredFonts() noexcept;

    QList<int> applicationFontIds_;
    QStringList registeredFamilies_;
    bool registered_ = false;
};

} // namespace ariadshot::render
