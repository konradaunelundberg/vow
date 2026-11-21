#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "cli.h"
#include "engine.h"
#include "model.h"
#include "theme.h"
#include "translator.h"

using namespace Model;

static QDateTime at(const QString &text) { return parseDateTime(text); }

static QJsonObject block(const QJsonArray &rules, QJsonObject extra = {}) {
    extra.insert("id", "b1");
    extra.insert("name", "Focus");
    extra.insert("rules", rules);
    if (!extra.contains("sites"))
        extra.insert("sites", QJsonArray{"reddit.com"});
    return normalizeBlock(extra);
}

class VowTests : public QObject {
    Q_OBJECT

private slots:
    void weeklyWindow() {
        // 2026-11-23 is a Monday.
        const auto b = block({QJsonObject{{"kind", "weekly"}, {"days", QJsonArray{1, 2, 3, 4, 5}},
                                          {"start", "09:00"}, {"end", "17:30"}}});
        QDateTime end;
        QVERIFY(scheduleActive(b, at("2026-11-23T09:00"), &end));
        QCOMPARE(end, at("2026-11-23T17:30"));
        QVERIFY(!scheduleActive(b, at("2026-11-23T17:30")));
        QVERIFY(!scheduleActive(b, at("2026-11-22T12:00")));   // Sunday
        QCOMPARE(nextScheduleStart(b, at("2026-11-21T12:00")), at("2026-11-23T09:00"));
    }

    void overnightWindowCrossesMidnight() {
        const auto b = block({QJsonObject{{"kind", "weekly"}, {"days", QJsonArray{5}},
                                          {"start", "22:00"}, {"end", "06:15"}}});
        QVERIFY(scheduleActive(b, at("2026-11-27T23:00")));   // Friday night
        QVERIFY(scheduleActive(b, at("2026-11-28T06:14")));   // into Saturday
        QVERIFY(!scheduleActive(b, at("2026-11-28T06:15")));
        QVERIFY(!scheduleActive(b, at("2026-11-27T05:00")));  // Friday morning is Thursday's night
    }

    void backToBackWindowsMerge() {
        const auto b = block({QJsonObject{{"kind", "daily"}, {"start", "08:00"}, {"end", "12:00"}},
                              QJsonObject{{"kind", "daily"}, {"start", "12:00"}, {"end", "13:00"}}});
        QDateTime end;
        QVERIFY(scheduleActive(b, at("2026-11-23T10:00"), &end));
        QCOMPARE(end, at("2026-11-23T13:00"));
    }

    void everySecondWeek() {
        const auto b = block({QJsonObject{{"kind", "weekly"}, {"days", QJsonArray{3}}, {"every", 2},
                                          {"anchor", "2026-11-25"}, {"start", "10:00"}, {"end", "11:00"}}});
        QVERIFY(scheduleActive(b, at("2026-11-25T10:30")));
        QVERIFY(!scheduleActive(b, at("2026-12-02T10:30")));
        QVERIFY(scheduleActive(b, at("2026-12-09T10:30")));
        QVERIFY(!scheduleActive(b, at("2026-11-18T10:30")));   // before the anchor
    }

    void lastFridayOfTheMonth() {
        const auto b = block({QJsonObject{{"kind", "monthly"}, {"ordinals", QJsonArray{-1}},
                                          {"weekdays", QJsonArray{5}}, {"start", "18:00"}, {"end", "23:00"}}});
        QVERIFY(scheduleActive(b, at("2026-11-27T19:00")));
        QVERIFY(!scheduleActive(b, at("2026-11-20T19:00")));
        QCOMPARE(nextScheduleStart(b, at("2026-11-28T00:00")), at("2026-12-25T18:00"));
    }

    void secondTuesdayAndMonthDays() {
        const auto b = block({QJsonObject{{"kind", "monthly"}, {"ordinals", QJsonArray{2}}, {"weekdays", QJsonArray{2}},
                                          {"monthDays", QJsonArray{1, -1}}, {"start", "00:00"}, {"end", "24:00"}}});
        QVERIFY(scheduleActive(b, at("2026-11-10T12:00")));    // 2nd Tuesday
        QVERIFY(scheduleActive(b, at("2026-11-01T12:00")));    // 1st
        QVERIFY(scheduleActive(b, at("2026-11-30T12:00")));    // last day
        QVERIFY(!scheduleActive(b, at("2026-11-17T12:00")));
    }

