#include "engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTcpSocket>
#include <QUuid>

#include "theme.h"

#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>

static const char *HostsBegin = "# >>> vow: managed block list, do not edit";
static const char *HostsEnd = "# <<< vow";

static bool writeAtomically(const QString &path, const QByteArray &data, QFileDevice::Permissions permissions) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(data);
    file.setPermissions(permissions);
    return file.commit();
}

static const QFileDevice::Permissions WorldReadable =
    QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther;

Engine::Engine(const EnginePaths &paths, QObject *parent) : QObject(parent), m_paths(paths) {
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, [this] { tick(now()); });
    connect(&m_local, &QLocalServer::newConnection, this, &Engine::acceptLocal);
    connect(&m_http, &QTcpServer::newConnection, this, &Engine::acceptHttp);
    connect(&m_wifiProcess, &QProcess::finished, this, [this] {
        const QString output = QString::fromUtf8(m_wifiProcess.readAllStandardOutput());
        const auto match = QRegularExpression(R"(^\s*ssid (.+)$)", QRegularExpression::MultilineOption).match(output);
        m_ssid = match.hasMatch() ? match.captured(1).trimmed() : QString();
    });
    load();
}

Engine::~Engine() {
    stop();
    if (m_wifiProcess.state() != QProcess::NotRunning) {
        m_wifiProcess.kill();
        m_wifiProcess.waitForFinished(500);
    }
}

QDateTime Engine::now() const {
    return m_fixedNow.isValid() ? m_fixedNow : QDateTime::currentDateTime();
}

bool Engine::start(QString *error) {
    QDir().mkpath(m_paths.runDir);
    const QString socketPath = m_paths.runDir + "/vow.sock";
    QLocalServer::removeServer(socketPath);
    // Any local account may ask; the daemon itself refuses anything that
    // would weaken a locked block, whoever asks.
    m_local.setSocketOptions(QLocalServer::WorldAccessOption);
    if (!m_local.listen(socketPath)) {
        if (error)
            *error = QString("Cannot listen on %1: %2").arg(socketPath, m_local.errorString());
        return false;
    }
    if (!m_http.listen(QHostAddress::LocalHost, m_paths.extensionPort))
        qWarning("vow: the Chromium extension port %d is busy: %s", m_paths.extensionPort,
                 qPrintable(m_http.errorString()));
    tick(now());
    m_timer.start();
    return true;
}

void Engine::stop() {
    if (!m_timer.isActive())
        return;
    m_timer.stop();
    save();
    saveUsage();
    // Nothing should stay blocked while the daemon is not there to say why.
    m_evaluations.clear();
    if (m_paths.enforce) {
        applyHosts();
        applyChromiumPolicy();
    }
    m_local.close();
    m_http.close();
}

// ---------------------------------------------------------------- storage

void Engine::load() {
    QFile file(m_paths.stateDir + "/state.json");
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject state = QJsonDocument::fromJson(file.readAll()).object();
        m_blocks = QJsonArray();
        for (const auto &block : state.value("blocks").toArray())
            m_blocks.append(Model::normalizeBlock(block.toObject()));
        m_settings = state.value("settings").toObject();
        m_runtime = state.value("runtime").toObject();
    }
    if (!m_settings.contains("notifyLead"))
        m_settings.insert("notifyLead", QJsonArray{5, 1});

    QFile usage(m_paths.stateDir + "/usage.json");
    if (usage.open(QIODevice::ReadOnly)) {
        const QJsonObject object = QJsonDocument::fromJson(usage.readAll()).object();
        const QJsonObject limits = object.value("limits").toObject();
        for (auto it = limits.begin(); it != limits.end(); ++it)
            m_usage.insert(it.key(), it.value().toInteger());
        m_stats = object.value("stats").toObject();
    }
}

void Engine::save() {
    QJsonObject state{{"version", 1}, {"blocks", m_blocks}, {"settings", m_settings}, {"runtime", m_runtime}};
    writeAtomically(m_paths.stateDir + "/state.json", QJsonDocument(state).toJson(), WorldReadable);
    m_dirty = false;
}

void Engine::saveUsage() {
    // Keep three months of statistics and only the limit periods still
    // relevant (this week and later).
    const QDate cutoff = now().date().addDays(-92);
    for (const auto &key : m_stats.keys())
        if (Model::parseDate(key) < cutoff)
            m_stats.remove(key);
    const QDate weekAgo = now().date().addDays(-8);
    QJsonObject limits;
    for (auto it = m_usage.begin(); it != m_usage.end(); ++it) {
        const QString period = it.key().section('|', 1);
        const QDate date = Model::parseDate(period.mid(2, 10));
        if (period.startsWith("w:") || !date.isValid() || date >= weekAgo)
            limits.insert(it.key(), it.value());
    }
    writeAtomically(m_paths.stateDir + "/usage.json",
                    QJsonDocument(QJsonObject{{"limits", limits}, {"stats", m_stats}}).toJson(QJsonDocument::Compact),
                    WorldReadable);
    m_usageDirty = false;
}

// ---------------------------------------------------------------- evaluation

Model::Context Engine::context(const QDateTime &at) const {
    Model::Context context;
    context.now = at;
    context.sessionStart = m_sessionStart;
    context.ssid = m_ssid;
    context.usage = m_usage;
    context.runtime = m_runtime;
    return context;
}

