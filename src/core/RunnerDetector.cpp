#include "RunnerDetector.h"
#include "I18n.h"
#include "SteamPrecondition.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

namespace {
struct RunnerSource
{
    QString directory;
    QString label;
};

QString canonicalOrEmpty(const QString &path)
{
    return QFileInfo(path).canonicalFilePath();
}

void appendUnique(QList<RunnerEntry> &entries, QSet<QString> &seen, const RunnerEntry &entry)
{
    if (entry.path.isEmpty())
        return;
    const QString key = canonicalOrEmpty(entry.path);
    if (key.isEmpty() || seen.contains(key))
        return;
    seen.insert(key);
    entries.append(entry);
}

QString executableInDirectory(const QString &directory)
{
    if (directory.isEmpty())
        return {};
    const QFileInfo info(directory);
    if (info.isFile() && info.fileName() == QStringLiteral("proton"))
        return info.absoluteFilePath();
    if (!info.isDir())
        return {};
    const QString direct = QDir(directory).filePath(QStringLiteral("proton"));
    if (QFileInfo(direct).isFile())
        return QFileInfo(direct).absoluteFilePath();
    const QString filesProton = QDir(directory).filePath(QStringLiteral("files/bin/proton"));
    if (QFileInfo(filesProton).isFile())
        return QFileInfo(filesProton).absoluteFilePath();
    return {};
}

QString wineExecutableInDirectory(const QString &directory)
{
    if (directory.isEmpty())
        return {};
    const QFileInfo info(directory);
    if (info.isFile()) {
        if (info.fileName() == QStringLiteral("wine") || info.fileName() == QStringLiteral("wine64"))
            return info.absoluteFilePath();
        return {};
    }
    if (!info.isDir())
        return {};
    const QStringList candidates = {
        QStringLiteral("bin/wine"),
        QStringLiteral("bin/wine64"),
        QStringLiteral("wine"),
        QStringLiteral("wine64")
    };
    for (const QString &relative : candidates) {
        const QString path = QDir(directory).filePath(relative);
        if (QFileInfo(path).isFile())
            return QFileInfo(path).absoluteFilePath();
    }
    return {};
}

QString protonNameFor(const QString &protonExecutable)
{
    QString base = QFileInfo(protonExecutable).absolutePath();
    QString name = QFileInfo(base).fileName();
    while ((name == QStringLiteral("bin") || name == QStringLiteral("files")) && !base.isEmpty()) {
        base = QFileInfo(base).absolutePath();
        name = QFileInfo(base).fileName();
    }
    if (name.isEmpty())
        name = QStringLiteral("Proton");
    return name;
}

QString dataHome()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
}

QString configHome()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

QString homePath()
{
    return QDir::homePath();
}

QString appLabel(const QString &name, const QString &source)
{
    return source.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, source);
}

QList<RunnerSource> wineSources()
{
    const QString data = dataHome();
    const QString config = configHome();
    const QString home = homePath();
    QList<RunnerSource> sources = {
        {data + QStringLiteral("/vanta/runners/wine"), QStringLiteral("Vanta")},
        {data + QStringLiteral("/lutris/runners/wine"), QStringLiteral("Lutris")},
        {QStringLiteral("/usr/share/lutris/runners/wine"), QStringLiteral("Lutris")},
        {config + QStringLiteral("/heroic/tools/wine"), QStringLiteral("Heroic")},
        {data + QStringLiteral("/bottles/runners"), QStringLiteral("Bottles")},
        {home + QStringLiteral("/.var/app/net.lutris.Lutris/data/lutris/runners/wine"), QStringLiteral("Lutris Flatpak")},
        {home + QStringLiteral("/.var/app/com.heroicgameslauncher.hgl/config/heroic/tools/wine"), QStringLiteral("Heroic Flatpak")},
        {home + QStringLiteral("/.var/app/com.usebottles.bottles/data/bottles/runners"), QStringLiteral("Bottles Flatpak")}
    };
    QList<RunnerSource> unique;
    QSet<QString> seen;
    for (const RunnerSource &source : sources) {
        const QString key = QDir::cleanPath(source.directory);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        unique.append(source);
    }
    return unique;
}

