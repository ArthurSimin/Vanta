#pragma once

#include "Common.h"
#include "CoverFetcher.h"
#include "Game.h"

#include <QList>

class CoverFetcher;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class ThemeManager;

class AddEditDialog : public VantaDialog
{
    Q_OBJECT

public:
    AddEditDialog(const Game &game, bool adding, const AppSettings &settings,
                  ThemeManager *theme, QWidget *parent = nullptr);

    Game resultGame() const { return m_game; }
    bool isAdding() const { return m_adding; }

private:
    void buildGeneralTab();
    void buildRunnerTab();
    void buildOnlineFixTab();
    void buildEnvironmentTab();
    void loadFromGame();
    void updateRunnerUi();
    void populateBuildCombo(RunnerType runner);
    void onBuildChanged();
    void applyBuildSelectionUi();
    void refreshDetection(bool applyDetectedValues);
    void browseExecutable();
    void browseWorkingDirectory();
    void browseCover();
    void fetchCover();
    void applyDetectedRunner(const QString &executable);
    void browseRunnerPath();
    void browsePrefix();
    void validateAndAccept();
    void showError(const QString &message, int tabIndex);
    QStringList splitCommandSafely(const QString &text) const;

    Game m_game;
    bool m_adding = false;
    AppSettings m_settings;

    QTabWidget *m_tabs = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_saveButton = nullptr;

    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_sourceCombo = nullptr;
    QLineEdit *m_executableEdit = nullptr;
    QLineEdit *m_argumentsEdit = nullptr;
    QLineEdit *m_workingDirectoryEdit = nullptr;
    QLabel *m_detectedRunner = nullptr;
    QLineEdit *m_coverEdit = nullptr;
    QPushButton *m_fetchCoverButton = nullptr;
    QLabel *m_coverStatus = nullptr;
    CoverFetcher *m_coverFetcher = nullptr;
    QList<CoverArt> m_pendingArtworks;
    QCheckBox *m_favoriteCheck = nullptr;

    QComboBox *m_runnerCombo = nullptr;
    QComboBox *m_platformCombo = nullptr;
    QComboBox *m_runnerBuildCombo = nullptr;
    QLabel *m_runnerBuildLabel = nullptr;
    QLineEdit *m_runnerPathEdit = nullptr;
    QPushButton *m_runnerBrowseButton = nullptr;
    QLineEdit *m_prefixEdit = nullptr;
    QPushButton *m_prefixBrowseButton = nullptr;
    QCheckBox *m_wineD3DCheck = nullptr;
    QCheckBox *m_nativeWaylandCheck = nullptr;
    QCheckBox *m_steamOverlayCheck = nullptr;
    QCheckBox *m_steamRuntimeCheck = nullptr;
    QCheckBox *m_bepInExCheck = nullptr;
    QLineEdit *m_overridesEdit = nullptr;
    QLineEdit *m_preArgumentsEdit = nullptr;
    QLabel *m_runnerHelp = nullptr;

    QComboBox *m_onlineFixModeCombo = nullptr;
    QLabel *m_detectionStatus = nullptr;
    QLabel *m_detectionDetails = nullptr;
    QLabel *m_detectionHint = nullptr;
    QLineEdit *m_realAppIdEdit = nullptr;
    QLineEdit *m_fakeAppIdEdit = nullptr;
    QLineEdit *m_fixPathEdit = nullptr;
    QLineEdit *m_photonDirectoryEdit = nullptr;

    QPlainTextEdit *m_environmentEdit = nullptr;
};
