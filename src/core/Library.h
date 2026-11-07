#pragma once

#include "Game.h"
#include "Storage.h"

#include <QObject>

class GameLibrary : public QObject
{
    Q_OBJECT

public:
    explicit GameLibrary(Storage &storage, QObject *parent = nullptr);

    const QList<Game> &games() const { return m_games; }
    const AppSettings &settings() const { return m_settings; }
    const QStringList &loadWarnings() const { return m_loadWarnings; }

    Game gameById(const QString &id, bool *found = nullptr) const;
    int indexOf(const QString &id) const;
    bool hasGame(const QString &id) const;

    bool addGame(const Game &game);
    bool updateGame(const Game &game);
    bool removeGame(const QString &id);
    bool updateSettings(const AppSettings &settings);

    Storage &storage() { return m_storage; }
    const Storage &storage() const { return m_storage; }

signals:
    void gamesChanged();
    void gameChanged(const QString &id);
    void settingsChanged();
    void saveError(const QString &message);

private:
    bool saveLibrary();
    bool saveSettings();

    Storage &m_storage;
    QList<Game> m_games;
    AppSettings m_settings;
    QStringList m_loadWarnings;
};
