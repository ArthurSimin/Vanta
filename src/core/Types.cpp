#include "Types.h"

#include "I18n.h"

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
    case ScanState::NotScanned: return Vanta::tr("Not scanned");
    case ScanState::Scanning: return Vanta::tr("Scanning…");
    case ScanState::NoKnownComponents: return Vanta::tr("No components");
    case ScanState::ComponentsDetected: return Vanta::tr("Components detected");
    case ScanState::IncompleteInstallation: return Vanta::tr("Incomplete install");
    case ScanState::IncompatibleArchitecture: return Vanta::tr("Wrong architecture");
    case ScanState::MissingRequiredFiles: return Vanta::tr("Missing files");
    case ScanState::ScanError: return Vanta::tr("Scan error");
    }
    return Vanta::tr("Not scanned");
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

QString compatibilityFilterDisplayName(CompatibilityFilter filter)
{
    switch (filter) {
    case CompatibilityFilter::All: return Vanta::tr("All");
    case CompatibilityFilter::Detected: return Vanta::tr("Detected");
    case CompatibilityFilter::NeedsAttention: return Vanta::tr("Needs attention");
    case CompatibilityFilter::NotDetected: return Vanta::tr("Not detected");
    }
    return Vanta::tr("All");
}

QString installFilterDisplayName(InstallFilter filter)
{
    switch (filter) {
    case InstallFilter::All: return Vanta::tr("All");
    case InstallFilter::Installed: return Vanta::tr("Installed");
    case InstallFilter::Missing: return Vanta::tr("Missing");
    }
    return Vanta::tr("All");
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

QString runnerDisplayName(RunnerType type)
{
    switch (type) {
    case RunnerType::Native: return Vanta::tr("Native");
    case RunnerType::Wine: return QStringLiteral("Wine");
    case RunnerType::Proton: return QStringLiteral("Proton");
    case RunnerType::Flatpak: return QStringLiteral("Flatpak");
    case RunnerType::Custom: return Vanta::tr("Custom");
    }
    return Vanta::tr("Native");
}

QString platformDisplayName(Platform platform)
{
    // Linux and Windows are product names, not common nouns, so they are the
    // same in every catalog.
    return platform == Platform::Windows ? QStringLiteral("Windows") : QStringLiteral("Linux");
}

QString onlineFixModeDisplayName(OnlineFixMode mode)
{
    switch (mode) {
    case OnlineFixMode::Auto: return Vanta::tr("Auto");
    case OnlineFixMode::Force: return Vanta::tr("Force");
    case OnlineFixMode::Disabled: return Vanta::tr("Disabled");
    }
    return Vanta::tr("Auto");
}

QString fixKindDisplayName(FixKind kind)
{
    switch (kind) {
    case FixKind::Unknown: return Vanta::tr("Unknown");
    case FixKind::OnlineFix: return QStringLiteral("OnlineFix");
    case FixKind::FreeTP: return QStringLiteral("FreeTP");
    case FixKind::Photon: return Vanta::tr("Photon launcher");
    case FixKind::SteamFixOnly: return QStringLiteral("SteamFix");
    }
    return Vanta::tr("Unknown");
}

QString viewModeDisplayName(ViewMode mode)
{
    return mode == ViewMode::Grid ? Vanta::tr("Grid") : Vanta::tr("List");
}

QString sortModeDisplayName(SortMode mode)
{
    switch (mode) {
    case SortMode::Name: return Vanta::tr("Name");
    case SortMode::LastPlayed: return Vanta::tr("Last played");
    case SortMode::Playtime: return Vanta::tr("Playtime");
    }
    return Vanta::tr("Name");
}

QString themeChoiceDisplayName(ThemeChoice choice)
{
    switch (choice) {
    case ThemeChoice::System: return Vanta::tr("System (light or dark)");
    case ThemeChoice::Light: return Vanta::tr("Light");
    case ThemeChoice::Dark: return Vanta::tr("Dark");
    case ThemeChoice::SystemColors: return Vanta::tr("Follow system colors");
    }
    return Vanta::tr("System (light or dark)");
}

QString coverSizeDisplayName(CoverSize size)
{
    switch (size) {
    case CoverSize::Small: return Vanta::tr("Small");
    case CoverSize::Medium: return Vanta::tr("Medium");
    case CoverSize::Large: return Vanta::tr("Large");
    }
    return Vanta::tr("Medium");
}

QString wineLogLevelDisplayName(WineLogLevel level)
{
    switch (level) {
    case WineLogLevel::Off: return Vanta::tr("Off");
    case WineLogLevel::Errors: return Vanta::tr("Errors only");
    case WineLogLevel::Warnings: return Vanta::tr("Warnings and errors");
    case WineLogLevel::Trace: return Vanta::tr("Everything (verbose)");
    }
    return Vanta::tr("Errors only");
}

QString sourceDisplayName(const QString &source)
{
    if (source == QStringLiteral("Manual"))
        return Vanta::tr("Manual");
    if (source == QStringLiteral("Other"))
        return Vanta::tr("Other");
    // Store and launcher brands are product names, so they are not translated.
    // An unknown source keeps whatever name it was stored with.
    return source;
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
