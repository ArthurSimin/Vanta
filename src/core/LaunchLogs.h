#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

// One stored launch log. `path` is the on-disk file; the header fields are
// repeated here so the list can be shown without reading every log.
struct LaunchLogEntry
{
    QString path;
    QDateTime startedAt;
    QDateTime finishedAt;
    QString gameId;
    QString gameName;
    QString runner;
    int exitCode = 0;
    bool crashed = false;
    bool stopped = false;
    bool running = false;
    qint64 bytes = 0;

    bool isValid() const { return !path.isEmpty(); }
    QString fileName() const;
};

// Persists the stdout/stderr of a launch under
// $XDG_DATA_HOME/vanta/logs/<game id>/<timestamp>.log so a game that closes
// unexpectedly can still be diagnosed after the process is gone. Logs are plain
// text with a short header; nothing here talks to the network or a display.
namespace LaunchLogs
{
// Directory that holds one subdirectory per game.
QString rootDirectory();

// Full path for a new log for `gameId` created at `timestamp`.
QString pathFor(const QString &gameId, const QDateTime &timestamp);

// Writes one log and returns its path, or an empty string on failure.
QString write(const QString &gameId, const QString &gameName, const QString &runner,
              const QDateTime &startedAt, const QDateTime &finishedAt, int exitCode, bool crashed,
              bool stopped, const QString &diagnostics, QString *error = nullptr);

// Starts a log while the game is still running so the viewer is not empty
// during a launch. `commandSummary` is written once under the header; output is
// appended with appendLive() as the process prints it. finishLive() rewrites the
// same file with the finished header and the complete diagnostics. appendLive()
// keeps the file bounded on disk, retaining the header and recent output.
QString beginLive(const QString &gameId, const QString &gameName, const QString &runner,
                  const QDateTime &startedAt, const QString &commandSummary, QString *error = nullptr);
bool appendLive(const QString &path, const QString &text);
QString finishLive(const QString &path, const QString &gameId, const QString &gameName,
                   const QString &runner, const QDateTime &startedAt, const QDateTime &finishedAt,
                   int exitCode, bool crashed, bool stopped, const QString &diagnostics,
                   QString *error = nullptr);

// Newest first. Only files matching the naming scheme are listed.
QList<LaunchLogEntry> list(const QString &gameId, int limit = 40);

// Reads one log file, trimmed to the last `maximumBytes` characters.
QString read(const QString &path, QString *error = nullptr);

// Deletes every stored log for a game. Returns the number removed.
int removeAll(const QString &gameId);

// Removes the oldest logs for a game once more than `keep` exist.
int prune(const QString &gameId, int keep = 20);
}
