#pragma once

#include "Common.h"
#include "LibraryImporter.h"

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class GameLibrary;
class ThemeManager;

class LibraryImportDialog : public VantaDialog
{
    Q_OBJECT

public:
    explicit LibraryImportDialog(GameLibrary *library, ThemeManager *theme, QWidget *parent = nullptr);

signals:
    void gamesImported(int count);
    void gamesImportedList(const QList<Game> &games);

private:
    void buildUi();
    void populateGames();
    void selectAll(bool select);
    void importSelected();
    void refreshIcons();

    GameLibrary *m_library = nullptr;
    ThemeManager *m_theme = nullptr;
    QList<ImportedGameCandidate> m_candidates;

    QLabel *m_summaryLabel = nullptr;
    QTreeWidget *m_tree = nullptr;
    QPushButton *m_selectAllButton = nullptr;
    QPushButton *m_deselectAllButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_closeButton = nullptr;
};