void Engine::evaluateAll(const QDateTime &at) {
    const auto ctx = context(at);
    m_evaluations.clear();
    for (const auto &value : m_blocks) {
        const QJsonObject block = value.toObject();
        m_evaluations.insert(block.value("id").toString(), Model::evaluate(block, ctx));
    }
}

QJsonObject Engine::blockById(const QString &id) const {
    for (const auto &value : m_blocks)
        if (value.toObject().value("id").toString() == id)
            return value.toObject();
    return {};
}

int Engine::blockIndex(const QString &id) const {
    for (int i = 0; i < m_blocks.size(); ++i)
        if (m_blocks.at(i).toObject().value("id").toString() == id)
            return i;
    return -1;
}

static QStringList strings(const QJsonValue &value) {
    QStringList list;
    for (const auto &item : value.toArray()) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty())
            list << text;
    }
    return list;
}

// Layering: a URL is blocked if any active block-list names it (and none of
// that block's exceptions do), or if any active allow-only block leaves it out.
QString Engine::siteBlockedBy(const QString &url) const {
    if (!url.startsWith("http://") && !url.startsWith("https://"))
        return {};
    for (const auto &value : m_blocks) {
        const QJsonObject block = value.toObject();
        if (!m_evaluations.value(block.value("id").toString()).blocking)
            continue;
        const QStringList sites = strings(block.value("sites"));
        const QStringList exceptions = strings(block.value("allowSites"));
        auto any = [&](const QStringList &patterns) {
            return std::any_of(patterns.begin(), patterns.end(),
                               [&](const QString &p) { return Model::urlMatches(p, url); });
        };
        if (block.value("mode").toString() == "allow") {
            if (!any(sites) && !any(exceptions))
                return block.value("name").toString();
        } else if (any(sites) && !any(exceptions)) {
            return block.value("name").toString();
        }
    }
    return {};
}

void Engine::tick(const QDateTime &at) {
    // Desktop session and Wi-Fi, both of which triggers and conditions read.
    const QString socket = findHyprlandSocket();
    if (socket != m_hyprSocket) {
        m_hyprSocket = socket;
        const QFileInfo dir(QFileInfo(socket).absolutePath());
        // The instance directory is made when Hyprland starts, so a daemon
        // restart does not look like a fresh login.
        m_sessionStart = socket.isEmpty() ? QDateTime()
                         : dir.birthTime().isValid() ? dir.birthTime() : dir.lastModified();
        m_uid = socket.isEmpty() ? 0 : QFileInfo(socket).ownerId();
        const passwd *entry = m_uid ? getpwuid(m_uid) : nullptr;
        m_userName = entry ? QString::fromLocal8Bit(entry->pw_name) : QString();
    }
    if (m_paths.enforce && (!m_wifiChecked.isValid() || m_wifiChecked.secsTo(at) >= 15)) {
        m_wifiChecked = at;
        refreshWifi();
    }

    // Sessions, breaks, and passes that ran out are cleared.
    for (const auto &id : m_runtime.keys()) {
        QJsonObject runtime = m_runtime.value(id).toObject();
        bool changed = false;
        const QDateTime sessionEnd = Model::parseDateTime(runtime.value("session").toObject().value("end").toString());
        if (runtime.contains("session") && (!sessionEnd.isValid() || at >= sessionEnd)) {
            runtime.remove("session");
            changed = true;
        }
        const QDateTime suspended = Model::parseDateTime(runtime.value("suspendedUntil").toString());
        if (runtime.contains("suspendedUntil") && (!suspended.isValid() || at >= suspended)) {
            runtime.remove("suspendedUntil");
            runtime.remove("suspendKind");
            changed = true;
        }
        if (changed) {
            m_runtime.insert(id, runtime);
            m_dirty = true;
        }
    }

    evaluateAll(at);

    // Starts and ends worth telling about.
    for (const auto &value : m_blocks) {
        const QJsonObject block = value.toObject();
        const QString id = block.value("id").toString();
        const auto evaluation = m_evaluations.value(id);
        const bool blocking = evaluation.blocking || evaluation.suspended;
        if (m_wasBlocking.contains(id) && m_wasBlocking.value(id) != blocking) {
            const QString name = block.value("name").toString();
            if (blocking)
                notify(name, evaluation.endsAt.isValid()
                                 ? QString("Blocking until %1").arg(evaluation.endsAt.toString("HH:mm"))
                                 : QString("Blocking now"));
            else
                notify(name, "Block finished");
        }
        m_wasBlocking.insert(id, blocking);
    }

    QJsonObject activeWindow;
    QJsonArray clients;
    if (!m_hyprSocket.isEmpty()) {
        activeWindow = QJsonDocument::fromJson(hyprctl("j/activewindow")).object();
        bool anyApps = false;
        for (const auto &value : m_blocks) {
            const QJsonObject block = value.toObject();
            if (m_evaluations.value(block.value("id").toString()).blocking &&
                (block.value("mode").toString() == "allow" || !strings(block.value("apps")).isEmpty()))
                anyApps = true;
        }
        if (anyApps)
            clients = QJsonDocument::fromJson(hyprctl("j/clients")).array();
    }

    trackUsage(at, activeWindow);
    if (m_paths.enforce) {
        applyHosts();
        applyChromiumPolicy();
        enforceProcesses(at);
        enforceWindows(clients);
    }
    notifyUpcoming(at);
    writeStatus(at);

    if (m_dirty)
        save();
    if (m_usageDirty && (!m_lastSave.isValid() || m_lastSave.secsTo(at) >= 30)) {
        m_lastSave = at;
        saveUsage();
    }
}

