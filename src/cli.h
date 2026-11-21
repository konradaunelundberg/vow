#pragma once

#include <QStringList>

namespace Cli {

// Returns -1 when the arguments ask for the window rather than a command.
int run(const QStringList &arguments);
// "90m", "1h30m", "2h", "45" (minutes); 0 when unreadable.
int parseMinutes(const QString &text);

} // namespace Cli
