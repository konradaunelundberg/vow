#include "cli.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QTextStream>

#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include "client.h"
#include "engine.h"
#include "model.h"

namespace Cli {

static QTextStream out(stdout);
static QTextStream err(stderr);

int parseMinutes(const QString &raw) {
    const QString text = raw.trimmed().toLower();
    bool plain = false;
    const int number = text.toInt(&plain);
    if (plain)
        return std::max(0, number);
    static const QRegularExpression part(R"((\d+)\s*(d|h|m))");
    int minutes = 0;
    int consumed = 0;
    auto it = part.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        const int value = match.captured(1).toInt();
        const QString unit = match.captured(2);
        minutes += unit == "d" ? value * 1440 : unit == "h" ? value * 60 : value;
        consumed += match.capturedLength();
    }
    return consumed == QString(text).remove(' ').size() ? minutes : 0;
}

static void usage() {
    out << "Vow — schedule-driven website and app blocking\n\n"
           "  vow                         open the window\n"
           "  vow status [--json]         what is blocking now and what comes next\n"
           "  vow list [--json]           every block\n"
           "  vow start <block> <time> [--lock]\n"
           "                              block now for a while (90m, 1h30m, 2h)\n"
           "  vow stop <block>            end a session that is not locked\n"
           "  vow lock <block> <time>     lock a block for a while, or until YYYY-MM-DDTHH:MM\n"
           "  vow break <block>           take break time the block allows\n"
           "  vow pass <block>            use one of the block's passes\n"
           "  vow setup                   turn on the Chromium extension and the bar widget\n"
           "  vow daemon                  run the enforcing service (as root, from systemd)\n";
    out.flush();
}

static QString resolveBlock(const QJsonObject &snapshot, const QString &nameOrId) {
    for (const auto &value : snapshot.value("blocks").toArray()) {
        const QJsonObject block = value.toObject();
        if (block.value("id").toString() == nameOrId ||
            block.value("name").toString().compare(nameOrId, Qt::CaseInsensitive) == 0)
            return block.value("id").toString();
    }
    return {};
}

static int finish(const QJsonObject &answer, bool json) {
    if (json) {
        out << QJsonDocument(answer).toJson(QJsonDocument::Indented);
        out.flush();
    }
    if (!answer.value("ok").toBool()) {
        err << answer.value("error").toString("Failed.") << "\n";
        err.flush();
        return 1;
    }
    return 0;
}

static int runDaemon(const QStringList &args) {
    // `vow daemon --dev <dir>` runs unprivileged against files in <dir>,
    // enforcing nothing, so the window can be tried without root.
    const int dev = args.indexOf("--dev");
    EnginePaths paths;
    if (dev >= 0) {
        const QString dir = args.value(dev + 1, "/tmp/vow-dev");
        paths.stateDir = dir + "/state";
        paths.runDir = dir + "/run";
        paths.hosts = dir + "/hosts";
        paths.chromiumPolicy = dir + "/chromium.json";
        paths.enforce = false;
    } else if (geteuid() != 0) {
        err << "vow daemon has to run as root, from the vow systemd service.\n";
        return 1;
    }
    // SIGTERM from systemd has to reach the event loop so the daemon can
    // undo its hosts and Chromium changes on the way out.
    static int pipe[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pipe) != 0)
        return 1;
    auto handler = [](int) {
        char byte = 1;
        [[maybe_unused]] auto written = ::write(pipe[0], &byte, 1);
    };
    ::signal(SIGTERM, handler);
    ::signal(SIGINT, handler);
    ::signal(SIGPIPE, SIG_IGN);

    Engine engine(paths);
    QSocketNotifier notifier(pipe[1], QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, [&] {
        engine.stop();
        QCoreApplication::quit();
    });
    QString error;
    if (!engine.start(&error)) {
        err << error << "\n";
        return 1;
    }
    return QCoreApplication::exec();
}


// Copies a directory tree, replacing what is there.
static bool copyTree(const QString &from, const QString &to) {
    QDir(to).removeRecursively();
    QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString source = it.next();
        const QString target = to + source.mid(from.size());
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(source, target))
            return false;
    }
    return true;
}

