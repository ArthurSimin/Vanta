#include "Types.h"

QString runnerName(RunnerType type)
{
    switch (type) {
    case RunnerType::Native: return QStringLiteral("Native");
    case RunnerType::Wine: return QStringLiteral("Wine");
    case RunnerType::Proton: return QStringLiteral("Proton");
    case RunnerType::Flatpak: return QStringLiteral("Flatpak");
    case RunnerType::Custom: return QStringLiteral("Custom");
    }
    return QStringLiteral("Native");
}

QString platformName(Platform platform)
{
    return platform == Platform::Windows ? QStringLiteral("Windows") : QStringLiteral("Linux");
}

QString onlineFixModeName(OnlineFixMode mode)
{
    switch (mode) {
    case OnlineFixMode::Auto: return QStringLiteral("Auto");
    case OnlineFixMode::Force: return QStringLiteral("Force");
    case OnlineFixMode::Disabled: return QStringLiteral("Disabled");
    }
    return QStringLiteral("Auto");
}

QString fixKindName(FixKind kind)
{
    switch (kind) {
    case FixKind::Unknown: return QStringLiteral("Unknown");
    case FixKind::OnlineFix: return QStringLiteral("OnlineFix");
    case FixKind::FreeTP: return QStringLiteral("FreeTP");
    case FixKind::Photon: return QStringLiteral("Photon launcher");
    case FixKind::SteamFixOnly: return QStringLiteral("SteamFix");
    }
    return QStringLiteral("Unknown");
}

QString scanStateLabel(ScanState state)
{
    switch (state) {
    case ScanState::NotScanned: return QStringLiteral("Not scanned");
    case ScanState::Scanning: return QStringLiteral("Scanning…");
    case ScanState::NoKnownComponents: return QStringLiteral("No components");
    case ScanState::ComponentsDetected: return QStringLiteral("Components detected");
    case ScanState::IncompleteInstallation: return QStringLiteral("Incomplete install");
    case ScanState::IncompatibleArchitecture: return QStringLiteral("Wrong architecture");
    case ScanState::MissingRequiredFiles: return QStringLiteral("Missing files");
    case ScanState::ScanError: return QStringLiteral("Scan error");
    }
    return QStringLiteral("Not scanned");
}

QString viewModeName(ViewMode mode)
{
    return mode == ViewMode::Grid ? QStringLiteral("Grid") : QStringLiteral("List");
}

QString sortModeName(SortMode mode)
{
    switch (mode) {
    case SortMode::Name: return QStringLiteral("Name");
    case SortMode::LastPlayed: return QStringLiteral("Last played");
    case SortMode::Playtime: return QStringLiteral("Playtime");
    }
    return QStringLiteral("Name");
}

QString themeChoiceName(ThemeChoice choice)
{
    switch (choice) {
    case ThemeChoice::System: return QStringLiteral("System (light or dark)");
    case ThemeChoice::Light: return QStringLiteral("Light");
    case ThemeChoice::Dark: return QStringLiteral("Dark");
    case ThemeChoice::SystemColors: return QStringLiteral("Follow system colors");
    }
    return QStringLiteral("System (light or dark)");
}

QString coverSizeName(CoverSize size)
{
    switch (size) {
    case CoverSize::Small: return QStringLiteral("Small");
    case CoverSize::Medium: return QStringLiteral("Medium");
    case CoverSize::Large: return QStringLiteral("Large");
    }
    return QStringLiteral("Medium");
}

QString onlineFixPromptName(OnlineFixPrompt prompt)
{
    switch (prompt) {
    case OnlineFixPrompt::Ask: return QStringLiteral("Ask before each launch");
    case OnlineFixPrompt::AlwaysApply: return QStringLiteral("Always apply without asking");
    case OnlineFixPrompt::NeverApply: return QStringLiteral("Never apply");
    }
    return QStringLiteral("Ask before each launch");
}

QString compatibilityFilterName(CompatibilityFilter filter)
{
    switch (filter) {
    case CompatibilityFilter::All: return QStringLiteral("All");
    case CompatibilityFilter::Detected: return QStringLiteral("Detected");
    case CompatibilityFilter::NeedsAttention: return QStringLiteral("Needs attention");
    case CompatibilityFilter::NotDetected: return QStringLiteral("Not detected");
    }
    return QStringLiteral("All");
}

QString installFilterName(InstallFilter filter)
{
    switch (filter) {
    case InstallFilter::All: return QStringLiteral("All");
    case InstallFilter::Installed: return QStringLiteral("Installed");
    case InstallFilter::Missing: return QStringLiteral("Missing");
    }
    return QStringLiteral("All");
}

QString wineLogLevelName(WineLogLevel level)
{
    switch (level) {
    case WineLogLevel::Off: return QStringLiteral("Off");
    case WineLogLevel::Errors: return QStringLiteral("Errors only");
    case WineLogLevel::Warnings: return QStringLiteral("Warnings and errors");
    case WineLogLevel::Trace: return QStringLiteral("Everything (verbose)");
    }
    return QStringLiteral("Errors only");
}

