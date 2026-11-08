#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// A plugin or patcher DLL discovered inside a BepInEx installation. The path is
// absolute so the mod manager can act on the exact file it listed, while the
// relative path is what the UI shows to avoid leaking a long home directory.
struct BepInExMod
{
    QString name;
    QString path;
    QString relativePath;
    bool patcher = false;
    bool enabled = true;
};

// Result of inspecting one game folder. Detection is read-only: it lists files
// but never changes them. `isValid()` only means a BepInEx folder exists, not
// that the installation is complete or that any mod will work.
struct BepInExInstall
{
    QString root;
    QString gameRoot;
    QString corePath;
    QString pluginsPath;
    QString patchersPath;
    QString generation;
    QStringList doorstopMarkers;
    QList<BepInExMod> mods;
    QStringList warnings;
    bool truncated = false;

    bool isValid() const { return !root.isEmpty(); }
    int enabledModCount() const;
    int disabledModCount() const;
};

namespace BepInEx
{
// Looks for a BepInEx folder in the game directory, next to the executable, and
// in their closest parents. Case-insensitive folder names are accepted because
// archives created on Windows occasionally preserve a different casing.
BepInExInstall detect(const QString &gameDirectory, const QString &executablePath = QString());

// Enables or disables one mod by renaming its DLL between `name.dll` and
// `name.dll.disabled`. The file contents are never read or written. Renames are
// refused outside the detected plugins/patchers folders, for symbolic links, and
// when the target name already exists. On failure the reason is stored in
// `error` and the file is left untouched.
bool setModEnabled(const BepInExInstall &install, const BepInExMod &mod, bool enabled,
                   QString *error);
}
