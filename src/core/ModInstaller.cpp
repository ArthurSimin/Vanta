#include "ModInstaller.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringList>

#include <zlib.h>

namespace {
constexpr quint32 kLocalHeader = 0x04034b50;
constexpr quint32 kCentralHeader = 0x02014b50;
constexpr quint32 kEndOfCentralDirectory = 0x06054b50;
constexpr qint64 kMaximumArchiveBytes = 512LL * 1024LL * 1024LL;
constexpr qint64 kMaximumEntryBytes = 512LL * 1024LL * 1024LL;
constexpr qint64 kMaximumExtractedBytes = 1024LL * 1024LL * 1024LL;
constexpr int kMaximumArchiveEntries = 10000;

struct ZipEntry
{
    QString name;
    QByteArray data;
    bool executable = false;
};

bool isRootMarker(const QString &lower);

// Read byte by byte instead of casting the buffer to a wider type: the cast
// made GCC warn about array bounds at every call site, and byte reads keep the
// parser independent of the host's alignment and endianness.
quint16 read16(const QByteArray &data, int offset)
{
    return static_cast<quint16>(static_cast<quint8>(data.at(offset)))
        | static_cast<quint16>(static_cast<quint16>(static_cast<quint8>(data.at(offset + 1))) << 8);
}

quint32 read32(const QByteArray &data, int offset)
{
    return static_cast<quint32>(static_cast<quint8>(data.at(offset)))
        | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 8)
        | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 16)
        | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 3))) << 24);
}

