#include "Game.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUuid>

static QString dateToString(const QDateTime &dateTime)
{
    return dateTime.isValid() ? dateTime.toUTC().toString(Qt::ISODateWithMs) : QString();
}

static QDateTime dateFromString(const QString &value)
{
    if (value.isEmpty())
        return {};
    QDateTime result = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!result.isValid())
        result = QDateTime::fromString(value, Qt::ISODate);
    return result;
}

Game Game::create()
{
    Game game;
    game.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    game.addedAt = QDateTime::currentDateTime();
    game.modifiedAt = game.addedAt;
    return game;
}

bool Game::executableExists() const
{
    return !executable.isEmpty() && QFileInfo::exists(executable) && QFileInfo(executable).isFile();
}

QString Game::effectiveDirectory() const
{
    if (!gameDirectory.isEmpty())
        return QDir::cleanPath(gameDirectory);
    if (!executable.isEmpty())
        return QFileInfo(executable).absolutePath();
    return QString();
}

QString Game::effectiveWorkingDirectory() const
{
    if (!workingDirectory.isEmpty())
        return workingDirectory;
    if (!executable.isEmpty())
        return QFileInfo(executable).absolutePath();
    return effectiveDirectory();
}

QJsonObject Game::toJson() const
{
    QJsonObject environmentObject;
    for (auto it = environment.cbegin(); it != environment.cend(); ++it)
        environmentObject.insert(it.key(), it.value());

    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("source"), source);
    object.insert(QStringLiteral("gameDirectory"), gameDirectory);
    object.insert(QStringLiteral("executable"), executable);
    object.insert(QStringLiteral("launchArguments"), launchArguments);
    object.insert(QStringLiteral("preArguments"), preArguments);
    object.insert(QStringLiteral("workingDirectory"), workingDirectory);
    object.insert(QStringLiteral("environment"), environmentObject);
    object.insert(QStringLiteral("runner"), runnerName(runner));
    object.insert(QStringLiteral("platform"), platformName(platform));
    object.insert(QStringLiteral("runnerPath"), runnerPath);
    object.insert(QStringLiteral("winePrefix"), winePrefix);
    object.insert(QStringLiteral("wineD3D"), wineD3D);
    object.insert(QStringLiteral("nativeWayland"), nativeWayland);
    object.insert(QStringLiteral("steamOverlay"), steamOverlay);
    object.insert(QStringLiteral("useSteamRuntime"), useSteamRuntime);
    object.insert(QStringLiteral("bepinexCompatibility"), bepinexCompatibility);
    object.insert(QStringLiteral("thunderstoreCommunity"), thunderstoreCommunity);
    object.insert(QStringLiteral("wineDllOverrides"), wineDllOverrides);
    object.insert(QStringLiteral("coverPath"), coverPath);
    object.insert(QStringLiteral("onlineFixMode"), onlineFixModeName(onlineFixMode));
    object.insert(QStringLiteral("fixKind"), fixKindName(fixKind));
    object.insert(QStringLiteral("realAppId"), realAppId);
    object.insert(QStringLiteral("fakeAppId"), fakeAppId);
    object.insert(QStringLiteral("fixPath"), fixPath);
    object.insert(QStringLiteral("photonLauncherDir"), photonLauncherDir);
    object.insert(QStringLiteral("compatibility"), static_cast<int>(compatibility));
    object.insert(QStringLiteral("lastScanned"), dateToString(lastScanned));
    object.insert(QStringLiteral("playtimeSeconds"), playtimeSeconds);
    object.insert(QStringLiteral("lastPlayed"), dateToString(lastPlayed));
    object.insert(QStringLiteral("favorite"), favorite);
    object.insert(QStringLiteral("addedAt"), dateToString(addedAt));
    object.insert(QStringLiteral("modifiedAt"), dateToString(modifiedAt));
    return object;
}