// The parts that live in the person's own config: the Chromium extension
// (Omarchy loads extensions from chromium-flags.conf) and the bar widget.
static int runSetup(const QStringList &args) {
    if (geteuid() == 0) {
        err << "Run vow setup as yourself, not as root.\n";
        return 1;
    }
    const QString share = args.contains("--from") ? args.value(args.indexOf("--from") + 1) : "/usr/share/vow";
    const QString extension = share + "/chromium-extension";
    bool ok = true;

    // Chromium: add the extension to the --load-extension line, or add one.
    const QString flagsPath = QDir::homePath() + "/.config/chromium-flags.conf";
    QFile flags(flagsPath);
    QString contents;
    if (flags.open(QIODevice::ReadOnly))
        contents = QString::fromUtf8(flags.readAll());
    flags.close();
    if (contents.contains(extension)) {
        out << "Chromium: the Vow extension is already loaded.\n";
    } else {
        if (QFile::exists(flagsPath))
            QFile::copy(flagsPath, flagsPath + ".bak-vow");
        QStringList lines = contents.split('\n');
        bool added = false;
        for (auto &line : lines) {
            if (line.trimmed().startsWith("--load-extension=")) {
                line = line.trimmed() + "," + extension;
                added = true;
                break;
            }
        }
        if (!added) {
            if (!lines.isEmpty() && lines.last().isEmpty())
                lines.removeLast();
            lines << "--load-extension=" + extension << "";
        }
        if (flags.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            flags.write(lines.join('\n').toUtf8());
            out << "Chromium: added the Vow extension to " << flagsPath << " (restart Chromium to load it).\n";
        } else {
            err << "Chromium: could not write " << flagsPath << "\n";
            ok = false;
        }
    }

    // Omarchy bar: install the widget as a user plugin and put it on the bar.
    const QString plugins = QDir::homePath() + "/.config/omarchy/plugins";
    if (QFileInfo::exists(QDir::homePath() + "/.config/omarchy")) {
        if (copyTree(share + "/shell-plugin", plugins + "/vow")) {
            out << "Bar: installed the Vow widget in " << plugins << "/vow.\n";
            const int code = QProcess::execute("omarchy", {"plugin", "enable", "vow.status", "--section", "centerBegin"});
            if (code != 0)
                err << "Bar: add it with: omarchy plugin enable vow.status\n";
        } else {
            err << "Bar: could not copy the widget into " << plugins << "\n";
            ok = false;
        }
    } else {
        out << "Bar: Omarchy's shell config was not found; skipping the widget.\n";
    }

    const QJsonObject probe = Client::request({{"cmd", "get"}}, 800);
    if (!probe.value("ok").toBool())
        out << "Service: not running yet. Start it with: sudo systemctl enable --now vow\n";
    out.flush();
    err.flush();
    return ok ? 0 : 1;
}

int run(const QStringList &arguments) {
    if (arguments.isEmpty() || arguments.first() == "open")
        return -1;

    QStringList args = arguments;
    const QString command = args.takeFirst();
    const bool json = args.removeAll("--json") > 0;
    const bool lock = args.removeAll("--lock") > 0;

    if (command == "help" || command == "--help" || command == "-h") {
        usage();
        return 0;
    }
    if (command == "daemon")
        return runDaemon(args);
    if (command == "setup")
        return runSetup(args);

    const QJsonObject snapshot = Client::request({{"cmd", "get"}});
    if (!snapshot.value("ok").toBool())
        return finish(snapshot, json);

    if (command == "status") {
        const QJsonObject status = snapshot.value("status").toObject();
        if (json) {
            out << QJsonDocument(status).toJson(QJsonDocument::Indented);
            out.flush();
            return 0;
        }
        const QJsonArray active = status.value("active").toArray();
        if (active.isEmpty())
            out << "Nothing is blocked.\n";
        for (const auto &value : active) {
            const QJsonObject block = value.toObject();
            const QDateTime end = Model::parseDateTime(block.value("endsAt").toString());
            out << block.value("name").toString()
                << (block.value("suspended").toBool() ? "  on a break" : "  blocking")
                << (end.isValid() ? "  until " + end.toString("ddd HH:mm") : QString())
                << (block.value("locked").toBool() ? "  locked" : "") << "\n";
        }
        const QJsonObject next = status.value("next").toObject();
        if (!next.isEmpty())
            out << "Next: " << next.value("name").toString() << " at "
                << Model::parseDateTime(next.value("startsAt").toString()).toString("ddd d MMM HH:mm") << "\n";
        out.flush();
        return 0;
    }

    if (command == "list") {
        if (json) {
            out << QJsonDocument(snapshot.value("blocks").toArray()).toJson(QJsonDocument::Indented);
            out.flush();
            return 0;
        }
        const QJsonObject evaluations = snapshot.value("evaluations").toObject();
        for (const auto &value : snapshot.value("blocks").toArray()) {
            const QJsonObject block = value.toObject();
            const QJsonObject evaluation = evaluations.value(block.value("id").toString()).toObject();
            out << block.value("id").toString() << "  " << block.value("name").toString()
                << (evaluation.value("blocking").toBool() ? "  [blocking]" : "")
                << (evaluation.value("locked").toBool() ? "  [locked]" : "") << "\n";
        }
        out.flush();
        return 0;
    }

    if (args.isEmpty()) {
        usage();
        return 1;
    }
    const QString id = resolveBlock(snapshot, args.first());
    if (id.isEmpty()) {
        err << "No block called " << args.first() << ".\n";
        return 1;
    }

    if (command == "start") {
        const int minutes = Cli::parseMinutes(args.value(1));
        if (minutes <= 0) {
            err << "Say for how long: vow start " << args.first() << " 90m\n";
            return 1;
        }
        return finish(Client::request({{"cmd", "session"}, {"id", id}, {"minutes", minutes}, {"lock", lock}}), json);
    }
    if (command == "stop")
        return finish(Client::request({{"cmd", "stopSession"}, {"id", id}}), json);
    if (command == "break" || command == "pass")
        return finish(Client::request({{"cmd", command}, {"id", id}}), json);
    if (command == "lock") {
        QDateTime until = Model::parseDateTime(args.value(1));
        if (!until.isValid()) {
            const int minutes = Cli::parseMinutes(args.value(1));
            if (minutes > 0)
                until = QDateTime::currentDateTime().addSecs(60LL * minutes);
        }
        if (!until.isValid()) {
            err << "Say how long: vow lock " << args.first() << " 7d, or until 2026-12-24T18:00\n";
            return 1;
        }
        return finish(Client::request({{"cmd", "lock"}, {"id", id}, {"until", Model::formatDateTime(until)}}), json);
    }

    usage();
    return 1;
}

} // namespace Cli
