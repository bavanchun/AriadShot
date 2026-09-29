// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#include "app/SignalBridge.h"

#include "app/Logging.h"

#include <QCoreApplication>
#include <QSocketNotifier>

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ariadshot::app {

namespace {

bool makeNonBlockingCloseOnExec(int fd) {
    const int flags = ::fcntl(fd, F_GETFL);
    return flags != -1 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 && ::fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

} // namespace

SignalBridge::SignalBridge(QObject* parent) : QObject(parent) {
    if (s_writeFd != -1) {
        qCWarning(lcApp) << "A signal bridge is already installed; SIGINT and SIGTERM keep their current handlers";
        return;
    }

    std::array<int, 2> fds{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) != 0) {
        qCWarning(lcApp) << "Could not create the signal socket pair; SIGINT and SIGTERM keep their default behaviour";
        return;
    }
    if (!makeNonBlockingCloseOnExec(fds[0]) || !makeNonBlockingCloseOnExec(fds[1])) {
        ::close(fds[0]);
        ::close(fds[1]);
        qCWarning(lcApp)
            << "Could not configure the signal socket pair; SIGINT and SIGTERM keep their default behaviour";
        return;
    }
    s_readFd = fds[0];
    s_writeFd = fds[1];

    m_notifier = std::make_unique<QSocketNotifier>(s_readFd, QSocketNotifier::Read);
    connect(m_notifier.get(), &QSocketNotifier::activated, this, &SignalBridge::handleActivation);

    struct sigaction action{};
    action.sa_handler = &SignalBridge::handleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;

    if (::sigaction(SIGINT, &action, &m_previousInterrupt) != 0) {
        uninstall();
        qCWarning(lcApp) << "Could not install the SIGINT handler";
        return;
    }
    if (::sigaction(SIGTERM, &action, &m_previousTerminate) != 0) {
        ::sigaction(SIGINT, &m_previousInterrupt, nullptr);
        uninstall();
        qCWarning(lcApp) << "Could not install the SIGTERM handler";
        return;
    }
    m_installed = true;
}

SignalBridge::~SignalBridge() {
    if (m_installed) {
        ::sigaction(SIGINT, &m_previousInterrupt, nullptr);
        ::sigaction(SIGTERM, &m_previousTerminate, nullptr);
    }
    uninstall();
}

bool SignalBridge::isInstalled() const { return m_installed; }

void SignalBridge::handleSignal(int signalNumber) {
    // Async-signal-safe: one write(2), errno preserved for the interrupted code.
    const int savedErrno = errno;
    const char byte = static_cast<char>(signalNumber);
    [[maybe_unused]] const ssize_t written = ::write(s_writeFd, &byte, 1);
    errno = savedErrno;
}

void SignalBridge::handleActivation() {
    std::array<char, 16> buffer{};
    while (::read(s_readFd, buffer.data(), buffer.size()) > 0) {
    }
    QCoreApplication::quit();
}

void SignalBridge::uninstall() {
    // Handlers are restored before this runs, so no handler writes to a closed descriptor. Only the instance that
    // created the socket pair owns the notifier and closes the descriptors.
    m_installed = false;
    if (!m_notifier) {
        return;
    }
    m_notifier.reset();
    if (s_readFd != -1) {
        ::close(s_readFd);
        s_readFd = -1;
    }
    if (s_writeFd != -1) {
        ::close(s_writeFd);
        s_writeFd = -1;
    }
}

} // namespace ariadshot::app
