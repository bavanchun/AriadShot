// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "support/ImageCompare.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSysInfo>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

namespace ariadshot::tests {

namespace {

using namespace Qt::StringLiterals;

constexpr double kDegrees = 180.0 / std::numbers::pi;
constexpr double kRadians = std::numbers::pi / 180.0;
// Pixels whose alphas are both below 8/255 are invisible and always pass.
constexpr int kInvisibleAlpha = 8;
// A larger alpha difference fails the pixel regardless of colour.
constexpr int kMaxAlphaDifference = 1;

double srgbToLinear(int channel) {
    const double value = channel / 255.0;
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double labF(double t) {
    constexpr double kDelta = 6.0 / 29.0;
    return t > kDelta * kDelta * kDelta ? std::cbrt(t) : t / (3.0 * kDelta * kDelta) + 4.0 / 29.0;
}

double hueDegrees(double b, double aPrime) {
    if (b == 0.0 && aPrime == 0.0) {
        return 0.0;
    }
    const double hue = std::atan2(b, aPrime) * kDegrees;
    return hue < 0.0 ? hue + 360.0 : hue;
}

double pow7(double value) {
    const double squared = value * value;
    return squared * squared * squared * value;
}

QImage failureMaskFor(QSize size) {
    QImage mask(size, QImage::Format_ARGB32);
    mask.fill(Qt::transparent);
    return mask;
}

} // namespace

ComparisonResult ImageCompare::compareExact(const QImage& actual, const QImage& expected) {
    ComparisonResult result;
    if (actual.isNull() || expected.isNull()) {
        result.failure = u"cannot compare a null image"_s;
        return result;
    }
    if (actual.size() != expected.size()) {
        result.failure = u"pixel size differs: actual %1x%2, expected %3x%4"_s.arg(actual.width())
                             .arg(actual.height())
                             .arg(expected.width())
                             .arg(expected.height());
        return result;
    }
    if (actual.devicePixelRatio() != expected.devicePixelRatio()) {
        result.failure = u"device pixel ratio differs: actual %1, expected %2"_s.arg(actual.devicePixelRatio())
                             .arg(expected.devicePixelRatio());
        return result;
    }

    const QImage left = actual.convertToFormat(QImage::Format_RGBA8888);
    const QImage right = expected.convertToFormat(QImage::Format_RGBA8888);
    result.failureMask = failureMaskFor(actual.size());
    result.comparedPixels = static_cast<qint64>(actual.width()) * actual.height();
    const auto rowBytes = static_cast<size_t>(actual.width()) * 4;
    for (int y = 0; y < actual.height(); ++y) {
        const uchar* leftRow = left.constScanLine(y);
        const uchar* rightRow = right.constScanLine(y);
        if (std::memcmp(leftRow, rightRow, rowBytes) == 0) {
            result.passingPixels += actual.width();
            continue;
        }
        for (int x = 0; x < actual.width(); ++x) {
            const auto offset = static_cast<qsizetype>(x) * 4;
            if (std::memcmp(leftRow + offset, rightRow + offset, 4) == 0) {
                ++result.passingPixels;
            } else {
                result.failureMask.setPixel(x, y, qRgba(255, 255, 255, 255));
            }
        }
    }
    result.passingFraction = static_cast<double>(result.passingPixels) / static_cast<double>(result.comparedPixels);
    result.passed = result.passingPixels == result.comparedPixels;
    if (!result.passed) {
        result.failure =
            u"%1 of %2 pixels differ (exact comparison)"_s.arg(result.comparedPixels - result.passingPixels)
                .arg(result.comparedPixels);
    }
    return result;
}

ComparisonResult ImageCompare::comparePerceptual(const QImage& actual, const QImage& expected, Threshold threshold,
                                                 const QImage& toleranceMask) {
    ComparisonResult result;
    if (actual.isNull() || expected.isNull()) {
        result.failure = u"cannot compare a null image"_s;
        return result;
    }
    if (actual.size() != expected.size()) {
        result.failure = u"pixel size differs: actual %1x%2, expected %3x%4"_s.arg(actual.width())
                             .arg(actual.height())
                             .arg(expected.width())
                             .arg(expected.height());
        return result;
    }
    if (!toleranceMask.isNull() && toleranceMask.size() != actual.size()) {
        result.failure = u"tolerance mask size differs from the image size"_s;
        return result;
    }

    const QImage left = actual.convertToFormat(QImage::Format_RGBA8888);
    const QImage right = expected.convertToFormat(QImage::Format_RGBA8888);
    const QImage mask = toleranceMask.isNull() ? QImage() : toleranceMask.convertToFormat(QImage::Format_ARGB32);
    result.failureMask = failureMaskFor(actual.size());

    std::vector<double> deltas;
    deltas.reserve(static_cast<size_t>(actual.width()) * static_cast<size_t>(actual.height()));
    for (int y = 0; y < actual.height(); ++y) {
        const uchar* leftRow = left.constScanLine(y);
        const uchar* rightRow = right.constScanLine(y);
        for (int x = 0; x < actual.width(); ++x) {
            if (!mask.isNull() && qAlpha(mask.pixel(x, y)) != 0) {
                continue;
            }
            ++result.comparedPixels;
            const auto offset = static_cast<qsizetype>(x) * 4;
            const uchar* l = leftRow + offset;
            const uchar* r = rightRow + offset;
            const int leftAlpha = l[3];
            const int rightAlpha = r[3];
            bool pass = false;
            if (leftAlpha < kInvisibleAlpha && rightAlpha < kInvisibleAlpha) {
                pass = true;
            } else if (std::abs(leftAlpha - rightAlpha) > kMaxAlphaDifference) {
                ++result.alphaMismatches;
            } else {
                const double delta = ciede2000(toLab(qRgb(l[0], l[1], l[2])), toLab(qRgb(r[0], r[1], r[2])));
                deltas.push_back(delta);
                pass = delta <= threshold.maxDeltaE;
            }
            if (pass) {
                ++result.passingPixels;
            } else {
                result.failureMask.setPixel(x, y, qRgba(255, 255, 255, 255));
            }
        }
    }

    if (!deltas.empty()) {
        result.maxDeltaE = *std::ranges::max_element(deltas);
        const auto rank = static_cast<size_t>(std::ceil(0.99 * static_cast<double>(deltas.size()))) - 1;
        std::ranges::nth_element(deltas, deltas.begin() + static_cast<std::ptrdiff_t>(rank));
        result.p99DeltaE = deltas[rank];
    }
    result.passingFraction = result.comparedPixels == 0 ? 1.0
                                                        : static_cast<double>(result.passingPixels) /
                                                              static_cast<double>(result.comparedPixels);
    result.passed = result.passingFraction >= threshold.minPassingFraction;
    if (!result.passed) {
        result.failure =
            u"passing fraction %1 is below %2 (ΔE00 ≤ %3; %4 alpha mismatches)"_s.arg(result.passingFraction, 0, 'f', 6)
                .arg(threshold.minPassingFraction, 0, 'f', 6)
                .arg(threshold.maxDeltaE)
                .arg(result.alphaMismatches);
    }
    return result;
}

Lab ImageCompare::toLab(QRgb srgb) {
    const double red = srgbToLinear(qRed(srgb));
    const double green = srgbToLinear(qGreen(srgb));
    const double blue = srgbToLinear(qBlue(srgb));
    // IEC 61966-2-1 matrix and its D65 white point (the row sums: 0.9505, 1.0000, 1.0890), so sRGB white maps to
    // L = 100, a = b = 0.
    constexpr double kWhiteX = 0.4124 + 0.3576 + 0.1805;
    constexpr double kWhiteY = 0.2126 + 0.7152 + 0.0722;
    constexpr double kWhiteZ = 0.0193 + 0.1192 + 0.9505;
    const double x = (0.4124 * red + 0.3576 * green + 0.1805 * blue) / kWhiteX;
    const double y = (0.2126 * red + 0.7152 * green + 0.0722 * blue) / kWhiteY;
    const double z = (0.0193 * red + 0.1192 * green + 0.9505 * blue) / kWhiteZ;
    const double fx = labF(x);
    const double fy = labF(y);
    const double fz = labF(z);
    return {.l = 116.0 * fy - 16.0, .a = 500.0 * (fx - fy), .b = 200.0 * (fy - fz)};
}

double ImageCompare::ciede2000(const Lab& first, const Lab& second) {
    const double c1 = std::hypot(first.a, first.b);
    const double c2 = std::hypot(second.a, second.b);
    const double cMean7 = pow7((c1 + c2) / 2.0);
    const double g = 0.5 * (1.0 - std::sqrt(cMean7 / (cMean7 + pow7(25.0))));
    const double a1 = (1.0 + g) * first.a;
    const double a2 = (1.0 + g) * second.a;
    const double c1Prime = std::hypot(a1, first.b);
    const double c2Prime = std::hypot(a2, second.b);
    const double h1 = hueDegrees(first.b, a1);
    const double h2 = hueDegrees(second.b, a2);

    const double deltaL = second.l - first.l;
    const double deltaC = c2Prime - c1Prime;
    const double chromaProduct = c1Prime * c2Prime;
    double deltaHue = 0.0;
    if (chromaProduct != 0.0) {
        deltaHue = h2 - h1;
        if (deltaHue > 180.0) {
            deltaHue -= 360.0;
        } else if (deltaHue < -180.0) {
            deltaHue += 360.0;
        }
    }
    const double deltaH = 2.0 * std::sqrt(chromaProduct) * std::sin(deltaHue / 2.0 * kRadians);

    const double lMean = (first.l + second.l) / 2.0;
    const double cMeanPrime = (c1Prime + c2Prime) / 2.0;
    double hMean = h1 + h2;
    if (chromaProduct != 0.0) {
        if (std::abs(h1 - h2) <= 180.0) {
            hMean = (h1 + h2) / 2.0;
        } else if (h1 + h2 < 360.0) {
            hMean = (h1 + h2 + 360.0) / 2.0;
        } else {
            hMean = (h1 + h2 - 360.0) / 2.0;
        }
    }

    const double t = 1.0 - 0.17 * std::cos((hMean - 30.0) * kRadians) + 0.24 * std::cos(2.0 * hMean * kRadians) +
                     0.32 * std::cos((3.0 * hMean + 6.0) * kRadians) - 0.20 * std::cos((4.0 * hMean - 63.0) * kRadians);
    const double deltaTheta = 30.0 * std::exp(-std::pow((hMean - 275.0) / 25.0, 2.0));
    const double cMeanPrime7 = pow7(cMeanPrime);
    const double rc = 2.0 * std::sqrt(cMeanPrime7 / (cMeanPrime7 + pow7(25.0)));
    const double lOffset = (lMean - 50.0) * (lMean - 50.0);
    const double sl = 1.0 + 0.015 * lOffset / std::sqrt(20.0 + lOffset);
    const double sc = 1.0 + 0.045 * cMeanPrime;
    const double sh = 1.0 + 0.015 * cMeanPrime * t;
    const double rt = -std::sin(2.0 * deltaTheta * kRadians) * rc;

    const double lightness = deltaL / sl;
    const double chroma = deltaC / sc;
    const double hue = deltaH / sh;
    return std::sqrt(lightness * lightness + chroma * chroma + hue * hue + rt * chroma * hue);
}

QString ImageCompare::outputDirectory() {
    const QString configured = qEnvironmentVariable("ARIADSHOT_TEST_OUTPUT_DIR");
    return configured.isEmpty() ? QDir::currentPath() : configured;
}

QString ImageCompare::goldenPath(const QString& goldenDir, const QString& suite, const QString& name) {
    QString variant = u"%1/%2/%3.%4.png"_s.arg(goldenDir, suite, name, QSysInfo::buildCpuArchitecture());
    if (QFileInfo::exists(variant)) {
        return variant;
    }
    return u"%1/%2/%3.png"_s.arg(goldenDir, suite, name);
}

QString ImageCompare::writeCandidateIfRequested(const QString& outputDir, const QString& suite, const QString& name,
                                                const QImage& image) {
    if (qEnvironmentVariable("ARIADSHOT_WRITE_GOLDEN_CANDIDATES") != u"1"_s) {
        return {};
    }
    const QString directory = u"%1/candidates/%2"_s.arg(outputDir, suite);
    QString path = u"%1/%2.png"_s.arg(directory, name);
    if (!QDir().mkpath(directory) || !image.save(path, "PNG")) {
        qWarning("Could not write the golden candidate %s", qPrintable(path));
        return {};
    }
    qInfo("Golden candidate written to %s", qPrintable(path));
    return path;
}

bool ImageCompare::writeFailureArtifacts(const QString& outputDir, const QString& name, const QImage& actual,
                                         const QImage& expected, const ComparisonResult& result) {
    const QString directory = u"%1/%2"_s.arg(outputDir, name);
    if (!QDir().mkpath(directory)) {
        return false;
    }
    bool written = actual.save(directory + u"/actual.png"_s, "PNG");
    written = expected.save(directory + u"/expected.png"_s, "PNG") && written;
    if (!result.failureMask.isNull()) {
        written = result.failureMask.save(directory + u"/mask.png"_s, "PNG") && written;
    }
    QFile statistics(directory + u"/statistics.txt"_s);
    if (!statistics.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&statistics);
    out << "failure: " << result.failure << '\n'
        << "compared pixels: " << result.comparedPixels << '\n'
        << "passing pixels: " << result.passingPixels << '\n'
        << "passing fraction: " << QString::number(result.passingFraction, 'f', 6) << '\n'
        << "alpha mismatches: " << result.alphaMismatches << '\n'
        << "max deltaE00: " << QString::number(result.maxDeltaE, 'f', 4) << '\n'
        << "p99 deltaE00: " << QString::number(result.p99DeltaE, 'f', 4) << '\n';
    qInfo("Comparison artifacts written to %s", qPrintable(directory));
    return written;
}

} // namespace ariadshot::tests
