#include "LibraryView.h"
#include "Common.h"
#include "GameGridView.h"
#include "GameListView.h"
#include "IconFactory.h"
#include "ThemeManager.h"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

LibraryView::LibraryView(ThemeManager *theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("libraryView"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("libraryStack"));
    root->addWidget(m_stack, 1);

    m_grid = new GameGridView(theme, m_stack);
    m_list = new GameListView(theme, m_stack);
    m_stack->addWidget(m_grid);
    m_stack->addWidget(m_list);

    m_emptyPage = new QWidget(m_stack);
    m_emptyPage->setObjectName(QStringLiteral("libraryEmptyPage"));
    auto *emptyLayout = new QVBoxLayout(m_emptyPage);
    emptyLayout->setContentsMargins(32, 32, 32, 32);
    emptyLayout->addStretch(1);

    auto *card = new QFrame(m_emptyPage);
    card->setObjectName(QStringLiteral("emptyState"));
    card->setMaximumWidth(540);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(38, 36, 38, 36);
    cardLayout->setSpacing(11);
    cardLayout->setAlignment(Qt::AlignCenter);

    m_emptyLogo = new QLabel(card);
    m_emptyLogo->setPixmap(Icons::logoPixmap(82));
    m_emptyLogo->setAlignment(Qt::AlignCenter);
    m_emptyLogo->setFixedHeight(82);
    cardLayout->addWidget(m_emptyLogo, 0, Qt::AlignHCenter);

    m_emptyTitle = new QLabel(card);
    QFont titleFont = m_emptyTitle->font();
    titleFont.setPixelSize(23);
    titleFont.setWeight(QFont::Black);
    m_emptyTitle->setFont(titleFont);
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_emptyTitle);

    m_emptyMessage = new QLabel(card);
    m_emptyMessage->setProperty("class", QStringLiteral("muted"));
    m_emptyMessage->setWordWrap(true);
    m_emptyMessage->setAlignment(Qt::AlignCenter);
    m_emptyMessage->setFixedWidth(420);
    QSizePolicy messagePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    messagePolicy.setHeightForWidth(true);
    m_emptyMessage->setSizePolicy(messagePolicy);
    cardLayout->addWidget(m_emptyMessage, 0, Qt::AlignHCenter);

    m_emptyButton = new QPushButton(QStringLiteral("Add game"), card);
    m_emptyButton->setCursor(Qt::PointingHandCursor);
    m_emptyButton->setMinimumWidth(150);
    UiHelp::makePrimary(m_emptyButton);
    cardLayout->addSpacing(7);
    cardLayout->addWidget(m_emptyButton, 0, Qt::AlignHCenter);

    emptyLayout->addWidget(card, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(1);
    m_stack->addWidget(m_emptyPage);

    connect(m_grid, &GameGridView::gameSelected, this, [this](const QString &id) {
        m_selectedId = id;
        m_list->setSelectedGame(id);
        emit gameSelected(id);
    });
    connect(m_grid, &GameGridView::gameActivated, this, &LibraryView::gameActivated);
    connect(m_grid, &GameGridView::contextMenuRequested, this, &LibraryView::contextMenuRequested);
    connect(m_list, &GameListView::gameSelected, this, [this](const QString &id) {
        m_selectedId = id;
        m_grid->setSelectedGame(id);
        emit gameSelected(id);
    });
    connect(m_list, &GameListView::gameActivated, this, &LibraryView::gameActivated);
    connect(m_list, &GameListView::contextMenuRequested, this, &LibraryView::contextMenuRequested);
    connect(m_emptyButton, &QPushButton::clicked, this, [this] {
        if (m_emptyOffersAdd)
            emit addRequested();
        else
            emit clearFiltersRequested();
    });

    showEmptyState(QStringLiteral("No games yet"),
                   QStringLiteral("Add a game folder or executable to start building your Vanta library."),
                   true);
    refreshTheme();
}

void LibraryView::setGames(const QList<Game> &games)
{
    m_games = games;
    if (!games.isEmpty())
        m_showEmpty = false;
    m_grid->setGames(games);
    m_list->setGames(games);
    updatePage();
}

void LibraryView::setViewMode(ViewMode mode)
{
    m_viewMode = mode;
    updatePage();
}

void LibraryView::setCoverSize(CoverSize size)
{
    m_grid->setCoverSize(size);
}

void LibraryView::setShowBadges(bool show)
{
    m_grid->setShowBadges(show);
}

void LibraryView::setAnimationsEnabled(bool enabled)
{
    m_grid->setAnimationsEnabled(enabled);
}

void LibraryView::setSelectedGame(const QString &gameId)
{
    m_selectedId = gameId;
    m_grid->setSelectedGame(gameId);
    m_list->setSelectedGame(gameId);
}

void LibraryView::setRunningGames(const QStringList &gameIds)
{
    m_grid->setRunningGames(gameIds);
}

void LibraryView::showEmptyState(const QString &title, const QString &message, bool offerAdd)
{
    m_emptyTitle->setText(title);
    m_emptyMessage->setText(message);
    m_emptyOffersAdd = offerAdd;
    m_emptyButton->setText(offerAdd ? QStringLiteral("Add game") : QStringLiteral("Clear filters"));
    m_emptyButton->setProperty("primary", offerAdd);
    m_emptyButton->style()->unpolish(m_emptyButton);
    m_emptyButton->style()->polish(m_emptyButton);
    if (m_theme) {
        Icons::setButtonIcon(m_emptyButton, offerAdd ? Glyph::Plus : Glyph::Close,
                             offerAdd ? QColor(Qt::white) : m_theme->colors().text, 16);
    }
    m_showEmpty = true;
    updatePage();
}

void LibraryView::showResults()
{
    m_showEmpty = false;
    updatePage();
}

void LibraryView::refreshTheme()
{
    m_grid->refreshTheme();
    m_list->refreshTheme();
    if (!m_theme)
        return;
    m_emptyLogo->setPixmap(Icons::logoPixmap(82));
    Icons::setButtonIcon(m_emptyButton, m_emptyOffersAdd ? Glyph::Plus : Glyph::Close,
                         m_emptyOffersAdd ? QColor(Qt::white) : m_theme->colors().text, 16);
}

void LibraryView::updatePage()
{
    if (m_games.isEmpty() || m_showEmpty) {
        if (m_games.isEmpty() && !m_showEmpty) {
            m_emptyTitle->setText(QStringLiteral("No games match your filters"));
            m_emptyMessage->setText(QStringLiteral("Try a different search, or clear the active filters to see your whole library."));
            m_emptyOffersAdd = false;
            m_emptyButton->setText(QStringLiteral("Clear filters"));
            m_emptyButton->setProperty("primary", false);
            m_emptyButton->style()->unpolish(m_emptyButton);
            m_emptyButton->style()->polish(m_emptyButton);
            if (m_theme)
                Icons::setButtonIcon(m_emptyButton, Glyph::Close, m_theme->colors().text, 16);
        }
        m_stack->setCurrentWidget(m_emptyPage);
        return;
    }
    m_stack->setCurrentWidget(m_viewMode == ViewMode::List ? static_cast<QWidget *>(m_list)
                                                           : static_cast<QWidget *>(m_grid));
}