bool readZip(const QByteArray &archive, QList<ZipEntry> *entries, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (archive.size() < 22)
        return fail(QStringLiteral("The archive is too small to be a ZIP file."));
    if (archive.size() > kMaximumArchiveBytes)
        return fail(QStringLiteral("The archive is larger than the 512 MiB import limit."));

    int endOffset = -1;
    const int minimumOffset = qMax(0, archive.size() - 22 - 65535);
    for (int offset = archive.size() - 22; offset >= minimumOffset; --offset) {
        if (read32(archive, offset) == kEndOfCentralDirectory) {
            endOffset = offset;
            break;
        }
    }
    if (endOffset < 0)
        return fail(QStringLiteral("The ZIP end-of-directory record was not found."));

    const quint16 entryCount = read16(archive, endOffset + 10);
    const quint32 centralSize = read32(archive, endOffset + 12);
    const quint32 centralOffset = read32(archive, endOffset + 16);
    if (entryCount == 0xffff || centralSize == 0xffffffff || centralOffset == 0xffffffff) {
        return fail(QStringLiteral("This ZIP uses ZIP64, which Vanta does not support. Use a package under 4 GiB."));
    }
    if (entryCount > kMaximumArchiveEntries)
        return fail(QStringLiteral("The archive contains more than %1 entries.").arg(kMaximumArchiveEntries));
    if (static_cast<qint64>(centralOffset) + static_cast<qint64>(centralSize) > archive.size())
        return fail(QStringLiteral("The ZIP central directory points outside the archive."));

    qint64 totalExtracted = 0;
    int offset = static_cast<int>(centralOffset);
    for (int index = 0; index < entryCount; ++index) {
        if (offset < 0 || offset + 46 > archive.size() || read32(archive, offset) != kCentralHeader)
            return fail(QStringLiteral("The ZIP central directory is damaged."));

        const quint16 madeBy = read16(archive, offset + 4);
        const quint16 flags = read16(archive, offset + 8);
        const quint16 method = read16(archive, offset + 10);
        const quint32 compressedSize = read32(archive, offset + 20);
        const quint32 uncompressedSize = read32(archive, offset + 24);
        const quint16 nameLength = read16(archive, offset + 28);
        const quint16 extraLength = read16(archive, offset + 30);
        const quint16 commentLength = read16(archive, offset + 32);
        const quint32 externalAttributes = read32(archive, offset + 38);
        const quint32 localOffset = read32(archive, offset + 42);

        if (compressedSize == 0xffffffff || uncompressedSize == 0xffffffff)
            return fail(QStringLiteral("This ZIP uses ZIP64, which Vanta does not support."));
        if (flags & 0x0001)
            return fail(QStringLiteral("The archive contains an encrypted entry."));

        const int nameStart = offset + 46;
        if (nameStart + nameLength > archive.size())
            return fail(QStringLiteral("The ZIP central directory is damaged."));
        const QByteArray nameBytes = archive.mid(nameStart, nameLength);
        QString name = (flags & 0x0800) ? QString::fromUtf8(nameBytes) : QString::fromLatin1(nameBytes);
        offset = nameStart + nameLength + extraLength + commentLength;

        name.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (name.endsWith(QLatin1Char('/')))
            continue;

        QStringList parts = name.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;
        bool unsafe = false;
        for (const QString &part : parts) {
            if (part == QStringLiteral("..") || part.contains(QLatin1Char(':')) || part.isEmpty()) {
                unsafe = true;
                break;
            }
        }
        if (unsafe)
            return fail(QStringLiteral("The archive contains an unsafe path: %1").arg(name));
        name = parts.join(QLatin1Char('/'));

        const QString lowerName = name.toLower();
        const int unixMode = (madeBy >> 8) == 3 || (madeBy >> 8) == 19
            ? static_cast<int>(externalAttributes >> 16) : 0;
        const bool symlink = (unixMode & 0170000) == 0120000;
        if (symlink)
            return fail(QStringLiteral("The archive contains a symbolic link, which Vanta refuses to create: %1")
                            .arg(name));
        // Some writers store a folder without the trailing slash. Its mode
        // still marks it as a directory, and treating it as a file would both
        // fail the install and block later packages from creating the folder.
        if ((unixMode & 0170000) == 0040000)
            continue;

        if (static_cast<qint64>(uncompressedSize) > kMaximumEntryBytes)
            return fail(QStringLiteral("The archive entry %1 is larger than the 512 MiB limit.").arg(name));
        totalExtracted += static_cast<qint64>(uncompressedSize);
        if (totalExtracted > kMaximumExtractedBytes)
            return fail(QStringLiteral("The archive expands to more than 1 GiB."));

        // Compare as 64-bit: a corrupt 32-bit offset near 4 GiB would otherwise
        // wrap when 30 is added and slip past the bounds check.
        const qint64 localStart = static_cast<qint64>(localOffset);
        if (localStart + 30 > static_cast<qint64>(archive.size())
            || read32(archive, static_cast<int>(localStart)) != kLocalHeader) {
            return fail(QStringLiteral("The archive entry %1 has a damaged local header.").arg(name));
        }
        const quint16 localNameLength = read16(archive, static_cast<int>(localStart) + 26);
        const quint16 localExtraLength = read16(archive, static_cast<int>(localStart) + 28);
        const qint64 dataStart = localStart + 30 + localNameLength + localExtraLength;
        if (dataStart < 0 || dataStart + compressedSize > archive.size())
            return fail(QStringLiteral("The archive entry %1 points outside the archive.").arg(name));

        QByteArray payload;
        if (method == 0) {
            payload = archive.mid(static_cast<int>(dataStart), static_cast<int>(compressedSize));
        } else if (method == 8) {
            payload.resize(qMax<int>(static_cast<int>(uncompressedSize), 1));
            z_stream stream {};
            stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(archive.constData() + dataStart));
            stream.avail_in = static_cast<uInt>(compressedSize);
            stream.next_out = reinterpret_cast<Bytef *>(payload.data());
            stream.avail_out = static_cast<uInt>(payload.size());
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
                return fail(QStringLiteral("The bundled decompressor could not start."));
            const int result = inflate(&stream, Z_FINISH);
            const qint64 produced = static_cast<qint64>(stream.total_out);
            inflateEnd(&stream);
            if (result != Z_STREAM_END || produced != static_cast<qint64>(uncompressedSize))
                return fail(QStringLiteral("The archive entry %1 could not be decompressed.").arg(name));
            payload.resize(static_cast<int>(produced));
        } else {
            return fail(QStringLiteral("The archive entry %1 uses an unsupported compression method (%2).")
                            .arg(name).arg(method));
        }
        // A stored entry carries its real length as compressedSize. If the
        // central directory claims another uncompressed length, the archive is
        // inconsistent and the payload would be installed truncated or padded.
        if (payload.size() != static_cast<int>(uncompressedSize))
            return fail(QStringLiteral("The archive entry %1 has inconsistent stored sizes.").arg(name));

        ZipEntry entry;
        entry.name = name;
        entry.data = payload;
        entry.executable = (unixMode & 0111) != 0 || lowerName.endsWith(QStringLiteral(".sh"));
        entries->append(entry);
    }

    if (entries->isEmpty())
        return fail(QStringLiteral("The archive contains no files."));
    return true;
}