// ---------------------------------------------------------------- enforcement

void Engine::applyHosts() {
    QSet<QString> domains;
    for (const auto &value : m_blocks) {
        const QJsonObject block = value.toObject();
        if (!m_evaluations.value(block.value("id").toString()).blocking || block.value("mode").toString() == "allow")
            continue;
        // Exceptions narrow a block, and hosts cannot express "all of
        // youtube.com except /watch", so those domains are left to Chromium.
        const QStringList exceptions = strings(block.value("allowSites"));
        for (QString site : strings(block.value("sites"))) {
            site = site.toLower();
            site.remove(QRegularExpression("^[a-z]+://"));
            if (site == "*" || site.contains('/') || site.contains('*') || !site.contains('.'))
                continue;
            if (site.startsWith("www."))
                site = site.mid(4);
            bool excepted = false;
            for (const auto &exception : exceptions)
                if (Model::hostOf(exception) == site || Model::hostOf(exception).endsWith("." + site))
                    excepted = true;
            if (!excepted)
                domains << site << "www." + site << "m." + site;
        }
    }
    QStringList sorted(domains.begin(), domains.end());
    sorted.sort();
    QString section;
    for (const auto &domain : sorted)
        section += QString("0.0.0.0 %1\n::1 %1\n").arg(domain);
    if (section == m_lastHosts && QFile::exists(m_paths.hosts))
        return;

    QFile file(m_paths.hosts);
    QString contents;
    if (file.open(QIODevice::ReadOnly))
        contents = QString::fromUtf8(file.readAll());
    file.close();
    const int begin = contents.indexOf(HostsBegin);
    if (begin >= 0) {
        int end = contents.indexOf(HostsEnd, begin);
        end = end < 0 ? contents.size() : contents.indexOf('\n', end) + 1;
        if (end <= 0)
            end = contents.size();
        contents.remove(begin, end - begin);
    }
    if (!section.isEmpty()) {
        if (!contents.isEmpty() && !contents.endsWith('\n'))
            contents += '\n';
        contents += QString(HostsBegin) + "\n" + section + HostsEnd + "\n";
    }
    if (writeAtomically(m_paths.hosts, contents.toUtf8(), WorldReadable))
        m_lastHosts = section;
}

void Engine::applyChromiumPolicy() {
    QStringList blocklist, allowlist;
    bool allowOnly = false;
    QStringList allowCandidates;
    for (const auto &value : m_blocks) {
        const QJsonObject block = value.toObject();
        if (!m_evaluations.value(block.value("id").toString()).blocking)
            continue;
        if (block.value("mode").toString() == "allow") {
            allowOnly = true;
            allowCandidates << strings(block.value("sites")) << strings(block.value("allowSites"));
        } else {
            blocklist << strings(block.value("sites"));
            allowlist << strings(block.value("allowSites"));
        }
    }
    if (allowOnly) {
        // Chromium lets an allowlist entry beat any blocklist entry, so only
        // allow-only sites no block-list names go on it; the extension
        // enforces the exact layering.
        for (const auto &site : allowCandidates) {
            bool named = false;
            for (const auto &blocked : blocklist)
                if (Model::urlMatches(blocked, "https://" + site))
                    named = true;
            if (!named)
                allowlist << site;
        }
        blocklist = QStringList{"*"};
        allowlist << "chrome://*" << "chrome-extension://*" << "about:blank";
    }
    blocklist.removeDuplicates();
    allowlist.removeDuplicates();

    QByteArray policy;
    if (!blocklist.isEmpty())
        policy = QJsonDocument(QJsonObject{{"URLBlocklist", QJsonArray::fromStringList(blocklist)},
                                           {"URLAllowlist", QJsonArray::fromStringList(allowlist)}})
                     .toJson();
    if (policy == m_lastPolicy && (policy.isEmpty() != QFile::exists(m_paths.chromiumPolicy)))
        return;
    if (policy.isEmpty())
        QFile::remove(m_paths.chromiumPolicy);
    else if (!writeAtomically(m_paths.chromiumPolicy, policy, WorldReadable))
        return;
    m_lastPolicy = policy;
}

static QString readFirstLine(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readLine()).trimmed();
}

// Never touched, whatever a block says, so a block cannot take the desktop
// itself down.
static bool protectedProcess(const QString &comm) {
    static const QSet<QString> names{"vow", "Hyprland", "systemd", "quickshell", "omarchy-shell", "qs",
                                     "dbus-daemon", "dbus-broker", "pipewire", "wireplumber", "uwsm",
                                     "Xwayland", "xdg-desktop-por", "sddm", "login", "bash", "sh", "zsh",
                                     "fish", "sudo"};
    return names.contains(comm);
}

