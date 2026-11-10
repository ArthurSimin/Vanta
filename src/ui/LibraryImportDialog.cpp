#include "LibraryImportDialog.h"
#include "I18n.h"
#include "IconFactory.h"
#include "Library.h"
#include "ThemeManager.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

LibraryImportDialog::LibraryImportDialog(GameLibrary *library, ThemeManager *theme, QWidget *parent)
    : VantaDialog(Vanta::tr("Port external libraries"), theme, parent)
    , m_library(library)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("libraryImportDialog"));
    setMinimumSize(780, 560);
    resize(850, 620);

    buildUi();
    populateGames();
    if (m_theme)
        connect(m_theme, &ThemeManager::themeChanged, this, &LibraryImportDialog::refreshIcons);
    refreshIcons();
}

void LibraryImportDialog::buildUi()
{
    auto *layout = bodyLayout();

    layout->addWidget(UiHelp::helpLabel(
        Vanta::tr("Vanta scans your system for games installed through Lutris and Heroic Games Launcher. "
                  "Select which games you want to port into your Vanta library. Games already in your library are marked and skipped."),
        this));

    m_summaryLabel = new QLabel(Vanta::tr("Scanning external libraries…"), this);
    m_summaryLabel->setProperty("class", QStringLiteral("muted"));
    layout->addWidget(m_summaryLabel);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("importTree"));
    m_tree->setHeaderLabels({
        Vanta::tr("Game"),
        Vanta::tr("Source"),
        Vanta::tr("Runner"),
        Vanta::tr("Location / Executable")
    });
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_tree->header()->resizeSection(3, 260);
    layout->addWidget(m_tree, 1);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->setSpacing(8);

    m_selectAllButton = new QPushButton(Vanta::tr("Select all new"), this);
    m_selectAllButton->setCursor(Qt::PointingHandCursor);
    // Enter belongs to Port selected games, not to the first button in the row.
    m_selectAllButton->setAutoDefault(false);
    connect(m_selectAllButton, &QPushButton::clicked, this, [this] { selectAll(true); });
    bottomRow->addWidget(m_selectAllButton);

    m_deselectAllButton = new QPushButton(Vanta::tr("Deselect all"), this);
    m_deselectAllButton->setCursor(Qt::PointingHandCursor);
    m_deselectAllButton->setAutoDefault(false);
    connect(m_deselectAllButton, &QPushButton::clicked, this, [this] { selectAll(false); });
    bottomRow->addWidget(m_deselectAllButton);

    bottomRow->addStretch(1);

    m_closeButton = new QPushButton(Vanta::tr("Cancel"), this);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    m_closeButton->setAutoDefault(false);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);
    bottomRow->addWidget(m_closeButton);

    m_importButton = new QPushButton(Vanta::tr("Port selected games"), this);
    m_importButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makePrimary(m_importButton);
    m_importButton->setDefault(true);
    connect(m_importButton, &QPushButton::clicked, this, &LibraryImportDialog::importSelected);
    bottomRow->addWidget(m_importButton);

    layout->addLayout(bottomRow);
}