bool isMetadataFile(const QString &path)
{
    const QString lower = path.toLower();
    return lower == QStringLiteral("manifest.json")
        || lower == QStringLiteral("icon.png")
        || lower == QStringLiteral("readme.md")
        || lower == QStringLiteral("readme.txt")
        || lower == QStringLiteral("changelog.md")
        || lower == QStringLiteral("changelog.txt")
        || lower == QStringLiteral("changelog")
        || lower == QStringLiteral("license")
        || lower == QStringLiteral("license.md")
        || lower == QStringLiteral("license.txt");
}

QString sanitizeComponent(const QString &value)
{
    QString result = value;
    result.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    result = result.trimmed();
    while (result.startsWith(QLatin1Char('.')))
        result.remove(0, 1);
    if (result.isEmpty())
        result = QStringLiteral("mod");
    return result;
}

QString relativePathFrom(const QString &base, const QString &path)
{
    const QString relative = QDir::fromNativeSeparators(QDir(base).relativeFilePath(path));
    return relative == QStringLiteral(".") ? QString() : relative;
}

bool startsWithPath(const QString &path, const QString &prefix)
{
    if (prefix.isEmpty())
        return false;
    return path.compare(prefix, Qt::CaseInsensitive) == 0
        || path.startsWith(prefix + QLatin1Char('/'), Qt::CaseInsensitive);
}

QString afterPrefix(const QString &path, const QString &prefix)
{
    if (path.compare(prefix, Qt::CaseInsensitive) == 0)
        return QString();
    return path.mid(prefix.size() + 1);
}

QString mapArchivePath(const QString &sourcePath, const QString &rootRelative,
                       const QString &pluginsRelative, const QString &patchersRelative,
                       const QString &packageFolder)
{
    const QString lower = sourcePath.toLower();
    if (startsWithPath(sourcePath, rootRelative)) {
        const QString rest = afterPrefix(sourcePath, rootRelative);
        const QString lowerRest = rest.toLower();
        if (lowerRest == QStringLiteral("plugins") || lowerRest.startsWith(QStringLiteral("plugins/")))
            return pluginsRelative + QLatin1Char('/') + afterPrefix(rest, QStringLiteral("plugins"));
        if (lowerRest == QStringLiteral("patchers") || lowerRest.startsWith(QStringLiteral("patchers/")))
            return patchersRelative + QLatin1Char('/') + afterPrefix(rest, QStringLiteral("patchers"));
        if (lowerRest == QStringLiteral("config") || lowerRest.startsWith(QStringLiteral("config/")))
            return rootRelative + QStringLiteral("/config/") + afterPrefix(rest, QStringLiteral("config"));
        if (lowerRest == QStringLiteral("core") || lowerRest.startsWith(QStringLiteral("core/")))
            return rootRelative + QStringLiteral("/core/") + afterPrefix(rest, QStringLiteral("core"));
        if (!rest.isEmpty())
            return rootRelative + QLatin1Char('/') + rest;
    }
    // A bare BepInEx member is the folder itself, not a file to install.
    if (lower == QStringLiteral("bepinex"))
        return {};
    if (lower.startsWith(QStringLiteral("bepinex/"))) {
        const QString rest = afterPrefix(sourcePath, QStringLiteral("BepInEx"));
        return mapArchivePath(rootRelative + QLatin1Char('/') + rest, rootRelative, pluginsRelative,
                              patchersRelative, packageFolder);
    }
    if (lower == QStringLiteral("plugins") || lower.startsWith(QStringLiteral("plugins/")))
        return pluginsRelative + QLatin1Char('/') + afterPrefix(sourcePath, QStringLiteral("plugins"));
    if (lower == QStringLiteral("patchers") || lower.startsWith(QStringLiteral("patchers/")))
        return patchersRelative + QLatin1Char('/') + afterPrefix(sourcePath, QStringLiteral("patchers"));
    if (lower == QStringLiteral("config") || lower.startsWith(QStringLiteral("config/")))
        return rootRelative + QStringLiteral("/config/") + afterPrefix(sourcePath, QStringLiteral("config"));
    if (lower == QStringLiteral("core") || lower.startsWith(QStringLiteral("core/")))
        return rootRelative + QStringLiteral("/core/") + afterPrefix(sourcePath, QStringLiteral("core"));
    if (lower == QStringLiteral("doorstop_libs") || lower.startsWith(QStringLiteral("doorstop_libs/")))
        return sourcePath;
    if (!sourcePath.contains(QLatin1Char('/')) && isRootMarker(lower))
        return sourcePath;
    return pluginsRelative + QLatin1Char('/') + packageFolder + QLatin1Char('/') + sourcePath;
}

