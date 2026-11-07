#pragma once

#include "Game.h"
#include "OnlineFix.h"

#include <QProcessEnvironment>
#include <QMap>
#include <QString>
#include <QStringList>

struct LaunchPlan
{
    RunnerType runner = RunnerType::Native;
    QString program;
    QStringList arguments;
    QString workingDirectory;
    QProcessEnvironment environment;
    QMap<QString, QString> environmentOverrides;
    QString prefixBase;
    QString winePrefix;
    QString wineServer;
    QString composedOverrides;
    QString realAppId;
    QString fakeAppId;
    bool onlineFixApplied = false;
    bool bepinexApplied = false;
    bool usedSteamRuntime = false;
    bool steamRuntimeRequested = false;
    bool steamRuntimeUnavailable = false;
    QStringList diagnostics;
    QStringList writtenFiles;
    QStringList backups;

    QString summary() const;
    QString environmentSummary() const;
    bool isValid() const { return !program.isEmpty(); }
};

class LaunchPlanBuilder
{
public:
    static LaunchPlan build(const Game &game, const AppSettings &settings,
                            const OnlineFixInfo &onlineFixInfo, bool onlineFixActive,
                            bool bepinexDetected, QString *error = nullptr);
    static QString effectivePrefix(const Game &game);
    static QString effectiveWinePrefix(const Game &game);
    static QString wineServerFor(const Game &game, const QString &protonProgram);
    static QString overridePreview(const Game &game, const OnlineFixInfo &onlineFixInfo,
                                   bool includeOnlineFix, bool includeBepInEx = false);
    static QStringList splitArguments(const QString &arguments);
    static bool isControlledEnvironmentKey(const QString &key);
};
