#pragma once

#include "Game.h"

#include <QList>
#include <QString>

struct RunnerEntry
{
    RunnerType type = RunnerType::Native;
    // Display label, which carries the source, for example
    // "proton-cachyos (Steam)".
    QString name;
    // The build name alone, for example "proton-cachyos". A recorded version
    // from another launcher names the build, not the display label.
    QString buildName;
    QString path;
    bool custom = false;
};

class RunnerDetector
{
public:
    static QList<RunnerEntry> wineEntries(const AppSettings &settings);
    static QList<RunnerEntry> protonEntries(const AppSettings &settings);
    static QList<RunnerEntry> allEntries(const AppSettings &settings);
    static QString wineExecutable(const AppSettings &settings);
    static QString protonExecutable(const QString &configuredPath);
    static QString protonBaseDirectory(const QString &configuredPath);
    static bool validateWine(const QString &path, QString *reason = nullptr);
    static bool validateProton(const QString &path, QString *reason = nullptr);
    static QString wineVersion(const QString &path);
    static QString protonVersion(const QString &path);
    static QStringList protonSearchDirectories();
};