QString wineDebugValue(WineLogLevel level)
{
    switch (level) {
    case WineLogLevel::Off: return QStringLiteral("-all");
    // Starting from -all and re-enabling err/warn keeps the real failures
    // visible without the fixme chatter that floods a terminal for every game.
    case WineLogLevel::Errors: return QStringLiteral("-all,err+all");
    case WineLogLevel::Warnings: return QStringLiteral("-all,err+all,warn+all");
    case WineLogLevel::Trace: return QStringLiteral("+all");
    }
    return QStringLiteral("-all,err+all");
}

RunnerType runnerFromName(const QString &name)
{
    if (name.compare(QStringLiteral("Wine"), Qt::CaseInsensitive) == 0) return RunnerType::Wine;
    if (name.compare(QStringLiteral("Proton"), Qt::CaseInsensitive) == 0) return RunnerType::Proton;
    if (name.compare(QStringLiteral("Flatpak"), Qt::CaseInsensitive) == 0) return RunnerType::Flatpak;
    if (name.compare(QStringLiteral("Custom"), Qt::CaseInsensitive) == 0) return RunnerType::Custom;
    return RunnerType::Native;
}

Platform platformFromName(const QString &name)
{
    return name.compare(QStringLiteral("Windows"), Qt::CaseInsensitive) == 0 ? Platform::Windows : Platform::Linux;
}

OnlineFixMode onlineFixModeFromName(const QString &name)
{
    if (name.compare(QStringLiteral("Force"), Qt::CaseInsensitive) == 0) return OnlineFixMode::Force;
    if (name.compare(QStringLiteral("Disabled"), Qt::CaseInsensitive) == 0) return OnlineFixMode::Disabled;
    return OnlineFixMode::Auto;
}

FixKind fixKindFromName(const QString &name)
{
    if (name == QStringLiteral("OnlineFix")) return FixKind::OnlineFix;
    if (name == QStringLiteral("FreeTP")) return FixKind::FreeTP;
    if (name == QStringLiteral("Photon launcher") || name == QStringLiteral("Photon")) return FixKind::Photon;
    if (name == QStringLiteral("SteamFix")) return FixKind::SteamFixOnly;
    return FixKind::Unknown;
}

ViewMode viewModeFromName(const QString &name)
{
    return name.compare(QStringLiteral("List"), Qt::CaseInsensitive) == 0 ? ViewMode::List : ViewMode::Grid;
}

SortMode sortModeFromName(const QString &name)
{
    if (name.compare(QStringLiteral("Last played"), Qt::CaseInsensitive) == 0) return SortMode::LastPlayed;
    if (name.compare(QStringLiteral("Playtime"), Qt::CaseInsensitive) == 0) return SortMode::Playtime;
    return SortMode::Name;
}

ThemeChoice themeChoiceFromName(const QString &name)
{
    if (name.compare(QStringLiteral("Light"), Qt::CaseInsensitive) == 0) return ThemeChoice::Light;
    if (name.compare(QStringLiteral("Dark"), Qt::CaseInsensitive) == 0) return ThemeChoice::Dark;
    if (name.compare(QStringLiteral("Follow system colors"), Qt::CaseInsensitive) == 0) return ThemeChoice::SystemColors;
    if (name.compare(QStringLiteral("System colors"), Qt::CaseInsensitive) == 0) return ThemeChoice::SystemColors;
    return ThemeChoice::System;
}

CoverSize coverSizeFromName(const QString &name)
{
    if (name.compare(QStringLiteral("Small"), Qt::CaseInsensitive) == 0) return CoverSize::Small;
    if (name.compare(QStringLiteral("Large"), Qt::CaseInsensitive) == 0) return CoverSize::Large;
    return CoverSize::Medium;
}

OnlineFixPrompt onlineFixPromptFromName(const QString &name)
{
    if (name.compare(QStringLiteral("Always apply without asking"), Qt::CaseInsensitive) == 0) return OnlineFixPrompt::AlwaysApply;
    if (name.compare(QStringLiteral("Never apply"), Qt::CaseInsensitive) == 0) return OnlineFixPrompt::NeverApply;
    return OnlineFixPrompt::Ask;
}

WineLogLevel wineLogLevelFromName(const QString &name)
{
    if (name.compare(QStringLiteral("Off"), Qt::CaseInsensitive) == 0) return WineLogLevel::Off;
    if (name.compare(QStringLiteral("Warnings and errors"), Qt::CaseInsensitive) == 0) return WineLogLevel::Warnings;
    if (name.compare(QStringLiteral("Everything (verbose)"), Qt::CaseInsensitive) == 0) return WineLogLevel::Trace;
    return WineLogLevel::Errors;
}

bool runnerIsWineLike(RunnerType type)
{
    return type == RunnerType::Wine || type == RunnerType::Proton;
}

bool runnerSupportsBepInEx(RunnerType type)
{
    return type == RunnerType::Native || runnerIsWineLike(type);
}

bool scanNeedsAttention(ScanState state)
{
    return state == ScanState::IncompleteInstallation
        || state == ScanState::IncompatibleArchitecture
        || state == ScanState::MissingRequiredFiles
        || state == ScanState::ScanError;
}

bool scanIsDetected(ScanState state)
{
    // A failed scan is not a detection. Counting it as one would list an
    // unreadable folder under "Detected" and in the detected total.
    return state == ScanState::ComponentsDetected
        || state == ScanState::IncompleteInstallation
        || state == ScanState::IncompatibleArchitecture
        || state == ScanState::MissingRequiredFiles;
}
