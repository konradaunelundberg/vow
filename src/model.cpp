#include "model.h"

#include <QJsonDocument>
#include <QSet>
#include <QUrl>
#include <algorithm>

namespace Model {

QTime parseTime(const QString &text, bool *ok) {
    const QTime time = QTime::fromString(text.trimmed(), "H:mm");
    const QTime fallback = time.isValid() ? time : QTime::fromString(text.trimmed(), "HH:mm");
    if (ok)
        *ok = fallback.isValid() || text.trimmed() == "24:00";
    if (text.trimmed() == "24:00")
        return QTime(0, 0);
    return fallback.isValid() ? fallback : QTime(0, 0);
}

QDate parseDate(const QString &text) {
    return QDate::fromString(text.trimmed(), "yyyy-MM-dd");
}

QDateTime parseDateTime(const QString &text) {
    QString value = text.trimmed();
    value.replace(' ', 'T');
    QDateTime result = QDateTime::fromString(value, "yyyy-MM-ddTHH:mm:ss");
    if (!result.isValid())
        result = QDateTime::fromString(value, "yyyy-MM-ddTHH:mm");
    return result;
}

QString formatDateTime(const QDateTime &value) {
    return value.isValid() ? value.toString("yyyy-MM-ddTHH:mm:ss") : QString();
}

static QList<int> intList(const QJsonValue &value) {
    QList<int> list;
    for (const auto &item : value.toArray())
        list << item.toInt();
    return list;
}

static bool dayMatches(const QJsonObject &rule, const QDate &day) {
    const QDate validFrom = parseDate(rule.value("validFrom").toString());
    const QDate validUntil = parseDate(rule.value("validUntil").toString());
    if (validFrom.isValid() && day < validFrom)
        return false;
    if (validUntil.isValid() && day > validUntil)
        return false;

    const QList<int> months = intList(rule.value("months"));
    if (!months.isEmpty() && !months.contains(day.month()))
        return false;

    const QString kind = rule.value("kind").toString("weekly");
    const int every = std::max(1, rule.value("every").toInt(1));
    const QDate anchor = parseDate(rule.value("anchor").toString());

    if (kind == "daily") {
        if (every == 1)
            return true;
        const QDate base = anchor.isValid() ? anchor : QDate(2024, 1, 1);
        const qint64 days = base.daysTo(day);
        return days >= 0 && days % every == 0;
    }

    if (kind == "weekly") {
        if (!intList(rule.value("days")).contains(day.dayOfWeek()))
            return false;
        if (every == 1)
            return true;
        // Weeks count from the Monday of the anchor's week, so "every second
        // week" means the anchor's week and every other one after it.
        const QDate base = anchor.isValid() ? anchor : QDate(2024, 1, 1);
        const QDate baseMonday = base.addDays(1 - base.dayOfWeek());
        const QDate monday = day.addDays(1 - day.dayOfWeek());
        const qint64 weeks = baseMonday.daysTo(monday) / 7;
        return weeks >= 0 && weeks % every == 0;
    }

    if (kind == "monthly") {
        const QList<int> monthDays = intList(rule.value("monthDays"));
        for (int d : monthDays) {
            if (d > 0 && day.day() == d)
                return true;
            if (d < 0 && day.daysInMonth() + 1 + d == day.day())
                return true;
        }
        const QList<int> ordinals = intList(rule.value("ordinals"));
        const QList<int> weekdays = intList(rule.value("weekdays"));
        if (!ordinals.isEmpty() && weekdays.contains(day.dayOfWeek())) {
            const int ordinal = (day.day() - 1) / 7 + 1;
            const bool last = day.addDays(7).month() != day.month();
            if (ordinals.contains(ordinal) || (last && ordinals.contains(-1)))
                return true;
        }
        return false;
    }

    return false;
}

QList<Interval> ruleIntervalsOn(const QJsonObject &rule, const QDate &day) {
    if (rule.value("enabled").toBool(true) == false)
        return {};

    if (rule.value("kind").toString() == "once") {
        const QDateTime start = parseDateTime(rule.value("startAt").toString());
        const QDateTime end = parseDateTime(rule.value("endAt").toString());
        if (start.isValid() && end.isValid() && end > start && start.date() == day)
            return {{start, end}};
        return {};
    }

    if (!dayMatches(rule, day))
        return {};

    const QTime startTime = parseTime(rule.value("start").toString("00:00"));
    const QTime endTime = parseTime(rule.value("end").toString("24:00"));
    QDateTime start(day, startTime);
    QDateTime end(day, endTime);
    // An end at or before the start runs past midnight, and equal times are a
    // whole day, so 22:00–06:00 and 00:00–24:00 both mean what they say.
    if (end <= start)
        end = end.addDays(1);
    return {{start, end}};
}

bool excepted(const QJsonObject &block, const QDateTime &at) {
    const QDate day = at.date();
    for (const auto &value : block.value("exceptions").toArray()) {
        const QJsonObject exception = value.toObject();
        const QDate from = parseDate(exception.value("from").toString());
        QDate to = parseDate(exception.value("to").toString());
        if (!to.isValid())
            to = from;
        if (from.isValid() && day >= from && day <= to)
            return true;
    }
    return false;
}

static QList<Interval> merge(QList<Interval> intervals) {
    std::sort(intervals.begin(), intervals.end(),
              [](const Interval &a, const Interval &b) { return a.start < b.start; });
    QList<Interval> merged;
    for (const auto &interval : intervals) {
        if (!merged.isEmpty() && interval.start <= merged.last().end)
            merged.last().end = std::max(merged.last().end, interval.end);
        else
            merged << interval;
    }
    return merged;
}

// Splits intervals at midnight boundaries of exception days so the excepted
// days are removed and the rest of an overnight window survives.
static QList<Interval> cutExceptions(const QJsonObject &block, const QList<Interval> &intervals) {
    if (block.value("exceptions").toArray().isEmpty())
        return intervals;
    QList<Interval> result;
    for (const auto &interval : intervals) {
        QDateTime cursor = interval.start;
        while (cursor < interval.end) {
            QDateTime dayEnd(cursor.date().addDays(1), QTime(0, 0));
            const QDateTime pieceEnd = std::min(dayEnd, interval.end);
            if (!excepted(block, cursor)) {
                if (!result.isEmpty() && result.last().end == cursor)
                    result.last().end = pieceEnd;
                else
                    result << Interval{cursor, pieceEnd};
            }
            cursor = pieceEnd;
        }
    }
    return result;
}

QList<Interval> scheduleIntervals(const QJsonObject &block, const QDateTime &from, const QDateTime &to) {
    QList<Interval> intervals;
    const QJsonArray rules = block.value("rules").toArray();
    if (rules.isEmpty() || to <= from)
        return {};

    // A start can lie up to a day before `from` (overnight windows), and once
    // rules can start much earlier, so they are checked directly.
    for (const auto &value : rules) {
        const QJsonObject rule = value.toObject();
        if (rule.value("kind").toString() == "once") {
            const QDateTime start = parseDateTime(rule.value("startAt").toString());
            const QDateTime end = parseDateTime(rule.value("endAt").toString());
            if (rule.value("enabled").toBool(true) && start.isValid() && end > start && end > from && start < to)
                intervals << Interval{start, end};
            continue;
        }
        for (QDate day = from.date().addDays(-1); day <= to.date(); day = day.addDays(1))
            for (const auto &interval : ruleIntervalsOn(rule, day))
                if (interval.end > from && interval.start < to)
                    intervals << interval;
    }
    return cutExceptions(block, merge(intervals));
}

bool scheduleActive(const QJsonObject &block, const QDateTime &at, QDateTime *endsAt) {
    // Look a few days ahead so back-to-back windows report one long stretch.
    const auto intervals = scheduleIntervals(block, at, at.addDays(8));
    if (intervals.isEmpty() || intervals.first().start > at)
        return false;
    if (endsAt)
        *endsAt = intervals.first().end;
    return true;
}

QDateTime nextScheduleStart(const QJsonObject &block, const QDateTime &after, int horizonDays) {
    // Search in growing windows so the common case stays cheap.
    for (int span : {2, 14, 62, horizonDays}) {
        const auto intervals = scheduleIntervals(block, after, after.addDays(span));
        for (const auto &interval : intervals)
            if (interval.start > after)
                return interval.start;
    }
    return {};
}

QString periodKey(const QString &period, const QDateTime &at) {
    if (period == "hour")
        return "h:" + at.toString("yyyy-MM-ddTHH");
    if (period == "week") {
        int year = 0;
        const int week = at.date().weekNumber(&year);
        return QString("w:%1-W%2").arg(year).arg(week, 2, 10, QChar('0'));
    }
    return "d:" + at.toString("yyyy-MM-dd");
}

QDateTime periodEnd(const QString &period, const QDateTime &at) {
    if (period == "hour")
        return QDateTime(at.date(), QTime(at.time().hour(), 0)).addSecs(3600);
    if (period == "week")
        return QDateTime(at.date().addDays(8 - at.date().dayOfWeek()), QTime(0, 0));
    return QDateTime(at.date().addDays(1), QTime(0, 0));
}

static bool conditionsHold(const QJsonObject &block, const Context &context) {
    for (const auto &value : block.value("conditions").toArray()) {
        const QJsonObject condition = value.toObject();
        if (condition.value("type").toString() == "wifi") {
            QSet<QString> networks;
            for (const auto &ssid : condition.value("networks").toArray())
                networks.insert(ssid.toString());
            const bool connected = networks.contains(context.ssid);
            if (condition.value("match").toString("on") == "on" ? !connected : connected)
                return false;
        }
    }
    return true;
}

static bool triggerActive(const QJsonObject &block, const Context &context, QDateTime *endsAt) {
    bool active = false;
    for (const auto &value : block.value("triggers").toArray()) {
        const QJsonObject trigger = value.toObject();
        if (trigger.value("type").toString() == "login" && context.sessionStart.isValid()) {
            const QDateTime end = context.sessionStart.addSecs(60LL * trigger.value("minutes").toInt(30));
            if (context.now >= context.sessionStart && context.now < end) {
                active = true;
                if (endsAt && (!endsAt->isValid() || end > *endsAt))
                    *endsAt = end;
            }
        }
    }
    return active;
}

Evaluation evaluate(const QJsonObject &block, const Context &context) {
    Evaluation result;
    if (!block.value("enabled").toBool(true) && !lockActive(block, context.now))
        return result;

    const QString id = block.value("id").toString();
    const QJsonObject runtime = context.runtime.value(id).toObject();
    const QJsonObject limit = block.value("limit").toObject();
    const bool hasLimit = limit.value("minutes").toInt(0) > 0;
    const bool hasRules = !block.value("rules").toArray().isEmpty();
    const bool hasTriggers = !block.value("triggers").toArray().isEmpty();

    // A manual session ("block for the next 90 minutes") overrides limits,
    // conditions, and exception days: it is a deliberate choice made now.
    const QDateTime sessionEnd = parseDateTime(runtime.value("session").toObject().value("end").toString());
    const bool inSession = sessionEnd.isValid() && context.now < sessionEnd;

    QDateTime windowEnd;
    bool inWindow = false;
    // A permanent block is on, with no end, until it is turned off by hand.
    // Rules, triggers, days off, conditions, and limits do not apply.
    const bool permanent = block.value("permanent").toBool(false);
    if (permanent) {
        inWindow = true;
    } else if (hasRules && !excepted(block, context.now))
        inWindow = scheduleActive(block, context.now, &windowEnd);
    if (hasTriggers && !excepted(block, context.now) && triggerActive(block, context, &windowEnd))
        inWindow = true;
    // A limit with nothing else to scope it applies all the time.
    if (!permanent && !hasRules && !hasTriggers && hasLimit && !excepted(block, context.now))
        inWindow = true;
    if (!permanent && inWindow && !conditionsHold(block, context))
        inWindow = false;

    bool blocking = false;
    if (inWindow) {
        blocking = true;
        result.reason = permanent ? "permanent" : hasRules ? "schedule" : "trigger";
        if (hasLimit && !permanent) {
            const QString period = limit.value("period").toString("day");
            result.limitTotal = 60LL * limit.value("minutes").toInt();
            result.limitUsed = context.usage.value(id + "|" + periodKey(period, context.now));
            blocking = result.limitUsed >= result.limitTotal;
            result.reason = "limit";
            const QDateTime end = periodEnd(period, context.now);
            if (!windowEnd.isValid() || end < windowEnd)
                windowEnd = end;
        }
    }
    if (inSession && !permanent) {
        result.reason = "session";
        if (!blocking || !windowEnd.isValid() || sessionEnd > windowEnd)
            windowEnd = sessionEnd;
        blocking = true;
        inWindow = true;
    }

    result.inWindow = inWindow;
    result.endsAt = blocking ? windowEnd : QDateTime();

    // Breaks and passes are allowances written into the block, so they hold
    // even while it is locked.
    const QDateTime suspendedUntil = parseDateTime(runtime.value("suspendedUntil").toString());
    if (blocking && suspendedUntil.isValid() && context.now < suspendedUntil) {
        result.suspended = true;
        result.suspendedUntil = suspendedUntil;
        blocking = false;
    }
    result.blocking = blocking;
    return result;
}

QString hostOf(const QString &url) {
    QUrl parsed(url.contains("://") ? url : "https://" + url);
    return parsed.host().toLower();
}

bool urlMatches(const QString &rawPattern, const QString &url) {
    QString pattern = rawPattern.trimmed().toLower();
    if (pattern.isEmpty())
        return false;
    if (pattern == "*")
        return url.startsWith("http://") || url.startsWith("https://");
    pattern.remove(QRegularExpression("^[a-z]+://"));
    if (pattern.startsWith("*."))
        pattern = pattern.mid(2);
    if (pattern.startsWith("www."))
        pattern = pattern.mid(4);

    const QUrl parsed(url.contains("://") ? url : "https://" + url);
    const QString scheme = parsed.scheme();
    if (scheme != "http" && scheme != "https")
        return false;
    const QString host = parsed.host().toLower();

    const int slash = pattern.indexOf('/');
    const QString patternHost = slash < 0 ? pattern : pattern.left(slash);
    const QString patternPath = slash < 0 ? QString() : pattern.mid(slash);
    if (host != patternHost && !host.endsWith("." + patternHost))
        return false;
    if (patternPath.isEmpty() || patternPath == "/")
        return true;
    QString path = parsed.path(QUrl::FullyEncoded).toLower();
    if (parsed.hasQuery())
        path += "?" + parsed.query(QUrl::FullyEncoded).toLower();
    return path.startsWith(patternPath);
}

bool appMatches(const QString &rawPattern, const QString &processName, const QString &exeName,
                const QString &windowClass) {
    const QString pattern = rawPattern.trimmed().toLower();
    if (pattern.isEmpty())
        return false;
    if (pattern.startsWith("class:"))
        return !windowClass.isEmpty() && windowClass.toLower() == pattern.mid(6).trimmed();
    // The kernel cuts process names at 15 characters.
    const QString comm = processName.toLower();
    return comm == pattern || (comm.size() == 15 && pattern.startsWith(comm)) ||
           exeName.toLower() == pattern || (!windowClass.isEmpty() && windowClass.toLower() == pattern);
}

QDateTime lockUntil(const QJsonObject &block) {
    QDateTime until;
    for (const auto &value : block.value("lock").toObject().value("conditions").toArray()) {
        const QJsonObject condition = value.toObject();
        if (condition.value("type").toString() == "timer") {
            const QDateTime time = parseDateTime(condition.value("until").toString());
            if (time.isValid() && (!until.isValid() || time > until))
                until = time;
        }
    }
    return until;
}

bool lockActive(const QJsonObject &block, const QDateTime &now) {
    const QJsonArray conditions = block.value("lock").toObject().value("conditions").toArray();
    if (conditions.isEmpty())
        return false;
    for (const auto &value : conditions) {
        const QJsonObject condition = value.toObject();
        const QString type = condition.value("type").toString();
        if (type == "timer") {
            const QDateTime until = parseDateTime(condition.value("until").toString());
            if (until.isValid() && now < until)
                return true;
        } else {
            // A condition this version cannot check stays unmet, so a lock
            // written by a newer Vow never opens early.
            if (!condition.value("met").toBool(false))
                return true;
        }
    }
    return false;
}

static QSet<QString> stringSet(const QJsonValue &value) {
    QSet<QString> set;
    for (const auto &item : value.toArray())
        set.insert(item.toString().trimmed().toLower());
    set.remove(QString());
    return set;
}

QString checkLockedEdit(const QJsonObject &before, const QJsonObject &after, const QDateTime &now) {
    if (!lockActive(before, now))
        return {};

    QJsonObject candidate = after;
    QJsonObject original = before;

    // Adding things to block makes a block stricter; removing them does not.
    // In allow-only mode the lists are what stays reachable, so there the
    // reverse holds.
    const bool allowMode = before.value("mode").toString() == "allow" || before.value("mode").toString() == "focus";
    for (const QString key : {"sites", "apps"}) {
        const auto was = stringSet(before.value(key));
        const auto now = stringSet(after.value(key));
        const bool stricter = allowMode ? was.contains(now) : now.contains(was);
        if (!stricter)
            return allowMode ? QString("A locked allow-only block can only lose entries.")
                             : QString("A locked block can only gain sites and apps.");
        candidate.remove(key);
        original.remove(key);
    }

    // Removing an exception site means less gets through.
    if (!stringSet(before.value("allowSites")).contains(stringSet(after.value("allowSites"))))
        return "A locked block cannot gain exception sites.";
    candidate.remove("allowSites");
    original.remove("allowSites");

    // Making a block permanent only adds blocked time; undoing it does not.
    if (before.value("permanent").toBool(false) && !after.value("permanent").toBool(false))
        return "A locked permanent block stays permanent.";
    candidate.remove("permanent");
    original.remove("permanent");

    if (lockUntil(after) < lockUntil(before))
        return "A lock can be extended but never shortened.";
    candidate.remove("lock");
    original.remove("lock");

    for (const QString key : {"name", "color"}) {
        candidate.remove(key);
        original.remove(key);
    }

    // Adding a rule only ever adds blocked time; any other schedule change is
    // compared whole.
    QJsonArray beforeRules = original.value("rules").toArray();
    QJsonArray afterRules = candidate.value("rules").toArray();
    bool rulesKept = afterRules.size() >= beforeRules.size();
    for (int i = 0; rulesKept && i < beforeRules.size(); ++i)
        rulesKept = beforeRules.at(i) == afterRules.at(i);
    if (!rulesKept)
        return "Rules on a locked block can be added but not changed or removed.";
    candidate.remove("rules");
    original.remove("rules");

    if (QJsonDocument(normalizeBlock(candidate)).toJson() != QJsonDocument(normalizeBlock(original)).toJson())
        return "This block is locked until %1; only stricter changes are allowed.";
    return {};
}

QJsonObject normalizeBlock(QJsonObject block) {
    if (!block.contains("enabled"))
        block.insert("enabled", true);
    if (!block.contains("mode"))
        block.insert("mode", "block");
    for (const QString key : {"sites", "apps", "allowSites", "rules", "exceptions", "triggers", "conditions"})
        if (!block.value(key).isArray())
            block.insert(key, QJsonArray());
    return block;
}

} // namespace Model
