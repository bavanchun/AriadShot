// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "core/BuildInfo.h"

#include <QRegularExpression>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;
using ariadshot::core::BuildInfo;

class BuildInfoTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void versionIsProjectVersionOrCarriesTheCommit();
    void applicationIdIsReverseDomainAndName();
};

void BuildInfoTest::versionIsProjectVersionOrCarriesTheCommit() {
    // project(VERSION) as configured for this build.
    const QString projectVersion = QStringLiteral(ARIADSHOT_TEST_PROJECT_VERSION);
    QCOMPARE(BuildInfo::projectVersion(), projectVersion);

    const QString version = BuildInfo::version();
    QVERIFY2(version.startsWith(projectVersion), qPrintable(version));
    if (BuildInfo::gitDescription().isEmpty()) {
        // A tagged release commit or a source tarball.
        QCOMPARE(version, projectVersion);
    } else {
        static const QRegularExpression abbreviatedHash(u"^g[0-9a-f]{4,40}$"_s);
        QVERIFY2(abbreviatedHash.match(BuildInfo::gitDescription()).hasMatch(),
                 qPrintable(BuildInfo::gitDescription()));
        QCOMPARE(version, projectVersion + u'+' + BuildInfo::gitDescription());
    }
}

void BuildInfoTest::applicationIdIsReverseDomainAndName() {
    QStringList labels = BuildInfo::organizationDomain().split(u'.');
    std::ranges::reverse(labels);
    QCOMPARE(BuildInfo::applicationId(), labels.join(u'.') + u'.' + BuildInfo::applicationName());
}

QTEST_GUILESS_MAIN(BuildInfoTest)
#include "tst_BuildInfo.moc"
