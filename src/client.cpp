#include "client.h"

#include <QJsonDocument>
#include <QLocalSocket>

namespace Client {

QString socketPath() {
    const QByteArray custom = qgetenv("VOW_SOCKET");
    return custom.isEmpty() ? QStringLiteral("/run/vow/vow.sock") : QString::fromLocal8Bit(custom);
}

QJsonObject request(const QJsonObject &message, int timeoutMs) {
    QLocalSocket socket;
    socket.connectToServer(socketPath());
    if (!socket.waitForConnected(timeoutMs))
        return {{"ok", false},
                {"offline", true},
                {"error", "The Vow service is not running. Start it with: sudo systemctl enable --now vow"}};
    socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + "\n");
    socket.flush();
    QByteArray data;
    while (!data.contains('\n')) {
        if (!socket.waitForReadyRead(timeoutMs))
            return {{"ok", false}, {"error", "The Vow service did not answer."}};
        data += socket.readAll();
    }
    return QJsonDocument::fromJson(data.left(data.indexOf('\n'))).object();
}

} // namespace Client