bool isRootMarker(const QString &lower)
{
    return lower == QStringLiteral("winhttp.dll")
        || lower == QStringLiteral("version.dll")
        || lower == QStringLiteral("doorstop_config.ini")
        || lower == QStringLiteral(".doorstop_version")
        || lower == QStringLiteral("run_bepinex.sh")
        || lower == QStringLiteral("libdoorstop.so");
}

QString commonTopLevel(const QList<ZipEntry> &entries)
{
    QString common;
    bool sawFile = false;
    for (const ZipEntry &entry : entries) {
        if (isMetadataFile(entry.name))
            continue;
        const int slash = entry.name.indexOf(QLatin1Char('/'));
        if (slash <= 0)
            return {};
        const QString first = entry.name.left(slash);
        if (common.isEmpty())
            common = first;
        else if (common.compare(first, Qt::CaseInsensitive) != 0)
            return {};
        sawFile = true;
    }
    if (!sawFile)
        return {};
    const QString lower = common.toLower();
    if (lower == QStringLiteral("bepinex") || lower == QStringLiteral("plugins")
        || lower == QStringLiteral("patchers") || lower == QStringLiteral("config")
        || lower == QStringLiteral("core") || lower == QStringLiteral("doorstop_libs")) {
        return {};
    }
    return common;
}

bool parseManifest(const QList<ZipEntry> &entries, QString *name, QString *version,
                   QStringList *dependencies, QString *warning)
{
    for (const ZipEntry &entry : entries) {
        if (entry.name.compare(QStringLiteral("manifest.json"), Qt::CaseInsensitive) != 0)
            continue;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(entry.data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (warning)
                *warning = QStringLiteral("manifest.json could not be parsed: %1").arg(parseError.errorString());
            return false;
        }
        const QJsonObject object = document.object();
        if (name)
            *name = object.value(QStringLiteral("name")).toString();
        if (version)
            *version = object.value(QStringLiteral("version_number")).toString();
        if (dependencies) {
            const QJsonArray array = object.value(QStringLiteral("dependencies")).toArray();
            for (const QJsonValue &value : array) {
                const QString dependency = value.toString().trimmed();
                if (!dependency.isEmpty())
                    dependencies->append(dependency);
            }
        }
        return true;
    }
    return false;
}

bool unsafePathMessage(const QString &path, QString *error)
{
    const QString clean = QDir::cleanPath(path);
    if (clean.isEmpty() || clean == QStringLiteral(".") || QDir::isAbsolutePath(clean)
        || clean == QStringLiteral("..") || clean.startsWith(QStringLiteral("../"))
        || clean.contains(QStringLiteral("/../")) || clean.contains(QLatin1Char(':'))) {
        if (error)
            *error = QStringLiteral("Refusing the unsafe install path: %1").arg(path);
        return true;
    }
    return false;
}

// Resolves the deepest existing ancestor of a path. A BepInEx folder that does not
// exist yet has to be validated against the folder it will be created in, not
// against its own absent path.
QString deepestExistingCanonical(const QString &path)
{
    QString current = QDir::cleanPath(QDir(path).absolutePath());
    while (!current.isEmpty() && !QFileInfo::exists(current)) {
        QDir directory(current);
        if (!directory.cdUp())
            break;
        current = directory.absolutePath();
    }
    QString canonical = QFileInfo(current).canonicalFilePath();
    if (canonical.isEmpty())
        canonical = QDir::cleanPath(current);
    return canonical;
}

