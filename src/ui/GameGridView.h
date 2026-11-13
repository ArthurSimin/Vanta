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
    // The game ids of the last populated list, in display order, so an
    // unchanged refresh can keep the scroll position instead of resetting it.
    QStringList m_order;
    // A restore that has been queued but has not run yet is the baseline for a
    // refresh that arrives in the meantime, so two refreshes in one turn of the
    // event loop still land on one offset. -1 means nothing is pending.
    int m_pendingScroll = -1;
    bool m_restoreScheduled = false;
    CoverSize m_coverSize = CoverSize::Medium;
    bool m_showBadges = true;
    bool m_animations = true;
    int m_columns = 0;
};
