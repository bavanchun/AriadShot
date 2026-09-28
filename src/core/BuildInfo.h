// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

namespace ariadshot::core {

// Identity and version of this build. The version comes from project(VERSION) and the git commit at build time
// (cmake/AriadShotVersion.cmake). Thread: any.
class BuildInfo {
  public:
    // "AriadShot".
    [[nodiscard]] static QString applicationName();
    // The reverse of the application ID's domain part, "bavanchun.github.io".
    [[nodiscard]] static QString organizationDomain();
    // "io.github.bavanchun.AriadShot": desktop entry, portal app ID and macOS bundle identifier.
    [[nodiscard]] static QString applicationId();
    // project(VERSION), for example "0.1.0".
    [[nodiscard]] static QString projectVersion();
    // projectVersion() on the commit tagged v<projectVersion()> or without git metadata, otherwise
    // projectVersion() + "+" + gitDescription().
    [[nodiscard]] static QString version();
    // "g" followed by the abbreviated commit hash, or empty when version() carries no commit.
    [[nodiscard]] static QString gitDescription();
    // The CMake build type, for example "Debug"; empty when none was set.
    [[nodiscard]] static QString buildType();
};

} // namespace ariadshot::core
