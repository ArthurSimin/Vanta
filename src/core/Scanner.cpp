#include "Scanner.h"

#include "I18n.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QQueue>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QtMath>

namespace {
constexpr quint16 kMachineX86 = 0x014c;
constexpr quint16 kMachineX64 = 0x8664;
constexpr quint16 kMachineArm = 0x01c4;
constexpr quint16 kMachineArm64 = 0xaa64;

bool pathWithin(const QString &root, const QString &candidate)
{
    if (root.isEmpty() || candidate.isEmpty())
        return false;
    const QString cleanRoot = QDir::cleanPath(root);
    const QString cleanCandidate = QDir::cleanPath(candidate);
    return cleanCandidate == cleanRoot || cleanCandidate.startsWith(cleanRoot + QLatin1Char('/'));
}

struct PeMetadata {
    bool valid = false;
    quint16 machine = 0;
    QString architecture;
    QString error;
};

PeMetadata readPe(const QString &path)
{
    PeMetadata result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    const QByteArray header = file.read(8192);
    if (header.size() < 64 || header.left(2) != QByteArrayLiteral("MZ")) {
        result.error = QStringLiteral("Not a PE file");
        return result;
    }
    const uchar *bytes = reinterpret_cast<const uchar *>(header.constData());
    const quint32 peOffset = static_cast<quint32>(bytes[0x3c])
        | (static_cast<quint32>(bytes[0x3d]) << 8)
        | (static_cast<quint32>(bytes[0x3e]) << 16)
        | (static_cast<quint32>(bytes[0x3f]) << 24);
    if (peOffset + 6 > static_cast<quint32>(header.size())
        || header.mid(static_cast<int>(peOffset), 4) != QByteArrayLiteral("PE\0\0")) {
        result.error = QStringLiteral("Invalid PE header");
        return result;
    }
    const int machineOffset = static_cast<int>(peOffset) + 4;
    result.machine = static_cast<quint16>(static_cast<uchar>(header.at(machineOffset)))
        | (static_cast<quint16>(static_cast<uchar>(header.at(machineOffset + 1))) << 8);
    result.valid = true;
    switch (result.machine) {
    case kMachineX86: result.architecture = Vanta::tr("32-bit x86"); break;
    case kMachineX64: result.architecture = Vanta::tr("64-bit x64"); break;
    case kMachineArm: result.architecture = Vanta::tr("32-bit ARM"); break;
    case kMachineArm64: result.architecture = Vanta::tr("64-bit ARM64"); break;
    default: result.architecture = Vanta::tr("Unknown architecture"); break;
    }
    return result;
}

bool looksLikeElf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return file.read(4) == QByteArray("\x7f" "ELF", 4);
}

QString componentNameFor(const QString &lower)
{
    if (lower == QStringLiteral("onlinefix64.dll")) return Vanta::tr("OnlineFix 64-bit proxy");
    if (lower == QStringLiteral("onlinefix32.dll")) return Vanta::tr("OnlineFix 32-bit proxy");
    if (lower == QStringLiteral("winmm.dll")) return Vanta::tr("winmm DLL proxy");
    if (lower == QStringLiteral("winhttp.dll")) return Vanta::tr("winhttp DLL proxy");
    if (lower == QStringLiteral("winmm.txt")) return Vanta::tr("winmm module list");
    if (lower == QStringLiteral("dlllist.txt")) return Vanta::tr("DLL module list");
    if (lower == QStringLiteral("onlinefix.ini")) return Vanta::tr("OnlineFix configuration");
    if (lower == QStringLiteral("steamfix.ini")) return Vanta::tr("SteamFix configuration");
    if (lower == QStringLiteral("onlinefix.json")) return Vanta::tr("OnlineFix launcher metadata");
    if (lower.startsWith(QStringLiteral("launch_data.of"))) return Vanta::tr("Photon launcher data");
    if (lower.startsWith(QStringLiteral("epicfix"))) return Vanta::tr("EpicFix compatibility library");
    if (lower.startsWith(QStringLiteral("steamfix")) && lower.endsWith(QStringLiteral(".dll"))) return Vanta::tr("SteamFix library");
    if (lower.startsWith(QStringLiteral("eos")) && lower.endsWith(QStringLiteral(".dll"))) return Vanta::tr("EOS SDK library");
    if (lower.startsWith(QStringLiteral("steam_api"))) return Vanta::tr("Steam API runtime");
    if (lower == QStringLiteral("unityplayer.dll")) return Vanta::tr("Unity Player runtime");
    if (lower == QStringLiteral("doorstop_config.ini")) return Vanta::tr("BepInEx Doorstop configuration");
    if (lower == QStringLiteral("bepinex.preloader.dll")) return Vanta::tr("BepInEx preloader");
    if (lower == QStringLiteral("bepinex.core.dll")) return Vanta::tr("BepInEx core");
    if (lower == QStringLiteral("bepinex.cfg")) return Vanta::tr("BepInEx configuration");
    if (lower == QStringLiteral(".doorstop_version")) return Vanta::tr("Doorstop version marker");
    if (lower == QStringLiteral("emp.dll") || lower == QStringLiteral("custom.dll")) return Vanta::tr("DLL proxy");
    return QString();
}