void Engine::enforceProcesses(const QDateTime &at) {
    QList<QPair<QString, QString>> patterns;   // pattern, block name
    for (const auto &value : m_blocks) {
        const QJsonObject block = value.toObject();
        if (!m_evaluations.value(block.value("id").toString()).blocking || block.value("mode").toString() == "allow")
            continue;
        for (const auto &app : strings(block.value("apps")))
            if (!app.startsWith("class:"))
                patterns << qMakePair(app, block.value("name").toString());
    }
    if (patterns.isEmpty()) {
        m_terminated.clear();
        return;
    }

    QSet<int> seen;
    QDirIterator it(m_paths.proc, QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        const QString dir = it.next();
        bool numeric = false;
        const int pid = QFileInfo(dir).fileName().toInt(&numeric);
        if (!numeric || pid <= 1)
            continue;
        // Only the person's own programs, never system services.
        if (QFileInfo(dir).ownerId() < 1000)
            continue;
        const QString comm = readFirstLine(dir + "/comm");
        if (comm.isEmpty() || protectedProcess(comm))
            continue;
        const QString exe = QFileInfo(QFile::symLinkTarget(dir + "/exe")).fileName();
        for (const auto &[pattern, name] : patterns) {
            if (!Model::appMatches(pattern, comm, exe, {}))
                continue;
            seen.insert(pid);
            // Ask nicely first, then insist after a few seconds.
            if (!m_terminated.contains(pid)) {
                ::kill(pid, SIGTERM);
                m_terminated.insert(pid, at);
            } else if (m_terminated.value(pid).secsTo(at) >= 3) {
                ::kill(pid, SIGKILL);
            }
            const QDateTime last = m_killNotified.value(pattern);
            if (!last.isValid() || last.secsTo(at) >= 30) {
                m_killNotified.insert(pattern, at);
                notify(QString("%1 is blocked").arg(comm), QString("Blocked by %1").arg(name));
            }
            break;
        }
    }
    for (auto pid : m_terminated.keys())
        if (!seen.contains(pid))
            m_terminated.remove(pid);
}

void Engine::enforceWindows(const QJsonArray &clients) {
    if (clients.isEmpty())
        return;
    QStringList classPatterns;
    QList<QStringList> allowLists;
    for (const auto &value : m_blocks) {
        const QJsonObject block = value.toObject();
        if (!m_evaluations.value(block.value("id").toString()).blocking)
            continue;
        if (block.value("mode").toString() == "allow")
            allowLists << strings(block.value("apps"));
        else
            for (const auto &app : strings(block.value("apps")))
                if (app.startsWith("class:"))
                    classPatterns << app;
    }
    for (const auto &value : clients) {
        const QJsonObject client = value.toObject();
        const QString cls = client.value("class").toString();
        const QString address = client.value("address").toString();
        if (cls.isEmpty() || cls == "vow" || address.isEmpty())
            continue;
        const QString comm = readFirstLine(QString("%1/%2/comm").arg(m_paths.proc).arg(client.value("pid").toInt()));
        bool close = std::any_of(classPatterns.begin(), classPatterns.end(),
                                 [&](const QString &p) { return Model::appMatches(p, {}, {}, cls); });
        for (const auto &allowed : allowLists) {
            const bool listed = std::any_of(allowed.begin(), allowed.end(), [&](const QString &p) {
                return Model::appMatches(p, comm, comm, cls);
            });
            if (!listed)
                close = true;
        }
        if (close) {
            // Hyprland's Lua config speaks hl.dsp; older ones the plain dispatcher.
            const QByteArray answer = hyprctl("dispatch hl.dsp.window.close({ window = \"address:" + address.toUtf8() + "\" })");
            if (answer.trimmed() != "ok")
                hyprctl("dispatch closewindow address:" + address.toUtf8());
        }
    }
}

// ---------------------------------------------------------------- usage

