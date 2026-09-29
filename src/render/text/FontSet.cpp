// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/text/FontSet.h"

#include <QFontDatabase>
#include <QGuiApplication>

#if defined(Q_OS_LINUX)
#include <QFile>

#include <array>

static void initializeFontResources() { Q_INIT_RESOURCE(fonts); }
#endif

namespace ariadshot::render {

QString describe(FontError error) {
    switch (error) {
    case FontError::GuiApplicationUnavailable:
        return QStringLiteral("font registration requires a QGuiApplication");
    case FontError::ResourceUnavailable:
        return QStringLiteral("a bundled font resource could not be read");
    case FontError::RegistrationFailed:
        return QStringLiteral("a bundled font could not be registered");
    case FontError::FamilyMismatch:
        return QStringLiteral("a bundled font registered with an unexpected family");
    }
    return QStringLiteral("unknown font registration error");
}

FontSet::~FontSet() { removeRegisteredFonts(); }

void FontSet::removeRegisteredFonts() noexcept {
    if (QGuiApplication::instance() != nullptr) {
        for (const int id : m_applicationFontIds) {
            QFontDatabase::removeApplicationFont(id);
        }
    }
    m_applicationFontIds.clear();
    m_registeredFamilies.clear();
    m_registered = false;
}

Expected<void, FontError> FontSet::registerFonts() {
    if (m_registered) {
        return {};
    }
    if (QGuiApplication::instance() == nullptr) {
        return tl::unexpected<FontError>(FontError::GuiApplicationUnavailable);
    }

#if defined(Q_OS_LINUX)
    initializeFontResources();

    struct FontResource {
        QString path;
        QString family;
    };
    static const std::array<FontResource, 5> resources{{
        {.path = QStringLiteral(":/fonts/inter/Inter-Regular.ttf"), .family = QStringLiteral("Inter")},
        {.path = QStringLiteral(":/fonts/inter/Inter-Italic.ttf"), .family = QStringLiteral("Inter")},
        {.path = QStringLiteral(":/fonts/inter/Inter-Bold.ttf"), .family = QStringLiteral("Inter")},
        {.path = QStringLiteral(":/fonts/inter/Inter-BoldItalic.ttf"), .family = QStringLiteral("Inter")},
        {.path = QStringLiteral(":/fonts/noto-color-emoji/NotoColorEmoji.ttf"),
         .family = QStringLiteral("Noto Color Emoji")},
    }};

    const auto fail = [this](FontError error) {
        removeRegisteredFonts();
        return tl::unexpected<FontError>(error);
    };

    for (const FontResource& resource : resources) {
        QFile file(resource.path);
        if (!file.open(QIODevice::ReadOnly)) {
            return fail(FontError::ResourceUnavailable);
        }

        const QByteArray data = file.readAll();
        if (data.isEmpty()) {
            return fail(FontError::ResourceUnavailable);
        }

        const int id = QFontDatabase::addApplicationFontFromData(data);
        if (id < 0) {
            return fail(FontError::RegistrationFailed);
        }
        m_applicationFontIds.append(id);

        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (!families.contains(resource.family)) {
            return fail(FontError::FamilyMismatch);
        }
        if (!m_registeredFamilies.contains(resource.family)) {
            m_registeredFamilies.append(resource.family);
        }
    }
#endif

    m_registered = true;
    return {};
}

QStringList FontSet::registeredFamilies() const { return m_registeredFamilies; }

QString FontSet::defaultFamily() const {
#if defined(Q_OS_MACOS)
    return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
#else
    return QStringLiteral("Inter");
#endif
}

QFont FontSet::textFont(const QString& requestedFamily, qreal pointSize, bool bold, bool italic) const {
    QString family = requestedFamily;
    if (family.isEmpty() || family == QStringLiteral("System") || !QFontDatabase::families().contains(family)) {
        family = defaultFamily();
    }

#if defined(Q_OS_MACOS)
    const QString emojiFamily = QStringLiteral("Apple Color Emoji");
#else
    const QString emojiFamily = QStringLiteral("Noto Color Emoji");
#endif

    QFont font;
    font.setFamilies({family, emojiFamily});
    font.setPointSizeF(pointSize);
    font.setBold(bold);
    font.setItalic(italic);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(QFont::NoSubpixelAntialias);
    return font;
}

} // namespace ariadshot::render
