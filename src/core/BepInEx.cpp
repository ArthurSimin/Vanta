#include "BepInEx.h"

#include "I18n.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>

namespace {
constexpr int kMaximumMods = 5000;
constexpr int kMaximumParentDepth = 4;

QString findChildEntry(const QString &parent, const QString &name)
{
    QDir directory(parent);
    const QString direct = directory.filePath(name);
    if (QFileInfo::exists(direct))
        return direct;

    const QFileInfoList entries = directory.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                                                          QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (entry.fileName().compare(name, Qt::CaseInsensitive) == 0)
            return entry.absoluteFilePath();
    }
    return {};
}

QString findChildDirectory(const QString &parent, const QString &name)
{
    const QString path = findChildEntry(parent, name);
    return QFileInfo(path).isDir() ? QDir(path).absolutePath() : QString();
}

QString findBepInExRoot(const QString &start)
{
    if (start.isEmpty())
        return {};
    QDir directory(start);
    if (!directory.exists())
        return {};
    directory.makeAbsolute();

    for (int depth = 0; depth < kMaximumParentDepth; ++depth) {
        const QString root = findChildDirectory(directory.absolutePath(), QStringLiteral("BepInEx"));
        if (!root.isEmpty())
            return QDir::cleanPath(root);
        if (!directory.cdUp())
            break;
    }
    return {};
}

bool pathIsInside(const QString &path, const QString &base)
{
    if (path.isEmpty() || base.isEmpty())
        return false;
    const QString cleanPath = QDir::cleanPath(path);
    const QString cleanBase = QDir::cleanPath(base);
    return cleanPath != cleanBase && cleanPath.startsWith(cleanBase + QLatin1Char('/'));
}

// A plugin can sit directly in plugins/ or patchers/, so containment must accept
// the folder itself and not only its descendants.
bool pathIsWithin(const QString &path, const QString &base)
{
    if (path.isEmpty() || base.isEmpty())
        return false;
    return QDir::cleanPath(path) == QDir::cleanPath(base) || pathIsInside(path, base);
}

bool containsFileName(const QString &directory, const QString &fileName)
{
    const QString path = findChildEntry(directory, fileName);
    return !path.isEmpty() && QFileInfo(path).isFile();
}

QString detectGeneration(const QString &corePath)
{
    if (corePath.isEmpty())
        return QStringLiteral("BepInEx");
    if (containsFileName(corePath, QStringLiteral("BepInEx.Core.dll"))
        || containsFileName(corePath, QStringLiteral("BepInEx.Preloader.Core.dll"))
        || containsFileName(corePath, QStringLiteral("BepInEx.Unity.Mono.dll"))) {
        return QStringLiteral("BepInEx 6");
    }
    if (containsFileName(corePath, QStringLiteral("BepInEx.dll"))
        || containsFileName(corePath, QStringLiteral("BepInEx.Preloader.dll"))) {
        return QStringLiteral("BepInEx 5");
    }
    return QStringLiteral("BepInEx");
}

QStringList detectDoorstopMarkers(const QString &gameRoot)
{
    QStringList markers;
    const QStringList candidates = {
        QStringLiteral("winhttp.dll"),
        QStringLiteral("version.dll"),
        QStringLiteral("doorstop_config.ini"),
        QStringLiteral(".doorstop_version"),
        QStringLiteral("run_bepinex.sh"),
        QStringLiteral("libdoorstop.so"),
        QStringLiteral("doorstop_libs")
    };
    for (const QString &candidate : candidates) {
        const QString path = findChildEntry(gameRoot, candidate);
        if (!path.isEmpty())
            markers.append(QFileInfo(path).fileName());
    }
    return markers;
}

void appendMods(const QString &root, const QString &directory, bool patcher, BepInExInstall *install)
{
    if (directory.isEmpty())
        return;

    QDirIterator iterator(directory, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        if (install->mods.size() >= kMaximumMods) {
            install->truncated = true;
            break;
        }
        const QString path = iterator.next();
        const QFileInfo info(path);
        if (info.isSymLink()) {
            install->warnings.append(
                Vanta::tr("Skipped symbolic link %1.").arg(QDir::toNativeSeparators(path)));
            continue;
        }
        if (!info.isFile())
            continue;

        const QString fileName = info.fileName();
        const bool disabled = fileName.endsWith(QStringLiteral(".dll.disabled"), Qt::CaseInsensitive);
        if (!disabled && !fileName.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive))
            continue;

        const QString enabledName = disabled ? fileName.left(fileName.size() - 9) : fileName;
        BepInExMod mod;
        mod.name = QFileInfo(enabledName).completeBaseName();
        if (mod.name.isEmpty())
            mod.name = enabledName;
        mod.path = info.absoluteFilePath();
        mod.relativePath = QDir::fromNativeSeparators(QDir(root).relativeFilePath(mod.path));
        mod.patcher = patcher;
        mod.enabled = !disabled;
        install->mods.append(mod);
    }
}

QString stateName(bool enabled)
{
    return enabled ? Vanta::tr("enabled") : Vanta::tr("disabled");
}
}

int BepInExInstall::enabledModCount() const
{
    int count = 0;
    for (const BepInExMod &mod : mods) {
        if (mod.enabled)
            ++count;
    }
    return count;
}

int BepInExInstall::disabledModCount() const
{
    return mods.size() - enabledModCount();
}