bool existingParentIsInside(const QString &targetPath, const QString &basePath, QString *error)
{
    const QFileInfo targetInfo(targetPath);
    if (targetInfo.isSymLink()) {
        if (error)
            *error = QStringLiteral("Refusing to replace the symbolic link %1.")
                         .arg(QDir::toNativeSeparators(targetPath));
        return false;
    }

    const QFileInfo baseInfo(basePath);
    if (baseInfo.exists() && baseInfo.isSymLink()) {
        if (error)
            *error = QStringLiteral("Refusing to write through the symbolic link %1.")
                         .arg(QDir::toNativeSeparators(basePath));
        return false;
    }

    QString parent = targetInfo.absolutePath();
    while (!parent.isEmpty() && !QFileInfo::exists(parent)) {
        QDir directory(parent);
        if (!directory.cdUp())
            break;
        parent = directory.absolutePath();
    }

    QString parentCanonical = QFileInfo(parent).canonicalFilePath();
    if (parentCanonical.isEmpty())
        parentCanonical = QDir::cleanPath(QDir(parent).absolutePath());
    QString baseCanonical = baseInfo.exists() ? baseInfo.canonicalFilePath()
                                              : deepestExistingCanonical(basePath);
    if (baseCanonical.isEmpty())
        baseCanonical = QDir::cleanPath(QDir(basePath).absolutePath());

    if (parentCanonical != baseCanonical && !parentCanonical.startsWith(baseCanonical + QLatin1Char('/'))) {
        if (error)
            *error = QStringLiteral("Refusing to write %1 because its folder resolves outside %2.")
                         .arg(QDir::toNativeSeparators(targetPath), QDir::toNativeSeparators(basePath));
        return false;
    }
    return true;
}

QString backupDirectoryFor(const QString &bepInExRoot)
{
    const QString base = QDir(bepInExRoot).filePath(QStringLiteral(".vanta-backups"));
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString candidate = QDir(base).filePath(stamp);
    int suffix = 1;
    while (QFileInfo::exists(candidate))
        candidate = QDir(base).filePath(QStringLiteral("%1-%2").arg(stamp).arg(suffix++));
    return candidate;
}

bool finalizePlan(ModInstallPlan *plan, const ModTarget &target, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (!target.isValid())
        return fail(QStringLiteral("The game folder is not available."));
    if (plan->files.isEmpty())
        return fail(QStringLiteral("The package does not contain any installable files."));

    const QString rootRelative = relativePathFrom(target.gameRoot, target.bepInExRoot);
    if (rootRelative.isEmpty())
        return fail(QStringLiteral("The BepInEx folder is not inside the game folder."));
    const QString pluginsRelative = relativePathFrom(target.gameRoot, target.pluginsPath);
    const QString patchersRelative = relativePathFrom(target.gameRoot, target.patchersPath);
    if (pluginsRelative.isEmpty() || patchersRelative.isEmpty())
        return fail(QStringLiteral("The BepInEx plugins or patchers folder is not inside the game folder."));

    QHash<QString, int> seen;
    for (int index = 0; index < plan->files.size();) {
        ModInstallFile &file = plan->files[index];
        file.relativePath = QDir::cleanPath(QDir::fromNativeSeparators(file.relativePath));
        if (unsafePathMessage(file.relativePath, error))
            return false;
        const QString folded = file.relativePath.toCaseFolded();
        if (seen.contains(folded)) {
            const ModInstallFile &previous = plan->files.at(seen.value(folded));
            if (previous.data == file.data) {
                plan->files.removeAt(index);
                continue;
            }
            return fail(QStringLiteral("Two files in this package target the same path: %1")
                            .arg(file.relativePath));
        }
        seen.insert(folded, index);
        ++index;
    }

    plan->conflicts.clear();
    plan->totalBytes = 0;
    const QString targetRoot = target.gameRoot;
    const QString backupBase = target.bepInExRoot;
    for (ModInstallFile &file : plan->files) {
        plan->totalBytes += file.data.size();
        const QString targetPath = QDir(targetRoot).filePath(file.relativePath);
        const bool insideBepInEx = startsWithPath(file.relativePath, rootRelative);
        const QString base = insideBepInEx ? backupBase : targetRoot;
        if (!existingParentIsInside(targetPath, base, error))
            return false;

        const QFileInfo info(targetPath);
        if (!info.exists())
            continue;
        if (info.isSymLink()) {
            return fail(QStringLiteral("Refusing to replace the symbolic link %1.")
                            .arg(QDir::toNativeSeparators(targetPath)));
        }
        if (info.isDir()) {
            return fail(QStringLiteral("A folder already exists where the package wants to write a file: %1")
                            .arg(QDir::toNativeSeparators(targetPath)));
        }
        if (!info.isFile()) {
            return fail(QStringLiteral("The package target is not a regular file: %1")
                            .arg(QDir::toNativeSeparators(targetPath)));
        }

        QFile existing(targetPath);
        if (!existing.open(QIODevice::ReadOnly)) {
            return fail(QStringLiteral("Could not read the existing file %1: %2")
                            .arg(QDir::toNativeSeparators(targetPath), existing.errorString()));
        }
        if (existing.readAll() != file.data)
            plan->conflicts.append(file.relativePath);
    }
    return true;
}
}

