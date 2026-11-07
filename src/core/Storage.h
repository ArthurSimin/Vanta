#pragma once

#include "Game.h"
#include "Scanner.h"

#include <QHash>
#include <QList>
#include <QString>

using ScanCache = QHash<QString, ScanResult>;

class Storage
{
public:
    Storage();

    QString configDirectory() const { return m_configDirectory; }
    QString cacheDirectory() const { return m_cacheDirectory; }
    QString libraryPath() const;
    QString settingsPath() const;
    QString scanCachePath() const;

    QList<Game> loadLibrary(QStringList *warnings = nullptr);
    bool saveLibrary(const QList<Game> &games, QString *error = nullptr);
    AppSettings loadSettings(QStringList *warnings = nullptr);
    bool saveSettings(const AppSettings &settings, QString *error = nullptr);
    ScanCache loadScanCache(QStringList *warnings = nullptr);
    bool saveScanCache(const ScanCache &cache, QString *error = nullptr);
    void clearScanCache();

    static bool writeJsonAtomically(const QString &path, const QJsonObject &object, QString *error);
    static QJsonObject readJsonObject(const QString &path, bool *ok, QString *error);
    static void quarantineCorruptFile(const QString &path);

private:
    static void ensureDirectory(const QString &path);
    static bool writeBytesAtomically(const QString &path, const QByteArray &data, QString *error);

    QString m_configDirectory;
    QString m_cacheDirectory;
};