QString categoryFor(const QString &lower)
{
    if (lower.contains(QStringLiteral("onlinefix")) || lower == QStringLiteral("steamfix.ini")
        || (lower.startsWith(QStringLiteral("steamfix")) && lower.endsWith(QStringLiteral(".dll")))
        || lower.startsWith(QStringLiteral("launch_data.of")) || lower == QStringLiteral("onlinefix.json"))
        return QStringLiteral("OnlineFix");
    if (lower.startsWith(QStringLiteral("bepinex")) || lower == QStringLiteral("doorstop_config.ini")
        || lower == QStringLiteral(".doorstop_version") || lower == QStringLiteral("winhttp.dll"))
        return QStringLiteral("Modding");
    if (lower == QStringLiteral("unityplayer.dll") || lower.startsWith(QStringLiteral("steam_api"))
        || (lower.startsWith(QStringLiteral("eos")) && lower.endsWith(QStringLiteral(".dll"))))
        return QStringLiteral("Runtime");
    return QStringLiteral("Compatibility");
}

int confidenceFor(const QString &lower)
{
    if (lower == QStringLiteral("onlinefix64.dll") || lower == QStringLiteral("onlinefix32.dll")
        || lower == QStringLiteral("onlinefix.ini"))
        return 98;
    if (lower.startsWith(QStringLiteral("steamfix")) || lower.startsWith(QStringLiteral("launch_data.of"))
        || lower == QStringLiteral("onlinefix.json"))
        return 94;
    if (lower == QStringLiteral("winmm.dll") || lower == QStringLiteral("winmm.txt") || lower == QStringLiteral("dlllist.txt"))
        return 84;
    if (lower == QStringLiteral("bepinex.preloader.dll") || lower == QStringLiteral("bepinex.core.dll")
        || lower == QStringLiteral("doorstop_config.ini"))
        return 92;
    if (lower == QStringLiteral("winhttp.dll") || lower == QStringLiteral("unityplayer.dll"))
        return 70;
    if (lower.startsWith(QStringLiteral("steam_api")))
        return 90;
    if (lower.startsWith(QStringLiteral("eos")))
        return 78;
    if (lower.startsWith(QStringLiteral("win")) && lower.endsWith(QStringLiteral(".dll")))
        return 62;
    return 65;
}

QString explanationFor(const QString &lower, const QString &category)
{
    if (category == QStringLiteral("OnlineFix"))
        return Vanta::tr("Filename signature associated with OnlineFix or SteamFix releases. Presence does not prove the component is functional.");
    if (category == QStringLiteral("Modding"))
        return Vanta::tr("BepInEx or Doorstop component. Vanta reports the file only and does not enable mod overrides unless requested.");
    if (category == QStringLiteral("Runtime"))
        return Vanta::tr("Game runtime library. Its presence is informational and does not imply compatibility has been verified.");
    if (lower.startsWith(QStringLiteral("win")) && lower.endsWith(QStringLiteral(".dll")))
        return Vanta::tr("DLL that may act as a Wine proxy. Vanta cannot verify the file's behaviour from its name alone.");
    return Vanta::tr("Recognizable compatibility-related file. Detection is based on the filename and metadata, not a functional test.");
}

QString cacheDate(const QDateTime &dateTime)
{
    return dateTime.isValid() ? dateTime.toUTC().toString(Qt::ISODateWithMs) : QString();
}

QDateTime cacheDateFrom(const QString &value)
{
    QDateTime result = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!result.isValid())
        result = QDateTime::fromString(value, Qt::ISODate);
    return result;
}
}

bool ScanResult::cacheMatches(const QString &root) const
{
    if (root.isEmpty() || gameDirectory.isEmpty())
        return false;
    const QFileInfo rootInfo(root);
    if (!rootInfo.exists() || !rootInfo.isDir())
        return false;
    return QDir::cleanPath(root) == QDir::cleanPath(gameDirectory)
        && directoryModified == rootInfo.lastModified().toMSecsSinceEpoch();
}