QList<RunnerSource> protonSources()
{
    const QString data = dataHome();
    const QString home = homePath();
    QList<RunnerSource> sources;
    sources.append({data + QStringLiteral("/vanta/runners/proton"), QStringLiteral("Vanta")});

    for (const QString &steamRoot : SteamIntegration::steamLibraryRoots()) {
        sources.append({QDir(steamRoot).filePath(QStringLiteral("compatibilitytools.d")), QStringLiteral("Steam custom")});
        sources.append({QDir(steamRoot).filePath(QStringLiteral("steamapps/common")), QStringLiteral("Steam")});
    }
    for (const QString &steamRoot : {
             QStringLiteral("/usr/share/steam"),
             QStringLiteral("/usr/lib/steam")}) {
        sources.append({QDir(steamRoot).filePath(QStringLiteral("compatibilitytools.d")), QStringLiteral("Steam")});
        sources.append({QDir(steamRoot).filePath(QStringLiteral("steamapps/common")), QStringLiteral("Steam")});
    }

    sources.append({data + QStringLiteral("/lutris/runners/proton"), QStringLiteral("Lutris")});
    sources.append({home + QStringLiteral("/.config/heroic/tools/proton"), QStringLiteral("Heroic")});
    sources.append({home + QStringLiteral("/.var/app/com.heroicgameslauncher.hgl/config/heroic/tools/proton"), QStringLiteral("Heroic Flatpak")});
    sources.append({home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam/compatibilitytools.d"), QStringLiteral("Steam Flatpak")});
    sources.append({home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common"), QStringLiteral("Steam Flatpak")});

    QList<RunnerSource> unique;
    QSet<QString> seen;
    for (const RunnerSource &source : sources) {
        const QString key = QDir::cleanPath(source.directory);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        unique.append(source);
    }
    return unique;
}
}

QString RunnerDetector::wineExecutable(const AppSettings &settings)
{
    if (!settings.customWinePath.isEmpty()) {
        const QString custom = wineExecutableInDirectory(settings.customWinePath);
        if (!custom.isEmpty())
            return custom;
    }
    const QString found = QStandardPaths::findExecutable(QStringLiteral("wine"));
    if (!found.isEmpty())
        return found;
    const QString found64 = QStandardPaths::findExecutable(QStringLiteral("wine64"));
    if (!found64.isEmpty())
        return found64;
    if (QFileInfo::exists(QStringLiteral("/usr/bin/wine")))
        return QStringLiteral("/usr/bin/wine");
    return {};
}

QStringList RunnerDetector::protonSearchDirectories()
{
    QStringList directories;
    for (const RunnerSource &source : protonSources())
        directories.append(source.directory);
    return directories;
}

QString RunnerDetector::protonExecutable(const QString &configuredPath)
{
    return executableInDirectory(configuredPath);
}

QString RunnerDetector::protonBaseDirectory(const QString &configuredPath)
{
    const QFileInfo info(configuredPath);
    if (info.isDir())
        return info.absoluteFilePath();
    if (info.isFile() && info.fileName() == QStringLiteral("proton")) {
        QString base = info.absolutePath();
        if (QFileInfo(base).fileName() == QStringLiteral("bin"))
            base = QFileInfo(base).absolutePath();
        if (QFileInfo(base).fileName() == QStringLiteral("files"))
            base = QFileInfo(base).absolutePath();
        return base;
    }
    return configuredPath;
}

QList<RunnerEntry> RunnerDetector::wineEntries(const AppSettings &settings)
{
    QList<RunnerEntry> entries;
    QSet<QString> seen;

    if (!settings.customWinePath.isEmpty()) {
        const QString custom = wineExecutableInDirectory(settings.customWinePath);
        if (!custom.isEmpty()) {
            RunnerEntry entry;
            entry.type = RunnerType::Wine;
            entry.name = QStringLiteral("Custom Wine");
            entry.buildName = QStringLiteral("Custom Wine");
            entry.path = custom;
            entry.custom = true;
            appendUnique(entries, seen, entry);
        }
    }

    QString systemWine = QStandardPaths::findExecutable(QStringLiteral("wine"));
    if (systemWine.isEmpty())
        systemWine = QStandardPaths::findExecutable(QStringLiteral("wine64"));
    if (!systemWine.isEmpty()) {
        RunnerEntry entry;
        entry.type = RunnerType::Wine;
        entry.name = QStringLiteral("System Wine");
        entry.buildName = QStringLiteral("System Wine");
        entry.path = systemWine;
        appendUnique(entries, seen, entry);
    }

    for (const RunnerSource &source : wineSources()) {
        QDir directory(source.directory);
        if (!directory.exists())
            continue;
        const QFileInfoList children = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                               QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &child : children) {
            const QString wine = wineExecutableInDirectory(child.absoluteFilePath());
            if (wine.isEmpty())
                continue;
            RunnerEntry entry;
            entry.type = RunnerType::Wine;
            entry.name = appLabel(child.fileName(), source.label);
            entry.buildName = child.fileName();
            entry.path = wine;
            entry.custom = settings.customWinePath == wine || settings.customWinePath == child.absoluteFilePath();
            appendUnique(entries, seen, entry);
        }
    }
    return entries;
}

QList<RunnerEntry> RunnerDetector::protonEntries(const AppSettings &settings)
{
    QList<RunnerEntry> entries;
    QSet<QString> seen;

    for (const RunnerSource &source : protonSources()) {
        QDir directory(source.directory);
        if (!directory.exists())
            continue;
        const QFileInfoList children = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                               QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &child : children) {
            const QString proton = executableInDirectory(child.absoluteFilePath());
            if (proton.isEmpty())
                continue;
            RunnerEntry entry;
            entry.type = RunnerType::Proton;
            entry.path = proton;
            entry.name = appLabel(protonNameFor(proton), source.label);
            entry.buildName = protonNameFor(proton);
            entry.custom = settings.customProtonPaths.contains(child.absoluteFilePath())
                || settings.customProtonPaths.contains(proton);
            appendUnique(entries, seen, entry);
        }
    }

    for (const QString &customPath : settings.customProtonPaths) {
        const QString proton = executableInDirectory(customPath);
        if (proton.isEmpty())
            continue;
        RunnerEntry entry;
        entry.type = RunnerType::Proton;
        entry.path = proton;
        entry.name = appLabel(protonNameFor(proton), QStringLiteral("Custom"));
        entry.buildName = protonNameFor(proton);
        entry.custom = true;
        appendUnique(entries, seen, entry);
    }
    return entries;
}

QList<RunnerEntry> RunnerDetector::allEntries(const AppSettings &settings)
{
    QList<RunnerEntry> entries = wineEntries(settings);
    entries.append(protonEntries(settings));
    return entries;
}

bool RunnerDetector::validateWine(const QString &path, QString *reason)
{
    const QString executable = wineExecutableInDirectory(path);
    if (path.isEmpty() || executable.isEmpty()) {
        if (reason)
            *reason = Vanta::tr("No Wine executable was found at this path.");
        return false;
    }
    const QFileInfo info(executable);
    if (!info.isFile()) {
        if (reason)
            *reason = Vanta::tr("The Wine executable path does not point to a file.");
        return false;
    }
    if (!info.isExecutable()) {
        if (reason)
            *reason = Vanta::tr("The Wine executable is not executable.");
        return false;
    }
    return true;
}

bool RunnerDetector::validateProton(const QString &path, QString *reason)
{
    const QString executable = protonExecutable(path);
    if (executable.isEmpty()) {
        if (reason)
            *reason = Vanta::tr("No Proton launcher was found at this path.");
        return false;
    }
    if (!QFileInfo(executable).isExecutable()) {
        if (reason)
            *reason = Vanta::tr("The Proton launcher is not executable.");
        return false;
    }
    return true;
}

QString RunnerDetector::wineVersion(const QString &path)
{
    const QString executable = wineExecutableInDirectory(path);
    if (executable.isEmpty())
        return {};
    QProcess process;
    process.start(executable, {QStringLiteral("--version")});
    if (!process.waitForFinished(2000)) {
        process.kill();
        process.waitForFinished(200);
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed().remove(QLatin1Char('\n'));
}

QString RunnerDetector::protonVersion(const QString &path)
{
    const QString base = protonBaseDirectory(path);
    if (base.isEmpty())
        return {};
    QFile versionFile(QDir(base).filePath(QStringLiteral("version")));
    if (!versionFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(versionFile.readAll()).trimmed();
}