ModTarget ModInstaller::targetFor(const BepInExInstall &install)
{
    ModTarget target;
    if (!install.isValid())
        return target;
    target.gameRoot = install.gameRoot;
    target.bepInExRoot = install.root;
    target.pluginsPath = install.pluginsPath.isEmpty()
        ? QDir(install.root).filePath(QStringLiteral("plugins")) : install.pluginsPath;
    target.patchersPath = install.patchersPath.isEmpty()
        ? QDir(install.root).filePath(QStringLiteral("patchers")) : install.patchersPath;
    return target;
}

ModTarget ModInstaller::targetForGame(const QString &gameDirectory)
{
    ModTarget target;
    const QString clean = QDir::cleanPath(gameDirectory);
    if (clean.isEmpty())
        return target;
    target.gameRoot = clean;
    target.bepInExRoot = QDir(clean).filePath(QStringLiteral("BepInEx"));
    target.pluginsPath = QDir(target.bepInExRoot).filePath(QStringLiteral("plugins"));
    target.patchersPath = QDir(target.bepInExRoot).filePath(QStringLiteral("patchers"));
    return target;
}

bool ModInstaller::planFromFile(const QString &path, const ModTarget &target,
                                ModInstallPlan *plan, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (!plan || !target.isValid())
        return fail(QStringLiteral("The game folder is not available."));
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return fail(QStringLiteral("The selected mod file does not exist."));
    if (info.size() > kMaximumArchiveBytes)
        return fail(QStringLiteral("The selected file is larger than the 512 MiB import limit."));

    const QString suffix = info.suffix().toLower();
    if (suffix != QStringLiteral("dll") && suffix != QStringLiteral("zip") && suffix != QStringLiteral("disabled"))
        return fail(QStringLiteral("Choose a .dll or .zip mod file."));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Could not read %1: %2")
                        .arg(QDir::toNativeSeparators(path), file.errorString()));
    const QByteArray payload = file.readAll();
    file.close();

    if (suffix != QStringLiteral("zip")) {
        ModInstallPlan local;
        QString base = info.fileName();
        if (base.endsWith(QStringLiteral(".disabled"), Qt::CaseInsensitive))
            base.chop(9);
        if (base.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive))
            base.chop(4);
        local.displayName = base;
        local.packageName = sanitizeComponent(base);
        local.sourceLabel = info.fileName();
        ModInstallFile installFile;
        const QString pluginsRelative = relativePathFrom(target.gameRoot, target.pluginsPath);
        installFile.relativePath = pluginsRelative + QLatin1Char('/') + info.fileName();
        installFile.data = payload;
        local.files.append(installFile);
        if (!QFileInfo::exists(target.bepInExRoot)) {
            local.warnings.append(
                QStringLiteral("BepInEx is not installed yet. The DLL will be placed in a new plugins folder, "
                               "but it will not load until BepInEx is installed."));
        }
        if (!finalizePlan(&local, target, error))
            return false;
        *plan = local;
        return true;
    }

    return planFromArchive(payload, info.fileName(), QString(), target, plan, error);
}

