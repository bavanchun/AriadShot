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
        for (const int id : applicationFontIds_) {
            QFontDatabase::removeApplicationFont(id);
        }
    }
    applicationFontIds_.clear();
    registeredFamilies_.clear();
    registered_ = false;
}

Expected<void, FontError> FontSet::registerFonts() {
    if (registered_) {
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
        {QStringLiteral(":/fonts/inter/Inter-Regular.ttf"), QStringLiteral("Inter")},
        {QStringLiteral(":/fonts/inter/Inter-Italic.ttf"), QStringLiteral("Inter")},
        {QStringLiteral(":/fonts/inter/Inter-Bold.ttf"), QStringLiteral("Inter")},
        {QStringLiteral(":/fonts/inter/Inter-BoldItalic.ttf"), QStringLiteral("Inter")},
        {QStringLiteral(":/fonts/noto-color-emoji/NotoColorEmoji.ttf"), QStringLiteral("Noto Color Emoji")},
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
        applicationFontIds_.append(id);

        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (!families.contains(resource.family)) {
            return fail(FontError::FamilyMismatch);
        }
        if (!registeredFamilies_.contains(resource.family)) {
            registeredFamilies_.append(resource.family);
        }
    }
#endif

    registered_ = true;
    return {};
}

QStringList FontSet::registeredFamilies() const { return registeredFamilies_; }

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
