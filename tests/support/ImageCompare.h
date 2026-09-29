// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QImage>
#include <QString>
#include <QtGlobal>

namespace ariadshot::tests {

// A CIELAB colour relative to the D65 white point.
struct Lab {
    double l = 0.0;
    double a = 0.0;
    double b = 0.0;
};

struct ComparisonResult {
    bool passed = false;
    // Why the comparison failed; empty when it passed.
    QString failure;
    // Pixels outside the tolerance mask, and how many of them passed.
    qint64 comparedPixels = 0;
    qint64 passingPixels = 0;
    // Pixels that failed on alpha alone (perceptual class).
    qint64 alphaMismatches = 0;
    double passingFraction = 0.0;
    // Over the pixels compared by colour (perceptual class).
    double maxDeltaE = 0.0;
    double p99DeltaE = 0.0;
    // Opaque white where a pixel failed, transparent elsewhere; null when the images could not be compared.
    QImage failureMask;
};

// The golden-image comparator of docs/dev/testing.md, implemented once for every test and tool. Thread: any.
class ImageCompare {
  public:
    struct Threshold {
        double maxDeltaE = 0.0;
        double minPassingFraction = 1.0;
    };

    // Fractional-scale presentation against the canonical image resampled with the documented filter.
    static constexpr Threshold kPresentation{.maxDeltaE = 1.0, .minPassingFraction = 0.999};
    // AriadShot output against the MacShot reference corpus.
    static constexpr Threshold kCorpus{.maxDeltaE = 2.0, .minPassingFraction = 0.995};

    // Exact class: equal pixel size and device pixel ratio, and byte-equal pixel data after converting both images to
    // QImage::Format_RGBA8888 (Qt's un-premultiplication is the single rounding rule).
    [[nodiscard]] static ComparisonResult compareExact(const QImage& actual, const QImage& expected);

    // Perceptual class. Per pixel, on un-premultiplied colours: both alphas below 8/255 pass; otherwise an alpha
    // difference above 1/255 fails; otherwise the pixel passes when the CIEDE2000 difference of the sRGB colours is at
    // most threshold.maxDeltaE. The comparison passes when the passing fraction of the pixels outside the tolerance
    // mask (pixels whose mask alpha is not zero are excluded) is at least threshold.minPassingFraction.
    [[nodiscard]] static ComparisonResult comparePerceptual(const QImage& actual, const QImage& expected,
                                                            Threshold threshold, const QImage& toleranceMask = {});

    // sRGB (IEC 61966-2-1) -> linear -> XYZ (D65) -> CIELAB, ignoring alpha.
    [[nodiscard]] static Lab toLab(QRgb srgb);
    // CIEDE2000 colour difference with kL = kC = kH = 1 (Sharma, Wu and Dalal, 2005).
    [[nodiscard]] static double ciede2000(const Lab& first, const Lab& second);

    // The directory for this test's artifacts: $ARIADSHOT_TEST_OUTPUT_DIR (set by CTest), else the working directory.
    [[nodiscard]] static QString outputDirectory();
    // <goldenDir>/<suite>/<name>.<cpu architecture>.png when that platform variant exists, else <name>.png.
    [[nodiscard]] static QString goldenPath(const QString& goldenDir, const QString& suite, const QString& name);
    // With ARIADSHOT_WRITE_GOLDEN_CANDIDATES=1, writes <outputDir>/candidates/<suite>/<name>.png and returns its path.
    // Candidates stay in the build directory; a reviewed pull request copies them into tests/golden/.
    static QString writeCandidateIfRequested(const QString& outputDir, const QString& suite, const QString& name,
                                             const QImage& image);
    // Writes actual.png, expected.png, mask.png and statistics.txt into <outputDir>/<name>/.
    static bool writeFailureArtifacts(const QString& outputDir, const QString& name, const QImage& actual,
                                      const QImage& expected, const ComparisonResult& result);
};

} // namespace ariadshot::tests
