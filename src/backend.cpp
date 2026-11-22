#include "backend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>

#include "cli.h"
#include "client.h"
#include "model.h"

Backend::Backend(bool poll, QObject *parent) : QObject(parent) {
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        refresh();
        emit tickChanged();
    });
    if (poll) {
        refresh();
        m_timer.start();
    }
}

void Backend::refresh() {
    applySnapshot(Client::request({{"cmd", "get"}}, 600));
}

void Backend::applySnapshot(const QJsonObject &snapshot) {
    const bool online = snapshot.value("ok").toBool();
    if (!online) {
        const QString reason = snapshot.value("error").toString();
        if (m_online || reason != m_offlineReason) {
            m_online = false;
            m_offlineReason = reason;
            emit changed();
        }
        return;
    }
    // Rebuilding every model each second would reset the editor's bindings,
    // so only a real change is announced.
    const QVariantList blocks = snapshot.value("blocks").toArray().toVariantList();
    const QVariantMap evaluations = snapshot.value("evaluations").toObject().toVariantMap();
    const QVariantMap status = snapshot.value("status").toObject().toVariantMap();
    const QVariantMap stats = snapshot.value("stats").toObject().toVariantMap();
    const QVariantMap settings = snapshot.value("settings").toObject().toVariantMap();
    const bool hyprland = snapshot.value("hyprland").toBool();
    const bool extension = snapshot.value("extension").toBool();
    const bool chromium = snapshot.value("chromium").toBool();
    const QString ssid = snapshot.value("ssid").toString();
    QVariantMap statusCompare = status;
    statusCompare.remove("updated");
    QVariantMap previousStatus = m_status;
    previousStatus.remove("updated");
    if (m_online && blocks == m_blocks && evaluations == m_evaluations && statusCompare == previousStatus &&
        settings == m_settings && hyprland == m_hyprland && extension == m_extension && chromium == m_chromium && ssid == m_ssid &&
        stats.keys() == m_stats.keys()) {
        m_stats = stats;   // Seconds tick up constantly; the stats page reads them on its own timer.
        return;
    }
    m_online = true;
    m_offlineReason.clear();
    m_blocks = blocks;
    m_evaluations = evaluations;
    m_status = status;
    m_stats = stats;
    m_settings = settings;
    m_hyprland = hyprland;
    m_extension = extension;
    m_chromium = chromium;
    m_ssid = ssid;
    emit changed();
}

QString Backend::send(const QJsonObject &request) {
    const QJsonObject answer = Client::request(request);
    refresh();
    return answer.value("ok").toBool() ? QString() : answer.value("error").toString(tr("Something went wrong."));
}

QString Backend::save(const QVariantMap &block) {
    return send({{"cmd", "save"}, {"block", QJsonObject::fromVariantMap(block)}});
}
QString Backend::remove(const QString &id) { return send({{"cmd", "delete"}, {"id", id}}); }
QString Backend::startSession(const QString &id, int minutes, bool lock) {
    return send({{"cmd", "session"}, {"id", id}, {"minutes", minutes}, {"lock", lock}});
}
QString Backend::stopSession(const QString &id) { return send({{"cmd", "stopSession"}, {"id", id}}); }
QString Backend::lock(const QString &id, const QString &until) {
    return send({{"cmd", "lock"}, {"id", id}, {"until", normalizeDateTime(until)}});
}
QString Backend::takeBreak(const QString &id) { return send({{"cmd", "break"}, {"id", id}}); }
QString Backend::usePass(const QString &id) { return send({{"cmd", "pass"}, {"id", id}}); }
QString Backend::endBreak(const QString &id) { return send({{"cmd", "endBreak"}, {"id", id}}); }
QString Backend::saveSettings(const QVariantMap &settings) {
    return send({{"cmd", "settings"}, {"settings", QJsonObject::fromVariantMap(settings)}});
}