// Omarchy web apps are Chromium windows whose class carries the site:
// "chrome-youtube.com__-Default".
static QString webAppUrl(const QString &cls) {
    static const QRegularExpression pattern("^chrome-([^_]+)__(.*)-[^-]+$", QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(cls);
    if (!match.hasMatch())
        return {};
    QString path = match.captured(2);
    path.replace('_', '/');
    return "https://" + match.captured(1) + "/" + path;
}

void Engine::trackUsage(const QDateTime &at, const QJsonObject &activeWindow) {
    const QString cls = activeWindow.value("class").toString();
    if (cls.isEmpty())
        return;
    const int pid = activeWindow.value("pid").toInt();
    const QString comm = readFirstLine(QString("%1/%2/comm").arg(m_paths.proc).arg(pid));

    QString url = webAppUrl(cls);
    if (url.isEmpty() && cls.toLower() == "chromium" && m_extensionSeen.isValid() &&
        m_extensionSeen.secsTo(at) <= 5) {
        for (const auto &tab : m_extensionTabs)
            if (tab.toObject().value("active").toBool())
                url = tab.toObject().value("url").toString();
    }

    const QString day = at.toString("yyyy-MM-dd");
    QJsonObject today = m_stats.value(day).toObject();
    const QString appKey = "app:" + cls.toLower();
    today.insert(appKey, today.value(appKey).toInteger() + 1);
    const QString host = url.startsWith("http") ? Model::hostOf(url) : QString();
    if (!host.isEmpty()) {
        const QString siteKey = "site:" + (host.startsWith("www.") ? host.mid(4) : host);
        today.insert(siteKey, today.value(siteKey).toInteger() + 1);
    }
    m_stats.insert(day, today);
    m_usageDirty = true;

    // Limits count focused time on anything the block names, but only while
    // the block's window is open (a limit "on weekdays" ignores weekends).
    for (const auto &value : m_blocks) {
        const QJsonObject block = value.toObject();
        const QJsonObject limit = block.value("limit").toObject();
        if (limit.value("minutes").toInt() <= 0)
            continue;
        const QString id = block.value("id").toString();
        const auto evaluation = m_evaluations.value(id);
        if (!evaluation.inWindow || evaluation.blocking)
            continue;
        bool matches = false;
        for (const auto &app : strings(block.value("apps")))
            if (Model::appMatches(app, comm, comm, cls))
                matches = true;
        if (!url.isEmpty())
            for (const auto &site : strings(block.value("sites")))
                if (Model::urlMatches(site, url))
                    matches = true;
        if (!matches)
            continue;
        const QString key = id + "|" + Model::periodKey(limit.value("period").toString("day"), at);
        const qint64 used = m_usage.value(key) + 1;
        m_usage.insert(key, used);
        const qint64 left = 60LL * limit.value("minutes").toInt() - used;
        if (left == 300 || left == 60)
            notify(block.value("name").toString(), QString("%1 minute%2 left").arg(left / 60).arg(left == 60 ? "" : "s"));
    }

    // Breaks are used up while they run.
    for (const auto &id : m_runtime.keys()) {
        QJsonObject runtime = m_runtime.value(id).toObject();
        if (runtime.value("suspendKind").toString() != "break")
            continue;
        const QJsonObject breaks = blockById(id).value("allowances").toObject().value("breaks").toObject();
        const int per = std::max(1, breaks.value("per").toInt(60));
        const QString bucket = day + "#" + QString::number((at.time().hour() * 60 + at.time().minute()) / per);
        QJsonObject used = runtime.value("breakUsed").toObject();
        used = QJsonObject{{bucket, used.value(bucket).toInteger() + 1}};
        runtime.insert("breakUsed", used);
        m_runtime.insert(id, runtime);
    }
}

// ---------------------------------------------------------------- notices

void Engine::notifyUpcoming(const QDateTime &at) {
    if (!m_nextStartComputed.isValid() || m_nextStartComputed.secsTo(at) >= 20) {
        m_nextStartComputed = at;
        m_nextStart.clear();
        for (const auto &value : m_blocks) {
            const QJsonObject block = value.toObject();
            if (!block.value("enabled").toBool(true))
                continue;
            const QDateTime next = Model::nextScheduleStart(block, at, 14);
            if (next.isValid())
                m_nextStart.insert(block.value("id").toString(), next);
        }
    }
    for (auto it = m_nextStart.begin(); it != m_nextStart.end(); ++it) {
        const QJsonObject block = blockById(it.key());
        if (block.value("limit").toObject().value("minutes").toInt() > 0)
            continue;   // A limit window opening blocks nothing yet.
        const qint64 seconds = at.secsTo(it.value());
        for (const auto &lead : m_settings.value("notifyLead").toArray()) {
            const qint64 leadSeconds = 60LL * lead.toInt();
            if (leadSeconds <= 0 || seconds > leadSeconds || seconds <= leadSeconds - 60)
                continue;
            const QString key = it.key() + "|" + Model::formatDateTime(it.value()) + "|" + QString::number(lead.toInt());
            if (m_notified.contains(key))
                continue;
            m_notified.insert(key);
            notify(block.value("name").toString(),
                   QString("Starts in %1 minute%2, at %3").arg(lead.toInt()).arg(lead.toInt() == 1 ? "" : "s")
                       .arg(it.value().toString("HH:mm")));
        }
    }
}

void Engine::notify(const QString &title, const QString &body) {
    if (!m_paths.enforce || m_userName.isEmpty())
        return;
    // Notifications belong to the person's session bus, so they are sent as
    // that person.
    QProcess::startDetached("runuser", {"-u", m_userName, "--", "env",
                                        QString("DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/%1/bus").arg(m_uid),
                                        "notify-send", "-a", "Vow", "-i", "vow", title, body});
}

void Engine::refreshWifi() {
    if (m_wifiProcess.state() != QProcess::NotRunning)
        return;
    // `iw dev` lists every wireless interface with the network it is on.
    m_wifiProcess.start("iw", {"dev"});
}

// ---------------------------------------------------------------- status

void Engine::writeStatus(const QDateTime &at) {
    QJsonArray active;
    for (const auto &value : m_blocks) {
        const QJsonObject block = value.toObject();
        const QString id = block.value("id").toString();
        const auto evaluation = m_evaluations.value(id);
        if (!evaluation.blocking && !evaluation.suspended)
            continue;
        active.append(QJsonObject{
            {"id", id},
            {"name", block.value("name").toString()},
            {"reason", evaluation.reason},
            {"endsAt", Model::formatDateTime(evaluation.endsAt)},
            {"suspended", evaluation.suspended},
            {"suspendedUntil", Model::formatDateTime(evaluation.suspendedUntil)},
            {"locked", Model::lockActive(block, at)},
        });
    }
    QJsonObject next;
    for (auto it = m_nextStart.begin(); it != m_nextStart.end(); ++it) {
        const auto evaluation = m_evaluations.value(it.key());
        if (evaluation.blocking || evaluation.suspended)
            continue;
        if (next.isEmpty() || it.value() < Model::parseDateTime(next.value("startsAt").toString()))
            next = QJsonObject{{"id", it.key()},
                               {"name", blockById(it.key()).value("name").toString()},
                               {"startsAt", Model::formatDateTime(it.value())}};
    }
    QJsonObject status{{"updated", Model::formatDateTime(at)}, {"active", active}, {"next", next}};
    const QJsonObject compare = status;
    QJsonObject previous = m_status;
    previous.remove("updated");
    QJsonObject current = compare;
    current.remove("updated");
    m_status = status;
    // The bar watches this file, so it is only rewritten when something it
    // shows changed, or once a minute to show the daemon is alive.
    if (previous != current || at.time().second() == 0)
        writeAtomically(m_paths.runDir + "/status.json", QJsonDocument(status).toJson(QJsonDocument::Compact),
                        WorldReadable);
}

// ---------------------------------------------------------------- hyprland

QString Engine::findHyprlandSocket() {
    QString best;
    QDateTime newest;
    QDirIterator users(m_paths.runUser, QDir::Dirs | QDir::NoDotAndDotDot);
    while (users.hasNext()) {
        const QString hypr = users.next() + "/hypr";
        QDirIterator instances(hypr, QDir::Dirs | QDir::NoDotAndDotDot);
        while (instances.hasNext()) {
            const QString instance = instances.next();
            const QString socket = instance + "/.socket.sock";
            if (!QFileInfo::exists(socket))
                continue;
            const QDateTime modified = QFileInfo(instance).lastModified();
            if (!newest.isValid() || modified > newest) {
                newest = modified;
                best = socket;
            }
        }
    }
    return best;
}

QByteArray Engine::hyprctl(const QByteArray &command) {
    if (m_hyprSocket.isEmpty())
        return {};
    QLocalSocket socket;
    socket.connectToServer(m_hyprSocket);
    if (!socket.waitForConnected(200)) {
        m_hyprSocket.clear();
        return {};
    }
    socket.write(command);
    socket.flush();
    QByteArray data;
    while (socket.waitForReadyRead(300))
        data += socket.readAll();
    return data;
}

// ---------------------------------------------------------------- requests

QJsonObject Engine::reply(bool ok, const QString &error) const {
    QJsonObject object{{"ok", ok}};
    if (!error.isEmpty())
        object.insert("error", error);
    return object;
}

QJsonObject Engine::snapshot() const {
    const QDateTime at = now();
    QJsonObject evaluations;
    for (const auto &value : m_blocks) {
        const QJsonObject block = value.toObject();
        const QString id = block.value("id").toString();
        const auto evaluation = m_evaluations.value(id);
        const QJsonObject runtime = m_runtime.value(id).toObject();
        const QJsonObject allowances = block.value("allowances").toObject();

        QJsonObject breaks;
        const QJsonObject breakRule = allowances.value("breaks").toObject();
        if (breakRule.value("minutes").toInt() > 0) {
            const int per = std::max(1, breakRule.value("per").toInt(60));
            const QString bucket = at.toString("yyyy-MM-dd") + "#" +
                                   QString::number((at.time().hour() * 60 + at.time().minute()) / per);
            const qint64 used = runtime.value("breakUsed").toObject().value(bucket).toInteger();
            breaks = QJsonObject{{"left", std::max<qint64>(0, 60LL * breakRule.value("minutes").toInt() - used)}};
        }
        QJsonObject passes;
        const QJsonObject passRule = allowances.value("passes").toObject();
        if (passRule.value("count").toInt() > 0) {
            const int used = runtime.value("passes").toObject().value(at.toString("yyyy-MM-dd")).toInt();
            passes = QJsonObject{{"left", std::max(0, passRule.value("count").toInt() - used)}};
        }

        evaluations.insert(id, QJsonObject{
            {"blocking", evaluation.blocking},
            {"inWindow", evaluation.inWindow},
            {"suspended", evaluation.suspended},
            {"suspendKind", runtime.value("suspendKind").toString()},
            {"suspendedUntil", Model::formatDateTime(evaluation.suspendedUntil)},
            {"reason", evaluation.reason},
            {"endsAt", Model::formatDateTime(evaluation.endsAt)},
            {"limitUsed", evaluation.limitUsed},
            {"limitTotal", evaluation.limitTotal},
            {"locked", Model::lockActive(block, at)},
            {"lockUntil", Model::formatDateTime(Model::lockUntil(block))},
            {"session", runtime.value("session")},
            {"nextStart", Model::formatDateTime(m_nextStart.value(id))},
            {"breaks", breaks},
            {"passes", passes},
        });
    }
    QJsonObject stats;
    for (int i = 0; i < 14; ++i) {
        const QString day = at.date().addDays(-i).toString("yyyy-MM-dd");
        if (m_stats.contains(day))
            stats.insert(day, m_stats.value(day));
    }
    return QJsonObject{
        {"ok", true},
        {"now", Model::formatDateTime(at)},
        {"blocks", m_blocks},
        {"evaluations", evaluations},
        {"settings", m_settings},
        {"stats", stats},
        {"status", m_status},
        {"ssid", m_ssid},
        {"hyprland", !m_hyprSocket.isEmpty()},
        {"extension", m_extensionSeen.isValid() && m_extensionSeen.secsTo(at) < 30},
    };
}

QJsonObject Engine::handle(const QJsonObject &request) {
    const QString command = request.value("cmd").toString();
    const QDateTime at = now();

    if (command == "get")
        return snapshot();

    if (command == "save") {
        QJsonObject block = Model::normalizeBlock(request.value("block").toObject());
        if (block.value("name").toString().trimmed().isEmpty())
            return reply(false, "A block needs a name.");
        QString id = block.value("id").toString();
        if (id.isEmpty()) {
            id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
            block.insert("id", id);
        }
        const int index = blockIndex(id);
        if (index >= 0) {
            const QJsonObject before = m_blocks.at(index).toObject();
            const QString refusal = Model::checkLockedEdit(before, block, at);
            if (!refusal.isEmpty())
                return reply(false, QString(refusal).replace("%1", Model::lockUntil(before).toString("d MMM HH:mm")));
            m_blocks.replace(index, block);
        } else {
            m_blocks.append(block);
        }
        m_dirty = true;
        m_nextStartComputed = {};
        tick(at);
        QJsonObject result = reply(true);
        result.insert("id", id);
        return result;
    }

    const QString id = request.value("id").toString();
    const int index = blockIndex(id);
    if (command != "settings" && index < 0)
        return reply(false, "No such block.");
    const QJsonObject block = index >= 0 ? m_blocks.at(index).toObject() : QJsonObject();
    QJsonObject runtime = m_runtime.value(id).toObject();

    if (command == "delete") {
        if (Model::lockActive(block, at))
            return reply(false, QString("%1 is locked until %2.").arg(block.value("name").toString(),
                                                                       Model::lockUntil(block).toString("d MMM HH:mm")));
        m_blocks.removeAt(index);
        m_runtime.remove(id);
        m_dirty = true;
        tick(at);
        return reply(true);
    }

    if (command == "lock") {
        const QDateTime until = Model::parseDateTime(request.value("until").toString());
        if (!until.isValid() || until <= at)
            return reply(false, "A lock needs a time in the future.");
        QJsonObject updated = block;
        QJsonObject lock = updated.value("lock").toObject();
        QJsonArray conditions = lock.value("conditions").toArray();
        // A lock is one timer that only ever moves later.
        QJsonArray kept;
        for (const auto &condition : conditions)
            if (condition.toObject().value("type").toString() != "timer")
                kept.append(condition);
        const QDateTime current = Model::lockUntil(block);
        kept.append(QJsonObject{{"type", "timer"},
                                {"until", Model::formatDateTime(current.isValid() && current > until ? current : until)}});
        lock.insert("conditions", kept);
        updated.insert("lock", lock);
        updated.insert("enabled", true);
        m_blocks.replace(index, updated);
        m_dirty = true;
        tick(at);
        return reply(true);
    }

    if (command == "session") {
        const int minutes = request.value("minutes").toInt();
        if (minutes <= 0)
            return reply(false, "A session needs a length.");
        QDateTime end = at.addSecs(60LL * minutes);
        const QDateTime existing = Model::parseDateTime(runtime.value("session").toObject().value("end").toString());
        const bool lockedBefore = runtime.value("session").toObject().value("locked").toBool();
        if (existing.isValid() && existing > end)
            end = existing;   // Starting again never shortens a running session.
        runtime.insert("session", QJsonObject{{"start", Model::formatDateTime(at)},
                                              {"end", Model::formatDateTime(end)},
                                              {"locked", lockedBefore || request.value("lock").toBool()}});
        m_runtime.insert(id, runtime);
        m_dirty = true;
        if (request.value("lock").toBool())
            handle(QJsonObject{{"cmd", "lock"}, {"id", id}, {"until", Model::formatDateTime(end)}});
        tick(at);
        return reply(true);
    }

    if (command == "stopSession") {
        if (runtime.value("session").toObject().value("locked").toBool() || Model::lockActive(block, at))
            return reply(false, "This session is locked and ends on its own.");
        runtime.remove("session");
        m_runtime.insert(id, runtime);
        m_dirty = true;
        tick(at);
        return reply(true);
    }

    if (command == "break" || command == "pass") {
        const auto evaluation = m_evaluations.value(id);
        if (!evaluation.blocking)
            return reply(false, "The block is not blocking right now.");
        const QJsonObject allowances = block.value("allowances").toObject();
        QDateTime until;
        if (command == "break") {
            const QJsonObject breaks = allowances.value("breaks").toObject();
            if (breaks.value("minutes").toInt() <= 0)
                return reply(false, "This block has no breaks.");
            const int per = std::max(1, breaks.value("per").toInt(60));
            const QString bucket = at.toString("yyyy-MM-dd") + "#" +
                                   QString::number((at.time().hour() * 60 + at.time().minute()) / per);
            const qint64 left = 60LL * breaks.value("minutes").toInt() -
                                runtime.value("breakUsed").toObject().value(bucket).toInteger();
            if (left <= 0)
                return reply(false, "No break time left in this period.");
            until = at.addSecs(left);
        } else {
            const QJsonObject passRule = allowances.value("passes").toObject();
            const QString day = at.toString("yyyy-MM-dd");
            QJsonObject passes = runtime.value("passes").toObject();
            if (passRule.value("count").toInt() <= 0)
                return reply(false, "This block has no passes.");
            if (passes.value(day).toInt() >= passRule.value("count").toInt())
                return reply(false, "No passes left today.");
            passes = QJsonObject{{day, passes.value(day).toInt() + 1}};
            runtime.insert("passes", passes);
            until = at.addSecs(60LL * std::max(1, passRule.value("minutes").toInt(5)));
        }
        runtime.insert("suspendedUntil", Model::formatDateTime(until));
        runtime.insert("suspendKind", command);
        m_runtime.insert(id, runtime);
        m_dirty = true;
        tick(at);
        return reply(true);
    }

    if (command == "endBreak") {
        runtime.remove("suspendedUntil");
        runtime.remove("suspendKind");
        m_runtime.insert(id, runtime);
        m_dirty = true;
        tick(at);
        return reply(true);
    }

    if (command == "settings") {
        const QJsonObject settings = request.value("settings").toObject();
        for (auto it = settings.begin(); it != settings.end(); ++it)
            m_settings.insert(it.key(), it.value());
        m_dirty = true;
        return reply(true);
    }

    return reply(false, "Unknown request.");
}

void Engine::acceptLocal() {
    while (QLocalSocket *socket = m_local.nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            while (socket->canReadLine()) {
                const QByteArray line = socket->readLine();
                QJsonParseError parseError;
                const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
                const QJsonObject answer = parseError.error == QJsonParseError::NoError
                                               ? handle(document.object())
                                               : reply(false, "Requests are one JSON object per line.");
                socket->write(QJsonDocument(answer).toJson(QJsonDocument::Compact) + "\n");
            }
            // Requests are small; anything this large is not one.
            if (socket->bytesAvailable() > 1 << 20)
                socket->abort();
        });
    }
}