    void monthsAndValidRange() {
        const auto b = block({QJsonObject{{"kind", "daily"}, {"months", QJsonArray{12}}, {"validUntil", "2026-12-20"},
                                          {"start", "00:00"}, {"end", "24:00"}}});
        QVERIFY(!scheduleActive(b, at("2026-11-30T12:00")));
        QVERIFY(scheduleActive(b, at("2026-12-05T12:00")));
        QVERIFY(!scheduleActive(b, at("2026-12-21T12:00")));
    }

    void onceRule() {
        const auto b = block({QJsonObject{{"kind", "once"}, {"startAt", "2026-12-24T14:00"}, {"endAt", "2026-12-26T09:00"}}});
        QVERIFY(scheduleActive(b, at("2026-12-25T12:00")));
        QVERIFY(!scheduleActive(b, at("2026-12-26T09:00")));
        QCOMPARE(nextScheduleStart(b, at("2026-11-21T00:00")), at("2026-12-24T14:00"));
    }

    void exceptionDaysCutWindows() {
        auto b = block({QJsonObject{{"kind", "daily"}, {"start", "20:00"}, {"end", "02:00"}}},
                       QJsonObject{{"exceptions", QJsonArray{QJsonObject{{"from", "2026-12-24"}, {"to", "2026-12-25"}}}}});
        QVERIFY(!scheduleActive(b, at("2026-12-24T21:00")));
        QVERIFY(scheduleActive(b, at("2026-12-23T21:00")));
        QDateTime end;
        QVERIFY(scheduleActive(b, at("2026-12-23T23:00"), &end));
        QCOMPARE(end, at("2026-12-24T00:00"));             // cut at the exception
        QVERIFY(scheduleActive(b, at("2026-12-26T00:30")));   // the 25th's night spills into the 26th
    }

    void limitBlocksOnlyWhenUsedUp() {
        auto b = block({}, QJsonObject{{"limit", QJsonObject{{"minutes", 45}, {"period", "day"}}}});
        Context context;
        context.now = at("2026-11-23T15:00");
        auto evaluation = evaluate(b, context);
        QVERIFY(evaluation.inWindow);
        QVERIFY(!evaluation.blocking);
        context.usage.insert("b1|" + periodKey("day", context.now), 45 * 60);
        evaluation = evaluate(b, context);
        QVERIFY(evaluation.blocking);
        QCOMPARE(evaluation.endsAt, at("2026-11-24T00:00"));
    }

    void wifiConditionAndLoginTrigger() {
        auto b = block({}, QJsonObject{{"triggers", QJsonArray{QJsonObject{{"type", "login"}, {"minutes", 20}}}},
                                       {"conditions", QJsonArray{QJsonObject{{"type", "wifi"}, {"match", "on"},
                                                                             {"networks", QJsonArray{"home"}}}}}});
        Context context;
        context.sessionStart = at("2026-11-23T08:00");
        context.now = at("2026-11-23T08:10");
        context.ssid = "home";
        QVERIFY(evaluate(b, context).blocking);
        context.ssid = "library";
        QVERIFY(!evaluate(b, context).blocking);
        context.ssid = "home";
        context.now = at("2026-11-23T08:20");
        QVERIFY(!evaluate(b, context).blocking);
    }

    void sessionAndBreaks() {
        auto b = block({});
        Context context;
        context.now = at("2026-11-23T10:00");
        context.runtime = QJsonObject{{"b1", QJsonObject{{"session", QJsonObject{{"end", "2026-11-23T11:30:00"}}}}}};
        auto evaluation = evaluate(b, context);
        QVERIFY(evaluation.blocking);
        QCOMPARE(evaluation.endsAt, at("2026-11-23T11:30"));
        QJsonObject runtime = context.runtime.value("b1").toObject();
        runtime.insert("suspendedUntil", "2026-11-23T10:05:00");
        context.runtime.insert("b1", runtime);
        evaluation = evaluate(b, context);
        QVERIFY(!evaluation.blocking);
        QVERIFY(evaluation.suspended);
    }

    void urlPatterns() {
        QVERIFY(urlMatches("reddit.com", "https://old.reddit.com/r/linux"));
        QVERIFY(urlMatches("www.reddit.com", "https://reddit.com/"));
        QVERIFY(!urlMatches("reddit.com", "https://notreddit.com/"));
        QVERIFY(urlMatches("youtube.com/shorts", "https://www.youtube.com/shorts/abc"));
        QVERIFY(!urlMatches("youtube.com/shorts", "https://www.youtube.com/watch?v=1"));
        QVERIFY(urlMatches("*", "https://example.org"));
        QVERIFY(!urlMatches("*", "chrome://settings"));
    }