bool ModInstaller::planFromArchive(const QByteArray &archive, const QString &archiveName,
                                   const QString &packageName, const ModTarget &target,
                                   ModInstallPlan *plan, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (!plan || !target.isValid())
        return fail(QStringLiteral("The game folder is not available."));

    QList<ZipEntry> entries;
    if (!readZip(archive, &entries, error))
        return false;

    QString manifestName;
    QString manifestVersion;
    QStringList dependencies;
    QString manifestWarning;
    const bool hasManifest = parseManifest(entries, &manifestName, &manifestVersion, &dependencies,
                                           &manifestWarning);

    const QString rootRelative = relativePathFrom(target.gameRoot, target.bepInExRoot);
    const QString pluginsRelative = relativePathFrom(target.gameRoot, target.pluginsPath);
    const QString patchersRelative = relativePathFrom(target.gameRoot, target.patchersPath);
    if (rootRelative.isEmpty() || pluginsRelative.isEmpty() || patchersRelative.isEmpty())
        return fail(QStringLiteral("The BepInEx folder is not inside the game folder."));

    const QString archiveBase = QFileInfo(archiveName).completeBaseName();
    const QString folderName = sanitizeComponent(packageName.isEmpty() ? archiveBase : packageName);
    const QString stripPrefix = commonTopLevel(entries);

    ModInstallPlan result;
    result.displayName = manifestName.isEmpty() ? folderName : manifestName;
    result.packageName = folderName;
    result.version = manifestVersion;
    result.sourceLabel = archiveName;
    result.dependencies = dependencies;
    if (!manifestWarning.isEmpty())
        result.warnings.append(manifestWarning);

    bool hasDll = false;
    bool hasCore = false;
    for (const ZipEntry &entry : entries) {
        QString sourcePath = entry.name;
        if (!stripPrefix.isEmpty() && sourcePath.startsWith(stripPrefix + QLatin1Char('/'), Qt::CaseInsensitive))
            sourcePath = sourcePath.mid(stripPrefix.size() + 1);
        if (isMetadataFile(sourcePath)) {
            result.skipped.append(sourcePath);
            continue;
        }

        const QString destination = mapArchivePath(sourcePath, rootRelative, pluginsRelative,
                                                   patchersRelative, folderName);
        if (destination.isEmpty())
            continue;
        // A folder marker stored without its trailing slash maps to a path
        // ending in one. It carries no data and is not a file the package wants
        // to install, so it must not become a zero-byte file in its place.
        if (entry.data.isEmpty()
            && (destination.endsWith(QLatin1Char('/'))
                || QFileInfo(QDir(target.gameRoot).filePath(destination)).isDir()))
            continue;
        if (destination.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive))
            hasDll = true;
        if (startsWithPath(destination, rootRelative + QStringLiteral("/core")))
            hasCore = true;

        ModInstallFile installFile;
        installFile.relativePath = destination;
        installFile.data = entry.data;
        installFile.executable = entry.executable;
        result.files.append(installFile);
    }

    if (result.files.isEmpty())
        return fail(QStringLiteral("The archive does not contain any installable files."));
    if (!hasDll) {
        result.warnings.append(
            QStringLiteral("The package does not contain a DLL. It may be an asset-only package."));
    }
    if (hasCore && QFileInfo::exists(target.bepInExRoot)) {
        result.warnings.append(
            QStringLiteral("This package includes BepInEx core files. Existing files will be backed up before "
                           "they are replaced."));
    }
    if (!hasCore && !QFileInfo::exists(target.bepInExRoot)) {
        result.warnings.append(
            QStringLiteral("BepInEx is not installed yet. A package that only contains a plugin will not work "
                           "until BepInEx is installed."));
    }
    if (!hasManifest) {
        result.warnings.append(
            QStringLiteral("The package has no manifest.json. Its declared dependencies cannot be read."));
    }

    if (!finalizePlan(&result, target, error))
        return false;
    *plan = result;
    return true;
}

bool ModInstaller::mergePlans(const QList<ModInstallPlan> &plans, const ModTarget &target,
                              ModInstallPlan *merged, QString *error)
{
    if (!merged || plans.isEmpty()) {
        if (error)
            *error = QStringLiteral("No package plans were produced.");
        return false;
    }

    ModInstallPlan result;
    result.displayName = plans.first().displayName;
    result.packageName = plans.first().packageName;
    result.version = plans.first().version;
    result.sourceLabel = plans.first().sourceLabel;
    for (const ModInstallPlan &plan : plans) {
        result.files.append(plan.files);
        result.skipped.append(plan.skipped);
        result.warnings.append(plan.warnings);
        for (const QString &dependency : plan.dependencies) {
            if (!result.dependencies.contains(dependency))
                result.dependencies.append(dependency);
        }
    }
    result.skipped.removeDuplicates();
    result.warnings.removeDuplicates();
    if (!finalizePlan(&result, target, error))
        return false;
    *merged = result;
    return true;
}

