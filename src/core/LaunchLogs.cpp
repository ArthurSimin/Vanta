#include "LaunchLogs.h"

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
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QString();
    };
    if (error)
        error->clear();

    const QString path = pathFor(gameId, finishedAt);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(QStringLiteral("Could not create the log folder %1.")
                        .arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath())));

    QString header = QStringLiteral("Vanta launch log\n");
    header += QStringLiteral("Game: %1\n").arg(gameName);
    header += QStringLiteral("Game id: %1\n").arg(gameId);
    header += QStringLiteral("Runner: %1\n").arg(runner.isEmpty() ? QStringLiteral("Unknown") : runner);
    header += QStringLiteral("Started: %1\n").arg(startedAt.toString(Qt::ISODate));
    header += QStringLiteral("Finished: %1\n").arg(finishedAt.toString(Qt::ISODate));
    header += exitCode < 0 ? QStringLiteral("Exit code: none (the process never started)\n")
                           : QStringLiteral("Exit code: %1\n").arg(exitCode);
    header += QStringLiteral("Result: %1\n").arg(labelFor(crashed, stopped, exitCode));
    header += QStringLiteral("----------------\n");

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("Could not create the log file %1: %2")
                        .arg(QDir::toNativeSeparators(path), file.errorString()));
    file.write(header.toUtf8());
    file.write(diagnostics.toUtf8());
    if (!diagnostics.endsWith(QLatin1Char('\n')))
        file.write("\n");
    if (!file.commit())
        return fail(QStringLiteral("Could not finish writing the log file %1: %2")
                        .arg(QDir::toNativeSeparators(path), file.errorString()));
    return path;
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
            else if (key == QStringLiteral("Result"))
                entry.crashed = value == QStringLiteral("exited unexpectedly");
        }
        entry.stopped = head.contains(QStringLiteral("Result: stopped by the user"));
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
            *error = QStringLiteral("Could not read %1: %2")
                         .arg(QDir::toNativeSeparators(path), file.errorString());
        return {};
    }
    const qint64 size = file.size();
    if (size > kMaximumReadBytes)
        file.seek(size - kMaximumReadBytes);
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    return size > kMaximumReadBytes
        ? QStringLiteral("… the first %1 KiB of this log were trimmed.\n\n").arg((size - kMaximumReadBytes) / 1024)
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
