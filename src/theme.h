#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTimer>

// Follows the Omarchy theme live: the accent and the rest of colors.toml, so
// Vow looks like the terminal and the bar, plus Hyprland's window rounding.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(QString background READ background NOTIFY changed)
    Q_PROPERTY(QString surface READ surface NOTIFY changed)
    Q_PROPERTY(QString surfaceHigh READ surfaceHigh NOTIFY changed)
    Q_PROPERTY(QString foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QString dim READ dim NOTIFY changed)
    Q_PROPERTY(QString muted READ muted NOTIFY changed)
    Q_PROPERTY(QString danger READ danger NOTIFY changed)
    Q_PROPERTY(QString warning READ warning NOTIFY changed)
    Q_PROPERTY(QString success READ success NOTIFY changed)
    Q_PROPERTY(bool light READ light NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return color("accent", "#FFD60A"); }
    QString accentForeground() const;
    QString background() const { return color("background", "#0e0e10"); }
    QString surface() const { return color("dark_background", shade(background(), light() ? 0.96 : 0.8)); }
    QString surfaceHigh() const { return color("lighter_background", shade(background(), light() ? 0.92 : 1.35)); }
    QString foreground() const { return color("foreground", light() ? "#202020" : "#d4d4d4"); }
    QString dim() const { return color("dark_foreground", "#808080"); }
    QString muted() const { return color("muted", "#404040"); }
    QString danger() const { return color("red", "#f7768e"); }
    QString warning() const { return color("yellow", "#e0af68"); }
    QString success() const { return color("green", "#9ece6a"); }
    bool light() const { return m_colors.value("mode") == "light"; }
    int radius() const { return m_radius; }

    static QString readAccent(const QString &path);
    static QHash<QString, QString> readColors(const QString &path);

signals:
    void changed();

private:
    QString color(const QString &key, const QString &fallback) const;
    static QString shade(const QString &hex, double factor);
    void reload();
    void refreshRounding();

    QString m_directory;
    QHash<QString, QString> m_colors;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
