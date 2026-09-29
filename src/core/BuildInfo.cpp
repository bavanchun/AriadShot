// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "core/BuildInfo.h"

#include "core/Version.h"

namespace ariadshot::core {

QString BuildInfo::applicationName() { return QStringLiteral("AriadShot"); }

QString BuildInfo::organizationDomain() { return QStringLiteral("bavanchun.github.io"); }

QString BuildInfo::applicationId() { return QStringLiteral("io.github.bavanchun.AriadShot"); }

QString BuildInfo::projectVersion() { return QString::fromLatin1(generated::kProjectVersion); }

QString BuildInfo::version() { return QString::fromLatin1(generated::kVersion); }

QString BuildInfo::gitDescription() { return QString::fromLatin1(generated::kGitDescription); }

QString BuildInfo::buildType() { return QString::fromLatin1(generated::kBuildType); }

} // namespace ariadshot::core
