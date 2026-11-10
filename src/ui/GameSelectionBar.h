#pragma once

#include "Game.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;
class QVariantAnimation;
class QGraphicsOpacityEffect;
class ThemeManager;

// Bottom detail strip for the game the user has selected. It keeps the common
// actions one click away instead of buried in a context menu: launch, a split
// arrow that opens the same menu as a right-click, and the play statistics.
//
// The strip slides up into place the first time a game is selected, and hides
// again when the selection is cleared. The animation collapses the widget height
// so the library view above it grows back smoothly instead of jumping.
class GameSelectionBar : public QWidget
{
    Q_OBJECT

public:
    explicit GameSelectionBar(ThemeManager *theme, QWidget *parent = nullptr);

    // An empty game id hides the bar.
    void setGame(const Game &game);
    void setRunning(bool running);
    void refreshTheme();
    void retranslate();

    QString gameId() const { return m_game.id; }

    // True when the launch should apply the OnlineFix configuration. Defaults to
    // enabled whenever a fix signature is present, because a game added here is
    // expected to be launched the way its fix expects.
    bool onlineFixEnabled() const;

signals:
    void launchRequested(const QString &gameId, bool applyOnlineFix);
    void stopRequested(const QString &gameId);
    void menuRequested(const QString &gameId, const QPoint &globalPosition);

private:
    void updateContent();
    void updatePrimaryButton();
    void animateReveal(bool visible);

    ThemeManager *m_theme = nullptr;
    Game m_game;
    bool m_running = false;

    QWidget *m_body = nullptr;
    QLabel *m_cover = nullptr;
    QLabel *m_name = nullptr;
    QToolButton *m_fixToggle = nullptr;
    QLabel *m_meta = nullptr;
    QPushButton *m_launchButton = nullptr;
    QToolButton *m_menuButton = nullptr;
    QVariantAnimation *m_reveal = nullptr;
    QGraphicsOpacityEffect *m_opacity = nullptr;
    int m_fullHeight = 0;
};