QVariantMap Backend::newBlock() const {
    return Model::normalizeBlock(QJsonObject{{"name", ""}, {"mode", "block"}}).toVariantMap();
}

QVariantMap Backend::newRule(const QString &kind) const {
    const QDate today = QDate::currentDate();
    if (kind == "once") {
        const QDateTime start = QDateTime(today, QTime(QTime::currentTime().hour() + 1 > 23 ? 23 : QTime::currentTime().hour() + 1, 0));
        return {{"kind", "once"}, {"startAt", Model::formatDateTime(start)},
                {"endAt", Model::formatDateTime(start.addSecs(2 * 3600))}};
    }
    QVariantMap rule{{"kind", kind}, {"start", "09:00"}, {"end", "17:00"}, {"every", 1}};
    if (kind == "weekly")
        rule.insert("days", QVariantList{1, 2, 3, 4, 5});
    if (kind == "monthly") {
        rule.insert("ordinals", QVariantList{-1});
        rule.insert("weekdays", QVariantList{5});
        rule.insert("monthDays", QVariantList{});
    }
    if (kind != "monthly")
        rule.insert("anchor", today.toString("yyyy-MM-dd"));
    return rule;
}

QVariantList Backend::preview(const QVariantMap &block, int days) const {
    const QDateTime from(QDate::currentDate(), QTime(0, 0));
    const QDateTime to = from.addDays(days);
    QVariantList result;
    for (const auto &interval : Model::scheduleIntervals(QJsonObject::fromVariantMap(block), from, to)) {
        // Split at midnight so each row of the grid gets its own piece.
        QDateTime cursor = std::max(interval.start, from);
        const QDateTime end = std::min(interval.end, to);
        while (cursor < end) {
            const QDateTime dayEnd(cursor.date().addDays(1), QTime(0, 0));
            const QDateTime pieceEnd = std::min(dayEnd, end);
            const int startMinute = cursor.time().hour() * 60 + cursor.time().minute();
            const int endMinute = pieceEnd == dayEnd ? 1440 : pieceEnd.time().hour() * 60 + pieceEnd.time().minute();
            result << QVariantMap{{"day", int(from.date().daysTo(cursor.date()))},
                                  {"start", startMinute},
                                  {"end", endMinute}};
            cursor = pieceEnd;
        }
    }
    return result;
}

static QString dayName(int day) {
    return QLocale().dayName(day, QLocale::ShortFormat);
}

