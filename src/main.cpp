// Vow: schedule-driven website and app blocking for Omarchy. One binary is
// the window, the CLI, and (as root, from systemd) the daemon that enforces.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include "backend.h"
#include "cli.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    QStringList arguments;
    for (int i = 1; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);

    if (!arguments.isEmpty() && arguments.first() != "open") {
        // Commands need no display, and the daemon runs where there is none.
        QCoreApplication app(argc, argv);
        app.setApplicationName("vow");
        app.setApplicationVersion("0.1.0");
        return Cli::run(arguments);
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("vow");
    app.setApplicationVersion("0.1.0");

    // Matches the window to vow.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("vow");
    app.setWindowIcon(QIcon::fromTheme("vow"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // VOW_SCREENSHOT=<file.png>[,page] renders the window, saves it, and
    // quits: how the README screenshot and visual checks are made, offscreen.
    const QString screenshot = qEnvironmentVariable("VOW_SCREENSHOT");
    if (!screenshot.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->setProperty("page", screenshot.section(',', 1, 1).toInt());
        const QString action = screenshot.section(',', 2, 2);
        if (screenshot.section(',', 3).toInt() > 0)
            window->setHeight(screenshot.section(',', 3).toInt());
        if (!action.isEmpty())
            QTimer::singleShot(400, window, [window, action] { QMetaObject::invokeMethod(window, "demo", Q_ARG(QVariant, action)); });
        QTimer::singleShot(1500, window, [window, screenshot] {
            window->grabWindow().save(screenshot.section(',', 0, 0));
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
