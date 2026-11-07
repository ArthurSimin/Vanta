#pragma once

#include "Game.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

struct OnlineFixFile
{
    QString fileName;
    QString absolutePath;
    qint64 size = 0;
    QDateTime modified;
    QString matchedRule;
};

struct OnlineFixInfo
{
    bool detected = false;
    FixKind kind = FixKind::Unknown;
    QList<OnlineFixFile> files;
    QStringList extraModules;
    QString realAppId;
    QString fakeAppId;
    QString fixPath;
    QString photonLauncherDir;
    bool hasIni = false;
    bool hasOnlineFixIni = false;
    bool hasSteamFixIni = false;

    QJsonObject toJson() const;
    static OnlineFixInfo fromJson(const QJsonObject &object);
};

class OnlineFixDetector
{
public:
    static OnlineFixInfo detect(const QString &executableDirectory, const QString &gameDirectory);
    static bool fileNameMatches(const QString &fileName, QString *rule = nullptr);
    static QStringList signaturePatterns();
};

class OnlineFixOverrides
{
public:
    static QString dvxkBaseChain();
    static QString compose(const Game &game, const OnlineFixInfo &info, bool includeOnlineFix, bool includeBepInEx = false);
};

struct PatchResult
{
    bool ok = true;
    QStringList writtenFiles;
    QStringList backedUpFiles;
    QStringList diagnostics;
    QStringList errors;
};

class OnlineFixPatcher
{
public:
    static PatchResult apply(const Game &game, const OnlineFixInfo &info);
    static QStringList plannedFiles(const OnlineFixInfo &info);
    static bool fileIsIni(const QString &fileName);
    static bool fileIsPhotonData(const QString &fileName);
};