BepInExInstall BepInEx::detect(const QString &gameDirectory, const QString &executablePath)
{
    BepInExInstall install;
    QStringList starts;
    if (!gameDirectory.trimmed().isEmpty())
        starts.append(QDir::cleanPath(gameDirectory));
    if (!executablePath.trimmed().isEmpty()) {
        const QString executableDirectory = QFileInfo(executablePath).absolutePath();
        if (!executableDirectory.isEmpty() && !starts.contains(executableDirectory))
            starts.append(executableDirectory);
    }

    for (const QString &start : starts) {
        install.root = findBepInExRoot(start);
        if (!install.root.isEmpty())
            break;
    }
    if (install.root.isEmpty())
        return install;

    install.gameRoot = QFileInfo(install.root).absolutePath();
    install.corePath = findChildDirectory(install.root, QStringLiteral("core"));
    install.pluginsPath = findChildDirectory(install.root, QStringLiteral("plugins"));
    install.patchersPath = findChildDirectory(install.root, QStringLiteral("patchers"));
    install.generation = detectGeneration(install.corePath);
    install.doorstopMarkers = detectDoorstopMarkers(install.gameRoot);

    // A fresh BepInEx pack ships only core/, winhttp.dll and the doorstop files;
    // BepInEx creates plugins/ and patchers/ on its first run. Their absence is
    // therefore normal and must not be reported as a broken installation.
    if (install.corePath.isEmpty() && install.pluginsPath.isEmpty() && install.patchersPath.isEmpty()) {
        install.warnings.append(
            Vanta::tr("The BepInEx folder does not contain a core, plugins, or patchers folder."));
    }

    appendMods(install.root, install.pluginsPath, false, &install);
    appendMods(install.root, install.patchersPath, true, &install);

    std::sort(install.mods.begin(), install.mods.end(), [](const BepInExMod &left, const BepInExMod &right) {
        if (left.patcher != right.patcher)
            return !left.patcher;
        const int folded = left.relativePath.compare(right.relativePath, Qt::CaseInsensitive);
        if (folded != 0)
            return folded < 0;
        return left.relativePath < right.relativePath;
    });

    if (install.truncated) {
        install.warnings.append(
            Vanta::tr("Only the first %1 DLLs were listed. Use the plugins folder for the complete list.")
                .arg(kMaximumMods));
    }
    return install;
}

bool BepInEx::setModEnabled(const BepInExInstall &install, const BepInExMod &mod, bool enabled,
                            QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (!install.isValid())
        return fail(Vanta::tr("BepInEx was not detected for this game."));
    if (mod.path.trimmed().isEmpty())
        return fail(Vanta::tr("The selected mod has no file path."));
    if (error)
        error->clear();

    const QFileInfo rootInfo(install.root);
    const QString rootPath = rootInfo.canonicalFilePath();
    if (rootPath.isEmpty())
        return fail(Vanta::tr("The BepInEx folder is no longer available: %1")
                        .arg(QDir::toNativeSeparators(install.root)));

    const QFileInfo sourceInfo(mod.path);
    if (sourceInfo.isSymLink()) {
        return fail(Vanta::tr("Refusing to change the symbolic link %1. Rename or replace the link yourself.")
                        .arg(QDir::toNativeSeparators(mod.path)));
    }
    if (!sourceInfo.isFile())
        return fail(Vanta::tr("The mod file no longer exists: %1")
                        .arg(QDir::toNativeSeparators(mod.path)));

    const QString sourcePath = sourceInfo.canonicalFilePath();
    if (sourcePath.isEmpty())
        return fail(Vanta::tr("The mod file could not be resolved: %1")
                        .arg(QDir::toNativeSeparators(mod.path)));

    const QString parentPath = sourceInfo.absoluteDir().canonicalPath();
    if (!pathIsInside(parentPath, rootPath)) {
        return fail(Vanta::tr("Refusing to change %1 because it is outside the detected BepInEx folder.")
                        .arg(QDir::toNativeSeparators(mod.path)));
    }

    const QString pluginsPath = QFileInfo(install.pluginsPath).canonicalFilePath();
    const QString patchersPath = QFileInfo(install.patchersPath).canonicalFilePath();
    if (!pathIsWithin(parentPath, pluginsPath) && !pathIsWithin(parentPath, patchersPath)) {
        return fail(Vanta::tr("Refusing to change %1 because it is not inside the plugins or patchers folder.")
                        .arg(QDir::toNativeSeparators(mod.path)));
    }

    const QString fileName = sourceInfo.fileName();
    const bool disabled = fileName.endsWith(QStringLiteral(".dll.disabled"), Qt::CaseInsensitive);
    if (!disabled && !fileName.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive)) {
        return fail(Vanta::tr("Only plugin and patcher DLLs can be enabled or disabled: %1")
                        .arg(QDir::toNativeSeparators(mod.path)));
    }

    if (enabled == !disabled) {
        return fail(Vanta::tr("%1 is already %2.")
                        .arg(QDir::toNativeSeparators(mod.relativePath), stateName(enabled)));
    }

    QString targetName;
    if (enabled)
        targetName = fileName.left(fileName.size() - 9);
    else
        targetName = fileName + QStringLiteral(".disabled");

    const QString targetPath = sourceInfo.absoluteDir().filePath(targetName);
    if (QFileInfo::exists(targetPath)) {
        return fail(Vanta::tr("Cannot rename %1 because %2 already exists.")
                        .arg(QDir::toNativeSeparators(sourceInfo.absoluteFilePath()),
                             QDir::toNativeSeparators(targetName)));
    }

    QFile file(sourceInfo.absoluteFilePath());
    if (!file.rename(targetPath)) {
        return fail(Vanta::tr("Could not rename %1 to %2: %3")
                        .arg(QDir::toNativeSeparators(sourceInfo.absoluteFilePath()),
                             QDir::toNativeSeparators(targetName), file.errorString()));
    }
    return true;
}
