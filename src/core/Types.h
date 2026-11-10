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

// Translated names for the interface. The *Name functions above stay English on
// purpose: settings.json, the library file, launch-log file names and sidebar
// filter ids are built from them, so translating those would break saved data.
QString runnerDisplayName(RunnerType type);
QString platformDisplayName(Platform platform);
QString onlineFixModeDisplayName(OnlineFixMode mode);
QString fixKindDisplayName(FixKind kind);
QString viewModeDisplayName(ViewMode mode);
QString sortModeDisplayName(SortMode mode);
QString themeChoiceDisplayName(ThemeChoice choice);
QString coverSizeDisplayName(CoverSize size);
QString wineLogLevelDisplayName(WineLogLevel level);
// `source` is a stored token ("Manual", "Steam", …). Only the generic values are
// translated; store and brand names stay as they are.
QString sourceDisplayName(const QString &source);
QString compatibilityFilterDisplayName(CompatibilityFilter filter);
QString installFilterDisplayName(InstallFilter filter);

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