QJsonObject ExecutableCandidate_toJson(const ExecutableCandidate &candidate)
{
    QJsonObject object;
    object.insert(QStringLiteral("path"), candidate.path);
    object.insert(QStringLiteral("size"), static_cast<double>(candidate.size));
    object.insert(QStringLiteral("architecture"), candidate.architecture);
    object.insert(QStringLiteral("depth"), candidate.depth);
    object.insert(QStringLiteral("score"), candidate.score);
    return object;
}

ExecutableCandidate executableCandidateFromJson(const QJsonObject &object)
{
    ExecutableCandidate candidate;
    candidate.path = object.value(QStringLiteral("path")).toString();
    candidate.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
    candidate.architecture = object.value(QStringLiteral("architecture")).toString();
    candidate.depth = object.value(QStringLiteral("depth")).toInt();
    candidate.score = object.value(QStringLiteral("score")).toInt();
    return candidate;
}

QJsonObject ScanResult::toJson() const
{
    QJsonArray executableArray;
    for (const ExecutableCandidate &candidate : executables)
        executableArray.append(ExecutableCandidate_toJson(candidate));

    QJsonArray componentArray;
    for (const CompatibilityComponent &component : components) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), component.name);
        object.insert(QStringLiteral("fileName"), component.fileName);
        object.insert(QStringLiteral("absolutePath"), component.absolutePath);
        object.insert(QStringLiteral("category"), component.category);
        object.insert(QStringLiteral("architecture"), component.architecture);
        object.insert(QStringLiteral("size"), static_cast<double>(component.size));
        object.insert(QStringLiteral("modified"), cacheDate(component.modified));
        object.insert(QStringLiteral("confidence"), component.confidence);
        object.insert(QStringLiteral("explanation"), component.explanation);
        componentArray.append(object);
    }

    auto stringsToArray = [](const QStringList &strings) {
        QJsonArray array;
        for (const QString &value : strings)
            array.append(value);
        return array;
    };

    QJsonObject object;
    object.insert(QStringLiteral("gameDirectory"), gameDirectory);
    object.insert(QStringLiteral("executablePath"), executablePath);
    object.insert(QStringLiteral("executableArchitecture"), executableArchitecture);
    object.insert(QStringLiteral("state"), static_cast<int>(state));
    object.insert(QStringLiteral("scannedAt"), cacheDate(scannedAt));
    object.insert(QStringLiteral("executables"), executableArray);
    object.insert(QStringLiteral("components"), componentArray);
    object.insert(QStringLiteral("onlineFix"), onlineFix.toJson());
    object.insert(QStringLiteral("warnings"), stringsToArray(warnings));
    object.insert(QStringLiteral("missingPaths"), stringsToArray(missingPaths));
    object.insert(QStringLiteral("inaccessiblePaths"), stringsToArray(inaccessiblePaths));
    object.insert(QStringLiteral("prefixStatus"), prefixStatus);
    object.insert(QStringLiteral("bepinexDetected"), bepinexDetected);
    object.insert(QStringLiteral("truncated"), truncated);
    object.insert(QStringLiteral("executablesTruncated"), executablesTruncated);
    object.insert(QStringLiteral("suggestedExecutable"), suggestedExecutable);
    object.insert(QStringLiteral("error"), error);
    object.insert(QStringLiteral("directoryModified"), static_cast<double>(directoryModified));
    return object;
}