bool ModInstaller::execute(const ModInstallPlan &plan, const ModTarget &target, bool overwrite,
                           QStringList *written, QString *backupRoot, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (written)
        written->clear();
    if (backupRoot)
        backupRoot->clear();
    if (!target.isValid())
        return fail(QStringLiteral("The game folder is not available."));
    if (plan.files.isEmpty())
        return fail(QStringLiteral("The package does not contain any installable files."));

    const QString rootRelative = relativePathFrom(target.gameRoot, target.bepInExRoot);
    if (rootRelative.isEmpty())
        return fail(QStringLiteral("The BepInEx folder is not inside the game folder."));

    QString backups;
    for (const ModInstallFile &file : plan.files) {
        if (unsafePathMessage(file.relativePath, error))
            return false;
        const QString targetPath = QDir(target.gameRoot).filePath(file.relativePath);
        const bool insideBepInEx = startsWithPath(file.relativePath, rootRelative);
        const QString base = insideBepInEx ? target.bepInExRoot : target.gameRoot;
        if (!existingParentIsInside(targetPath, base, error))
            return false;

        const QFileInfo info(targetPath);
        if (info.exists()) {
            if (info.isSymLink()) {
                return fail(QStringLiteral("Refusing to replace the symbolic link %1.")
                                .arg(QDir::toNativeSeparators(targetPath)));
            }
            if (info.isDir()) {
                return fail(QStringLiteral("A folder already exists where the package wants to write a file: %1")
                                .arg(QDir::toNativeSeparators(targetPath)));
            }

            QFile existing(targetPath);
            if (!existing.open(QIODevice::ReadOnly)) {
                return fail(QStringLiteral("Could not read the existing file %1: %2")
                                .arg(QDir::toNativeSeparators(targetPath), existing.errorString()));
            }
            if (existing.readAll() == file.data) {
                existing.close();
                continue;
            }
            existing.close();

            if (!overwrite) {
                return fail(QStringLiteral("Refusing to overwrite the existing file %1 without confirmation.")
                                .arg(QDir::toNativeSeparators(targetPath)));
            }
            if (backups.isEmpty())
                backups = backupDirectoryFor(target.bepInExRoot);
            const QString backupPath = QDir(backups).filePath(file.relativePath);
            if (!QDir().mkpath(QFileInfo(backupPath).absolutePath())) {
                return fail(QStringLiteral("Could not create the backup folder %1.")
                                .arg(QDir::toNativeSeparators(QFileInfo(backupPath).absolutePath())));
            }
            if (QFileInfo::exists(backupPath) || !QFile::copy(targetPath, backupPath)) {
                return fail(QStringLiteral("Could not back up %1 before replacing it.")
                                .arg(QDir::toNativeSeparators(targetPath)));
            }
        }

        const QString parent = QFileInfo(targetPath).absolutePath();
        if (!QDir().mkpath(parent)) {
            return fail(QStringLiteral("Could not create the folder %1.")
                            .arg(QDir::toNativeSeparators(parent)));
        }

        QSaveFile output(targetPath);
        if (!output.open(QIODevice::WriteOnly)) {
            return fail(QStringLiteral("Could not create %1: %2")
                            .arg(QDir::toNativeSeparators(targetPath), output.errorString()));
        }
        if (output.write(file.data) != file.data.size()) {
            output.cancelWriting();
            return fail(QStringLiteral("Could not write %1: %2")
                            .arg(QDir::toNativeSeparators(targetPath), output.errorString()));
        }
        if (!output.commit()) {
            return fail(QStringLiteral("Could not finish writing %1: %2")
                            .arg(QDir::toNativeSeparators(targetPath), output.errorString()));
        }
        if (file.executable) {
            QFile::Permissions permissions = QFile::permissions(targetPath);
            permissions |= QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther;
            QFile::setPermissions(targetPath, permissions);
        }
        if (written)
            written->append(targetPath);
    }

    if (backupRoot)
        *backupRoot = backups;
    return true;
}
