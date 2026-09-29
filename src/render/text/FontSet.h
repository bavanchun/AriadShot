// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/Expected.h"

#include <QFont>
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

// Registers the bundled Linux font resources once. On macOS, the system font and Apple Color Emoji remain in use.
// Call on the GUI thread before constructing text fonts. Thread: GUI.
class FontSet final {
  public:
    [[nodiscard]] static Expected<void, FontError> registerFonts();
    [[nodiscard]] static QStringList registeredFamilies();
    [[nodiscard]] static QString defaultFamily();

    // A missing family falls back to the platform default, as MacShot does for an unavailable font-picker choice
    // (macshot/macshot/Model/Annotation.swift:1679-1684@b4d4f3a).
    // Call registerFonts() first and check its result. Thread: GUI.
    [[nodiscard]] static QFont textFont(const QString& requestedFamily, qreal pointSize, bool bold = false,
                                        bool italic = false);
};

} // namespace ariadshot::render