ScanResult ScanResult::fromJson(const QJsonObject &object)
{
    ScanResult result;
    result.gameDirectory = object.value(QStringLiteral("gameDirectory")).toString();
    result.executablePath = object.value(QStringLiteral("executablePath")).toString();
    result.executableArchitecture = object.value(QStringLiteral("executableArchitecture")).toString();
    const int state = object.value(QStringLiteral("state")).toInt(static_cast<int>(ScanState::NotScanned));
    result.state = state >= static_cast<int>(ScanState::NotScanned) && state <= static_cast<int>(ScanState::ScanError)
        ? static_cast<ScanState>(state) : ScanState::NotScanned;
    result.scannedAt = cacheDateFrom(object.value(QStringLiteral("scannedAt")).toString());
    const QJsonArray executableArray = object.value(QStringLiteral("executables")).toArray();
    for (const QJsonValue &value : executableArray)
        result.executables.append(executableCandidateFromJson(value.toObject()));
    const QJsonArray componentArray = object.value(QStringLiteral("components")).toArray();
    for (const QJsonValue &value : componentArray) {
        const QJsonObject entry = value.toObject();
        CompatibilityComponent component;
        component.name = entry.value(QStringLiteral("name")).toString();
        component.fileName = entry.value(QStringLiteral("fileName")).toString();
        component.absolutePath = entry.value(QStringLiteral("absolutePath")).toString();
        component.category = entry.value(QStringLiteral("category")).toString();
        component.architecture = entry.value(QStringLiteral("architecture")).toString();
        component.size = static_cast<qint64>(entry.value(QStringLiteral("size")).toDouble());
        component.modified = cacheDateFrom(entry.value(QStringLiteral("modified")).toString());
        component.confidence = entry.value(QStringLiteral("confidence")).toInt();
        component.explanation = entry.value(QStringLiteral("explanation")).toString();
        if (!component.absolutePath.isEmpty())
            result.components.append(component);
    }
    result.onlineFix = OnlineFixInfo::fromJson(object.value(QStringLiteral("onlineFix")).toObject());
    auto arrayToStrings = [](const QJsonArray &array) {
        QStringList strings;
        for (const QJsonValue &value : array)
            strings.append(value.toString());
        return strings;
    };
    result.warnings = arrayToStrings(object.value(QStringLiteral("warnings")).toArray());
    result.missingPaths = arrayToStrings(object.value(QStringLiteral("missingPaths")).toArray());
    result.inaccessiblePaths = arrayToStrings(object.value(QStringLiteral("inaccessiblePaths")).toArray());
    result.prefixStatus = object.value(QStringLiteral("prefixStatus")).toString();
    result.bepinexDetected = object.value(QStringLiteral("bepinexDetected")).toBool(false);
    result.truncated = object.value(QStringLiteral("truncated")).toBool(false);
    result.executablesTruncated = object.value(QStringLiteral("executablesTruncated")).toBool(false);
    result.suggestedExecutable = object.value(QStringLiteral("suggestedExecutable")).toString();
    result.error = object.value(QStringLiteral("error")).toString();
    result.directoryModified = static_cast<qint64>(object.value(QStringLiteral("directoryModified")).toDouble());
    return result;
}

bool GameScanner::directoryIsSafeToScan(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir())
        return false;
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty())
        return false;
    const QString home = QDir::homePath();
    const QStringList forbidden = {
        QStringLiteral("/"), QStringLiteral("/home"), QStringLiteral("/usr"),
        QStringLiteral("/etc"), QStringLiteral("/var"), QStringLiteral("/proc"),
        QStringLiteral("/sys"), QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
    };
    for (const QString &entry : forbidden) {
        if (!entry.isEmpty() && QDir::cleanPath(entry) == canonical)
            return false;
    }
    if (canonical == QDir::cleanPath(home))
        return false;
    return true;
}

QString GameScanner::likelyGameRoot(const QString &executableOrDirectory)
{
    if (executableOrDirectory.isEmpty())
        return {};
    QFileInfo info(executableOrDirectory);
    if (!info.exists())
        return QDir::cleanPath(executableOrDirectory);
    QDir startDir = info.isDir() ? QDir(info.absoluteFilePath()) : QDir(info.absolutePath());
    if (info.isDir())
        return startDir.absolutePath();

    const QStringList rootMarkers = {
        QStringLiteral("steam_api64.dll"), QStringLiteral("steam_api.dll"),
        QStringLiteral("UnityPlayer.dll"), QStringLiteral("unityplayer.dll"),
        QStringLiteral("Engine"), QStringLiteral("Content"), QStringLiteral("Binaries"),
        QStringLiteral("Data"), QStringLiteral("_CommonRedist"), QStringLiteral("BepInEx"),
        QStringLiteral("onlinefix.ini"), QStringLiteral("steamfix.ini"),
        QStringLiteral("onlinefix.json"), QStringLiteral("Launcher.exe"),
        QStringLiteral("EOSAuthLauncher.exe")
    };
    const QStringList weakNames = {
        QStringLiteral("bin"), QStringLiteral("binaries"), QStringLiteral("win64"),
        QStringLiteral("win32"), QStringLiteral("x64"), QStringLiteral("x86"),
        QStringLiteral("system"), QStringLiteral("plugins")
    };

    QString best = startDir.absolutePath();
    int bestScore = 0;
    QDir current = startDir;
    for (int depth = 0; depth < 5; ++depth) {
        int score = 0;
        const QString dirName = current.dirName().toLower();
        if (weakNames.contains(dirName))
            score -= 8;
        const QStringList entries = current.entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
        const QSet<QString> entrySet(entries.begin(), entries.end());
        for (const QString &marker : rootMarkers) {
            if (entrySet.contains(marker))
                score += 10;
        }
        if (current.exists(QStringLiteral("Content")) && current.exists(QStringLiteral("Binaries")))
            score += 15;
        if (current.exists(QStringLiteral("BepInEx")))
            score += 8;
        if (entries.size() >= 5)
            score += 2;
        if (depth == 0)
            score += 3;
        score -= depth;
        if (score > bestScore) {
            bestScore = score;
            best = current.absolutePath();
        }
        if (!current.cdUp())
            break;
    }
    return QDir::cleanPath(best);
}

