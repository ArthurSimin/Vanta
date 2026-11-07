#pragma once

#include "Game.h"
#include "Scanner.h"
#include "Storage.h"

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>

class GameLibrary;
class Storage;

class ScanController : public QObject
{
    Q_OBJECT

public:
    ScanController(Storage &storage, GameLibrary &library, QObject *parent = nullptr);
    ~ScanController() override;

    void loadCache();
    const ScanResult *resultFor(const QString &gameId) const;
    bool hasCachedResult(const QString &gameId) const;
    bool isScanning(const QString &gameId) const;
    int activeScanCount() const { return m_watchers.size(); }

    void scanGame(const QString &gameId, bool force = false);
    void scanLibrary(bool force = false);
    void invalidate(const QString &gameId);
    void clearCache();
    void saveCache();

signals:
    void scanStarted(const QString &gameId);
    void scanFinished(const QString &gameId, const ScanResult &result);
    void warning(const QString &message);

private:
    bool cacheIsValidFor(const Game &game, const ScanResult &result) const;
    void startScan(const Game &game);
    void finishScan(const QString &gameId);

    Storage &m_storage;
    GameLibrary &m_library;
    ScanCache m_cache;
    QHash<QString, ScanResult> m_results;
    QHash<QString, QPointer<QFutureWatcher<ScanResult>>> m_watchers;
};
