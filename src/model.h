#pragma once

// The pure half of Vow: how blocks, schedules, limits, allowances, and locks
// are evaluated at an instant. Nothing here touches the disk, the network, or
// the clock, so every rule can be tested with made-up times.

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace Model {

struct Interval {
    QDateTime start;
    QDateTime end;
};

// Everything outside the block itself that decides whether it bites.
struct Context {
    QDateTime now;
    QDateTime sessionStart;               // When the current desktop session began.
    QString ssid;                         // Connected Wi-Fi network, empty when none.
    QHash<QString, qint64> usage;         // "<blockId>|<periodKey>" -> seconds used.
    QJsonObject runtime;                  // Manual sessions, breaks and passes per block id.
};

struct Evaluation {
    bool blocking = false;
    bool inWindow = false;                // Inside a schedule, trigger, or manual session.
    bool suspended = false;               // On a break or pass.
    QString reason;                       // permanent, schedule, trigger, limit, session
    QDateTime endsAt;                     // When the current blocking stretch ends, if known.
    QDateTime suspendedUntil;
    qint64 limitUsed = 0;                 // Seconds, when the block has a limit.
    qint64 limitTotal = 0;
};

QTime parseTime(const QString &text, bool *ok = nullptr);
QDate parseDate(const QString &text);
QDateTime parseDateTime(const QString &text);
QString formatDateTime(const QDateTime &value);

// Intervals a single rule produces that start on the given day.
QList<Interval> ruleIntervalsOn(const QJsonObject &rule, const QDate &day);
// Every rule interval of a block that overlaps [from, to), merged and sorted,
// with exception days cut out.
QList<Interval> scheduleIntervals(const QJsonObject &block, const QDateTime &from, const QDateTime &to);
bool excepted(const QJsonObject &block, const QDateTime &at);

// The schedule alone: active now, when it ends, and when it next starts.
bool scheduleActive(const QJsonObject &block, const QDateTime &at, QDateTime *endsAt = nullptr);
QDateTime nextScheduleStart(const QJsonObject &block, const QDateTime &after, int horizonDays = 400);

// Limits count usage per period: "hour", "day", or "week".
QString periodKey(const QString &period, const QDateTime &at);
QDateTime periodEnd(const QString &period, const QDateTime &at);

Evaluation evaluate(const QJsonObject &block, const Context &context);

// Website patterns: "reddit.com" matches the host and its subdomains,
// "youtube.com/shorts" also needs the path prefix, "*" matches everything.
bool urlMatches(const QString &pattern, const QString &url);
QString hostOf(const QString &url);
// App patterns match a process name or executable ("steam"), or a window
// class when written "class:discord".
bool appMatches(const QString &pattern, const QString &processName, const QString &exeName,
                const QString &windowClass);

// Locks only open when every one of their conditions is met. Today that is a
// timer; task conditions (XP on a learning site, for instance) slot in here.
bool lockActive(const QJsonObject &block, const QDateTime &now);
QDateTime lockUntil(const QJsonObject &block);
// Returns an empty string when the edit only makes a locked block stricter,
// or why it is refused.
QString checkLockedEdit(const QJsonObject &before, const QJsonObject &after, const QDateTime &now);

QJsonObject normalizeBlock(QJsonObject block);

} // namespace Model
