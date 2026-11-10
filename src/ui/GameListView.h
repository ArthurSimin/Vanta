#pragma once

#include "Game.h"

#include <QTableWidget>
#include <QStringList>

class ThemeManager;

class GameListView : public QTableWidget
{
    Q_OBJECT

public:
    explicit GameListView(ThemeManager *theme, QWidget *parent = nullptr);

    void setGames(const QList<Game> &games);
    void setSelectedGame(const QString &gameId);
    void setRunningGames(const QStringList &gameIds);
    void refreshTheme();
    void retranslate();
    QString selectedGameId() const;

signals:
    void gameSelected(const QString &gameId);
    void gameActivated(const QString &gameId);
    void contextMenuRequested(const QString &gameId, const QPoint &globalPosition);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QString idForRow(int row) const;
    QString displayName(const Game &game, bool running) const;
    QString compatibilityText(const Game &game) const;

    ThemeManager *m_theme = nullptr;
    bool m_populating = false;
    QList<Game> m_games;
    QStringList m_runningIds;
};