Game Game::fromJson(const QJsonObject &object)
{
    Game game;
    // Keep a missing id empty: loadLibrary assigns one and saves it back, so
    // the id that keys logs and the scan cache stays stable across restarts.
    game.id = object.value(QStringLiteral("id")).toString();
    game.name = object.value(QStringLiteral("name")).toString();
    game.source = object.value(QStringLiteral("source")).toString(QStringLiteral("Manual"));
    game.gameDirectory = object.value(QStringLiteral("gameDirectory")).toString();
    game.executable = object.value(QStringLiteral("executable")).toString();
    game.launchArguments = object.value(QStringLiteral("launchArguments")).toString();
    game.preArguments = object.value(QStringLiteral("preArguments")).toString();
    game.workingDirectory = object.value(QStringLiteral("workingDirectory")).toString();
    const QJsonObject environmentObject = object.value(QStringLiteral("environment")).toObject();
    for (auto it = environmentObject.constBegin(); it != environmentObject.constEnd(); ++it)
        game.environment.insert(it.key(), it.value().toString());
    game.runner = runnerFromName(object.value(QStringLiteral("runner")).toString());
    game.platform = platformFromName(object.value(QStringLiteral("platform")).toString(
        game.runner == RunnerType::Wine || game.runner == RunnerType::Proton ? QStringLiteral("Windows") : QStringLiteral("Linux")));
    game.runnerPath = object.value(QStringLiteral("runnerPath")).toString();
    game.winePrefix = object.value(QStringLiteral("winePrefix")).toString();
    game.wineD3D = object.value(QStringLiteral("wineD3D")).toBool(false);
    game.nativeWayland = object.value(QStringLiteral("nativeWayland")).toBool(false);
    game.steamOverlay = object.value(QStringLiteral("steamOverlay")).toBool(true);
    game.useSteamRuntime = object.value(QStringLiteral("useSteamRuntime")).toBool(false);
    game.bepinexCompatibility = object.value(QStringLiteral("bepinexCompatibility")).toBool(false);
    game.thunderstoreCommunity = object.value(QStringLiteral("thunderstoreCommunity")).toString();
    game.wineDllOverrides = object.value(QStringLiteral("wineDllOverrides")).toString();
    game.coverPath = object.value(QStringLiteral("coverPath")).toString();
    game.onlineFixMode = onlineFixModeFromName(object.value(QStringLiteral("onlineFixMode")).toString());
    game.fixKind = fixKindFromName(object.value(QStringLiteral("fixKind")).toString());
    game.realAppId = object.value(QStringLiteral("realAppId")).toString();
    game.fakeAppId = object.value(QStringLiteral("fakeAppId")).toString();
    game.fixPath = object.value(QStringLiteral("fixPath")).toString();
    game.photonLauncherDir = object.value(QStringLiteral("photonLauncherDir")).toString();
    const int state = object.value(QStringLiteral("compatibility")).toInt(static_cast<int>(ScanState::NotScanned));
    game.compatibility = state >= static_cast<int>(ScanState::NotScanned) && state <= static_cast<int>(ScanState::ScanError)
        ? static_cast<ScanState>(state) : ScanState::NotScanned;
    game.lastScanned = dateFromString(object.value(QStringLiteral("lastScanned")).toString());
    game.playtimeSeconds = static_cast<qint64>(object.value(QStringLiteral("playtimeSeconds")).toDouble());
    game.lastPlayed = dateFromString(object.value(QStringLiteral("lastPlayed")).toString());
    game.favorite = object.value(QStringLiteral("favorite")).toBool(false);
    game.addedAt = dateFromString(object.value(QStringLiteral("addedAt")).toString());
    game.modifiedAt = dateFromString(object.value(QStringLiteral("modifiedAt")).toString());
    return game;
}

QJsonObject AppSettings::toJson() const
{
    QJsonArray protons;
    for (const QString &path : customProtonPaths)
        protons.append(path);

    QJsonObject object;
    object.insert(QStringLiteral("theme"), themeChoiceName(theme));
    object.insert(QStringLiteral("animations"), animations);
    object.insert(QStringLiteral("coverSize"), coverSizeName(coverSize));
    object.insert(QStringLiteral("sortMode"), sortModeName(sortMode));
    object.insert(QStringLiteral("ascending"), ascending);
    object.insert(QStringLiteral("viewMode"), viewModeName(viewMode));
    object.insert(QStringLiteral("showBadges"), showBadges);
    object.insert(QStringLiteral("rememberSidebarSelection"), rememberSidebarSelection);
    object.insert(QStringLiteral("lastSidebarItem"), lastSidebarItem);
    object.insert(QStringLiteral("automaticScanning"), automaticScanning);
    object.insert(QStringLiteral("rescanOnDirectoryChange"), rescanOnDirectoryChange);
    object.insert(QStringLiteral("scanDepth"), scanDepth);
    object.insert(QStringLiteral("scanEntryLimit"), scanEntryLimit);
    object.insert(QStringLiteral("rememberCollapsedGroups"), rememberCollapsedGroups);
    object.insert(QStringLiteral("collapsedGroups"), collapsedGroups);
    object.insert(QStringLiteral("tutorialCompleted"), tutorialCompleted);
    object.insert(QStringLiteral("onlineFixPrompt"), onlineFixPromptName(onlineFixPrompt));
    object.insert(QStringLiteral("requireSteamForOnlineFix"), requireSteamForOnlineFix);
    object.insert(QStringLiteral("customWinePath"), customWinePath);
    object.insert(QStringLiteral("customProtonPaths"), protons);
    object.insert(QStringLiteral("lastView"), viewModeName(viewMode));
    object.insert(QStringLiteral("windowGeometry"), QString::fromLatin1(windowGeometry.toBase64()));
    object.insert(QStringLiteral("windowMaximized"), windowMaximized);
    object.insert(QStringLiteral("steamGridDbApiKey"), steamGridDbApiKey);
    object.insert(QStringLiteral("fetchCoversAutomatically"), fetchCoversAutomatically);
    object.insert(QStringLiteral("wineLogLevel"), wineLogLevelName(wineLogLevel));
    return object;
}