QString Backend::describeRule(const QVariantMap &variant) const {
    const QJsonObject rule = QJsonObject::fromVariantMap(variant);
    const QString kind = rule.value("kind").toString();
    const QString start = rule.value("start").toString("00:00");
    const QString end = rule.value("end").toString("24:00");
    const QString times = (start == "00:00" && (end == "24:00" || end == "00:00")) ? tr("all day")
                                                                                     : QString("%1–%2").arg(start, end);
    const int every = std::max(1, rule.value("every").toInt(1));
    QString text;

    if (kind == "once") {
        const QDateTime a = Model::parseDateTime(rule.value("startAt").toString());
        const QDateTime b = Model::parseDateTime(rule.value("endAt").toString());
        return QString("%1 → %2").arg(QLocale().toString(a, "ddd d MMM HH:mm"),
                                      QLocale().toString(b, a.date() == b.date() ? "HH:mm" : "ddd d MMM HH:mm"));
    }
    if (kind == "daily") {
        text = every == 1 ? tr("Every day, %1").arg(times) : tr("Every %1 days, %2").arg(every).arg(times);
    } else if (kind == "weekly") {
        QList<int> days;
        for (const auto &d : rule.value("days").toArray())
            days << d.toInt();
        std::sort(days.begin(), days.end());
        QStringList parts;
        for (int i = 0; i < days.size();) {
            int j = i;
            while (j + 1 < days.size() && days[j + 1] == days[j] + 1)
                ++j;
            parts << (j - i >= 2 ? QString("%1–%2").arg(dayName(days[i]), dayName(days[j]))
                                 : j == i ? dayName(days[i]) : dayName(days[i]) + ", " + dayName(days[j]));
            i = j + 1;
        }
        const QString dayText = days.size() == 7 ? tr("Every day") : days.isEmpty() ? tr("No days") : parts.join(", ");
        text = QString("%1, %2").arg(dayText, times);
        if (every > 1)
            text += ", " + tr("every %1 weeks").arg(every);
    } else if (kind == "monthly") {
        QStringList parts;
        static const QStringList ordinalNames{"", "1st", "2nd", "3rd", "4th", "5th"};
        QStringList ordinals;
        for (const auto &o : rule.value("ordinals").toArray())
            ordinals << (o.toInt() == -1 ? tr("last") : ordinalNames.value(o.toInt()));
        QStringList weekdays;
        for (const auto &w : rule.value("weekdays").toArray())
            weekdays << dayName(w.toInt());
        if (!ordinals.isEmpty() && !weekdays.isEmpty())
            parts << tr("%1 %2").arg(ordinals.join("/"), weekdays.join("/"));
        QStringList monthDays;
        for (const auto &d : rule.value("monthDays").toArray())
            monthDays << (d.toInt() == -1 ? tr("last day") : d.toInt() < 0 ? tr("%1 from end").arg(-d.toInt())
                                                                            : QString::number(d.toInt()));
        if (!monthDays.isEmpty())
            parts << tr("day %1").arg(monthDays.join(", "));
        text = tr("%1 of the month, %2").arg(parts.isEmpty() ? tr("Never") : parts.join(" + "), times);
    }
    QStringList months;
    for (const auto &m : rule.value("months").toArray())
        months << QLocale().monthName(m.toInt(), QLocale::ShortFormat);
    if (!months.isEmpty())
        text += " " + tr("in %1").arg(months.join(", "));
    const QString from = rule.value("validFrom").toString();
    const QString until = rule.value("validUntil").toString();
    if (!from.isEmpty() && !until.isEmpty())
        text += " " + tr("(%1 to %2)").arg(from, until);
    else if (!from.isEmpty())
        text += " " + tr("(from %1)").arg(from);
    else if (!until.isEmpty())
        text += " " + tr("(until %1)").arg(until);
    return text;
}

QString Backend::describeBlock(const QVariantMap &variant) const {
    const QJsonObject block = QJsonObject::fromVariantMap(variant);
    QStringList parts;
    const int sites = block.value("sites").toArray().size();
    const int apps = block.value("apps").toArray().size();
    if (block.value("mode").toString() == "focus")
        parts << tr("Focus on %1").arg(Model::hostOf(block.value("focus").toObject().value("url").toString(
                     block.value("sites").toArray().at(0).toString())));
    else if (block.value("mode").toString() == "allow")
        parts << tr("Allow only") + " " + tr("%n sites", "", sites) + ", " + tr("%n apps", "", apps);
    else
        parts << tr("%n sites", "", sites) + ", " + tr("%n apps", "", apps);
    const int minutes = block.value("limit").toObject().value("minutes").toInt();
    if (minutes > 0)
        parts << tr("%1 per %2").arg(inMinutes(minutes), block.value("limit").toObject().value("period").toString("day"));
    const int rules = block.value("rules").toArray().size();
    if (block.value("permanent").toBool(false))
        parts << tr("permanent");
    else if (rules > 0)
        parts << tr("%n rules", "", rules);
    return parts.join(" · ");
}

bool Backend::validTime(const QString &text) const {
    bool ok = false;
    Model::parseTime(text, &ok);
    return ok;
}

bool Backend::validDate(const QString &text) const { return Model::parseDate(text).isValid(); }
bool Backend::validDateTime(const QString &text) const { return Model::parseDateTime(text).isValid(); }

