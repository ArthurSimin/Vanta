#pragma once

#include "Types.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

struct Game
{
    QString id;
    QString name;
    QString source = QStringLiteral("Manual");
    QString gameDirectory;
    QString executable;
    QString launchArguments;
    QString preArguments;
    QString workingDirectory;
    QMap<QString, QString> environment;
    RunnerType runner = RunnerType::Native;
    Platform platform = Platform::Linux;
    QString runnerPath;
    QString winePrefix;
    bool wineD3D = false;
    bool nativeWayland = false;
    bool steamOverlay = true;
    bool useSteamRuntime = false;
    bool bepinexCompatibility = false;
    QString thunderstoreCommunity;
    QString wineDllOverrides;
    QString coverPath;
    OnlineFixMode onlineFixMode = OnlineFixMode::Auto;
    FixKind fixKind = FixKind::Unknown;
    QString realAppId;
    QString fakeAppId;
    QString fixPath;
    QString photonLauncherDir;
    ScanState compatibility = ScanState::NotScanned;
    QDateTime lastScanned;
    qint64 playtimeSeconds = 0;
    QDateTime lastPlayed;
    bool favorite = false;
    QDateTime addedAt;
    QDateTime modifiedAt;

    bool executableExists() const;

    // True when the library should present an OnlineFix marker for this game.
    // A disabled mode hides every marker, because a badge the launch path
    // ignores promises something the launcher will not do. Detection stays
    // reportable through the scan result and the details sheet, which say what
    // was found rather than what will be applied.
    bool showsOnlineFix() const
    {
        if (onlineFixMode == OnlineFixMode::Disabled)
            return false;
        return fixKind != FixKind::Unknown || onlineFixMode == OnlineFixMode::Force;
    }

    QString effectiveDirectory() const;
    QString effectiveWorkingDirectory() const;
    QJsonObject toJson() const;
    static Game fromJson(const QJsonObject &object);
    static Game create();
};

struct AppSettings
{
    ThemeChoice theme = ThemeChoice::System;
    // Stable language code from I18n::languages(), or "system"/"en". It is a
    // code, never a display name, so it survives a change of UI language.
    QString language = QStringLiteral("system");
    bool animations = true;
    CoverSize coverSize = CoverSize::Medium;
    SortMode sortMode = SortMode::Name;
    bool ascending = true;
    ViewMode viewMode = ViewMode::Grid;
    bool showBadges = true;
    bool rememberSidebarSelection = true;
    QString lastSidebarItem = QStringLiteral("library.games");
    bool automaticScanning = true;
    bool rescanOnDirectoryChange = true;
    int scanDepth = 8;
    int scanEntryLimit = 40000;
    bool rememberCollapsedGroups = true;
    QJsonObject collapsedGroups;
    bool tutorialCompleted = false;
    OnlineFixPrompt onlineFixPrompt = OnlineFixPrompt::Ask;
    bool requireSteamForOnlineFix = true;
    QString customWinePath;
    QStringList customProtonPaths;
    QString lastView = QStringLiteral("Grid");
    QByteArray windowGeometry;
    bool windowMaximized = false;
    QString steamGridDbApiKey;
    bool fetchCoversAutomatically = true;
    WineLogLevel wineLogLevel = WineLogLevel::Errors;

    // Persisted filter list. `hideNeedsAttention` removes the games the scan
    // flagged from every view, and `hiddenRunners` holds the display name of
    // each runner to hide (for example "Native" for a Windows-only library).
    // They are stored so a filter the user set up survives a restart.
    bool hideNeedsAttention = false;
    QStringList hiddenRunners;

    QJsonObject toJson() const;
    static AppSettings fromJson(const QJsonObject &object);
};

QString formatPlaytime(qint64 seconds);
QString formatLastPlayed(const QDateTime &dateTime);
QString initialsForGame(const QString &name);
