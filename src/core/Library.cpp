#include "Library.h"

#include <algorithm>

GameLibrary::GameLibrary(Storage &storage, QObject *parent)
    : QObject(parent)
    , m_storage(storage)
{
    m_games = m_storage.loadLibrary(&m_loadWarnings);
    m_settings = m_storage.loadSettings(&m_loadWarnings);
}

Game GameLibrary::gameById(const QString &id, bool *found) const
{
    if (found)
        *found = false;
    for (const Game &game : m_games) {
        if (game.id == id) {
            if (found)
                *found = true;
            return game;
        }
    }
    return {};
}

int GameLibrary::indexOf(const QString &id) const
{
    for (int index = 0; index < m_games.size(); ++index) {
        if (m_games.at(index).id == id)
            return index;
    }
    return -1;
}

bool GameLibrary::hasGame(const QString &id) const
{
    return indexOf(id) >= 0;
}

bool GameLibrary::addGame(const Game &game)
{
    if (game.id.isEmpty() || hasGame(game.id))
        return false;
    m_games.append(game);
    if (!saveLibrary()) {
        m_games.removeLast();
        return false;
    }
    emit gamesChanged();
    emit gameChanged(game.id);
    return true;
}

int GameLibrary::addGames(const QList<Game> &newGames)
{
    QList<Game> appended;
    for (const Game &game : newGames) {
        if (game.id.isEmpty() || hasGame(game.id))
            continue;
        // hasGame() only sees the library, so a duplicate id inside the incoming
        // batch would otherwise be added twice.
        bool alreadyStaged = false;
        for (const Game &staged : appended) {
            if (staged.id == game.id) {
                alreadyStaged = true;
                break;
            }
        }
        if (alreadyStaged)
            continue;
        appended.append(game);
    }
    if (appended.isEmpty())
        return 0;

    m_games.append(appended);
    if (!saveLibrary()) {
        // Without the rollback the caller would report success and the entries
        // would vanish at the next start, because nothing on disk changed.
        for (int index = 0; index < appended.size(); ++index)
            m_games.removeLast();
        return 0;
    }
    emit gamesChanged();
    return appended.size();
}

bool GameLibrary::updateGame(const Game &game)
{
    const int index = indexOf(game.id);
    if (index < 0)
        return false;
    const Game previous = m_games.at(index);
    Game updated = game;
    updated.modifiedAt = QDateTime::currentDateTime();
    m_games[index] = updated;
    if (!saveLibrary()) {
        m_games[index] = previous;
        return false;
    }
    emit gameChanged(game.id);
    emit gamesChanged();
    return true;
}

bool GameLibrary::removeGame(const QString &id)
{
    const int index = indexOf(id);
    if (index < 0)
        return false;
    const Game removed = m_games.takeAt(index);
    if (!saveLibrary()) {
        m_games.insert(index, removed);
        return false;
    }
    emit gamesChanged();
    return true;
}

bool GameLibrary::updateSettings(const AppSettings &settings)
{
    const AppSettings previous = m_settings;
    m_settings = settings;
    if (!saveSettings()) {
        m_settings = previous;
        return false;
    }
    emit settingsChanged();
    return true;
}

bool GameLibrary::saveLibrary()
{
    QString error;
    if (!m_storage.saveLibrary(m_games, &error)) {
        emit saveError(error.isEmpty() ? QStringLiteral("Could not save the game library.") : error);
        return false;
    }
    return true;
}

bool GameLibrary::saveSettings()
{
    QString error;
    if (!m_storage.saveSettings(m_settings, &error)) {
        emit saveError(error.isEmpty() ? QStringLiteral("Could not save settings.") : error);
        return false;
    }
    return true;
}
