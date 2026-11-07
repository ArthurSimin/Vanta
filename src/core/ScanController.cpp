#include "ScanController.h"
#include "Library.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>

ScanController::ScanController(Storage &storage, GameLibrary &library, QObject *parent)
    : QObject(parent)
    , m_storage(storage)
    , m_library(library)
{
}

ScanController::~ScanController()
{
    const auto watchers = m_watchers;
    for (const QPointer<QFutureWatcher<ScanResult>> &watcher : watchers) {
        if (!watcher)
            continue;
        watcher->disconnect(this);
        watcher->deleteLater();
    }
    m_watchers.clear();
}

void ScanController::loadCache()
{
    QStringList warnings;
    const ScanCache stored = m_storage.loadScanCache(&warnings);
    for (const QString &warning : warnings)
        emit this->warning(warning);

    m_cache.clear();
    m_results.clear();
    for (const Game &game : m_library.games()) {
        const auto it = stored.constFind(game.id);
        if (it == stored.constEnd())
            continue;
        if (!cacheIsValidFor(game, it.value()))
            continue;
        m_cache.insert(game.id, it.value());
        m_results.insert(game.id, it.value());
    }
}

const ScanResult *ScanController::resultFor(const QString &gameId) const
{
    const auto it = m_results.constFind(gameId);
    if (it == m_results.constEnd())
        return nullptr;
    return &it.value();
}

bool ScanController::hasCachedResult(const QString &gameId) const
{
    return m_results.contains(gameId);
}

bool ScanController::isScanning(const QString &gameId) const
{
    return m_watchers.contains(gameId);
}

bool ScanController::cacheIsValidFor(const Game &game, const ScanResult &result) const
{
    const QString directory = game.effectiveDirectory();
    if (directory.isEmpty() || !result.cacheMatches(directory))
        return false;
    const QString gameExecutable = game.executable.isEmpty()
        ? QString() : QFileInfo(game.executable).absoluteFilePath();
    const QString scannedExecutable = result.executablePath.isEmpty()
        ? QString() : QFileInfo(result.executablePath).absoluteFilePath();
    return gameExecutable == scannedExecutable;
}

void ScanController::scanGame(const QString &gameId, bool force)
{
    bool found = false;
    const Game game = m_library.gameById(gameId, &found);
    if (!found || game.effectiveDirectory().isEmpty())
        return;
    if (m_watchers.contains(gameId))
        return;

    if (!force) {
        const auto cached = m_cache.constFind(gameId);
        if (cached != m_cache.constEnd() && cacheIsValidFor(game, cached.value())) {
            m_results.insert(gameId, cached.value());
            emit scanFinished(gameId, cached.value());
            return;
        }
    }

    startScan(game);
}

void ScanController::scanLibrary(bool force)
{
    const QList<Game> games = m_library.games();
    for (const Game &game : games) {
        if (game.effectiveDirectory().isEmpty())
            continue;
        scanGame(game.id, force);
    }
}

void ScanController::startScan(const Game &game)
{
    ScanOptions options;
    options.maxDepth = m_library.settings().scanDepth;
    options.entryLimit = m_library.settings().scanEntryLimit;

    const QString gameId = game.id;
    auto *watcher = new QFutureWatcher<ScanResult>(this);
    m_watchers.insert(gameId, watcher);
    connect(watcher, &QFutureWatcher<ScanResult>::finished, this, [this, gameId] {
        finishScan(gameId);
    });
    emit scanStarted(gameId);

    const QFuture<ScanResult> future = QtConcurrent::run([game, options] {
        return GameScanner::scan(game.executable, game.gameDirectory, game.winePrefix,
                                 game.runner, options);
    });
    watcher->setFuture(future);
}

void ScanController::finishScan(const QString &gameId)
{
    const auto watcherIt = m_watchers.find(gameId);
    if (watcherIt == m_watchers.end())
        return;
    const QPointer<QFutureWatcher<ScanResult>> watcher = watcherIt.value();
    m_watchers.erase(watcherIt);
    if (!watcher)
        return;
    if (!watcher->isFinished()) {
        watcher->deleteLater();
        return;
    }
    if (!m_library.hasGame(gameId)) {
        watcher->deleteLater();
        return;
    }

    ScanResult result = watcher->result();
    watcher->deleteLater();
    if (!result.scannedAt.isValid())
        result.scannedAt = QDateTime::currentDateTime();
    m_results.insert(gameId, result);
    m_cache.insert(gameId, result);
    saveCache();
    emit scanFinished(gameId, result);
}

void ScanController::invalidate(const QString &gameId)
{
    m_results.remove(gameId);
    m_cache.remove(gameId);
    const auto it = m_watchers.find(gameId);
    if (it != m_watchers.end()) {
        if (QPointer<QFutureWatcher<ScanResult>> watcher = it.value()) {
            watcher->disconnect(this);
            watcher->deleteLater();
        }
        m_watchers.erase(it);
    }
    saveCache();
}

void ScanController::clearCache()
{
    m_results.clear();
    m_cache.clear();
    m_storage.clearScanCache();
}

void ScanController::saveCache()
{
    QString error;
    if (!m_storage.saveScanCache(m_cache, &error) && !error.isEmpty())
        emit warning(error);
}
