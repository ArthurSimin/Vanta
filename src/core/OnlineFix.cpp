#include "OnlineFix.h"
#include "IniFile.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace {
const QString kOnlineFixSection = QStringLiteral("OnlineFix Linux");
const QString kMainSection = QStringLiteral("Main");
const QString kMiscSection = QStringLiteral("Misc");

QString canonicalKey(const QString &path)
{
    return QFileInfo(path).absoluteFilePath();
}

QStringList topLevelFiles(const QString &directory)
{
    if (directory.isEmpty())
        return {};
    QDir dir(directory);
    if (!dir.exists())
        return {};
    QStringList result;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &entry : entries)
        result.append(entry.absoluteFilePath());
    return result;
}

bool hasLauncherBeside(const QString &directory)
{
    const QFileInfoList entries = QDir(directory).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (entry.fileName().compare(QStringLiteral("Launcher.exe"), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString backupPathFor(const QString &path)
{
    return path + QStringLiteral(".vanta.bak");
}

bool ensureBackup(const QString &path, PatchResult &result)
{
    const QString backup = backupPathFor(path);
    if (QFileInfo::exists(backup)) {
        result.diagnostics.append(QStringLiteral("Existing backup kept unchanged: %1").arg(backup));
        return true;
    }
    if (!QFile::copy(path, backup)) {
        result.errors.append(QStringLiteral("Could not back up %1 before modification.").arg(path));
        return false;
    }
    result.backedUpFiles.append(backup);
    return true;
}

QString findNewtonsoft(const QString &root, const QString &skipDirectory)
{
    if (root.isEmpty() || !QDir(root).exists())
        return {};
    QDirIterator iterator(root, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir skip(skipDirectory);
    int inspected = 0;
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (++inspected > 250000)
            break;
        if (iterator.fileName().compare(QStringLiteral("Newtonsoft.Json.dll"), Qt::CaseInsensitive) != 0)
            continue;
        if (QFileInfo(path).isSymLink())
            continue;
        if (QFileInfo(path).absolutePath() == skip.absolutePath())
            continue;
        return path;
    }
    return {};
}

bool patchPhoton(const Game &game, const OnlineFixInfo &info, PatchResult &result)
{
    bool success = true;
    QSet<QString> handledDirectories;
    for (const OnlineFixFile &file : info.files) {
        if (!OnlineFixPatcher::fileIsPhotonData(file.fileName))
            continue;
        const QFileInfo source(file.absolutePath);
        const QString launcherDirectory = source.absolutePath();
        if (!hasLauncherBeside(launcherDirectory))
            continue;
        if (handledDirectories.contains(launcherDirectory))
            continue;
        handledDirectories.insert(launcherDirectory);

        const QFileInfoList entries = QDir(launcherDirectory).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        bool hasNewtonsoft = false;
        for (const QFileInfo &entry : entries) {
            if (entry.fileName().compare(QStringLiteral("Newtonsoft.Json.dll"), Qt::CaseInsensitive) == 0) {
                hasNewtonsoft = true;
                break;
            }
        }
        if (hasNewtonsoft)
            continue;

        const QString dependencySource = findNewtonsoft(game.effectiveDirectory(), launcherDirectory);
        if (dependencySource.isEmpty()) {
            result.diagnostics.append(QStringLiteral("Photon launcher dependency Newtonsoft.Json.dll was not found in the game tree. The launch will continue without it."));
            continue;
        }
        const QString target = QDir(launcherDirectory).filePath(QStringLiteral("Newtonsoft.Json.dll"));
        if (QFileInfo::exists(target))
            continue;
        if (!QFile::link(dependencySource, target)) {
            result.diagnostics.append(QStringLiteral("Could not create Newtonsoft.Json.dll link beside the Photon launcher. The launch will continue without it."));
            success = false;
            continue;
        }
        result.writtenFiles.append(target);
        result.diagnostics.append(QStringLiteral("Linked %1 beside the Photon launcher.").arg(target));
    }
    return success;
}
}

QJsonObject OnlineFixInfo::toJson() const
{
    QJsonArray fileArray;
    for (const OnlineFixFile &file : files) {
        QJsonObject entry;
        entry.insert(QStringLiteral("fileName"), file.fileName);
        entry.insert(QStringLiteral("absolutePath"), file.absolutePath);
        entry.insert(QStringLiteral("size"), static_cast<double>(file.size));
        entry.insert(QStringLiteral("modified"), file.modified.toUTC().toString(Qt::ISODateWithMs));
        entry.insert(QStringLiteral("matchedRule"), file.matchedRule);
        fileArray.append(entry);
    }
    QJsonArray extras;
    for (const QString &module : extraModules)
        extras.append(module);

    QJsonObject object;
    object.insert(QStringLiteral("detected"), detected);
    object.insert(QStringLiteral("kind"), fixKindName(kind));
    object.insert(QStringLiteral("files"), fileArray);
    object.insert(QStringLiteral("extraModules"), extras);
    object.insert(QStringLiteral("realAppId"), realAppId);
    object.insert(QStringLiteral("fakeAppId"), fakeAppId);
    object.insert(QStringLiteral("fixPath"), fixPath);
    object.insert(QStringLiteral("photonLauncherDir"), photonLauncherDir);
    object.insert(QStringLiteral("hasIni"), hasIni);
    object.insert(QStringLiteral("hasOnlineFixIni"), hasOnlineFixIni);
    object.insert(QStringLiteral("hasSteamFixIni"), hasSteamFixIni);
    return object;
}

OnlineFixInfo OnlineFixInfo::fromJson(const QJsonObject &object)
{
    OnlineFixInfo info;
    info.detected = object.value(QStringLiteral("detected")).toBool(false);
    info.kind = fixKindFromName(object.value(QStringLiteral("kind")).toString());
    const QJsonArray files = object.value(QStringLiteral("files")).toArray();
    for (const QJsonValue &value : files) {
        const QJsonObject entry = value.toObject();
        OnlineFixFile file;
        file.fileName = entry.value(QStringLiteral("fileName")).toString();
        file.absolutePath = entry.value(QStringLiteral("absolutePath")).toString();
        file.size = static_cast<qint64>(entry.value(QStringLiteral("size")).toDouble());
        file.modified = QDateTime::fromString(entry.value(QStringLiteral("modified")).toString(), Qt::ISODateWithMs);
        file.matchedRule = entry.value(QStringLiteral("matchedRule")).toString();
        if (!file.absolutePath.isEmpty())
            info.files.append(file);
    }
    const QJsonArray extras = object.value(QStringLiteral("extraModules")).toArray();
    for (const QJsonValue &value : extras)
        info.extraModules.append(value.toString());
    info.realAppId = object.value(QStringLiteral("realAppId")).toString();
    info.fakeAppId = object.value(QStringLiteral("fakeAppId")).toString();
    info.fixPath = object.value(QStringLiteral("fixPath")).toString();
    info.photonLauncherDir = object.value(QStringLiteral("photonLauncherDir")).toString();
    info.hasIni = object.value(QStringLiteral("hasIni")).toBool(false);
    info.hasOnlineFixIni = object.value(QStringLiteral("hasOnlineFixIni")).toBool(false);
    info.hasSteamFixIni = object.value(QStringLiteral("hasSteamFixIni")).toBool(false);
    return info;
}

QStringList OnlineFixDetector::signaturePatterns()
{
    // These are the file names an OnlineFix / SteamFix release actually ships.
    // Broader patterns were tried and rejected: "win*.dll" matched wininet.dll
    // and windowscodecs.dll, "steam*.dll" matched steam_api64.dll, and
    // "eos*.dll" matched EOSSDK-Win64-Shipping.dll, so almost every Steam or
    // Epic title was reported as carrying a fix and the launch prompt appeared
    // before games that never had one.
    return {
        QStringLiteral("^(emp|custom)\\.dll$"),
        QStringLiteral("^epicfix\\.dll$"),
        QStringLiteral("^onlinefix(32|64)?\\.(dll|ini|json)$"),
        QStringLiteral("^steamfix(32|64)?\\.(dll|ini)$"),
        QStringLiteral("^winmm\\.(dll|txt)$"),
        QStringLiteral("^dlllist\\.txt$"),
        QStringLiteral("^launch_data\\.of.*$")
    };
}

bool OnlineFixDetector::fileNameMatches(const QString &fileName, QString *rule)
{
    for (const QString &pattern : signaturePatterns()) {
        const QRegularExpression expression(pattern, QRegularExpression::CaseInsensitiveOption);
        if (expression.match(fileName).hasMatch()) {
            if (rule)
                *rule = pattern;
            return true;
        }
    }
    return false;
}

OnlineFixInfo OnlineFixDetector::detect(const QString &executableDirectory, const QString &gameDirectory)
{
    OnlineFixInfo info;
    QStringList directories;
    if (!executableDirectory.isEmpty())
        directories.append(QDir(executableDirectory).absolutePath());
    if (!gameDirectory.isEmpty()) {
        const QString root = QDir(gameDirectory).absolutePath();
        if (!directories.contains(root))
            directories.append(root);
    }

    QSet<QString> seenPaths;
    for (const QString &directory : directories) {
        for (const QString &path : topLevelFiles(directory)) {
            const QFileInfo fileInfo(path);
            const QString key = canonicalKey(path);
            if (seenPaths.contains(key))
                continue;
            QString rule;
            if (!fileNameMatches(fileInfo.fileName(), &rule))
                continue;
            seenPaths.insert(key);
            OnlineFixFile file;
            file.fileName = fileInfo.fileName();
            file.absolutePath = fileInfo.absoluteFilePath();
            file.size = fileInfo.size();
            file.modified = fileInfo.lastModified();
            file.matchedRule = rule;
            info.files.append(file);
        }
    }

    info.detected = !info.files.isEmpty();
    bool hasOnlineFixIni = false;
    bool hasSteamFixIni = false;
    bool steamFixHasLinuxSection = false;
    bool hasSteamFixDll = false;
    bool hasPhoton = false;
    bool readRealAppId = false;
    bool readFakeAppId = false;

    for (const OnlineFixFile &file : info.files) {
        const QString lower = file.fileName.toLower();
        if (lower == QStringLiteral("onlinefix.ini")) {
            hasOnlineFixIni = true;
            info.hasIni = true;
        } else if (lower == QStringLiteral("steamfix.ini")) {
            hasSteamFixIni = true;
            info.hasIni = true;
        } else if (lower.startsWith(QStringLiteral("steamfix")) && lower.endsWith(QStringLiteral(".dll"))) {
            hasSteamFixDll = true;
            info.fixPath = QFileInfo(file.absolutePath).absolutePath();
        } else if (lower.startsWith(QStringLiteral("onlinefix")) && lower.endsWith(QStringLiteral(".ini"))) {
            info.hasIni = true;
        }

        if (OnlineFixPatcher::fileIsIni(file.fileName)) {
            IniFile ini;
            QString loadError;
            if (ini.load(file.absolutePath, &loadError)) {
                if (lower == QStringLiteral("steamfix.ini"))
                    steamFixHasLinuxSection = ini.hasSection(kOnlineFixSection);
                bool found = false;
                const QString linuxReal = ini.value(kOnlineFixSection, QStringLiteral("RealAppId"), &found);
                if (found && !readRealAppId) {
                    info.realAppId = linuxReal;
                    readRealAppId = true;
                }
                if (!readRealAppId) {
                    const QString mainReal = ini.value(kMainSection, QStringLiteral("RealAppId"), &found);
                    if (found) {
                        info.realAppId = mainReal;
                        readRealAppId = true;
                    }
                }
                const QString linuxFake = ini.value(kOnlineFixSection, QStringLiteral("FakeAppId"), &found);
                if (found && !readFakeAppId) {
                    info.fakeAppId = linuxFake;
                    readFakeAppId = true;
                }
                if (!readFakeAppId) {
                    const QString mainFake = ini.value(kMainSection, QStringLiteral("FakeAppId"), &found);
                    if (found) {
                        info.fakeAppId = mainFake;
                        readFakeAppId = true;
                    }
                }
            }
        }

        if ((lower.startsWith(QStringLiteral("launch_data.of")) || lower == QStringLiteral("onlinefix.json"))) {
            const QString directory = QFileInfo(file.absolutePath).absolutePath();
            if (hasLauncherBeside(directory)) {
                hasPhoton = true;
                info.photonLauncherDir = directory;
            }
        }
    }

    info.hasOnlineFixIni = hasOnlineFixIni;
    info.hasSteamFixIni = hasSteamFixIni;

    if (hasOnlineFixIni) {
        info.kind = FixKind::OnlineFix;
    } else if (hasSteamFixIni && !steamFixHasLinuxSection) {
        info.kind = FixKind::FreeTP;
    } else if (hasPhoton) {
        info.kind = FixKind::Photon;
    } else if (hasSteamFixDll && !hasSteamFixIni) {
        info.kind = FixKind::SteamFixOnly;
    } else {
        info.kind = FixKind::Unknown;
    }

    QStringList modules;
    QSet<QString> seenModules;
    for (const OnlineFixFile &file : info.files) {
        const QString lower = file.fileName.toLower();
        if (lower != QStringLiteral("winmm.txt") && lower != QStringLiteral("dlllist.txt"))
            continue;
        QFile listFile(file.absolutePath);
        if (!listFile.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QStringList lines = QString::fromUtf8(listFile.readAll()).split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
        for (const QString &rawLine : lines) {
            QString line = rawLine.trimmed();
            line.replace(QLatin1Char('\\'), QLatin1Char('/'));
            const QString base = line.section(QLatin1Char('/'), -1);
            if (!base.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive))
                continue;
            const QString module = base.left(base.size() - 4).toLower();
            if (module.isEmpty() || seenModules.contains(module))
                continue;
            seenModules.insert(module);
            modules.append(module);
        }
    }
    info.extraModules = modules;
    return info;
}

QString OnlineFixOverrides::dvxkBaseChain()
{
    return QStringLiteral("d3d11=n;d3d10=n;d3d10core=n;dxgi=n;openvr_api_dxvk=n;d3d12=n;d3d12core=n;d3d9=n;d3d8=n;");
}

QString OnlineFixOverrides::compose(const Game &game, const OnlineFixInfo &info, bool includeOnlineFix, bool includeBepInEx)
{
    QString result = dvxkBaseChain();
    const QStringList userEntries = game.wineDllOverrides.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &entry : userEntries) {
        const QString trimmed = entry.trimmed();
        if (trimmed.isEmpty())
            continue;
        if (!result.endsWith(QLatin1Char(';')))
            result += QLatin1Char(';');
        result += trimmed + QLatin1Char(';');
    }

    auto appendModule = [&result](const QString &rawModule, const QString &suffix) {
        QString module = rawModule.toLower();
        if (module.endsWith(QStringLiteral(".dll")))
            module.chop(4);
        if (module.isEmpty())
            return;
        const QRegularExpression existing(QStringLiteral("(?:^|;)%1=").arg(QRegularExpression::escape(module)),
                                         QRegularExpression::CaseInsensitiveOption);
        if (existing.match(result).hasMatch())
            return;
        if (!result.endsWith(QLatin1Char(';')) && !result.isEmpty())
            result += QLatin1Char(';');
        result += module + suffix;
    };

    if (includeOnlineFix) {
        for (const OnlineFixFile &file : info.files) {
            if (!file.fileName.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive))
                continue;
            const QString suffix = file.fileName.startsWith(QStringLiteral("win"), Qt::CaseInsensitive)
                ? QStringLiteral("=n,b;") : QStringLiteral("=n;");
            appendModule(file.fileName, suffix);
        }
        for (const QString &module : info.extraModules)
            appendModule(module, QStringLiteral("=n;"));
    }

    if (includeBepInEx)
        appendModule(QStringLiteral("winhttp"), QStringLiteral("=n,b;"));

    while (result.endsWith(QLatin1Char(';')))
        result.chop(1);
    return result;
}