bool GameScanner::isSkippedExecutable(const QString &fileName)
{
    static const QList<QRegularExpression> patterns = [] {
        const QStringList raw = {
            QStringLiteral("^vcredist.*\\.exe$"), QStringLiteral("^vc_redist.*\\.exe$"),
            QStringLiteral("^dxsetup\\.exe$"), QStringLiteral("^directx.*setup.*\\.exe$"),
            QStringLiteral("^dxwebsetup\\.exe$"), QStringLiteral("^dotnet.*setup.*\\.exe$"),
            QStringLiteral("^install.*\\.exe$"), QStringLiteral("^uninstall.*\\.exe$"),
            QStringLiteral("^unins[0-9]+\\.exe$"), QStringLiteral("^updater.*\\.exe$"),
            QStringLiteral("^patch.*\\.exe$"), QStringLiteral("^steam\\.exe$"),
            QStringLiteral("^unitycrashhandler.*\\.exe$"), QStringLiteral("^unitybugreporter.*\\.exe$"),
            QStringLiteral("^ue4.*prereq.*\\.exe$"), QStringLiteral("^crashreportclient.*\\.exe$"),
            QStringLiteral("^bugreporter.*\\.exe$"), QStringLiteral("^sandbox.*\\.exe$"),
            QStringLiteral("^editor.*\\.exe$"), QStringLiteral("^config.*\\.exe$"),
            QStringLiteral("^settings.*\\.exe$"), QStringLiteral("^options.*\\.exe$"),
            QStringLiteral("^benchmark.*\\.exe$"), QStringLiteral("^profil(er|ing).*\\.exe$"),
            QStringLiteral("^crashpad_handler\\.exe$")
        };
        QList<QRegularExpression> result;
        for (const QString &pattern : raw)
            result.append(QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption));
        return result;
    }();
    for (const QRegularExpression &pattern : patterns) {
        if (pattern.match(fileName).hasMatch())
            return true;
    }
    return false;
}

QString GameScanner::executableArchitecture(const QString &path)
{
    return readPe(path).architecture;
}

bool GameScanner::isWindowsExecutable(const QString &path)
{
    return readPe(path).valid;
}

QString GameScanner::suggestedExecutable(const QString &gameDirectory, const QList<ExecutableCandidate> &candidates)
{
    Q_UNUSED(gameDirectory);
    for (const ExecutableCandidate &candidate : candidates) {
        if (QFileInfo(candidate.path).fileName().compare(QStringLiteral("EOSAuthLauncher.exe"), Qt::CaseInsensitive) == 0)
            return candidate.path;
    }
    for (const ExecutableCandidate &candidate : candidates) {
        if (QFileInfo(candidate.path).fileName().compare(QStringLiteral("Launcher.exe"), Qt::CaseInsensitive) != 0)
            continue;
        QDir directory = QFileInfo(candidate.path).absoluteDir();
        const QFileInfoList entries = directory.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            if (entry.fileName().compare(QStringLiteral("onlinefix.json"), Qt::CaseInsensitive) == 0)
                return candidate.path;
        }
    }
    if (!candidates.isEmpty())
        return candidates.first().path;
    return {};
}