void LibraryImportDialog::populateGames()
{
    m_tree->clear();
    m_candidates = LibraryImporter::scanAll(m_library);

    int newCount = 0;
    int alreadyInCount = 0;
    int lutrisCount = 0;
    int heroicCount = 0;

    const ThemeColors &colors = m_theme ? m_theme->colors() : ThemeColors();

    for (int i = 0; i < m_candidates.size(); ++i) {
        const ImportedGameCandidate &cand = m_candidates.at(i);
        auto *item = new QTreeWidgetItem(m_tree);

        item->setText(0, cand.game.name);
        item->setText(1, cand.sourceLauncher);
        item->setText(2, runnerDisplayName(cand.game.runner));

        const QString pathDisplay = !cand.game.executable.isEmpty()
            ? cand.game.executable
            : cand.game.gameDirectory;
        item->setText(3, pathDisplay);
        item->setToolTip(3, pathDisplay);
        item->setData(0, Qt::UserRole, i);

        if (cand.sourceLauncher == QStringLiteral("Lutris"))
            ++lutrisCount;
        else if (cand.sourceLauncher == QStringLiteral("Heroic"))
            ++heroicCount;

        if (cand.alreadyInLibrary) {
            item->setCheckState(0, Qt::Unchecked);
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setForeground(0, colors.muted);
            item->setText(0, Vanta::tr("%1 (Already in library)").arg(cand.game.name));
            ++alreadyInCount;
        } else {
            item->setCheckState(0, Qt::Checked);
            ++newCount;
        }
    }

    QStringList sourceSummaries;
    if (lutrisCount > 0)
        sourceSummaries.append(Vanta::tr("%1 from Lutris").arg(lutrisCount));
    if (heroicCount > 0)
        sourceSummaries.append(Vanta::tr("%1 from Heroic").arg(heroicCount));

    const QString sourcesText = sourceSummaries.isEmpty()
        ? Vanta::tr("no external games found")
        : sourceSummaries.join(QStringLiteral(", "));

    m_summaryLabel->setText(
        Vanta::tr("Found %n external game(s) (%1). %2 ready to port, %3 already present.",
                  nullptr, static_cast<int>(m_candidates.size()))
            .arg(sourcesText,
                 QString::number(newCount),
                 QString::number(alreadyInCount)));

    m_importButton->setEnabled(newCount > 0);
    m_importButton->setText(Vanta::tr("Port %1 games to Vanta").arg(newCount));
}

void LibraryImportDialog::selectAll(bool select)
{
    int checkedCount = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *item = m_tree->topLevelItem(i);
        const int index = item->data(0, Qt::UserRole).toInt();
        if (index >= 0 && index < m_candidates.size() && !m_candidates.at(index).alreadyInLibrary) {
            item->setCheckState(0, select ? Qt::Checked : Qt::Unchecked);
            if (select)
                ++checkedCount;
        }
    }
    m_importButton->setEnabled(checkedCount > 0);
    m_importButton->setText(checkedCount > 0
        ? Vanta::tr("Port %1 games to Vanta").arg(checkedCount)
        : Vanta::tr("Port selected games"));
}

void LibraryImportDialog::importSelected()
{
    if (!m_library)
        return;

    QList<Game> toAdd;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *item = m_tree->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked) {
            const int index = item->data(0, Qt::UserRole).toInt();
            if (index >= 0 && index < m_candidates.size()) {
                const auto &cand = m_candidates.at(index);
                if (!cand.alreadyInLibrary) {
                    toAdd.append(cand.game);
                }
            }
        }
    }

    if (toAdd.isEmpty()) {
        QMessageBox::information(this, Vanta::tr("No games selected"),
                                 Vanta::tr("No new games were selected for porting."));
        return;
    }

    QList<Game> addedGames;
    for (const Game &game : toAdd) {
        if (!game.id.isEmpty() && !m_library->hasGame(game.id)) {
            addedGames.append(game);
        }
    }

    const int imported = m_library->addGames(addedGames);
    if (imported < addedGames.size()) {
        // The porting plan is complete before the write, so a shortfall means
        // the library could not be saved. Saying "ported" for entries that will
        // disappear on the next start would be a lie.
        QMessageBox::warning(
            this, Vanta::tr("Could not save the library"),
            Vanta::tr("%1 of %2 games could not be saved. The library file may be unwritable.")
                .arg(QString::number(addedGames.size() - imported), QString::number(addedGames.size())));
        return;
    }
    emit gamesImported(imported);
    emit gamesImportedList(addedGames);

    QMessageBox::information(
        this,
        Vanta::tr("Port completed"),
        Vanta::tr("Successfully ported %n game(s) into your Vanta library.",
                  nullptr, static_cast<int>(imported)));

    accept();
}

void LibraryImportDialog::refreshIcons()
{
    if (!m_theme)
        return;
    const ThemeColors &colors = m_theme->colors();
    Icons::setButtonIcon(m_importButton, Glyph::Plus, QColor(Qt::white), 16);
    Icons::setButtonIcon(m_selectAllButton, Glyph::Check, colors.text, 16);
    Icons::setButtonIcon(m_deselectAllButton, Glyph::Close, colors.text, 16);
    Icons::setButtonIcon(m_closeButton, Glyph::Close, colors.text, 16);
}
