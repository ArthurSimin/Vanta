#include "LaunchLogs.h"

#include "I18n.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace {
constexpr qint64 kMaximumReadBytes = 512LL * 1024LL;
constexpr int kMaximumHeaderBytes = 1024;
// The live file grows while a game runs, so it is bounded on disk as well as in
// memory. Once it passes the threshold the header and the most recent output are
// kept and the middle is dropped, which also covers a single oversized chunk.
constexpr qint64 kMaximumLiveBytes = 1024LL * 1024LL;
constexpr qint64 kLiveTrimThreshold = 2 * kMaximumLiveBytes;
constexpr char kLiveHeaderEnd[] = "----------------\n";
constexpr char kLiveTrimMarker[] = "... earlier output was trimmed to keep the live log small ...\n\n";

QString sanitizeComponent(const QString &value)
{
    QString result = value;
    result.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    while (result.startsWith(QLatin1Char('.')))
        result.remove(0, 1);
    return result.isEmpty() ? QStringLiteral("game") : result;
}

QString labelFor(bool crashed, bool stopped, int exitCode)
{
    if (crashed)
        return QStringLiteral("exited unexpectedly");
    if (stopped)
        return QStringLiteral("stopped by the user");
    if (exitCode < 0)
        return QStringLiteral("never started");
    if (exitCode != 0)
        return QStringLiteral("exited with a non-zero code");
    return QStringLiteral("closed normally");
}

QString buildHeader(const QString &gameId, const QString &gameName, const QString &runner,
                    const QDateTime &startedAt, const QDateTime &finishedAt, int exitCode,
                    bool crashed, bool stopped, bool running)
{
    QString header = QStringLiteral("Vanta launch log\n");
    header += QStringLiteral("Game: %1\n").arg(gameName);
    header += QStringLiteral("Game id: %1\n").arg(gameId);
    header += QStringLiteral("Runner: %1\n").arg(runner.isEmpty() ? QStringLiteral("Unknown") : runner);
    header += QStringLiteral("Started: %1\n").arg(startedAt.toString(Qt::ISODate));
    if (running)
        header += QStringLiteral("Finished: (still running)\n");
    else
        header += QStringLiteral("Finished: %1\n").arg(finishedAt.toString(Qt::ISODate));
    if (running)
        header += QStringLiteral("Exit code: unknown (the game is still running)\n");
    else if (exitCode < 0)
        header += QStringLiteral("Exit code: none (the process never started)\n");
    else
        header += QStringLiteral("Exit code: %1\n").arg(exitCode);
    header += QStringLiteral("Result: %1\n").arg(running ? QStringLiteral("running")
                                                        : labelFor(crashed, stopped, exitCode));
    header += QStringLiteral("----------------\n");
    return header;
}

QString writeBody(const QString &path, const QString &text, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = Vanta::tr("Could not create the log file %1: %2")
                         .arg(QDir::toNativeSeparators(path), file.errorString());
        return {};
    }
    file.write(text.toUtf8());
    if (!text.endsWith(QLatin1Char('\n')))
        file.write("\n");
    if (!file.commit()) {
        if (error)
            *error = Vanta::tr("Could not finish writing the log file %1: %2")
                         .arg(QDir::toNativeSeparators(path), file.errorString());
        return {};
    }
    return path;
}

// Rewrites an oversized live log as the header plus its most recent output so a
// chatty runner cannot fill the disk. The header is kept so the viewer still
// knows the game, runner and start time after the middle is dropped.
bool trimLiveLog(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const qint64 size = file.size();
    if (size <= kMaximumLiveBytes) {
        file.close();
        return true;
    }

    QByteArray header = file.read(kMaximumHeaderBytes);
    const int headerEnd = header.indexOf(kLiveHeaderEnd);
    if (headerEnd >= 0)
        header = header.left(headerEnd + int(sizeof(kLiveHeaderEnd)) - 1);
    else
        header.clear();

    const QByteArray marker(kLiveTrimMarker);
    const qint64 tailBudget = qMax<qint64>(0, kMaximumLiveBytes - header.size() - marker.size());
    file.seek(size - tailBudget);
    const QByteArray tail = file.readAll();
    file.close();

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(header);
    out.write(marker);
    out.write(tail);
    return out.commit();
}
}

QString LaunchLogEntry::fileName() const
{
    return QFileInfo(path).fileName();
}

QString LaunchLogs::rootDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/vanta/logs");
    return base;
}

QString LaunchLogs::pathFor(const QString &gameId, const QDateTime &timestamp)
{
    const QString directory = QDir(rootDirectory()).filePath(sanitizeComponent(gameId));
    const QString stamp = timestamp.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    return QDir(directory).filePath(stamp + QStringLiteral(".log"));
}

QString LaunchLogs::write(const QString &gameId, const QString &gameName, const QString &runner,
                          const QDateTime &startedAt, const QDateTime &finishedAt, int exitCode,
                          bool crashed, bool stopped, const QString &diagnostics, QString *error)
{
    if (error)
        error->clear();

    const QString path = pathFor(gameId, finishedAt);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error)
            *error = Vanta::tr("Could not create the log folder %1.")
                         .arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
        return {};
    }

    const QString header = buildHeader(gameId, gameName, runner, startedAt, finishedAt, exitCode,
                                       crashed, stopped, false);
    return writeBody(path, header + diagnostics, error);
}

