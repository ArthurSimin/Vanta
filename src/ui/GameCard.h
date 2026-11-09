#pragma once

#include "Game.h"
#include "Types.h"

#include <QAbstractButton>
#include <QElapsedTimer>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QTimer>

class QPropertyAnimation;
class ThemeManager;

class GameCard : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal selectionProgress READ selectionProgress WRITE setSelectionProgress)

public:
    explicit GameCard(ThemeManager *theme, QWidget *parent = nullptr);

    void setGame(const Game &game);
    const Game &game() const { return m_game; }
    void setRunning(bool running);
    void setSelectedState(bool selected);
    void setCoverSize(CoverSize size);
    void setShowBadges(bool show);
    void setAnimationsEnabled(bool enabled);
    void refreshTheme();

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal value);
    qreal selectionProgress() const { return m_selectionProgress; }
    void setSelectionProgress(qreal value);

    static QSize sizeForCover(CoverSize size);
    QSize sizeHint() const override;

signals:
    void activated(const QString &gameId);
    void contextMenuRequested(const QString &gameId, const QPoint &globalPosition);
    // Emitted for Enter/Space so the grid can treat a card as activated the
    // same way a double click does.
    void keyboardActivated(const QString &gameId);
    // Arrow keys move focus between cards; the grid owns the layout and
    // therefore the neighbour calculation.
    void focusMoveRequested(const QString &gameId, int key);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void animateHover(qreal target);
    void animateSelection(qreal target);
    void updateFixedSize();
    void updatePlaceholder();
    QRect coverRect() const;
    void drawBadge(QPainter &painter, const QRect &rect, const QString &text,
                   const QColor &background, const QColor &foreground) const;

    ThemeManager *m_theme = nullptr;
    Game m_game;
    bool m_selected = false;
    bool m_running = false;
    bool m_showBadges = true;
    bool m_animations = true;
    CoverSize m_coverSize = CoverSize::Medium;
    qreal m_hoverProgress = 0.0;
    qreal m_selectionProgress = 0.0;
    QPropertyAnimation *m_hoverAnimation = nullptr;
    QPropertyAnimation *m_selectionAnimation = nullptr;
    QTimer m_pulseTimer;
    QElapsedTimer m_runClock;
    QPixmap m_placeholder;
};
