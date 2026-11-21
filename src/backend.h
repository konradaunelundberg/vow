#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

// The window's view of the daemon. It polls the daemon's snapshot once a
// second and turns QML actions into requests; it never enforces anything.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool online READ online NOTIFY changed)
    Q_PROPERTY(QString offlineReason READ offlineReason NOTIFY changed)
    Q_PROPERTY(QVariantList blocks READ blocks NOTIFY changed)
    Q_PROPERTY(QVariantMap evaluations READ evaluations NOTIFY changed)
    Q_PROPERTY(QVariantMap status READ status NOTIFY changed)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY changed)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY changed)
    Q_PROPERTY(bool hyprland READ hyprland NOTIFY changed)
    Q_PROPERTY(bool extension READ extension NOTIFY changed)
    Q_PROPERTY(bool chromium READ chromium NOTIFY changed)
    Q_PROPERTY(QString ssid READ ssid NOTIFY changed)
    Q_PROPERTY(QDateTime now READ now NOTIFY tickChanged)

public:
    explicit Backend(bool poll = true, QObject *parent = nullptr);

    bool online() const { return m_online; }
    QString offlineReason() const { return m_offlineReason; }
    QVariantList blocks() const { return m_blocks; }
    QVariantMap evaluations() const { return m_evaluations; }
    QVariantMap status() const { return m_status; }
    QVariantMap stats() const { return m_stats; }
    QVariantMap settings() const { return m_settings; }
    bool hyprland() const { return m_hyprland; }
    bool extension() const { return m_extension; }
    bool chromium() const { return m_chromium; }
    QString ssid() const { return m_ssid; }
    QDateTime now() const { return QDateTime::currentDateTime(); }

    Q_INVOKABLE void refresh();
    // Each returns an empty string on success, or what went wrong.
    Q_INVOKABLE QString save(const QVariantMap &block);
    Q_INVOKABLE QString remove(const QString &id);
    Q_INVOKABLE QString startSession(const QString &id, int minutes, bool lock);
    Q_INVOKABLE QString stopSession(const QString &id);
    Q_INVOKABLE QString lock(const QString &id, const QString &until);
    Q_INVOKABLE QString takeBreak(const QString &id);
    Q_INVOKABLE QString usePass(const QString &id);
    Q_INVOKABLE QString endBreak(const QString &id);
    Q_INVOKABLE QString saveSettings(const QVariantMap &settings);

    // Helpers the editor uses; all pure.
    Q_INVOKABLE QVariantMap newBlock() const;
    Q_INVOKABLE QVariantMap newRule(const QString &kind) const;
    Q_INVOKABLE QVariantList preview(const QVariantMap &block, int days) const;
    Q_INVOKABLE QString describeRule(const QVariantMap &rule) const;
    Q_INVOKABLE QString describeBlock(const QVariantMap &block) const;
    Q_INVOKABLE bool validTime(const QString &text) const;
    Q_INVOKABLE bool validDate(const QString &text) const;
    Q_INVOKABLE bool validDateTime(const QString &text) const;
    Q_INVOKABLE QString normalizeDateTime(const QString &text) const;
    Q_INVOKABLE int parseMinutes(const QString &text) const;
    Q_INVOKABLE QString remaining(const QString &iso) const;
    Q_INVOKABLE QString clock(const QString &iso) const;
    Q_INVOKABLE QString duration(qint64 seconds) const;
    Q_INVOKABLE QString inMinutes(int minutes) const;
    Q_INVOKABLE QVariantList usage(int days) const;
    Q_INVOKABLE QVariantList dailyTotals(int days) const;
    Q_INVOKABLE QString listToText(const QVariantList &list) const;
    Q_INVOKABLE QVariantList textToList(const QString &text) const;

    void applySnapshot(const QJsonObject &snapshot);

signals:
    void changed();
    void tickChanged();

private:
    QString send(const QJsonObject &request);

    bool m_online = false;
    QString m_offlineReason;
    QVariantList m_blocks;
    QVariantMap m_evaluations;
    QVariantMap m_status;
    QVariantMap m_stats;
    QVariantMap m_settings;
    bool m_hyprland = false;
    bool m_extension = false;
    bool m_chromium = false;
    QString m_ssid;
    QTimer m_timer;
};
