#include "GameGridView.h"
#include "GameCard.h"
#include "ThemeManager.h"

#include <QGridLayout>
#include <QLayoutItem>
#include <QResizeEvent>
#include <QScrollBar>

GameGridView::GameGridView(ThemeManager *theme, QWidget *parent)
    : QScrollArea(parent)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("gameGridScroll"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFocusPolicy(Qt::NoFocus);

    m_content = new QWidget(this);
    m_content->setObjectName(QStringLiteral("gridContent"));
    m_grid = new QGridLayout(m_content);
    m_grid->setContentsMargins(14, 14, 14, 18);
    m_grid->setHorizontalSpacing(14);
    m_grid->setVerticalSpacing(16);
    m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    setWidget(m_content);
    viewport()->setAutoFillBackground(false);
}

void GameGridView::clearCards()
{
    for (GameCard *card : m_cards) {
        m_grid->removeWidget(card);
        card->deleteLater();
    }
    m_cards.clear();
    m_orderedCards.clear();
    while (QLayoutItem *item = m_grid->takeAt(0))
        delete item;
}

void GameGridView::setGames(const QList<Game> &games)
{
    clearCards();
    for (const Game &game : games) {
        auto *card = new GameCard(m_theme, m_content);
        card->setGame(game);
        card->setCoverSize(m_coverSize);
        card->setShowBadges(m_showBadges);
        card->setAnimationsEnabled(m_animations);
        card->setSelectedState(game.id == m_selectedId);
        card->setRunning(m_runningIds.contains(game.id));
        connect(card, &GameCard::clicked, this, [this, card] {
            // Going through setSelectedGame is what clears the border on the
            // previously selected card. Assigning m_selectedId directly made the
            // later round-trip early-return, leaving two cards looking selected.
            setSelectedGame(card->game().id);
            emit gameSelected(card->game().id);
        });
        connect(card, &GameCard::activated, this, &GameGridView::gameActivated);
        connect(card, &GameCard::contextMenuRequested, this, &GameGridView::contextMenuRequested);
        m_cards.insert(game.id, card);
        m_orderedCards.append(card);
    }
    m_columns = 0;
    relayout();
    verticalScrollBar()->setValue(0);
}

void GameGridView::setSelectedGame(const QString &gameId)
{
    if (m_selectedId == gameId)
        return;
    m_selectedId = gameId;
    for (auto it = m_cards.begin(); it != m_cards.end(); ++it)
        it.value()->setSelectedState(it.key() == gameId);
}

void GameGridView::setRunningGames(const QStringList &gameIds)
{
    m_runningIds = gameIds;
    for (auto it = m_cards.begin(); it != m_cards.end(); ++it)
        it.value()->setRunning(gameIds.contains(it.key()));
}

void GameGridView::setCoverSize(CoverSize size)
{
    if (m_coverSize == size)
        return;
    m_coverSize = size;
    for (GameCard *card : m_cards)
        card->setCoverSize(size);
    m_columns = 0;
    relayout();
}

void GameGridView::setShowBadges(bool show)
{
    m_showBadges = show;
    for (GameCard *card : m_cards)
        card->setShowBadges(show);
}

void GameGridView::setAnimationsEnabled(bool enabled)
{
    m_animations = enabled;
    for (GameCard *card : m_cards)
        card->setAnimationsEnabled(enabled);
}

void GameGridView::refreshTheme()
{
    for (GameCard *card : m_cards)
        card->refreshTheme();
}

void GameGridView::relayout()
{
    if (!m_grid || m_cards.isEmpty()) {
        if (m_grid)
            m_content->setMinimumHeight(0);
        return;
    }
    const QSize cardSize = GameCard::sizeForCover(m_coverSize);
    const int margin = 28;
    const int spacing = 14;
    const int available = qMax(cardSize.width(), viewport()->width() - margin);
    const int columns = qMax(1, (available + spacing) / (cardSize.width() + spacing));
    m_columns = columns;

    for (int column = 0; column < 24; ++column)
        m_grid->setColumnStretch(column, 0);
    int index = 0;
    for (GameCard *card : m_orderedCards) {
        if (!card)
            continue;

        const int row = index / columns;
        const int column = index % columns;
        m_grid->addWidget(card, row, column, Qt::AlignLeft | Qt::AlignTop);
        ++index;
    }
    m_grid->setColumnStretch(columns, 1);
    const int rows = (m_orderedCards.size() + columns - 1) / columns;
    const int height = 14 + 18 + rows * cardSize.height() + qMax(0, rows - 1) * 16;
    m_content->setMinimumHeight(height);
    m_content->updateGeometry();
}

void GameGridView::resizeEvent(QResizeEvent *event)
{
    QScrollArea::resizeEvent(event);
    relayout();
}
