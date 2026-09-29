// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/CanonicalImage.h"
#include "render/text/FontSet.h"

#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QRawFont>
#include <QTest>
#include <QTextLayout>

#include <cstdlib>
#include <type_traits>

using ariadshot::render::FontError;
using ariadshot::render::FontSet;

class FontSetTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void initTestCase();
    void registersBundledFamilies();
    void textFontUsesExplicitFamiliesAndRenderingSettings();
    void missingFamilyFallsBackToDefault();
    void requestedFamilyIsNotTrimmed();
    void describesFontErrors();
    void emojiGlyphRendersColouredPixels();
    void resolvedEmojiFontUsesBundledData();

  private:
    FontSet m_fonts;
};

static_assert(std::is_member_function_pointer_v<decltype(&FontSet::registerFonts)>);
static_assert(std::is_member_function_pointer_v<decltype(&FontSet::textFont)>);

void FontSetTest::initTestCase() {
    const auto result = m_fonts.registerFonts();
    if (!result.has_value()) {
        QFAIL(qPrintable(ariadshot::render::describe(result.error())));
    }
}

void FontSetTest::registersBundledFamilies() {
    const auto result = m_fonts.registerFonts();
    if (!result.has_value()) {
        QFAIL(qPrintable(ariadshot::render::describe(result.error())));
    }

#if defined(Q_OS_MACOS)
    QVERIFY(m_fonts.registeredFamilies().isEmpty());
    QVERIFY(!m_fonts.registeredFamilies().contains(QStringLiteral("Noto Color Emoji")));
#else
    QCOMPARE(m_fonts.registeredFamilies(), (QStringList{QStringLiteral("Inter"), QStringLiteral("Noto Color Emoji")}));
    QVERIFY(QFontDatabase::families().contains(QStringLiteral("Inter")));
    QVERIFY(QFontDatabase::families().contains(QStringLiteral("Noto Color Emoji")));
#endif

    const auto repeated = m_fonts.registerFonts();
    if (!repeated.has_value()) {
        QFAIL(qPrintable(ariadshot::render::describe(repeated.error())));
    }
#if defined(Q_OS_MACOS)
    QVERIFY(m_fonts.registeredFamilies().isEmpty());
#else
    QCOMPARE(m_fonts.registeredFamilies(), (QStringList{QStringLiteral("Inter"), QStringLiteral("Noto Color Emoji")}));
#endif
}

void FontSetTest::textFontUsesExplicitFamiliesAndRenderingSettings() {
    // MacShot starts from its system font and converts it to bold/italic when those text traits are selected
    // (macshot/macshot/Model/Annotation.swift:1679-1693@b4d4f3a).
#if defined(Q_OS_MACOS)
    const QFont font = m_fonts.textFont(QString{}, 20.0, true, true);
#else
    const QFont font = m_fonts.textFont(QStringLiteral("Inter"), 20.0, true, true);
#endif

#if defined(Q_OS_MACOS)
    QCOMPARE(font.families().first(), QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
    QCOMPARE(font.families().last(), QStringLiteral("Apple Color Emoji"));
#else
    QCOMPARE(font.families(), (QStringList{QStringLiteral("Inter"), QStringLiteral("Noto Color Emoji")}));
#endif
    QVERIFY(font.bold());
    QVERIFY(font.italic());
    QCOMPARE(font.hintingPreference(), QFont::PreferNoHinting);
    QCOMPARE(font.styleStrategy(), QFont::NoSubpixelAntialias);
}

void FontSetTest::missingFamilyFallsBackToDefault() {
    const QFont font = m_fonts.textFont(QStringLiteral("AriadShot Missing Font"), 20.0);
    QCOMPARE(font.families().first(), m_fonts.defaultFamily());
#if defined(Q_OS_MACOS)
    QCOMPARE(font.families().last(), QStringLiteral("Apple Color Emoji"));
#else
    QCOMPARE(font.families(), (QStringList{QStringLiteral("Inter"), QStringLiteral("Noto Color Emoji")}));
#endif
}

void FontSetTest::requestedFamilyIsNotTrimmed() {
    // MacShot resolves the exact selected family name, then falls back if it is unavailable
    // (macshot/macshot/Model/Annotation.swift:1679-1684@b4d4f3a).
#if defined(Q_OS_LINUX)
    const QFont font = m_fonts.textFont(QStringLiteral("Noto Color Emoji "), 20.0);
    QCOMPARE(font.families().first(), m_fonts.defaultFamily());
#else
    QSKIP("the bundled picker family is available only on Linux");
#endif
}

void FontSetTest::describesFontErrors() {
    QCOMPARE(ariadshot::render::describe(FontError::GuiApplicationUnavailable),
             QStringLiteral("font registration requires a QGuiApplication"));
    QCOMPARE(ariadshot::render::describe(FontError::ResourceUnavailable),
             QStringLiteral("a bundled font resource could not be read"));
    QCOMPARE(ariadshot::render::describe(FontError::RegistrationFailed),
             QStringLiteral("a bundled font could not be registered"));
    QCOMPARE(ariadshot::render::describe(FontError::FamilyMismatch),
             QStringLiteral("a bundled font registered with an unexpected family"));
}

void FontSetTest::emojiGlyphRendersColouredPixels() {
    QImage image = ariadshot::render::makeCanonicalImage(QSize(128, 128), 1.0);
    QVERIFY(!image.isNull());

    QPainter painter(&image);
    QFont font = m_fonts.textFont(QStringLiteral("Inter"), 20.0);
    font.setPixelSize(112);
    painter.setFont(font);
    painter.drawText(image.rect(), Qt::AlignCenter, QStringLiteral("😀"));
    painter.end();

    int colouredPixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor colour = image.pixelColor(x, y);
            if (colour.alpha() > 0 &&
                (std::abs(colour.red() - colour.green()) > 12 || std::abs(colour.green() - colour.blue()) > 12)) {
                ++colouredPixels;
            }
        }
    }
    QVERIFY2(colouredPixels > 0, "the emoji glyph must paint coloured pixels into a canonical image");
}

void FontSetTest::resolvedEmojiFontUsesBundledData() {
#if defined(Q_OS_LINUX)
    const QFont font = m_fonts.textFont(QStringLiteral("Inter"), 20.0);
    QTextLayout layout(QStringLiteral("😀"), font);
    layout.beginLayout();
    Q_UNUSED(layout.createLine());
    layout.endLayout();

    QFile bundledFont(QStringLiteral(":/fonts/noto-color-emoji/NotoColorEmoji.ttf"));
    QVERIFY(bundledFont.open(QIODevice::ReadOnly));
    const QRawFont expectedFont(bundledFont.readAll(), 16.0);
    QVERIFY(expectedFont.isValid());

    bool resolvedBundledEmoji = false;
    const QList<QGlyphRun> runs = layout.glyphRuns();
    for (const QGlyphRun& run : runs) {
        const QRawFont resolvedFont = run.rawFont();
        if (resolvedFont.familyName() == QStringLiteral("Noto Color Emoji")) {
            QCOMPARE(resolvedFont.fontTable("head"), expectedFont.fontTable("head"));
            resolvedBundledEmoji = true;
        }
    }
    QVERIFY2(resolvedBundledEmoji, "the emoji fallback must resolve to the bundled Noto Color Emoji font");
#else
    QSKIP("macOS uses Apple Color Emoji");
#endif
}

QTEST_MAIN(FontSetTest)
#include "tst_FontSet.moc"
