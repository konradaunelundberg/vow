#pragma once

// The daemon. It runs as root under systemd, owns the blocks, and every
// second decides what is blocked, then makes the system agree: /etc/hosts, a
// Chromium policy, the Chromium extension, and killing blocked programs.
// The GUI and CLI only ever ask it for changes over a local socket, so
// closing them never ends a block.

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QTcpServer>
#include <QTimer>

#include "model.h"

struct EnginePaths {
    QString stateDir = "/var/lib/vow";
    QString runDir = "/run/vow";
    QString hosts = "/etc/hosts";
    QString chromiumPolicy = "/etc/chromium/policies/managed/vow.json";
    QString proc = "/proc";
    QString runUser = "/run/user";
    quint16 extensionPort = 47321;
    bool enforce = true;      // Tests turn off killing and notifications.
};

class Engine : public QObject {
    Q_OBJECT

public:
    explicit Engine(const EnginePaths &paths = {}, QObject *parent = nullptr);
    ~Engine() override;

    bool start(QString *error);
    void stop();

    // One request from the GUI, CLI, or tests; returns the reply.
    QJsonObject handle(const QJsonObject &request);
    // One second of the daemon's life, at the given instant.
    void tick(const QDateTime &now);

    QString siteBlockedBy(const QString &url) const;
    QJsonObject status() const { return m_status; }

    void setNow(const QDateTime &now) { m_fixedNow = now; }
    static QString focusUrlOf(const QJsonObject &block);

private:
    QDateTime now() const;
    void load();
    void save();
    void saveUsage();
    Model::Context context(const QDateTime &now) const;
    void evaluateAll(const QDateTime &now);
    void applyHosts();
    void applyChromiumPolicy();
    void enforceProcesses(const QDateTime &now);
    void enforceWindows(const QJsonArray &clients);
    void trackUsage(const QDateTime &now, const QJsonObject &activeWindow);
    void notifyUpcoming(const QDateTime &now);
    void writeStatus(const QDateTime &now);
    void refreshWifi();
    void notify(const QString &title, const QString &body);

    QString findHyprlandSocket();
    QByteArray hyprctl(const QByteArray &command);

    QJsonObject blockById(const QString &id) const;
    int blockIndex(const QString &id) const;
    QJsonObject reply(bool ok, const QString &error = {}) const;
    QJsonObject snapshot() const;
    bool chromiumRunning() const;

    void acceptLocal();
    void acceptHttp();
    QByteArray handleHttp(const QByteArray &method, const QByteArray &path, const QByteArray &origin,
                          const QByteArray &body);

    EnginePaths m_paths;
    QJsonArray m_blocks;
    QJsonObject m_settings;
    QJsonObject m_runtime;
    QHash<QString, qint64> m_usage;          // "<block>|<period>" -> seconds
    QJsonObject m_stats;                     // "yyyy-MM-dd" -> { "app:x": secs, "site:y": secs }
    QHash<QString, Model::Evaluation> m_evaluations;
    QHash<QString, bool> m_wasBlocking;
    QHash<QString, QDateTime> m_nextStart;
    QDateTime m_nextStartComputed;
    QSet<QString> m_notified;
    QHash<int, QDateTime> m_terminated;      // pid -> when SIGTERM was sent
    QHash<QString, QDateTime> m_killNotified;
    QJsonObject m_status;

    QString m_hyprSocket;
    QString m_hyprSignature;
    uint m_uid = 0;
    QString m_userName;
    QDateTime m_sessionStart;
    QString m_ssid;
    QDateTime m_wifiChecked;
    QProcess m_wifiProcess;

    QJsonArray m_extensionTabs;
    bool m_extensionFocused = false;
    QDateTime m_extensionSeen;

    QString m_lastHosts;
    QByteArray m_lastPolicy;
    QDateTime m_lastSave;
    bool m_dirty = false;
    bool m_usageDirty = false;
    QDateTime m_fixedNow;
    QDateTime m_focusLaunched;

    QLocalServer m_local;
    QTcpServer m_http;
    QTimer m_timer;
};
