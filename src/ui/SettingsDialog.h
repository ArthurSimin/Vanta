#pragma once

#include "Common.h"
#include "Game.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class ThemeManager;

class SettingsDialog : public VantaDialog
{
    Q_OBJECT

public:
    SettingsDialog(const AppSettings &settings, ThemeManager *theme, QWidget *parent = nullptr);

    AppSettings resultSettings() const { return m_settings; }
    bool replayTutorialRequested() const { return m_replayTutorial; }

signals:
    void runnersChanged();

private:
    void buildUi();
    void loadFromSettings();
    void saveAndAccept();
    void browseWine();
    void addProtonPath();
    void removeProtonPath();
    void openRunnersDialog();
    void refreshIcons();
    void applyThemePreview();
    void applyThemeChoice(ThemeChoice choice);
    void refreshRunnerSummary();

    AppSettings m_settings;
    ThemeManager *m_theme = nullptr;
    ThemeChoice m_originalTheme = ThemeChoice::System;
    bool m_replayTutorial = false;

    QComboBox *m_themeCombo = nullptr;
    QCheckBox *m_animationsCheck = nullptr;
    QComboBox *m_coverSizeCombo = nullptr;
    QComboBox *m_viewModeCombo = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QCheckBox *m_ascendingCheck = nullptr;
    QCheckBox *m_badgesCheck = nullptr;
    QCheckBox *m_rememberSidebarCheck = nullptr;
    QCheckBox *m_rememberGroupsCheck = nullptr;
    QCheckBox *m_autoScanCheck = nullptr;
    QCheckBox *m_rescanCheck = nullptr;
    QSpinBox *m_depthSpin = nullptr;
    QSpinBox *m_limitSpin = nullptr;
    QComboBox *m_onlineFixPromptCombo = nullptr;
    QCheckBox *m_requireSteamCheck = nullptr;
    QLineEdit *m_winePathEdit = nullptr;
    QListWidget *m_protonList = nullptr;
    QLabel *m_runnerSummary = nullptr;
    QLabel *m_dataPaths = nullptr;
    QCheckBox *m_fetchCoversCheck = nullptr;
    QLineEdit *m_coverApiKeyEdit = nullptr;
    QPushButton *m_replayButton = nullptr;
    QPushButton *m_runnersButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_saveButton = nullptr;
};
