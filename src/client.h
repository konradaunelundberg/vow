#pragma once

// Talks to the daemon over /run/vow/vow.sock: one JSON object per line each
// way. The GUI and the CLI both go through this.

#include <QJsonObject>
#include <QString>

namespace Client {

QString socketPath();
// Blocks for at most `timeoutMs`. On failure the reply has ok=false and an
// error that says what to do.
QJsonObject request(const QJsonObject &message, int timeoutMs = 2000);

} // namespace Client
