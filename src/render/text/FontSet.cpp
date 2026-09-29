// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/text/FontSet.h"

#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QResource>

#include <array>

static void initializeFontResources() { Q_INIT_RESOURCE(fonts); }

namespace ariadshot::render {

namespace {

struct RegistrationState {
    QList<int> applicationFontIds;
    QStringList families;
    bool complete = false;
};

RegistrationState& registrationState() {
    static RegistrationState state;
    return state;
}

void removeRegisteredFonts(RegistrationState& state) {
    for (const int id : state.applicationFontIds) {
        QFontDatabase::removeApplicationFont(id);
    }
    state.applicationFontIds.clear();
    state.families.clear();
    state.complete = false;
}

} // namespace

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

Expected<void, FontError> FontSet::registerFonts() {
    RegistrationState& state = registrationState();
    if (state.complete) {
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

    const auto fail = [&state](FontError error) {
        removeRegisteredFonts(state);
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
        state.applicationFontIds.append(id);

        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (!families.contains(resource.family)) {
            return fail(FontError::FamilyMismatch);
        }
        if (!state.families.contains(resource.family)) {
            state.families.append(resource.family);
        }
    }
#endif

    state.complete = true;
    return {};
}

QStringList FontSet::registeredFamilies() { return registrationState().families; }

QString FontSet::defaultFamily() {
#if defined(Q_OS_MACOS)
    return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
#else
    return QStringLiteral("Inter");
#endif
}

QFont FontSet::textFont(const QString& requestedFamily, qreal pointSize, bool bold, bool italic) {
    QString family = requestedFamily.trimmed();
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