ScanResult GameScanner::scan(const QString &executablePath, const QString &gameDirectory,
                             const QString &configuredPrefix, RunnerType runner,
                             const ScanOptions &options)
{
    ScanResult result;
    result.scannedAt = QDateTime::currentDateTime();
    result.executablePath = executablePath;

    QString root = gameDirectory;
    if (root.isEmpty() && !executablePath.isEmpty())
        root = QFileInfo(executablePath).absolutePath();
    root = QDir::cleanPath(root);
    result.gameDirectory = root;

    const QFileInfo rootInfo(root);
    if (root.isEmpty() || !rootInfo.exists() || !rootInfo.isDir()) {
        result.state = ScanState::ScanError;
        result.error = root.isEmpty() ? Vanta::tr("No game directory was selected.") : Vanta::tr("The game directory does not exist: %1").arg(root);
        result.missingPaths.append(root.isEmpty() ? Vanta::tr("Game directory") : root);
        return result;
    }
    if (!directoryIsSafeToScan(root)) {
        result.state = ScanState::ScanError;
        result.error = Vanta::tr("This directory is too broad to scan safely. Select the game's own folder.");
        return result;
    }
    result.directoryModified = rootInfo.lastModified().toMSecsSinceEpoch();
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    QSet<QString> visitedDirectories;
    QSet<QString> visitedFiles;
    bool incomplete = false;
    bool incompatible = false;
    bool prefixInvalid = false;
    bool selectedArchitectureKnown = false;

    if (!executablePath.isEmpty()) {
        const QFileInfo executableInfo(executablePath);
        if (!executableInfo.exists() || !executableInfo.isFile())
            result.missingPaths.append(Vanta::tr("Game executable: %1").arg(executablePath));
        else if (executableInfo.fileName().endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
            const PeMetadata pe = readPe(executablePath);
            result.executableArchitecture = pe.architecture;
            selectedArchitectureKnown = pe.valid;
            if (pe.valid && (pe.machine == kMachineArm || pe.machine == kMachineArm64)) {
                const QString host = QSysInfo::currentCpuArchitecture();
                if (!host.contains(QStringLiteral("arm64"), Qt::CaseInsensitive)
                    && !host.contains(QStringLiteral("aarch64"), Qt::CaseInsensitive)) {
                    incompatible = true;
                    result.warnings.append(Vanta::tr("The selected executable targets ARM, which the configured %1 runner cannot execute on this host.").arg(runnerName(runner)));
                }
            }
            if (pe.valid && pe.machine != kMachineX86 && pe.machine != kMachineX64
                && pe.machine != kMachineArm && pe.machine != kMachineArm64) {
                incompatible = true;
                result.warnings.append(Vanta::tr("The selected executable uses an unsupported PE machine type."));
            }
            if (!pe.valid && runnerIsWineLike(runner)) {
                result.warnings.append(Vanta::tr("The selected executable does not have a readable PE header."));
            }
        }
    }

    struct DirectoryTask { QString path; int depth; };
    QQueue<DirectoryTask> queue;
    queue.enqueue({root, 0});
    int inspectedEntries = 0;

    auto addComponent = [&result](const QString &path, const QFileInfo &info) {
        const QString lower = info.fileName().toLower();
        QString name = componentNameFor(lower);
        if (name.isEmpty() && !lower.startsWith(QStringLiteral("win")) )
            return;
        if (name.isEmpty())
            name = Vanta::tr("DLL proxy candidate");
        for (const CompatibilityComponent &existing : result.components) {
            if (existing.absolutePath == info.absoluteFilePath())
                return;
        }
        CompatibilityComponent component;
        component.name = name;
        component.fileName = info.fileName();
        component.absolutePath = info.absoluteFilePath();
        component.category = categoryFor(lower);
        component.architecture = lower.endsWith(QStringLiteral(".dll")) || lower.endsWith(QStringLiteral(".exe"))
            ? readPe(info.absoluteFilePath()).architecture : QString();
        component.size = info.size();
        component.modified = info.lastModified();
        component.confidence = confidenceFor(lower);
        component.explanation = explanationFor(lower, component.category);
        if (component.category == QStringLiteral("Modding") || component.category == QStringLiteral("OnlineFix"))
            result.bepinexDetected = result.bepinexDetected || component.category == QStringLiteral("Modding");
        result.components.append(component);
        Q_UNUSED(path);
    };

    auto isRecognizedComponent = [](const QFileInfo &info, bool insideBepInEx) {
        const QString lower = info.fileName().toLower();
        if (insideBepInEx)
            return true;
        if (OnlineFixDetector::fileNameMatches(info.fileName()))
            return true;
        if (lower == QStringLiteral("unityplayer.dll") || lower.startsWith(QStringLiteral("steam_api"))
            || (lower.startsWith(QStringLiteral("eos")) && lower.endsWith(QStringLiteral(".dll")))
            || lower == QStringLiteral("doorstop_config.ini")
            || lower == QStringLiteral("winhttp.dll"))
            return true;
        return false;
    };

    bool limitReached = false;
    while (!queue.isEmpty() && !limitReached) {
        const DirectoryTask task = queue.dequeue();
        const QFileInfo dirInfo(task.path);
        const QString canonicalDirectory = dirInfo.canonicalFilePath();
        if (canonicalDirectory.isEmpty() || visitedDirectories.contains(canonicalDirectory))
            continue;
        visitedDirectories.insert(canonicalDirectory);
        QDir dir(task.path);
        if (!dir.exists()) {
            result.inaccessiblePaths.append(task.path);
            continue;
        }
        const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                                                         QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &entry : entries) {
            ++inspectedEntries;
            if (inspectedEntries > options.entryLimit) {
                result.truncated = true;
                limitReached = true;
                break;
            }
            const QString path = entry.absoluteFilePath();
            bool insideRoot = true;
            if (entry.isSymLink()) {
                const QString canonical = entry.canonicalFilePath();
                insideRoot = !canonical.isEmpty() && pathWithin(canonicalRoot, canonical);
                if (!insideRoot) {
                    result.warnings.append(Vanta::tr("Skipped a symbolic link pointing outside the game directory: %1").arg(path));
                    continue;
                }
            }
            if (entry.isDir()) {
                if (task.depth >= options.maxDepth) {
                    result.warnings.append(Vanta::tr("Scan depth limit reached at %1").arg(path));
                    continue;
                }
                queue.enqueue({path, task.depth + 1});
                continue;
            }
            if (!entry.isFile())
                continue;
            const QString canonicalFile = entry.canonicalFilePath();
            if (canonicalFile.isEmpty() || !pathWithin(canonicalRoot, canonicalFile))
                continue;
            if (visitedFiles.contains(canonicalFile))
                continue;
            visitedFiles.insert(canonicalFile);
            if (!entry.isReadable()) {
                result.inaccessiblePaths.append(path);
                continue;
            }

            const QString lower = entry.fileName().toLower();
            const QString relative = QDir(root).relativeFilePath(path);
            const int depth = relative.count(QLatin1Char('/'));
            const bool insideBepInEx = relative.contains(QStringLiteral("BepInEx/"), Qt::CaseInsensitive)
                || relative.startsWith(QStringLiteral("BepInEx/"), Qt::CaseInsensitive)
                || relative.startsWith(QStringLiteral("BepInEx\\"), Qt::CaseInsensitive);

            if (lower.endsWith(QStringLiteral(".part")) || lower.endsWith(QStringLiteral(".crdownload"))
                || lower.endsWith(QStringLiteral(".download")))
                incomplete = true;

            if (insideBepInEx && entry.isDir())
                result.bepinexDetected = true;
            if (isRecognizedComponent(entry, insideBepInEx)) {
                addComponent(path, entry);
                if (insideBepInEx)
                    result.bepinexDetected = true;
            }

            const bool isWindowsExecutable = lower.endsWith(QStringLiteral(".exe"));
            const bool isScript = lower.endsWith(QStringLiteral(".sh")) && entry.isExecutable();
            const bool isElf = !isWindowsExecutable && !isScript && entry.isExecutable() && looksLikeElf(path);
            if (!isWindowsExecutable && !isScript && !isElf)
                continue;
            if (isWindowsExecutable && isSkippedExecutable(entry.fileName()))
                continue;
            if (isWindowsExecutable && !entry.isExecutable()) {
                // Windows executables are frequently checked out without the Unix execute bit.
            }

            int score = 30 - depth * 6;
            const QString base = entry.completeBaseName().toLower();
            if (base.contains(QStringLiteral("launcher")))
                score += 18;
            if (base == QStringLiteral("eosauthlauncher"))
                score += 30;
            if (base == QStringLiteral("game") || base.startsWith(QStringLiteral("game")))
                score += 12;
            if (base.contains(QStringLiteral("start")))
                score += 6;
            if (entry.size() > 1024 * 1024)
                score += 4;
            if (isWindowsExecutable) {
                score += 5;
            } else if (isElf) {
                score += 4;
            }
            ExecutableCandidate candidate;
            candidate.path = path;
            candidate.size = entry.size();
            candidate.architecture = isWindowsExecutable ? readPe(path).architecture
                : (isElf ? Vanta::tr("Linux ELF") : Vanta::tr("Script"));
            candidate.depth = depth;
            candidate.score = score;
            // The cap only matters for pathological folders, but it must not be
            // silent: a user wondering why the right binary is missing from the
            // list has no other hint.
            if (result.executables.size() < 400) {
                result.executables.append(candidate);
            } else if (!result.executablesTruncated) {
                result.executablesTruncated = true;
                result.warnings.append(Vanta::tr("More than 400 executables were found. Only the %1 best matches are listed; set the executable explicitly if the right one is missing.").arg(400));
            }
        }
    }

    std::sort(result.executables.begin(), result.executables.end(), [](const ExecutableCandidate &a, const ExecutableCandidate &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return QString::localeAwareCompare(a.path, b.path) < 0;
    });
    result.suggestedExecutable = suggestedExecutable(root, result.executables);
    if (result.truncated)
        result.warnings.append(Vanta::tr("The scan stopped after %1 entries. Increase the scan entry limit in Settings if the game folder is larger.").arg(options.entryLimit));

    if (!configuredPrefix.isEmpty()) {
        const QFileInfo prefixInfo(configuredPrefix);
        const QDir prefixDir(configuredPrefix);
        const bool hasDrive = QFileInfo::exists(prefixDir.filePath(QStringLiteral("drive_c")))
            || QFileInfo::exists(prefixDir.filePath(QStringLiteral("pfx/drive_c")));
        if (!prefixInfo.exists()) {
            prefixInvalid = true;
            result.prefixStatus = Vanta::tr("Configured Wine prefix is missing: %1").arg(configuredPrefix);
            result.missingPaths.append(result.prefixStatus);
        } else if (!hasDrive) {
            prefixInvalid = true;
            result.prefixStatus = Vanta::tr("Configured Wine prefix has no drive_c: %1").arg(configuredPrefix);
            result.warnings.append(result.prefixStatus);
        } else {
            result.prefixStatus = Vanta::tr("Wine/Proton prefix detected: %1").arg(configuredPrefix);
        }
    } else {
        result.prefixStatus = Vanta::tr("No prefix configured. Vanta will create one beside the executable when needed.");
    }

    const QString executableDirectory = executablePath.isEmpty() ? root : QFileInfo(executablePath).absolutePath();
    result.onlineFix = OnlineFixDetector::detect(executableDirectory, root);
    if (result.onlineFix.detected) {
        for (const OnlineFixFile &file : result.onlineFix.files) {
            const QFileInfo fileInfo(file.absolutePath);
            if (!fileInfo.isReadable()) {
                result.inaccessiblePaths.append(file.absolutePath);
                continue;
            }
            bool exists = false;
            for (const CompatibilityComponent &component : result.components) {
                if (component.absolutePath == file.absolutePath) {
                    exists = true;
                    break;
                }
            }
            if (!exists)
                addComponent(file.absolutePath, fileInfo);
        }
    }

    result.warnings.removeDuplicates();
    result.missingPaths.removeDuplicates();
    result.inaccessiblePaths.removeDuplicates();

    if (!result.error.isEmpty()) {
        result.state = ScanState::ScanError;
    } else if (incomplete) {
        result.state = ScanState::IncompleteInstallation;
    } else if (!result.missingPaths.isEmpty()) {
        result.state = ScanState::MissingRequiredFiles;
    } else if (incompatible) {
        result.state = ScanState::IncompatibleArchitecture;
    } else if (result.executables.isEmpty() && !executablePath.isEmpty()
               && runner != RunnerType::Flatpak && runner != RunnerType::Custom) {
        result.state = ScanState::MissingRequiredFiles;
        result.missingPaths.append(Vanta::tr("No launchable game executable was found."));
    } else if (prefixInvalid) {
        result.state = ScanState::MissingRequiredFiles;
    } else if (!result.components.isEmpty() || result.onlineFix.detected) {
        result.state = ScanState::ComponentsDetected;
    } else {
        result.state = ScanState::NoKnownComponents;
    }

    result.bepinexDetected = result.bepinexDetected
        || QFileInfo::exists(QDir(root).filePath(QStringLiteral("BepInEx")))
        || QFileInfo::exists(QDir(executableDirectory).filePath(QStringLiteral("BepInEx")));
    if (!selectedArchitectureKnown && result.executableArchitecture.isEmpty()
        && !result.executablePath.isEmpty())
        result.executableArchitecture = result.executables.isEmpty() ? QString() : result.executables.first().architecture;
    return result;
}

