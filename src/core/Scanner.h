#pragma once

#include "Game.h"
#include "OnlineFix.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

struct ScanOptions
{
    int maxDepth = 8;
    int entryLimit = 40000;
};

struct ExecutableCandidate
{
    QString path;
    qint64 size = 0;
    QString architecture;
    int depth = 0;
    int score = 0;
};

struct CompatibilityComponent
{
    QString name;
    QString fileName;
    QString absolutePath;
    QString category;
    QString architecture;
    qint64 size = 0;
    QDateTime modified;
    int confidence = 0;
    QString explanation;
};

struct ScanResult
{
    QString gameDirectory;
    QString executablePath;
    QString executableArchitecture;
    ScanState state = ScanState::NotScanned;
    QDateTime scannedAt;
    QList<ExecutableCandidate> executables;
    QList<CompatibilityComponent> components;
    OnlineFixInfo onlineFix;
    QStringList warnings;
    QStringList missingPaths;
    QStringList inaccessiblePaths;
    QString prefixStatus;
    bool bepinexDetected = false;
    bool truncated = false;
    QString suggestedExecutable;
    QString error;
    qint64 directoryModified = 0;

    bool cacheMatches(const QString &root) const;
    QJsonObject toJson() const;
    static ScanResult fromJson(const QJsonObject &object);
};

class GameScanner
{
public:
    static ScanResult scan(const QString &executablePath, const QString &gameDirectory,
                           const QString &configuredPrefix, RunnerType runner,
                           const ScanOptions &options = {});
    static QString likelyGameRoot(const QString &executableOrDirectory);
    static QString suggestedExecutable(const QString &gameDirectory, const QList<ExecutableCandidate> &candidates);
    static bool isSkippedExecutable(const QString &fileName);
    static QString executableArchitecture(const QString &path);
    // True when the file carries a valid PE header, regardless of its extension.
    static bool isWindowsExecutable(const QString &path);
    static bool directoryIsSafeToScan(const QString &path);
    static QString compatibilityCacheLabel(const Game &game);
    static void applyResultToGame(Game &game, const ScanResult &result);
};