    void appPatterns() {
        QVERIFY(appMatches("steam", "steam", "steam", {}));
        QVERIFY(appMatches("signal-desktop-beta", "signal-desktop-", "", {}));
        QVERIFY(appMatches("class:discord", {}, {}, "discord"));
        QVERIFY(!appMatches("class:discord", "discord", "discord", {}));
    }

    void lockedEditsOnlyTighten() {
        const QDateTime now = at("2026-11-23T10:00");
        auto b = block({QJsonObject{{"kind", "daily"}, {"start", "09:00"}, {"end", "17:00"}}},
                       QJsonObject{{"lock", QJsonObject{{"conditions", QJsonArray{QJsonObject{
                                                            {"type", "timer"}, {"until", "2026-11-30T00:00:00"}}}}}}});
        auto stricter = b;
        stricter.insert("sites", QJsonArray{"reddit.com", "x.com"});
        QVERIFY(checkLockedEdit(b, stricter, now).isEmpty());

        auto fewer = b;
        fewer.insert("sites", QJsonArray{});
        QVERIFY(!checkLockedEdit(b, fewer, now).isEmpty());

        auto shorter = b;
        shorter.insert("rules", QJsonArray{QJsonObject{{"kind", "daily"}, {"start", "09:00"}, {"end", "10:00"}}});
        QVERIFY(!checkLockedEdit(b, shorter, now).isEmpty());

        auto disabled = b;
        disabled.insert("enabled", false);
        QVERIFY(!checkLockedEdit(b, disabled, now).isEmpty());

        auto unlocked = b;
        unlocked.insert("lock", QJsonObject{});
        QVERIFY(!checkLockedEdit(b, unlocked, now).isEmpty());

        // Once the timer has run out, anything goes.
        QVERIFY(checkLockedEdit(b, fewer, at("2026-11-30T00:00")).isEmpty());

        // A condition this version does not know stays locked.
        auto task = b;
        task.insert("lock", QJsonObject{{"conditions", QJsonArray{QJsonObject{{"type", "mathacademy-xp"}, {"xp", 50}}}}});
        QVERIFY(lockActive(task, at("2030-01-01T00:00")));
    }

    void durations() {
        QCOMPARE(Cli::parseMinutes("90m"), 90);
        QCOMPARE(Cli::parseMinutes("1h30m"), 90);
        QCOMPARE(Cli::parseMinutes("2h"), 120);
        QCOMPARE(Cli::parseMinutes("1d"), 1440);
        QCOMPARE(Cli::parseMinutes("45"), 45);
        QCOMPARE(Cli::parseMinutes("soon"), 0);
    }