QString LaunchLogs::beginLive(const QString &gameId, const QString &gameName, const QString &runner,
                              const QDateTime &startedAt, const QString &commandSummary, QString *error)
{
    if (error)
        error->clear();

    const QString path = pathFor(gameId, startedAt);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error)
            *error = Vanta::tr("Could not create the log folder %1.")
                         .arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
        return {};
    }

    const QString header = buildHeader(gameId, gameName, runner, startedAt, QDateTime(), -1, false,
                                       false, true);
    QString body = QDir::toNativeSeparators(commandSummary);
    body += QStringLiteral("\n\nOutput:\n");
    return writeBody(path, header + body, error);
}

bool LaunchLogs::appendLive(const QString &path, const QString &text)
{
    if (path.isEmpty() || text.isEmpty())
        return false;
    QFile file(path);
    if (!file.open(QIODevice::Append))
        return false;
    const QByteArray bytes = text.toUtf8();
    const bool written = file.write(bytes) == bytes.size();
    file.flush();
    const qint64 size = file.size();
    file.close();
    if (!written)
        return false;
    if (size > kLiveTrimThreshold)
        trimLiveLog(path);
    return true;
}

QString LaunchLogs::finishLive(const QString &path, const QString &gameId, const QString &gameName,
                               const QString &runner, const QDateTime &startedAt,
                               const QDateTime &finishedAt, int exitCode, bool crashed, bool stopped,
                               const QString &diagnostics, QString *error)
{
    if (error)
        error->clear();
    if (path.isEmpty())
        return write(gameId, gameName, runner, startedAt, finishedAt, exitCode, crashed, stopped,
                     diagnostics, error);

    const QString header = buildHeader(gameId, gameName, runner, startedAt, finishedAt, exitCode,
                                       crashed, stopped, false);
    return writeBody(path, header + diagnostics, error);
}

QList<LaunchLogEntry> LaunchLogs::list(const QString &gameId, int limit)
{
    QList<LaunchLogEntry> entries;
    const QString directory = QDir(rootDirectory()).filePath(sanitizeComponent(gameId));
    QFileInfoList files = QDir(directory).entryInfoList({QStringLiteral("*.log")}, QDir::Files, QDir::Name);
    std::reverse(files.begin(), files.end());

    for (const QFileInfo &info : files) {
        if (limit > 0 && entries.size() >= limit)
            break;

        LaunchLogEntry entry;
        entry.path = info.absoluteFilePath();
        entry.bytes = info.size();

        QFile file(entry.path);
        if (!file.open(QIODevice::ReadOnly)) {
            entries.append(entry);
            continue;
        }
        const QString head = QString::fromUtf8(file.read(kMaximumHeaderBytes));
        file.close();
        const QStringList lines = head.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const int separator = line.indexOf(QStringLiteral(": "));
            if (separator <= 0)
                continue;
            const QString key = line.left(separator);
            const QString value = line.mid(separator + 2).trimmed();
            if (key == QStringLiteral("Game"))
                entry.gameName = value;
            else if (key == QStringLiteral("Game id"))
                entry.gameId = value;
            else if (key == QStringLiteral("Runner"))
                entry.runner = value;
            else if (key == QStringLiteral("Started"))
                entry.startedAt = QDateTime::fromString(value, Qt::ISODate);
            else if (key == QStringLiteral("Finished"))
                entry.finishedAt = QDateTime::fromString(value, Qt::ISODate);
            else if (key == QStringLiteral("Exit code"))
                entry.exitCode = value.startsWith(QStringLiteral("none")) ? -1 : value.toInt();
            else if (key == QStringLiteral("Result")) {
                entry.crashed = value == QStringLiteral("exited unexpectedly");
                entry.stopped = value == QStringLiteral("stopped by the user");
                entry.running = value == QStringLiteral("running");
            }
        }
        if (!entry.finishedAt.isValid())
            entry.finishedAt = info.lastModified();
        entries.append(entry);
    }
    return entries;
}

QString LaunchLogs::read(const QString &path, QString *error)
{
    if (error)
        error->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = Vanta::tr("Could not read %1: %2")
                         .arg(QDir::toNativeSeparators(path), file.errorString());
        return {};
    }
    const qint64 size = file.size();
    if (size > kMaximumReadBytes)
        file.seek(size - kMaximumReadBytes);
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    return size > kMaximumReadBytes
        ? Vanta::tr("… the first %1 KiB of this log were trimmed.\n\n").arg((size - kMaximumReadBytes) / 1024)
              + text
        : text;
}

int LaunchLogs::removeAll(const QString &gameId)
{
    const QString directory = QDir(rootDirectory()).filePath(sanitizeComponent(gameId));
    const QFileInfoList files = QDir(directory).entryInfoList({QStringLiteral("*.log")}, QDir::Files);
    int removed = 0;
    for (const QFileInfo &info : files) {
        if (QFile::remove(info.absoluteFilePath()))
            ++removed;
    }
    QDir().rmdir(directory);
    return removed;
}

int LaunchLogs::prune(const QString &gameId, int keep)
{
    const QList<LaunchLogEntry> entries = list(gameId, 0);
    int removed = 0;
    for (int index = qMax(0, keep); index < entries.size(); ++index) {
        if (QFile::remove(entries.at(index).path))
            ++removed;
    }
    return removed;
}