void Engine::acceptHttp() {
    while (QTcpSocket *socket = m_http.nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            *buffer += socket->readAll();
            if (buffer->size() > 256 * 1024) {
                socket->abort();
                return;
            }
            const int headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            const QByteArray head = buffer->left(headerEnd);
            const QList<QByteArray> lines = head.split('\n');
            const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
            qsizetype length = 0;
            QByteArray origin;
            for (const auto &line : lines) {
                const int colon = line.indexOf(':');
                if (colon < 0)
                    continue;
                const QByteArray name = line.left(colon).trimmed().toLower();
                const QByteArray value = line.mid(colon + 1).trimmed();
                if (name == "content-length")
                    length = value.toLongLong();
                else if (name == "origin")
                    origin = value;
            }
            if (buffer->size() < headerEnd + 4 + length)
                return;
            const QByteArray body = buffer->mid(headerEnd + 4, length);
            const QByteArray answer = handleHttp(requestLine.value(0), requestLine.value(1), origin, body);
            socket->write(answer);
            socket->disconnectFromHost();
        });
    }
}

QByteArray Engine::handleHttp(const QByteArray &method, const QByteArray &path, const QByteArray &origin,
                              const QByteArray &body) {
    auto respond = [&](int code, const QByteArray &json) {
        QByteArray response = "HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK" : " Error") + "\r\n";
        response += "Content-Type: application/json\r\nConnection: close\r\n";
        if (origin.startsWith("chrome-extension://"))
            response += "Access-Control-Allow-Origin: " + origin + "\r\nAccess-Control-Allow-Headers: Content-Type\r\n";
        response += "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
        return response;
    };
    // Only the extension talks to this port; web pages could otherwise probe
    // what is blocked.
    if (!origin.isEmpty() && !origin.startsWith("chrome-extension://"))
        return respond(403, R"({"error":"forbidden"})");
    if (method == "OPTIONS")
        return respond(200, "{}");
    if (method != "POST" || (path != "/report" && path != "/check"))
        return respond(404, R"({"error":"not found"})");

    // The blocked page asks about its one URL, and wears the desktop's colors.
    if (path == "/check") {
        const QString url = QJsonDocument::fromJson(body).object().value("url").toString().left(4096);
        const QString name = siteBlockedBy(url);
        QString until;
        for (const auto &blockValue : m_blocks) {
            const QJsonObject block = blockValue.toObject();
            if (!name.isEmpty() && block.value("name").toString() == name)
                until = Model::formatDateTime(m_evaluations.value(block.value("id").toString()).endsAt);
        }
        QJsonObject colors;
        const passwd *entry = m_uid ? getpwuid(m_uid) : nullptr;
        if (entry) {
            const auto palette = Theme::readColors(QString::fromLocal8Bit(entry->pw_dir) +
                                                   "/.local/state/omarchy/current/theme/colors.toml");
            for (const QString key : {"accent", "background", "foreground", "dark_foreground", "muted"})
                if (palette.contains(key))
                    colors.insert(key, palette.value(key));
        }
        return respond(200, QJsonDocument(QJsonObject{{"block", name}, {"until", until}, {"colors", colors}})
                                .toJson(QJsonDocument::Compact));
    }

    const QJsonObject report = QJsonDocument::fromJson(body).object();
    const QDateTime at = now();
    m_extensionTabs = report.value("tabs").toArray();
    m_extensionFocused = report.value("focused").toBool();
    m_extensionSeen = at;

    QJsonArray blocked;
    for (const auto &value : m_extensionTabs) {
        const QJsonObject tab = value.toObject();
        const QString url = tab.value("url").toString().left(4096);
        const QString name = siteBlockedBy(url);
        if (name.isEmpty())
            continue;
        QString until;
        for (const auto &blockValue : m_blocks) {
            const QJsonObject block = blockValue.toObject();
            if (block.value("name").toString() == name)
                until = Model::formatDateTime(m_evaluations.value(block.value("id").toString()).endsAt);
        }
        blocked.append(QJsonObject{{"id", tab.value("id")}, {"block", name}, {"until", until}});
    }
    return respond(200, QJsonDocument(QJsonObject{{"blocked", blocked}}).toJson(QJsonDocument::Compact));
}
