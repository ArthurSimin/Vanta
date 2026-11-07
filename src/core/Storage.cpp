#include "Storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

Storage::Storage()
    : m_configDirectory(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/vanta"))
    , m_cacheDirectory(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/vanta"))
{
    ensureDirectory(m_configDirectory);
    ensureDirectory(m_cacheDirectory);
}

QString Storage::libraryPath() const
{
    return QDir(m_configDirectory).filePath(QStringLiteral("library.json"));
}

QString Storage::settingsPath() const
{
    return QDir(m_configDirectory).filePath(QStringLiteral("settings.json"));
}

QString Storage::scanCachePath() const
{
    return QDir(m_cacheDirectory).filePath(QStringLiteral("scan-cache.json"));
}

void Storage::ensureDirectory(const QString &path)
{
    QDir().mkpath(path);
}

QJsonObject Storage::readJsonObject(const QString &path, bool *ok, QString *error)
{
    if (ok)
        *ok = false;
    QFile file(path);
    if (!file.exists()) {
        if (ok)
            *ok = true;
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not read %1: %2").arg(path, file.errorString());
        return {};
    }
    const QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        if (error)
            *error = QStringLiteral("Could not read %1: %2").arg(path, file.errorString());
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("Invalid JSON in %1: %2").arg(path, parseError.errorString());
        if (!data.trimmed().isEmpty())
            quarantineCorruptFile(path);
        return {};
    }
    if (ok)
        *ok = true;
    return document.object();
}

void Storage::quarantineCorruptFile(const QString &path)
{
    if (!QFileInfo::exists(path))
        return;
    const QString suffix = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString target = path + QStringLiteral(".corrupt-") + suffix;
    QFile::rename(path, target);
}

bool Storage::writeBytesAtomically(const QString &path, const QByteArray &data, QString *error)
{
    ensureDirectory(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Could not open %1 for writing: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(data) != data.size()) {
        if (error)
            *error = QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.flush()) {
        if (error)
            *error = QStringLiteral("Could not flush %1: %2").arg(path, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("Could not commit %1: %2").arg(path, file.errorString());
        return false;
    }
    QFile verify(path);
    if (!verify.open(QIODevice::ReadOnly) || verify.size() != data.size()) {
        if (error)
            *error = QStringLiteral("Verification failed after writing %1").arg(path);
        return false;
    }
    return true;
}

bool Storage::writeJsonAtomically(const QString &path, const QJsonObject &object, QString *error)
{
    const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Indented);
    return writeBytesAtomically(path, data, error);
}

QList<Game> Storage::loadLibrary(QStringList *warnings)
{
    bool ok = false;
    QString error;
    const QJsonObject root = readJsonObject(libraryPath(), &ok, &error);
    if (!ok) {
        if (warnings)
            warnings->append(error);
        return {};
    }
    QList<Game> games;
    if (root.isEmpty())
        return games;
    const QJsonValue gamesValue = root.value(QStringLiteral("games"));
    if (!gamesValue.isArray()) {
        if (!root.isEmpty() && warnings)
            warnings->append(QStringLiteral("Library file has no games array; starting with an empty library."));
        return games;
    }
    const QJsonArray array = gamesValue.toArray();
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        Game game = Game::fromJson(value.toObject());
        if (game.name.trimmed().isEmpty() && game.executable.isEmpty())
            continue;
        if (game.id.isEmpty())
            game.id = Game::create().id;
        games.append(game);
    }
    return games;
}

bool Storage::saveLibrary(const QList<Game> &games, QString *error)
{
    QJsonArray array;
    for (const Game &game : games)
        array.append(game.toJson());
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("games"), array);
    return writeJsonAtomically(libraryPath(), root, error);
}

AppSettings Storage::loadSettings(QStringList *warnings)
{
    bool ok = false;
    QString error;
    const QJsonObject root = readJsonObject(settingsPath(), &ok, &error);
    if (!ok) {
        if (warnings)
            warnings->append(error);
        return {};
    }
    return AppSettings::fromJson(root);
}

bool Storage::saveSettings(const AppSettings &settings, QString *error)
{
    return writeJsonAtomically(settingsPath(), settings.toJson(), error);
}

ScanCache Storage::loadScanCache(QStringList *warnings)
{
    ScanCache cache;
    bool ok = false;
    QString error;
    const QJsonObject root = readJsonObject(scanCachePath(), &ok, &error);
    if (!ok) {
        if (warnings)
            warnings->append(error);
        return cache;
    }
    const QJsonObject scans = root.value(QStringLiteral("scans")).toObject();
    for (auto it = scans.constBegin(); it != scans.constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        const ScanResult result = ScanResult::fromJson(it.value().toObject());
        if (!result.gameDirectory.isEmpty())
            cache.insert(it.key(), result);
    }
    return cache;
}

bool Storage::saveScanCache(const ScanCache &cache, QString *error)
{
    QJsonObject scans;
    for (auto it = cache.constBegin(); it != cache.constEnd(); ++it)
        scans.insert(it.key(), it.value().toJson());
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("scans"), scans);
    return writeJsonAtomically(scanCachePath(), root, error);
}

void Storage::clearScanCache()
{
    QFile::remove(scanCachePath());
}
