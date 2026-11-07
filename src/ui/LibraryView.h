#pragma once

#include "Game.h"

#include <QList>
#include <QStringList>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class ThemeManager;
class GameGridView;
class GameListView;

class LibraryView : public QWidget
{
    Q_OBJECT

public:
    explicit LibraryView(ThemeManager *theme, QWidget *parent = nullptr);

    void setGames(const QList<Game> &games);
    void setViewMode(ViewMode mode);
    void setCoverSize(CoverSize size);
    void setShowBadges(bool show);
    void setAnimationsEnabled(bool enabled);
    void setSelectedGame(const QString &gameId);
    void setRunningGames(const QStringList &gameIds);
    void showEmptyState(const QString &title, const QString &message, bool offerAdd);
    void showResults();
    void refreshTheme();

    QString selectedGameId() const { return m_selectedId; }
    ViewMode viewMode() const { return m_viewMode; }

signals:
    void gameSelected(const QString &gameId);
    void gameActivated(const QString &gameId);
    void contextMenuRequested(const QString &gameId, const QPoint &globalPosition);
    void addRequested();
    void clearFiltersRequested();

private:
    void updatePage();

    ThemeManager *m_theme = nullptr;
    QStackedWidget *m_stack = nullptr;
    GameGridView *m_grid = nullptr;
    GameListView *m_list = nullptr;
    QWidget *m_emptyPage = nullptr;
    QLabel *m_emptyLogo = nullptr;
    QLabel *m_emptyTitle = nullptr;
    QLabel *m_emptyMessage = nullptr;
    QPushButton *m_emptyButton = nullptr;
    QList<Game> m_games;
    QString m_selectedId;
    ViewMode m_viewMode = ViewMode::Grid;
    bool m_showEmpty = false;
    bool m_emptyOffersAdd = true;
};
