#pragma once

#include "BepInEx.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// Where a mod package should be written. A detected BepInEx installation fills
// every path; for a game with no BepInEx yet, the root is the conventional
// <game>/BepInEx and the installer creates it on demand.
struct ModTarget
{
    QString gameRoot;
    QString bepInExRoot;
    QString pluginsPath;
    QString patchersPath;

    bool isValid() const { return !gameRoot.isEmpty() && !bepInExRoot.isEmpty(); }
};

struct ModInstallFile
{
    // Path relative to the game root, using forward slashes. It is validated to
    // stay inside the game or BepInEx tree before any write.
    QString relativePath;
    QByteArray data;
    bool executable = false;
};

struct ModInstallPlan
{
    QString displayName;
    QString packageName;
    QString version;
    QString sourceLabel;
    QStringList dependencies;
    QList<ModInstallFile> files;
    QStringList skipped;
    QStringList conflicts;
    QStringList warnings;
    qint64 totalBytes = 0;

    bool isValid() const { return !files.isEmpty(); }
};

namespace ModInstaller
{
ModTarget targetFor(const BepInExInstall &install);
ModTarget targetForGame(const QString &gameDirectory);

// Builds a plan from a local .dll or .zip file. Nothing is written.
bool planFromFile(const QString &path, const ModTarget &target, ModInstallPlan *plan, QString *error);

// Builds a plan from bytes already downloaded from Thunderstore. `packageName`
// controls the plugin subfolder used for files that do not carry their own
// BepInEx path.
bool planFromArchive(const QByteArray &archive, const QString &archiveName,
                     const QString &packageName, const ModTarget &target,
                     ModInstallPlan *plan, QString *error);

// Combines the plans for a package and its dependencies into one unit, rejecting
// two different payloads that would land on the same path.
bool mergePlans(const QList<ModInstallPlan> &plans, const ModTarget &target,
                ModInstallPlan *merged, QString *error);

// Writes a plan. Existing files are left alone unless `overwrite` is true; in
// that case each one is copied under BepInEx/.vanta-backups/<timestamp>/ first.
// Nothing is ever deleted. `written` lists completed files even when a later
// write fails.
bool execute(const ModInstallPlan &plan, const ModTarget &target, bool overwrite,
             QStringList *written, QString *backupRoot, QString *error);
}
