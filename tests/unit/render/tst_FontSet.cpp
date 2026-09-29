// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "render/CanonicalImage.h"
#include "render/text/FontSet.h"

#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QTest>

#include <cstdlib>

using ariadshot::render::FontError;
using ariadshot::render::FontSet;

class FontSetTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void registersBundledFamilies();
    void textFontUsesExplicitFamiliesAndRenderingSettings();
    void missingFamilyFallsBackToDefault();
    void emojiGlyphRendersColouredPixels();
};

void FontSetTest::registersBundledFamilies() {
    const auto result = FontSet::registerFonts();
    if (!result.has_value()) {
        QFAIL(qPrintable(ariadshot::render::describe(result.error())));
    }

#if defined(Q_OS_MACOS)
    QVERIFY(FontSet::registeredFamilies().isEmpty());
    QVERIFY(!FontSet::registeredFamilies().contains(QStringLiteral("Noto Color Emoji")));
#else
    QCOMPARE(FontSet::registeredFamilies(), (QStringList{QStringLiteral("Inter"), QStringLiteral("Noto Color Emoji")}));
    QVERIFY(QFontDatabase::families().contains(QStringLiteral("Inter")));
    QVERIFY(QFontDatabase::families().contains(QStringLiteral("Noto Color Emoji")));
#endif

    const auto repeated = FontSet::registerFonts();
    if (!repeated.has_value()) {
        QFAIL(qPrintable(ariadshot::render::describe(repeated.error())));
    }
#if defined(Q_OS_MACOS)
    QVERIFY(FontSet::registeredFamilies().isEmpty());
#else
    QCOMPARE(FontSet::registeredFamilies(), (QStringList{QStringLiteral("Inter"), QStringLiteral("Noto Color Emoji")}));
#endif
}

void FontSetTest::textFontUsesExplicitFamiliesAndRenderingSettings() {
    // MacShot starts from its system font and converts it to bold/italic when those text traits are selected
    // (macshot/macshot/Model/Annotation.swift:1679-1693@b4d4f3a).
#if defined(Q_OS_MACOS)
    const QFont font = FontSet::textFont(QString{}, 20.0, true, true);
#else
    const QFont font = FontSet::textFont(QStringLiteral("Inter"), 20.0, true, true);
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
    const QFont font = FontSet::textFont(QStringLiteral("AriadShot Missing Font"), 20.0);
    QCOMPARE(font.families().first(), FontSet::defaultFamily());
#if defined(Q_OS_MACOS)
    QCOMPARE(font.families().last(), QStringLiteral("Apple Color Emoji"));
#else
    QCOMPARE(font.families(), (QStringList{QStringLiteral("Inter"), QStringLiteral("Noto Color Emoji")}));
#endif
}

void FontSetTest::emojiGlyphRendersColouredPixels() {
    QImage image = ariadshot::render::makeCanonicalImage(QSize(128, 128), 1.0);
    QVERIFY(!image.isNull());

    QPainter painter(&image);
    QFont font = FontSet::textFont(QStringLiteral("Inter"), 20.0);
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

QTEST_MAIN(FontSetTest)
#include "tst_FontSet.moc"
