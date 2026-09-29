// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>

#include <csignal>
#include <memory>

class QSocketNotifier;

namespace ariadshot::app {

// Turns SIGINT and SIGTERM into a clean QCoreApplication::quit(). The signal handler only writes one byte to a
// socket pair (async-signal-safe); a QSocketNotifier on the other end quits the event loop on the GUI thread. The
// previous handlers are restored on destruction. At most one instance may be installed at a time. Thread: GUI.
class SignalBridge : public QObject {
    Q_OBJECT

  public:
    explicit SignalBridge(QObject* parent = nullptr);
    ~SignalBridge() override;

    // False when the socket pair or the handlers could not be installed; the process then keeps the default signal
    // behaviour.
    [[nodiscard]] bool isInstalled() const;

  private:
    static void handleSignal(int signalNumber);
    void handleActivation();
    void uninstall();

    // Written by the signal handler, read by the notifier; -1 while no bridge is installed.
    static inline int s_readFd = -1;
    static inline int s_writeFd = -1;

    std::unique_ptr<QSocketNotifier> m_notifier;
    struct sigaction m_previousInterrupt{};
    struct sigaction m_previousTerminate{};
    bool m_installed = false;
};

} // namespace ariadshot::app
