#pragma once

#include "BepInEx.h"
#include "Common.h"
#include "Game.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class ThemeManager;

// Lists the plugins and patchers inside a detected BepInEx installation and lets
// the user enable or disable each one. The dialog never edits DLL contents:
// toggling a mod renames it between `name.dll` and `name.dll.disabled`, which is
// the same convention BepInEx managers use. The game folder is re-detected on
// every refresh so changes made outside Vanta are reflected instead of being
// overwritten from a stale snapshot.
class ModManagerDialog : public VantaDialog
{
    Q_OBJECT

public:
    ModManagerDialog(const Game &game, ThemeManager *theme, QWidget *parent = nullptr);

signals:
    // Emitted after a mod file was installed or a DLL was enabled/disabled, so
    // the window can persist compatibility settings and rescan the game.
    void modsChanged();
    void communityResolved(const QString &community);

protected:
    // Turns Space on a selected row into the same action as the Toggle button.
    // QTableWidget consumes the key for its own navigation otherwise.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void refresh();
    void refreshIcons();
    void updateDetection();
    void updateTable();
    void setFeedback(const QString &message, bool error);
    void updateSelectionActions();
    void toggleSelected();
    void toggleMod(const QString &path, bool enabled);
    void addLocalMod();
    void openThunderstore();
    void openModFolder();
    QString modFolder() const;
    const BepInExMod *modForPath(const QString &path) const;

    Game m_game;
    ThemeManager *m_theme = nullptr;
    BepInExInstall m_install;
    bool m_populating = false;

    QLabel *m_generationChip = nullptr;
    QLabel *m_enabledChip = nullptr;
    QLabel *m_disabledChip = nullptr;
    QLabel *m_doorstopChip = nullptr;
    QLineEdit *m_rootField = nullptr;
    QLineEdit *m_doorstopField = nullptr;
    QLabel *m_warningLabel = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QLabel *m_feedbackLabel = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_toggleButton = nullptr;
    QPushButton *m_thunderstoreButton = nullptr;
    QPushButton *m_openFolderButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_closeButton = nullptr;
};
