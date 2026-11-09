#pragma once

#include "Game.h"

#include <QHash>
#include <QList>
#include <QScrollArea>
#include <QStringList>
#include <QWidget>

class GameCard;
class QGridLayout;
class QWidget;
class ThemeManager;

class GameGridView : public QScrollArea
{
    Q_OBJECT

public:
    explicit GameGridView(ThemeManager *theme, QWidget *parent = nullptr);

    void setGames(const QList<Game> &games);
    void setSelectedGame(const QString &gameId);
    void setRunningGames(const QStringList &gameIds);
    void setCoverSize(CoverSize size);
    void setShowBadges(bool show);
    void setAnimationsEnabled(bool enabled);
    void refreshTheme();
    QString selectedGameId() const { return m_selectedId; }

signals:
    void gameSelected(const QString &gameId);
    void gameActivated(const QString &gameId);
    void contextMenuRequested(const QString &gameId, const QPoint &globalPosition);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void relayout();
    void clearCards();
    void moveFocus(const QString &gameId, int key);

    ThemeManager *m_theme = nullptr;
    QWidget *m_content = nullptr;
    QGridLayout *m_grid = nullptr;
    QHash<QString, GameCard *> m_cards;
    QList<GameCard *> m_orderedCards;
    QString m_selectedId;
    QStringList m_runningIds;
    CoverSize m_coverSize = CoverSize::Medium;
    bool m_showBadges = true;
    bool m_animations = true;
    int m_columns = 0;
};
