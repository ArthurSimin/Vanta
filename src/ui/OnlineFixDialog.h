#pragma once

#include "Common.h"
#include "Game.h"
#include "OnlineFix.h"

class QComboBox;
class QLineEdit;
class QPushButton;
class ThemeManager;

class OnlineFixDialog : public VantaDialog
{
    Q_OBJECT

public:
    enum class Mode { Manage, LaunchPrompt };
    enum class LaunchChoice { Cancel, Apply, Skip };

    OnlineFixDialog(const Game &game, const OnlineFixInfo &info, Mode mode,
                    ThemeManager *theme, QWidget *parent = nullptr);

    static LaunchChoice promptForLaunch(const Game &game, const OnlineFixInfo &info,
                                        ThemeManager *theme, QWidget *parent = nullptr);

    Game resultGame() const { return m_game; }
    LaunchChoice launchChoice() const { return m_launchChoice; }

private:
    void buildManageUi();
    void buildPromptUi();
    void refreshIcons();
    void saveAndAccept();
    void openFixFolder();
    QString detectionSummary() const;

    Game m_game;
    OnlineFixInfo m_info;
    Mode m_mode = Mode::Manage;
    LaunchChoice m_launchChoice = LaunchChoice::Cancel;
    ThemeManager *m_theme = nullptr;

    QComboBox *m_modeCombo = nullptr;
    QLineEdit *m_realAppIdEdit = nullptr;
    QLineEdit *m_fakeAppIdEdit = nullptr;
    QPushButton *m_openFolderButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_skipButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
};
