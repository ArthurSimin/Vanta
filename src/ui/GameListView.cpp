#include "GameListView.h"
#include "IconFactory.h"
#include "ThemeManager.h"

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QPainter>
#include <QPixmap>
#include <QTableWidgetItem>

GameListView::GameListView(ThemeManager *theme, QWidget *parent)
    : QTableWidget(parent)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("gameList"));
    setColumnCount(6);
    setHorizontalHeaderLabels({
        QStringLiteral("Game"), QStringLiteral("Runner"), QStringLiteral("Platform"),
        QStringLiteral("Compatibility"), QStringLiteral("Last played"), QStringLiteral("Playtime")
    });
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setAlternatingRowColors(true);
    setShowGrid(false);
    setWordWrap(false);
    setIconSize(QSize(40, 54));
    verticalHeader()->setVisible(false);
    verticalHeader()->setDefaultSectionSize(62);
    horizontalHeader()->setHighlightSections(false);
    horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    horizontalHeader()->setStretchLastSection(false);
    horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    setSortingEnabled(false);
    setFocusPolicy(Qt::StrongFocus);

    connect(this, &QTableWidget::itemSelectionChanged, this, [this] {
        if (m_populating)
            return;
        const QString id = selectedGameId();
        if (!id.isEmpty())
            emit gameSelected(id);
    });
    connect(this, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *item) {
        if (!item)
            return;
        const QString id = idForRow(item->row());
        if (!id.isEmpty())
            emit gameActivated(id);
    });
}

QString GameListView::idForRow(int row) const
{
    QTableWidgetItem *item = this->item(row, 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

QString GameListView::selectedGameId() const
{
    const auto rows = selectionModel()->selectedRows();
    if (rows.isEmpty())
        return {};
    return idForRow(rows.first().row());
}

QString GameListView::displayName(const Game &game, bool running) const
{
    QString name = game.favorite ? QStringLiteral("★  %1").arg(game.name) : game.name;
    if (running)
        name.prepend(QStringLiteral("●  "));
    return name;
}

QString GameListView::compatibilityText(const Game &game) const
{
    if ((game.compatibility == ScanState::ComponentsDetected || scanNeedsAttention(game.compatibility))
        && game.showsOnlineFix()) {
        return QStringLiteral("OnlineFix detected");
    }
    return scanStateLabel(game.compatibility);
}

void GameListView::setGames(const QList<Game> &games)
{
    const QString selected = selectedGameId();
    m_populating = true;
    m_games = games;
    clearContents();
    setRowCount(games.size());
    const ThemeColors &colors = m_theme ? m_theme->colors() : ThemeColors();
    for (int row = 0; row < games.size(); ++row) {
        const Game &game = games.at(row);
        // The grid card shows a pulsing Running chip, so the same game must not
        // look idle here. A dot keeps the sort order and the column widths as
        // they were; the tooltip spells the state out.
        const bool running = m_runningIds.contains(game.id);
        auto *nameItem = new QTableWidgetItem(displayName(game, running));
        nameItem->setData(Qt::UserRole, game.id);
        nameItem->setToolTip(running ? QStringLiteral("%1\nThis game is running.").arg(game.name)
                                     : game.name);
        const QSize iconSize(40, 54);
        const qreal dpr = devicePixelRatioF();
        const QSize pixels(qRound(iconSize.width() * dpr), qRound(iconSize.height() * dpr));
        nameItem->setIcon(Icons::coverIcon(game.coverPath, pixels, initialsForGame(game.name),
                                           colors.accentSoft, colors.accent));
        QFont nameFont = nameItem->font();
        nameFont.setWeight(QFont::DemiBold);
        nameItem->setFont(nameFont);
        setItem(row, 0, nameItem);

        auto *runnerItem = new QTableWidgetItem(runnerName(game.runner));
        auto *platformItem = new QTableWidgetItem(platformName(game.platform));
        auto *compatItem = new QTableWidgetItem(compatibilityText(game));
        auto *playedItem = new QTableWidgetItem(formatLastPlayed(game.lastPlayed));
        auto *playtimeItem = new QTableWidgetItem(formatPlaytime(game.playtimeSeconds));
        playtimeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        playedItem->setForeground(palette().color(QPalette::Text));
        setItem(row, 1, runnerItem);
        setItem(row, 2, platformItem);
        setItem(row, 3, compatItem);
        setItem(row, 4, playedItem);
        setItem(row, 5, playtimeItem);
        if (running)
            nameItem->setForeground(colors.success);
        if (!game.executableExists())
            compatItem->setToolTip(QStringLiteral("The configured executable is missing."));
        else if (game.showsOnlineFix() && game.compatibility != ScanState::NoKnownComponents
                 && game.compatibility != ScanState::NotScanned)
            compatItem->setToolTip(QStringLiteral("Filename signatures associated with OnlineFix were detected. Detection is not proof that a copy is functional."));
        compatItem->setForeground(scanNeedsAttention(game.compatibility)
            ? colors.warning : palette().color(QPalette::Text));
        setRowHeight(row, 62);
    }
    verticalHeader()->setDefaultSectionSize(62);
    if (!selected.isEmpty()) {
        for (int row = 0; row < rowCount(); ++row) {
            if (idForRow(row) == selected) {
                selectRow(row);
                break;
            }
        }
    }
    m_populating = false;
}

void GameListView::setRunningGames(const QStringList &gameIds)
{
    if (m_runningIds == gameIds)
        return;
    m_runningIds = gameIds;
    // Rebuilding the table would reset the scroll position, so the marker is
    // applied to the rows that are already there.
    const ThemeColors &colors = m_theme ? m_theme->colors() : ThemeColors();
    for (int row = 0; row < rowCount(); ++row) {
        QTableWidgetItem *nameItem = item(row, 0);
        if (!nameItem)
            continue;
        const QString id = idForRow(row);
        const bool running = m_runningIds.contains(id);
        const Game *game = nullptr;
        for (const Game &candidate : m_games) {
            if (candidate.id == id) {
                game = &candidate;
                break;
            }
        }
        if (!game)
            continue;
        nameItem->setText(displayName(*game, running));
        nameItem->setToolTip(running ? QStringLiteral("%1\nThis game is running.").arg(game->name)
                                     : game->name);
        nameItem->setForeground(QBrush(running ? colors.success : colors.text));
    }
}

void GameListView::setSelectedGame(const QString &gameId)
{
    if (m_populating)
        return;
    m_populating = true;
    clearSelection();
    if (!gameId.isEmpty()) {
        for (int row = 0; row < rowCount(); ++row) {
            if (idForRow(row) == gameId) {
                selectRow(row);
                break;
            }
        }
    }
    m_populating = false;
}

void GameListView::refreshTheme()
{
    const QString selected = selectedGameId();
    setGames(m_games);
    setSelectedGame(selected);
}

void GameListView::contextMenuEvent(QContextMenuEvent *event)
{
    const QModelIndex index = indexAt(event->pos());
    if (!index.isValid())
        return;
    selectRow(index.row());
    const QString id = idForRow(index.row());
    if (!id.isEmpty())
        emit contextMenuRequested(id, event->globalPos());
    event->accept();
}
