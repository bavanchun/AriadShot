// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QTest>

namespace {

// Every raw pixel loop lives in src/render/pixel/ (docs/spec/02-modules-and-interfaces.md section 5). Code elsewhere
// calls the module and never reaches image memory: no view over it, no scan line, no pointer to its bytes and no
// single-pixel accessor of QImage or of the view.
struct Access {
    const char* what;
    QRegularExpression expression;
};

const QList<Access>& accesses() {
    static const QList<Access> list = {
        {.what = "a pixel view", .expression = QRegularExpression(QStringLiteral("PixelSpan"))},
        {.what = "a scan line", .expression = QRegularExpression(QStringLiteral("\\b(constScanLine|scanLine)\\s*\\("))},
        {.what = "the image bytes", .expression = QRegularExpression(QStringLiteral("\\b(constBits|bits)\\s*\\("))},
        {.what = "a single pixel",
         .expression = QRegularExpression(QStringLiteral("\\b(setPixelColor|setPixel|pixelColor)\\s*\\("))},
        {.what = "a single pixel", .expression = QRegularExpression(QStringLiteral("(\\.|->)pixel\\s*\\("))},
        {.what = "a row of a view", .expression = QRegularExpression(QStringLiteral("(\\.|->)row\\s*\\(\\s*[^)\\s]"))},
    };
    return list;
}

// The text without comments and include directives: a header may be included without anything being reached.
QString withoutCommentsAndIncludes(QString text) {
    static const QRegularExpression blockComments(QStringLiteral("/\\*.*?\\*/"),
                                                  QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression lineComments(QStringLiteral("//[^\\n]*"));
    static const QRegularExpression includes(QStringLiteral("^[ \\t]*#[ \\t]*include[^\\n]*"),
                                             QRegularExpression::MultilineOption);
    text.remove(blockComments);
    text.remove(lineComments);
    text.remove(includes);
    return text;
}

// What the text reaches that only the pixel module may reach, one entry per kind of access.
QStringList accessesIn(const QString& text) {
    const QString code = withoutCommentsAndIncludes(text);
    QStringList found;
    for (const Access& access : accesses()) {
        if (code.contains(access.expression)) {
            found.append(QString::fromLatin1(access.what));
        }
    }
    return found;
}

} // namespace

class PixelBoundaryTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void theScanRecognisesEveryKindOfAccess_data();
    void theScanRecognisesEveryKindOfAccess();
    void theScanIgnoresUnrelatedCode_data();
    void theScanIgnoresUnrelatedCode();
    void pixelMemoryIsReachedOnlyThroughThePixelModule();
};

void PixelBoundaryTest::theScanRecognisesEveryKindOfAccess_data() {
    QTest::addColumn<QString>("code");
    QTest::addColumn<QString>("what");

    QTest::newRow("a writable view") << QStringLiteral("const PixelSpan view(image);")
                                     << QStringLiteral("a pixel view");
    QTest::newRow("a read-only view") << QStringLiteral("ConstPixelSpan in(source);") << QStringLiteral("a pixel view");
    QTest::newRow("a scan line") << QStringLiteral("auto* p = image.scanLine(y);") << QStringLiteral("a scan line");
    QTest::newRow("a const scan line") << QStringLiteral("auto* p = image.constScanLine(y);")
                                       << QStringLiteral("a scan line");
    QTest::newRow("the image bytes") << QStringLiteral("uchar* p = image.bits();") << QStringLiteral("the image bytes");
    QTest::newRow("the const image bytes")
        << QStringLiteral("const uchar* p = image.constBits();") << QStringLiteral("the image bytes");
    QTest::newRow("a pixel of an image") << QStringLiteral("auto v = image.pixel(1, 2);")
                                         << QStringLiteral("a single pixel");
    QTest::newRow("a pixel through a pointer")
        << QStringLiteral("auto v = view->pixel(1, 2);") << QStringLiteral("a single pixel");
    QTest::newRow("setting a pixel") << QStringLiteral("image.setPixel(1, 2, v);") << QStringLiteral("a single pixel");
    QTest::newRow("a pixel colour") << QStringLiteral("QColor c = image.pixelColor(1, 2);")
                                    << QStringLiteral("a single pixel");
    QTest::newRow("a row of a view") << QStringLiteral("auto line = view.row(y);") << QStringLiteral("a row of a view");
    QTest::newRow("a row with a computed index")
        << QStringLiteral("view.row( y + 1 )[x] = 0;") << QStringLiteral("a row of a view");
}

void PixelBoundaryTest::theScanRecognisesEveryKindOfAccess() {
    QFETCH(QString, code);
    QFETCH(QString, what);

    QVERIFY(accessesIn(code).contains(what));
}

void PixelBoundaryTest::theScanIgnoresUnrelatedCode_data() {
    QTest::addColumn<QString>("code");

    QTest::newRow("a model index row") << QStringLiteral("const int r = index.row();");
    QTest::newRow("a function named like a pixel") << QStringLiteral("const int n = pixelCount(image);");
    QTest::newRow("a bit count") << QStringLiteral("const int n = countBits(mask);");
    QTest::newRow("a line comment") << QStringLiteral("// the view is a PixelSpan, never scanLine(y)\nint x = 0;");
    QTest::newRow("a block comment") << QStringLiteral("/* image.bits() and\n   view.row(y) */ int x = 0;");
    QTest::newRow("a canonical image factory") << QStringLiteral("QImage image = makeCanonicalImage(size, 1.0);");
    QTest::newRow("the include of the pixel module header")
        << QStringLiteral("#include \"render/pixel/PixelSpan.h\"\nconst bool ok = canViewPixels(image);");
}

void PixelBoundaryTest::theScanIgnoresUnrelatedCode() {
    QFETCH(QString, code);

    QVERIFY2(accessesIn(code).isEmpty(), qPrintable(accessesIn(code).join(QLatin1Char(','))));
}

void PixelBoundaryTest::pixelMemoryIsReachedOnlyThroughThePixelModule() {
    const QDir sources(QStringLiteral(ARIADSHOT_TEST_SOURCE_DIR));
    QVERIFY2(sources.exists(), "the source directory is missing");

    int scanned = 0;
    QStringList violations;
    QDirIterator files(sources.path(), {QStringLiteral("*.h"), QStringLiteral("*.cpp"), QStringLiteral("*.mm")},
                       QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString path = sources.relativeFilePath(files.next());
        if (path.startsWith(QStringLiteral("render/pixel/"))) {
            continue;
        }
        QFile file(sources.filePath(path));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));
        ++scanned;
        for (const QString& what : accessesIn(QString::fromUtf8(file.readAll()))) {
            violations.append(QStringLiteral("%1 reaches %2").arg(path, what));
        }
    }

    // The render sources sit next to the pixel module; seeing none of them means the scan looked in the wrong place.
    QVERIFY2(scanned >= 10, qPrintable(QStringLiteral("only %1 files scanned").arg(scanned)));
    QVERIFY2(violations.isEmpty(), qPrintable(violations.join(QLatin1Char('\n'))));
}

QTEST_GUILESS_MAIN(PixelBoundaryTest)
#include "tst_PixelBoundary.moc"
