// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "support/ImageCompare.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QImage>
#include <QTextStream>

namespace {

int usageError(const QString& message) {
    QTextStream(stderr) << QStringLiteral("usage error: ") << message << Qt::endl;
    return 2;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ariadshot-imgdiff"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Compare images using AriadShot's golden-image rules."));
    parser.addHelpOption();
    parser.addOption(QCommandLineOption(QStringLiteral("class"),
                                        QStringLiteral("Comparison class: exact, presentation or corpus."),
                                        QStringLiteral("name")));
    parser.addOption(QCommandLineOption(QStringLiteral("mask"), QStringLiteral("Perceptual tolerance mask image."),
                                        QStringLiteral("path")));
    parser.addOption(QCommandLineOption(QStringLiteral("out"), QStringLiteral("Directory for failure artifacts."),
                                        QStringLiteral("dir")));
    parser.addPositionalArgument(QStringLiteral("actual"), QStringLiteral("Actual image path."));
    parser.addPositionalArgument(QStringLiteral("expected"), QStringLiteral("Expected image path."));

    if (!parser.parse(app.arguments())) {
        return usageError(parser.errorText());
    }
    if (parser.isSet(QStringLiteral("help"))) {
        QTextStream(stdout) << parser.helpText();
        return 0;
    }

    const QString comparisonClass = parser.value(QStringLiteral("class"));
    if (comparisonClass != QStringLiteral("exact") && comparisonClass != QStringLiteral("presentation") &&
        comparisonClass != QStringLiteral("corpus")) {
        return usageError(QStringLiteral("--class must be exact, presentation or corpus"));
    }

    const QStringList positional = parser.positionalArguments();
    if (positional.size() != 2) {
        return usageError(QStringLiteral("expected exactly two image paths"));
    }
    if (parser.isSet(QStringLiteral("mask")) && comparisonClass == QStringLiteral("exact")) {
        return usageError(QStringLiteral("--mask is available only for perceptual classes"));
    }

    const QImage actual(positional.at(0));
    const QImage expected(positional.at(1));
    if (actual.isNull() || expected.isNull()) {
        return usageError(QStringLiteral("actual and expected must be readable images"));
    }

    QImage mask;
    if (parser.isSet(QStringLiteral("mask"))) {
        mask = QImage(parser.value(QStringLiteral("mask")));
        if (mask.isNull()) {
            return usageError(QStringLiteral("the tolerance mask must be a readable image"));
        }
    }

    ariadshot::tests::ComparisonResult result;
    if (comparisonClass == QStringLiteral("exact")) {
        result = ariadshot::tests::ImageCompare::compareExact(actual, expected);
    } else if (comparisonClass == QStringLiteral("presentation")) {
        result = ariadshot::tests::ImageCompare::comparePerceptual(actual, expected,
                                                                   ariadshot::tests::ImageCompare::kPresentation, mask);
    } else {
        result = ariadshot::tests::ImageCompare::comparePerceptual(actual, expected,
                                                                   ariadshot::tests::ImageCompare::kCorpus, mask);
    }

    QTextStream output(stdout);
    output << QStringLiteral("comparison: ") << (result.passed ? QStringLiteral("passed") : QStringLiteral("failed"))
           << QStringLiteral("; class: ") << comparisonClass << Qt::endl;
    output << QStringLiteral("compared pixels: ") << result.comparedPixels << Qt::endl;
    output << QStringLiteral("passing pixels: ") << result.passingPixels << Qt::endl;
    output << QStringLiteral("alpha mismatches: ") << result.alphaMismatches << Qt::endl;
    output << QStringLiteral("passing fraction: ") << QString::number(result.passingFraction, 'f', 6) << Qt::endl;
    output << QStringLiteral("max deltaE00: ") << QString::number(result.maxDeltaE, 'f', 4) << Qt::endl;
    output << QStringLiteral("p99 deltaE00: ") << QString::number(result.p99DeltaE, 'f', 4) << Qt::endl;
    if (!result.failure.isEmpty()) {
        output << QStringLiteral("failure: ") << result.failure << Qt::endl;
    }

    if (!result.passed) {
        const QString outputDirectory = parser.isSet(QStringLiteral("out"))
                                            ? parser.value(QStringLiteral("out"))
                                            : ariadshot::tests::ImageCompare::outputDirectory();
        if (!ariadshot::tests::ImageCompare::writeFailureArtifacts(outputDirectory, QStringLiteral("imgdiff"), actual,
                                                                   expected, result)) {
            QTextStream(stderr) << QStringLiteral("imgdiff: failed to write comparison artifacts") << Qt::endl;
        }
    }

    return result.passed ? 0 : 1;
}