AppSettings AppSettings::fromJson(const QJsonObject &object)
{
    AppSettings settings;
    settings.theme = themeChoiceFromName(object.value(QStringLiteral("theme")).toString());
    settings.animations = object.value(QStringLiteral("animations")).toBool(true);
    settings.coverSize = coverSizeFromName(object.value(QStringLiteral("coverSize")).toString());
    settings.sortMode = sortModeFromName(object.value(QStringLiteral("sortMode")).toString());
    settings.ascending = object.value(QStringLiteral("ascending")).toBool(true);
    settings.viewMode = viewModeFromName(object.value(QStringLiteral("viewMode")).toString());
    settings.showBadges = object.value(QStringLiteral("showBadges")).toBool(true);
    settings.rememberSidebarSelection = object.value(QStringLiteral("rememberSidebarSelection")).toBool(true);
    settings.lastSidebarItem = object.value(QStringLiteral("lastSidebarItem")).toString(QStringLiteral("library.games"));
    settings.automaticScanning = object.value(QStringLiteral("automaticScanning")).toBool(true);
    settings.rescanOnDirectoryChange = object.value(QStringLiteral("rescanOnDirectoryChange")).toBool(true);
    settings.scanDepth = qBound(2, object.value(QStringLiteral("scanDepth")).toInt(8), 24);
    settings.scanEntryLimit = qBound(1000, object.value(QStringLiteral("scanEntryLimit")).toInt(40000), 250000);
    settings.rememberCollapsedGroups = object.value(QStringLiteral("rememberCollapsedGroups")).toBool(true);
    settings.collapsedGroups = object.value(QStringLiteral("collapsedGroups")).toObject();
    settings.tutorialCompleted = object.value(QStringLiteral("tutorialCompleted")).toBool(false);
    settings.onlineFixPrompt = onlineFixPromptFromName(object.value(QStringLiteral("onlineFixPrompt")).toString());
    settings.requireSteamForOnlineFix = object.value(QStringLiteral("requireSteamForOnlineFix")).toBool(true);
    settings.customWinePath = object.value(QStringLiteral("customWinePath")).toString();
    const QJsonArray protons = object.value(QStringLiteral("customProtonPaths")).toArray();
    for (const QJsonValue &value : protons)
        settings.customProtonPaths.append(value.toString());
    settings.lastView = viewModeName(settings.viewMode);
    settings.windowGeometry = QByteArray::fromBase64(
        object.value(QStringLiteral("windowGeometry")).toString().toLatin1());
    settings.windowMaximized = object.value(QStringLiteral("windowMaximized")).toBool(false);
    settings.steamGridDbApiKey = object.value(QStringLiteral("steamGridDbApiKey")).toString();
    settings.fetchCoversAutomatically = object.value(QStringLiteral("fetchCoversAutomatically")).toBool(true);
    settings.wineLogLevel = wineLogLevelFromName(object.value(QStringLiteral("wineLogLevel")).toString());
    return settings;
}

QString formatPlaytime(qint64 seconds)
{
    if (seconds <= 0)
        return QStringLiteral("Never played");
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    if (hours > 0)
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
    if (minutes > 0)
        return QStringLiteral("%1m").arg(minutes);
    return QStringLiteral("<1m");
}

QString formatLastPlayed(const QDateTime &dateTime)
{
    if (!dateTime.isValid())
        return QStringLiteral("Never");
    return dateTime.toLocalTime().toString(QStringLiteral("d MMM yyyy, HH:mm"));
}

QString initialsForGame(const QString &name)
{
    const QStringList words = name.split(QRegularExpression(QStringLiteral("[\\s_-]+")), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return QStringLiteral("?");
    if (words.size() == 1)
        return words.first().left(2).toUpper();
    return (words.first().left(1) + words.at(1).left(1)).toUpper();
}