bool OnlineFixPatcher::fileIsIni(const QString &fileName)
{
    return fileName.compare(QStringLiteral("onlinefix.ini"), Qt::CaseInsensitive) == 0
        || fileName.compare(QStringLiteral("steamfix.ini"), Qt::CaseInsensitive) == 0;
}

bool OnlineFixPatcher::fileIsPhotonData(const QString &fileName)
{
    const QString lower = fileName.toLower();
    return lower.startsWith(QStringLiteral("launch_data.of"))
        || lower == QStringLiteral("onlinefix.json");
}

QStringList OnlineFixPatcher::plannedFiles(const OnlineFixInfo &info)
{
    QStringList result;
    for (const OnlineFixFile &file : info.files) {
        if (fileIsIni(file.fileName))
            result.append(file.absolutePath);
        else if (fileIsPhotonData(file.fileName)) {
            const QString directory = QFileInfo(file.absolutePath).absolutePath();
            if (hasLauncherBeside(directory)) {
                const QString link = QDir(directory).filePath(QStringLiteral("Newtonsoft.Json.dll"));
                if (!QFileInfo::exists(link))
                    result.append(link);
            }
        }
    }
    result.removeDuplicates();
    return result;
}

PatchResult OnlineFixPatcher::apply(const Game &game, const OnlineFixInfo &info)
{
    PatchResult result;
    QStringList iniPaths;
    for (const OnlineFixFile &file : info.files) {
        if (fileIsIni(file.fileName))
            iniPaths.append(file.absolutePath);
    }
    iniPaths.removeDuplicates();

    for (const QString &path : iniPaths) {
        const QFileInfo infoFile(path);
        IniFile ini;
        QString error;
        if (!ini.load(path, &error)) {
            result.errors.append(error);
            result.ok = false;
            continue;
        }

        bool changed = false;
        auto set = [&ini, &changed](const QString &section, const QString &key, const QString &value) {
            if (ini.setValue(section, key, value))
                changed = true;
        };

        const QString lower = infoFile.fileName().toLower();
        if (lower == QStringLiteral("steamfix.ini") && !ini.hasSection(kOnlineFixSection)) {
            bool hasReal = false;
            bool hasFake = false;
            const QString originalReal = ini.value(kMainSection, QStringLiteral("RealAppId"), &hasReal);
            const QString originalFake = ini.value(kMainSection, QStringLiteral("FakeAppId"), &hasFake);
            if (hasFake)
                set(kMainSection, QStringLiteral("RealAppId"), originalFake);
            else if (!hasReal)
                result.diagnostics.append(QStringLiteral("steamfix.ini has no RealAppId or FakeAppId to swap."));
            if (hasReal) {
                ini.ensureSection(kOnlineFixSection);
                set(kOnlineFixSection, QStringLiteral("RealAppId"), originalReal);
            }
        }

        if (ini.contains(kMiscSection, QStringLiteral("ExtraProtection")))
            set(kMiscSection, QStringLiteral("ExtraProtection"), QStringLiteral("false"));

        if (!changed)
            continue;
        if (!ensureBackup(path, result)) {
            result.ok = false;
            continue;
        }
        const QFile::Permissions permissions = infoFile.permissions();
        if (!ini.save(&error)) {
            result.errors.append(error);
            result.ok = false;
            continue;
        }
        if (QFileInfo::exists(path))
            QFile::setPermissions(path, permissions);
        result.writtenFiles.append(path);
        result.diagnostics.append(QStringLiteral("Updated OnlineFix configuration: %1").arg(path));
    }

    if (!patchPhoton(game, info, result))
        result.ok = false;
    result.writtenFiles.removeDuplicates();
    result.backedUpFiles.removeDuplicates();
    return result;
}