QString Backend::normalizeDateTime(const QString &text) const {
    const QDateTime value = Model::parseDateTime(text);
    if (value.isValid())
        return Model::formatDateTime(value);
    const int minutes = Cli::parseMinutes(text);
    return minutes > 0 ? Model::formatDateTime(QDateTime::currentDateTime().addSecs(60LL * minutes)) : QString();
}

int Backend::parseMinutes(const QString &text) const { return Cli::parseMinutes(text); }

QString Backend::duration(qint64 seconds) const {
    if (seconds < 0)
        seconds = 0;
    const qint64 days = seconds / 86400, hours = seconds % 86400 / 3600, minutes = seconds % 3600 / 60;
    if (days > 0)
        return QString("%1d %2h").arg(days).arg(hours);
    if (hours > 0)
        return QString("%1h %2m").arg(hours).arg(minutes, 2, 10, QChar('0'));
    if (minutes > 0)
        return QString("%1m %2s").arg(minutes).arg(seconds % 60, 2, 10, QChar('0'));
    return QString("%1s").arg(seconds);
}

QString Backend::remaining(const QString &iso) const {
    const QDateTime end = Model::parseDateTime(iso);
    return end.isValid() ? duration(QDateTime::currentDateTime().secsTo(end)) : QString();
}

QString Backend::clock(const QString &iso) const {
    const QDateTime value = Model::parseDateTime(iso);
    if (!value.isValid())
        return {};
    const QDate today = QDate::currentDate();
    if (value.date() == today)
        return value.toString("HH:mm");
    if (value.date() == today.addDays(1))
        return tr("tomorrow %1").arg(value.toString("HH:mm"));
    if (today.daysTo(value.date()) < 7)
        return QLocale().toString(value, "ddd HH:mm");
    return QLocale().toString(value, "d MMM yyyy HH:mm");
}

QString Backend::inMinutes(int minutes) const {
    if (minutes >= 60 && minutes % 60 == 0)
        return QString("%1h").arg(minutes / 60);
    if (minutes > 60)
        return QString("%1h %2m").arg(minutes / 60).arg(minutes % 60);
    return QString("%1m").arg(minutes);
}

QVariantList Backend::usage(int days) const {
    QHash<QString, qint64> totals;
    const QDate today = QDate::currentDate();
    for (int i = 0; i < days; ++i) {
        const QVariantMap day = m_stats.value(today.addDays(-i).toString("yyyy-MM-dd")).toMap();
        for (auto it = day.begin(); it != day.end(); ++it)
            totals[it.key()] += it.value().toLongLong();
    }
    QVariantList list;
    for (auto it = totals.begin(); it != totals.end(); ++it)
        list << QVariantMap{{"key", it.key()},
                            {"kind", it.key().section(':', 0, 0)},
                            {"name", it.key().section(':', 1)},
                            {"seconds", it.value()}};
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("seconds").toLongLong() > b.toMap().value("seconds").toLongLong();
    });
    return list;
}

QVariantList Backend::dailyTotals(int days) const {
    QVariantList list;
    const QDate today = QDate::currentDate();
    for (int i = days - 1; i >= 0; --i) {
        const QDate date = today.addDays(-i);
        const QVariantMap day = m_stats.value(date.toString("yyyy-MM-dd")).toMap();
        qint64 apps = 0, sites = 0;
        for (auto it = day.begin(); it != day.end(); ++it)
            (it.key().startsWith("site:") ? sites : apps) += it.value().toLongLong();
        list << QVariantMap{{"label", QLocale().toString(date, "ddd")}, {"apps", apps}, {"sites", sites}};
    }
    return list;
}

QString Backend::listToText(const QVariantList &list) const {
    QStringList lines;
    for (const auto &item : list)
        lines << item.toString();
    return lines.join('\n');
}

QVariantList Backend::textToList(const QString &text) const {
    QVariantList list;
    for (QString line : text.split(QRegularExpression("[\\n,]"))) {
        line = line.trimmed();
        if (!line.isEmpty() && !list.contains(line))
            list << line;
    }
    return list;
}