QString GameScanner::compatibilityCacheLabel(const Game &game)
{
    if (game.compatibility == ScanState::ComponentsDetected)
        return game.showsOnlineFix() ? Vanta::tr("OnlineFix detected") : scanStateLabel(game.compatibility);
    return scanStateLabel(game.compatibility);
}

void GameScanner::applyResultToGame(Game &game, const ScanResult &result)
{
    game.compatibility = result.state;
    if (result.scannedAt.isValid())
        game.lastScanned = result.scannedAt;
    if (game.gameDirectory.isEmpty() && !result.gameDirectory.isEmpty())
        game.gameDirectory = result.gameDirectory;
    if (game.executable.isEmpty() && !result.suggestedExecutable.isEmpty())
        game.executable = result.suggestedExecutable;

    if (result.onlineFix.detected) {
        game.fixKind = result.onlineFix.kind;
        if (!result.onlineFix.realAppId.isEmpty())
            game.realAppId = result.onlineFix.realAppId;
        if (!result.onlineFix.fakeAppId.isEmpty())
            game.fakeAppId = result.onlineFix.fakeAppId;
        if (!result.onlineFix.fixPath.isEmpty())
            game.fixPath = result.onlineFix.fixPath;
        if (!result.onlineFix.photonLauncherDir.isEmpty())
            game.photonLauncherDir = result.onlineFix.photonLauncherDir;
    } else if (result.error.isEmpty() && result.state != ScanState::ScanError) {
        game.fixKind = FixKind::Unknown;
    }
}
