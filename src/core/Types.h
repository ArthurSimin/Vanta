#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

enum class RunnerType { Native, Wine, Proton, Flatpak, Custom };
enum class Platform { Linux, Windows };
enum class OnlineFixMode { Auto, Force, Disabled };
enum class FixKind { Unknown, OnlineFix, FreeTP, Photon, SteamFixOnly };
enum class ScanState {
    NotScanned,
    Scanning,
    NoKnownComponents,
    ComponentsDetected,
    IncompleteInstallation,
    IncompatibleArchitecture,
    MissingRequiredFiles,
    ScanError
};
enum class ViewMode { Grid, List };
enum class SortMode { Name, LastPlayed, Playtime };
enum class ThemeChoice { System, Light, Dark, SystemColors };
enum class CoverSize { Small, Medium, Large };
enum class OnlineFixPrompt { Ask, AlwaysApply, NeverApply };
enum class CompatibilityFilter { All, Detected, NeedsAttention, NotDetected };
enum class InstallFilter { All, Installed, Missing };
enum class WineLogLevel { Off, Errors, Warnings, Trace };

QString runnerName(RunnerType type);
QString platformName(Platform platform);
QString onlineFixModeName(OnlineFixMode mode);
QString fixKindName(FixKind kind);
QString scanStateLabel(ScanState state);
QString viewModeName(ViewMode mode);
QString sortModeName(SortMode mode);
QString themeChoiceName(ThemeChoice choice);
QString coverSizeName(CoverSize size);
QString onlineFixPromptName(OnlineFixPrompt prompt);
QString compatibilityFilterName(CompatibilityFilter filter);
QString installFilterName(InstallFilter filter);
QString wineLogLevelName(WineLogLevel level);
QString wineDebugValue(WineLogLevel level);

RunnerType runnerFromName(const QString &name);
Platform platformFromName(const QString &name);
OnlineFixMode onlineFixModeFromName(const QString &name);
FixKind fixKindFromName(const QString &name);
ViewMode viewModeFromName(const QString &name);
SortMode sortModeFromName(const QString &name);
ThemeChoice themeChoiceFromName(const QString &name);
CoverSize coverSizeFromName(const QString &name);
OnlineFixPrompt onlineFixPromptFromName(const QString &name);
WineLogLevel wineLogLevelFromName(const QString &name);

bool runnerIsWineLike(RunnerType type);
// Native games load BepInEx through the doorstop launcher script, Wine and
// Proton through the winhttp override. Flatpak and Custom launches cannot load
// it, so the mods UI stays hidden for them.
bool runnerSupportsBepInEx(RunnerType type);
bool scanNeedsAttention(ScanState state);
bool scanIsDetected(ScanState state);