    void engineEnforcesToFiles() {
        QTemporaryDir dir;
        EnginePaths paths;
        paths.stateDir = dir.filePath("state");
        paths.runDir = dir.filePath("run");
        paths.hosts = dir.filePath("hosts");
        paths.chromiumPolicy = dir.filePath("policy/vow.json");
        paths.proc = dir.filePath("proc");
        paths.runUser = dir.filePath("user");
        paths.enforce = true;
        QFile hosts(paths.hosts);
        QVERIFY(hosts.open(QIODevice::WriteOnly));
        hosts.write("127.0.0.1 localhost\n");
        hosts.close();

        Engine engine(paths);
        engine.setNow(at("2026-11-23T10:00"));
        QJsonObject b{{"name", "Work"},
                      {"sites", QJsonArray{"reddit.com", "youtube.com/shorts"}},
                      {"rules", QJsonArray{QJsonObject{{"kind", "daily"}, {"start", "09:00"}, {"end", "17:00"}}}}};
        const auto saved = engine.handle({{"cmd", "save"}, {"block", b}});
        QVERIFY(saved.value("ok").toBool());
        const QString id = saved.value("id").toString();

        QCOMPARE(engine.siteBlockedBy("https://www.reddit.com/"), QString("Work"));
        QVERIFY(engine.siteBlockedBy("https://youtube.com/watch").isEmpty());

        QVERIFY(hosts.open(QIODevice::ReadOnly));
        const QString contents = QString::fromUtf8(hosts.readAll());
        hosts.close();
        QVERIFY(contents.startsWith("127.0.0.1 localhost\n"));
        QVERIFY(contents.contains("0.0.0.0 reddit.com"));
        QVERIFY(!contents.contains("youtube"));

        QFile policy(paths.chromiumPolicy);
        QVERIFY(policy.open(QIODevice::ReadOnly));
        const auto list = QJsonDocument::fromJson(policy.readAll()).object().value("URLBlocklist").toArray();
        QVERIFY(list.contains("youtube.com/shorts"));
        policy.close();

        // A lock refuses deletion and loosening, then the day ends.
        QVERIFY(engine.handle({{"cmd", "lock"}, {"id", id}, {"until", "2026-11-23T18:00:00"}}).value("ok").toBool());
        QVERIFY(!engine.handle({{"cmd", "delete"}, {"id", id}}).value("ok").toBool());
        engine.setNow(at("2026-11-23T17:00"));
        engine.tick(at("2026-11-23T17:00"));
        QVERIFY(engine.siteBlockedBy("https://reddit.com/").isEmpty());
        QVERIFY(!QFile::exists(paths.chromiumPolicy));
        QVERIFY(hosts.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(hosts.readAll()), QString("127.0.0.1 localhost\n"));
        hosts.close();

        // A locked session cannot be stopped early.
        QVERIFY(engine.handle({{"cmd", "session"}, {"id", id}, {"minutes", 30}, {"lock", true}}).value("ok").toBool());
        QVERIFY(!engine.handle({{"cmd", "stopSession"}, {"id", id}}).value("ok").toBool());
        QCOMPARE(engine.siteBlockedBy("https://reddit.com/"), QString("Work"));

        // State survives a restart.
        Engine reloaded(paths);
        reloaded.setNow(at("2026-11-23T17:10"));
        reloaded.tick(at("2026-11-23T17:10"));
        QCOMPARE(reloaded.siteBlockedBy("https://reddit.com/"), QString("Work"));
    }

    void allowOnlyLayering() {
        QTemporaryDir dir;
        EnginePaths paths;
        paths.stateDir = dir.filePath("state");
        paths.runDir = dir.filePath("run");
        paths.hosts = dir.filePath("hosts");
        paths.chromiumPolicy = dir.filePath("vow.json");
        paths.runUser = dir.filePath("user");
        paths.enforce = false;
        Engine engine(paths);
        engine.setNow(at("2026-11-23T10:00"));
        const QJsonArray always{QJsonObject{{"kind", "daily"}, {"start", "00:00"}, {"end", "24:00"}}};
        engine.handle({{"cmd", "save"}, {"block", QJsonObject{{"name", "Study"}, {"mode", "allow"},
                                                              {"sites", QJsonArray{"mathacademy.com", "wikipedia.org"}},
                                                              {"rules", always}}}});
        engine.handle({{"cmd", "save"}, {"block", QJsonObject{{"name", "No wiki rabbit holes"},
                                                              {"sites", QJsonArray{"wikipedia.org/wiki/Special:Random"}},
                                                              {"rules", always}}}});
        QVERIFY(engine.siteBlockedBy("https://mathacademy.com/learn").isEmpty());
        QCOMPARE(engine.siteBlockedBy("https://news.ycombinator.com/"), QString("Study"));
        QCOMPARE(engine.siteBlockedBy("https://en.wikipedia.org/wiki/Special:Random"), QString("No wiki rabbit holes"));
        QVERIFY(engine.siteBlockedBy("chrome://settings").isEmpty());
    }

    void previewSplitsAtMidnight() {
        Backend backend(false);
        QVariantMap b = block({QJsonObject{{"kind", "daily"}, {"start", "22:00"}, {"end", "02:00"}}}).toVariantMap();
        const auto pieces = backend.preview(b, 2);
        QVERIFY(pieces.size() >= 3);
        const auto first = pieces.first().toMap();
        QCOMPARE(first.value("day").toInt(), 0);
    }

    void describesRules() {
        Backend backend(false);
        QLocale::setDefault(QLocale::c());
        QCOMPARE(backend.describeRule(QVariantMap{{"kind", "weekly"}, {"days", QVariantList{1, 2, 3, 4, 5}},
                                                  {"start", "09:00"}, {"end", "17:00"}}),
                 QString("Mon–Fri, 09:00–17:00"));
    }

    void themeReadsPalette() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\nbackground = \"#000000\"\nred = \"#ff0000\"\n");
        colors.close();
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
        QCOMPARE(theme.danger(), QString("#ff0000"));
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }
};

QTEST_MAIN(VowTests)
#include "vow_tests.moc"
