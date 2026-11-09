#include "AddEditDialog.h"
#include "CoverFetcher.h"
#include "CoverPickerDialog.h"
#include "IconFactory.h"
#include "LaunchPlan.h"
#include "RunnerDetector.h"
#include "Scanner.h"
#include "OnlineFix.h"
#include "ThemeManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
// Sentinel data value for the "custom path" entry in the runner build chooser.
const char *const kCustomBuild = "__vanta_custom_runner__";

QScrollArea *makeScrollArea(QWidget *content, QWidget *parent)
{
    auto *area = new QScrollArea(parent);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setWidget(content);
    return area;
}

QWidget *pathRow(QLineEdit **edit, QPushButton **button, const QString &placeholder,
                 const QString &buttonText, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(7);
    auto *line = new QLineEdit(row);
    line->setPlaceholderText(placeholder);
    auto *browse = new QPushButton(buttonText, row);
    browse->setCursor(Qt::PointingHandCursor);
    // Enter in the field beside it belongs to the dialog's primary action, not
    // to Browse. Without this Qt made the first created Browse button the
    // implicit default, so pressing Enter while typing a name opened a file
    // dialog.
    browse->setAutoDefault(false);
    UiHelp::makeFlat(browse);
    browse->setMinimumWidth(82);
    layout->addWidget(line, 1);
    layout->addWidget(browse);
    *edit = line;
    *button = browse;
    return row;
}

QFormLayout *formFor(QWidget *content)
{
    auto *form = new QFormLayout(content);
    form->setContentsMargins(20, 20, 20, 24);
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return form;
}
}

AddEditDialog::AddEditDialog(const Game &game, bool adding, const AppSettings &settings,
                             ThemeManager *theme, QWidget *parent)
    : VantaDialog(adding ? QStringLiteral("Add game") : QStringLiteral("Edit game"), theme, parent)
    , m_game(game)
    , m_adding(adding)
    , m_settings(settings)
{
    setObjectName(QStringLiteral("addEditDialog"));
    setMinimumSize(800, 650);
    resize(860, 750);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(false);
    bodyLayout()->addWidget(m_tabs, 1);

    buildGeneralTab();
    buildRunnerTab();
    buildOnlineFixTab();
    buildEnvironmentTab();

    m_errorLabel = new QLabel(this);
    m_errorLabel->setProperty("class", QStringLiteral("danger"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    bodyLayout()->addWidget(m_errorLabel);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setCursor(Qt::PointingHandCursor);
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);
    m_saveButton = new QPushButton(adding ? QStringLiteral("Add game") : QStringLiteral("Save changes"), this);
    m_saveButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makePrimary(m_saveButton);
    m_saveButton->setDefault(true);
    connect(m_saveButton, &QPushButton::clicked, this, &AddEditDialog::validateAndAccept);
    buttons->addWidget(m_saveButton);
    bodyLayout()->addLayout(buttons);

    loadFromGame();
    updateRunnerUi();

    m_coverFetcher = new CoverFetcher(this);
    m_coverFetcher->setApiKey(m_settings.steamGridDbApiKey);
    connect(m_coverFetcher, &CoverFetcher::resolved, this,
            [this](const QString &, const QString &matchedName, int score) {
                if (m_coverStatus) {
                    m_coverStatus->setProperty("class", QStringLiteral("fieldHelp"));
                    m_coverStatus->setText(
                        QStringLiteral("Matched “%1” (%2% similar). Loading the available artwork…")
                            .arg(matchedName)
                            .arg(score));
                    m_coverStatus->style()->unpolish(m_coverStatus);
                    m_coverStatus->style()->polish(m_coverStatus);
                }
            });
    connect(m_coverFetcher, &CoverFetcher::artworksListed, this,
            [this](const QString &gameId, const QString &matchedName, const QList<CoverArt> &artworks) {
                m_fetchCoverButton->setEnabled(true);
                m_pendingArtworks = artworks;

                CoverPickerDialog picker(m_nameEdit->text().trimmed(), matchedName, artworks,
                                         themeManager(), this);
                connect(m_coverFetcher, &CoverFetcher::thumbnailReady, &picker,
                        &CoverPickerDialog::applyThumbnail);
                // Stream the thumbnails while the dialog is open.
                for (const CoverArt &art : artworks)
                    m_coverFetcher->downloadThumbnail(art.gridId, art.thumbUrl);

                if (picker.exec() != QDialog::Accepted) {
                    if (m_coverStatus) {
                        m_coverStatus->setProperty("class", QStringLiteral("fieldHelp"));
                        m_coverStatus->setText(QStringLiteral("No artwork was selected."));
                        m_coverStatus->style()->unpolish(m_coverStatus);
                        m_coverStatus->style()->polish(m_coverStatus);
                    }
                    return;
                }
                const CoverArt chosen = picker.selectedArt();
                if (chosen.fullUrl.isEmpty())
                    return;
                m_fetchCoverButton->setEnabled(false);
                if (m_coverStatus) {
                    m_coverStatus->setProperty("class", QStringLiteral("fieldHelp"));
                    m_coverStatus->setText(QStringLiteral("Downloading the selected artwork…"));
                    m_coverStatus->style()->unpolish(m_coverStatus);
                    m_coverStatus->style()->polish(m_coverStatus);
                }
                m_coverFetcher->downloadCoverArt(gameId, chosen);
            });
    connect(m_coverFetcher, &CoverFetcher::finished, this, [this](const QString &, const QString &coverPath) {
        m_coverEdit->setText(coverPath);
        m_fetchCoverButton->setEnabled(true);
        if (m_coverStatus) {
            m_coverStatus->setProperty("class", QStringLiteral("success"));
            m_coverStatus->setText(QStringLiteral("Cover saved to %1").arg(QDir::toNativeSeparators(coverPath)));
            m_coverStatus->style()->unpolish(m_coverStatus);
            m_coverStatus->style()->polish(m_coverStatus);
        }
    });
    connect(m_coverFetcher, &CoverFetcher::failed, this, [this](const QString &, const QString &message) {
        m_fetchCoverButton->setEnabled(true);
        if (m_coverStatus) {
            m_coverStatus->setProperty("class", QStringLiteral("warning"));
            m_coverStatus->setText(message);
            m_coverStatus->style()->unpolish(m_coverStatus);
            m_coverStatus->style()->polish(m_coverStatus);
        }
    });

    connect(m_runnerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        updateRunnerUi();
        const auto runner = static_cast<RunnerType>(m_runnerCombo->currentData().toInt());
        if (runner == RunnerType::Wine || runner == RunnerType::Proton) {
            const int index = m_platformCombo->findData(static_cast<int>(Platform::Windows));
            if (index >= 0)
                m_platformCombo->setCurrentIndex(index);
        }
    });
    connect(m_executableEdit, &QLineEdit::editingFinished, this, [this] {
        const QString executable = m_executableEdit->text().trimmed();
        if (!executable.isEmpty()) {
            applyDetectedRunner(executable);
            refreshDetection(false);
        }
    });
    refreshDetection(false);
}

void AddEditDialog::buildGeneralTab()
{
    auto *content = new QWidget;
    auto *form = formFor(content);

    m_nameEdit = new QLineEdit(content);
    m_nameEdit->setPlaceholderText(QStringLiteral("Game title"));
    form->addRow(QStringLiteral("Name"), m_nameEdit);

    m_sourceCombo = new QComboBox(content);
    m_sourceCombo->addItem(QStringLiteral("Manual"), QStringLiteral("Manual"));
    m_sourceCombo->addItem(QStringLiteral("Steam"), QStringLiteral("Steam"));
    m_sourceCombo->addItem(QStringLiteral("GOG"), QStringLiteral("GOG"));
    m_sourceCombo->addItem(QStringLiteral("Epic Games Store"), QStringLiteral("Epic Games Store"));
    m_sourceCombo->addItem(QStringLiteral("EA App"), QStringLiteral("EA App"));
    m_sourceCombo->addItem(QStringLiteral("Ubisoft Connect"), QStringLiteral("Ubisoft Connect"));
    m_sourceCombo->addItem(QStringLiteral("Other"), QStringLiteral("Other"));
    form->addRow(QStringLiteral("Source"), m_sourceCombo);

    // Only an executable is needed: the game folder is always its parent folder,
    // so asking for both was redundant.
    QPushButton *executableButton = nullptr;
    form->addRow(QStringLiteral("Executable"),
                 pathRow(&m_executableEdit, &executableButton,
                         QStringLiteral("Path to the game executable"),
                         QStringLiteral("Browse…"), content));
    connect(executableButton, &QPushButton::clicked, this, &AddEditDialog::browseExecutable);

    m_argumentsEdit = new QLineEdit(content);
    m_argumentsEdit->setPlaceholderText(QStringLiteral("Optional arguments passed after the executable"));
    form->addRow(QStringLiteral("Game arguments"), m_argumentsEdit);

    QPushButton *workingButton = nullptr;
    form->addRow(QStringLiteral("Working folder"),
                 pathRow(&m_workingDirectoryEdit, &workingButton,
                         QStringLiteral("Defaults to the executable's folder"), QStringLiteral("Browse…"), content));
    connect(workingButton, &QPushButton::clicked, this, &AddEditDialog::browseWorkingDirectory);

    m_detectedRunner = UiHelp::helpLabel(QString(), content);
    m_detectedRunner->setVisible(false);
    form->addRow(QString(), m_detectedRunner);

    auto *coverRow = new QWidget(content);
    auto *coverLayout = new QHBoxLayout(coverRow);
    coverLayout->setContentsMargins(0, 0, 0, 0);
    coverLayout->setSpacing(7);
    m_coverEdit = new QLineEdit(coverRow);
    m_coverEdit->setPlaceholderText(QStringLiteral("Optional PNG, JPEG, WebP or SVG cover"));
    coverLayout->addWidget(m_coverEdit, 1);
    m_fetchCoverButton = new QPushButton(QStringLiteral("SteamGridDB…"), coverRow);
    m_fetchCoverButton->setCursor(Qt::PointingHandCursor);
    m_fetchCoverButton->setToolTip(
        QStringLiteral("Search SteamGridDB and choose which artwork to use"));
    m_fetchCoverButton->setAutoDefault(false);
    UiHelp::makeFlat(m_fetchCoverButton);
    m_fetchCoverButton->setMinimumWidth(122);
    coverLayout->addWidget(m_fetchCoverButton);
    auto *coverButton = new QPushButton(QStringLiteral("Browse…"), coverRow);
    coverButton->setCursor(Qt::PointingHandCursor);
    coverButton->setAutoDefault(false);
    UiHelp::makeFlat(coverButton);
    coverButton->setMinimumWidth(82);
    coverLayout->addWidget(coverButton);
    form->addRow(QStringLiteral("Cover image"), coverRow);
    connect(coverButton, &QPushButton::clicked, this, &AddEditDialog::browseCover);
    connect(m_fetchCoverButton, &QPushButton::clicked, this, &AddEditDialog::fetchCover);

    m_coverStatus = UiHelp::helpLabel(QString(), content);
    m_coverStatus->setVisible(false);
    form->addRow(QString(), m_coverStatus);

    m_favoriteCheck = new QCheckBox(QStringLiteral("Mark this game as a favorite"), content);
    form->addRow(QString(), m_favoriteCheck);

    form->addRow(UiHelp::helpLabel(
        QStringLiteral("Vanta never downloads games. Point it at a folder or executable you already own or manage."),
        content));
    m_tabs->addTab(makeScrollArea(content, m_tabs), QStringLiteral("General"));
}

void AddEditDialog::buildRunnerTab()
{
    auto *content = new QWidget;
    auto *form = formFor(content);

    m_runnerCombo = new QComboBox(content);
    m_runnerCombo->addItem(runnerName(RunnerType::Native), static_cast<int>(RunnerType::Native));
    m_runnerCombo->addItem(runnerName(RunnerType::Wine), static_cast<int>(RunnerType::Wine));
    m_runnerCombo->addItem(runnerName(RunnerType::Proton), static_cast<int>(RunnerType::Proton));
    m_runnerCombo->addItem(runnerName(RunnerType::Flatpak), static_cast<int>(RunnerType::Flatpak));
    m_runnerCombo->addItem(runnerName(RunnerType::Custom), static_cast<int>(RunnerType::Custom));
    form->addRow(QStringLiteral("Runner"), m_runnerCombo);

    m_platformCombo = new QComboBox(content);
    m_platformCombo->addItem(platformName(Platform::Linux), static_cast<int>(Platform::Linux));
    m_platformCombo->addItem(platformName(Platform::Windows), static_cast<int>(Platform::Windows));
    form->addRow(QStringLiteral("Platform"), m_platformCombo);

    m_runnerBuildCombo = new QComboBox(content);
    m_runnerBuildCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_runnerBuildCombo->setToolTip(QStringLiteral(
        "Pick one of the Wine or Proton builds Vanta found on this system, or choose a custom path."));
    form->addRow(QStringLiteral("Build"), m_runnerBuildCombo);
    m_runnerBuildLabel = qobject_cast<QLabel *>(form->labelForField(m_runnerBuildCombo));
    connect(m_runnerBuildCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { onBuildChanged(); });

    auto *runnerRow = new QWidget(content);
    auto *runnerLayout = new QHBoxLayout(runnerRow);
    runnerLayout->setContentsMargins(0, 0, 0, 0);
    runnerLayout->setSpacing(7);
    m_runnerPathEdit = new QLineEdit(runnerRow);
    m_runnerBrowseButton = new QPushButton(QStringLiteral("Browse…"), runnerRow);
    m_runnerBrowseButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makeFlat(m_runnerBrowseButton);
    runnerLayout->addWidget(m_runnerPathEdit, 1);
    runnerLayout->addWidget(m_runnerBrowseButton);
    form->addRow(QStringLiteral("Runner path"), runnerRow);
    connect(m_runnerBrowseButton, &QPushButton::clicked, this, &AddEditDialog::browseRunnerPath);

    QPushButton *prefixButton = nullptr;
    form->addRow(QStringLiteral("Prefix folder"),
                 pathRow(&m_prefixEdit, &prefixButton,
                         QStringLiteral("Optional; defaults to a Vanta Prefix folder beside the game"),
                         QStringLiteral("Browse…"), content));
    m_prefixBrowseButton = prefixButton;
    connect(prefixButton, &QPushButton::clicked, this, &AddEditDialog::browsePrefix);

    auto *options = new QWidget(content);
    auto *optionsLayout = new QVBoxLayout(options);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setSpacing(5);
    m_wineD3DCheck = new QCheckBox(QStringLiteral("Use WineD3D instead of DXVK (Proton)"), options);
    m_nativeWaylandCheck = new QCheckBox(QStringLiteral("Enable Proton native Wayland"), options);
    m_steamOverlayCheck = new QCheckBox(QStringLiteral("Enable Steam overlay environment"), options);
    m_steamRuntimeCheck = new QCheckBox(QStringLiteral("Wrap Proton with SteamLinuxRuntime"), options);
    m_bepInExCheck = new QCheckBox(QStringLiteral("BepInEx compatibility (winhttp=n,b when detected)"), options);
    optionsLayout->addWidget(m_wineD3DCheck);
    optionsLayout->addWidget(m_nativeWaylandCheck);
    optionsLayout->addWidget(m_steamOverlayCheck);
    optionsLayout->addWidget(m_steamRuntimeCheck);
    optionsLayout->addWidget(m_bepInExCheck);
    form->addRow(QStringLiteral("Options"), options);

    m_overridesEdit = new QLineEdit(content);
    m_overridesEdit->setPlaceholderText(QStringLiteral("winmm=n;d3d11=n,b"));
    form->addRow(QStringLiteral("DLL overrides"), m_overridesEdit);
    form->addRow(UiHelp::helpLabel(
        QStringLiteral("Stored overrides are appended after Vanta's forced DXVK chain. OnlineFix modules are added only for the launch and are never written here."),
        content));

    m_preArgumentsEdit = new QLineEdit(content);
    m_preArgumentsEdit->setPlaceholderText(QStringLiteral("Optional wrapper, for example: gamescope -W 1920 -H 1080 --"));
    form->addRow(QStringLiteral("Wrapper arguments"), m_preArgumentsEdit);

    m_runnerHelp = UiHelp::helpLabel(QString(), content);
    form->addRow(m_runnerHelp);
    m_tabs->addTab(makeScrollArea(content, m_tabs), QStringLiteral("Runner"));
}

void AddEditDialog::buildOnlineFixTab()
{
    auto *content = new QWidget;
    auto *form = formFor(content);

    m_onlineFixModeCombo = new QComboBox(content);
    m_onlineFixModeCombo->addItem(onlineFixModeName(OnlineFixMode::Auto), static_cast<int>(OnlineFixMode::Auto));
    m_onlineFixModeCombo->addItem(onlineFixModeName(OnlineFixMode::Force), static_cast<int>(OnlineFixMode::Force));
    m_onlineFixModeCombo->addItem(onlineFixModeName(OnlineFixMode::Disabled), static_cast<int>(OnlineFixMode::Disabled));
    form->addRow(QStringLiteral("Mode"), m_onlineFixModeCombo);
    form->addRow(UiHelp::helpLabel(
        QStringLiteral("Auto applies the launch configuration only when OnlineFix signatures are detected. Force applies app IDs and the override chain for Wine or Proton even when no files are found. Disabled never applies it."),
        content));

    auto *panel = new QFrame(content);
    panel->setObjectName(QStringLiteral("infoPanel"));
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(14, 12, 14, 12);
    panelLayout->setSpacing(5);
    m_detectionStatus = new QLabel(panel);
    m_detectionStatus->setProperty("class", QStringLiteral("sectionTitle"));
    m_detectionStatus->setWordWrap(true);
    m_detectionDetails = new QLabel(panel);
    m_detectionDetails->setProperty("class", QStringLiteral("muted"));
    m_detectionDetails->setWordWrap(true);
    m_detectionHint = UiHelp::helpLabel(QString(), panel);
    auto *detectButton = new QPushButton(QStringLiteral("Detect now"), panel);
    detectButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makeFlat(detectButton);
    panelLayout->addWidget(m_detectionStatus);
    panelLayout->addWidget(m_detectionDetails);
    panelLayout->addWidget(m_detectionHint);
    panelLayout->addWidget(detectButton, 0, Qt::AlignLeft);
    connect(detectButton, &QPushButton::clicked, this, [this] { refreshDetection(true); });
    form->addRow(QStringLiteral("Detection"), panel);

    m_realAppIdEdit = new QLineEdit(content);
    m_realAppIdEdit->setPlaceholderText(QStringLiteral("Optional; read from the fix configuration"));
    form->addRow(QStringLiteral("Real app ID"), m_realAppIdEdit);
    m_fakeAppIdEdit = new QLineEdit(content);
    m_fakeAppIdEdit->setPlaceholderText(QStringLiteral("Optional; used for the Steam overlay game ID"));
    form->addRow(QStringLiteral("Fake app ID"), m_fakeAppIdEdit);

    m_fixPathEdit = new QLineEdit(content);
    m_fixPathEdit->setPlaceholderText(QStringLiteral("Detected automatically when a SteamFix library is present"));
    form->addRow(QStringLiteral("Fix folder"), m_fixPathEdit);
    m_photonDirectoryEdit = new QLineEdit(content);
    m_photonDirectoryEdit->setPlaceholderText(QStringLiteral("Detected automatically beside a Photon Launcher.exe"));
    form->addRow(QStringLiteral("Photon folder"), m_photonDirectoryEdit);

    form->addRow(UiHelp::helpLabel(
        QStringLiteral("OnlineFix configuration is launch-only. Vanta backs up OnlineFix INI files before editing them and never claims that detected files make a copy functional. A Steam precondition can be required in Settings."),
        content));
    m_tabs->addTab(makeScrollArea(content, m_tabs), QStringLiteral("OnlineFix"));
}

void AddEditDialog::buildEnvironmentTab()
{
    auto *content = new QWidget;
    auto *form = formFor(content);
    m_environmentEdit = new QPlainTextEdit(content);
    m_environmentEdit->setProperty("monospace", true);
    m_environmentEdit->setPlaceholderText(QStringLiteral("KEY=VALUE\nMANGOHUD=1\nDXVK_HUD=fps"));
    m_environmentEdit->setMinimumHeight(220);
    form->addRow(QStringLiteral("Environment"), m_environmentEdit);
    form->addRow(UiHelp::helpLabel(
        QStringLiteral("One KEY=VALUE pair per line. Blank lines and lines starting with # are ignored. These values are inherited by the runner and the game."),
        content));
    m_tabs->addTab(makeScrollArea(content, m_tabs), QStringLiteral("Environment"));
}

void AddEditDialog::loadFromGame()
{
    m_nameEdit->setText(m_game.name);
    const int sourceIndex = m_sourceCombo->findData(m_game.source);
    if (sourceIndex >= 0)
        m_sourceCombo->setCurrentIndex(sourceIndex);
    else if (!m_game.source.trimmed().isEmpty())
        m_sourceCombo->addItem(m_game.source, m_game.source), m_sourceCombo->setCurrentIndex(m_sourceCombo->count() - 1);
    m_executableEdit->setText(m_game.executable);
    m_argumentsEdit->setText(m_game.launchArguments);
    m_workingDirectoryEdit->setText(m_game.workingDirectory);
    m_coverEdit->setText(m_game.coverPath);
    m_favoriteCheck->setChecked(m_game.favorite);

    const int runnerIndex = m_runnerCombo->findData(static_cast<int>(m_game.runner));
    m_runnerCombo->setCurrentIndex(runnerIndex >= 0 ? runnerIndex : 0);
    const int platformIndex = m_platformCombo->findData(static_cast<int>(m_game.platform));
    m_platformCombo->setCurrentIndex(platformIndex >= 0 ? platformIndex : 0);
    m_runnerPathEdit->setText(m_game.runnerPath);
    m_prefixEdit->setText(m_game.winePrefix);
    m_wineD3DCheck->setChecked(m_game.wineD3D);
    m_nativeWaylandCheck->setChecked(m_game.nativeWayland);
    m_steamOverlayCheck->setChecked(m_game.steamOverlay);
    m_steamRuntimeCheck->setChecked(m_game.useSteamRuntime);
    m_bepInExCheck->setChecked(m_game.bepinexCompatibility);
    m_overridesEdit->setText(m_game.wineDllOverrides);
    m_preArgumentsEdit->setText(m_game.preArguments);

    const int modeIndex = m_onlineFixModeCombo->findData(static_cast<int>(m_game.onlineFixMode));
    m_onlineFixModeCombo->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
    m_realAppIdEdit->setText(m_game.realAppId);
    m_fakeAppIdEdit->setText(m_game.fakeAppId);
    m_fixPathEdit->setText(m_game.fixPath);
    m_photonDirectoryEdit->setText(m_game.photonLauncherDir);
    m_environmentEdit->setPlainText(UiHelp::environmentToText(m_game.environment));
}

void AddEditDialog::updateRunnerUi()
{
    const auto runner = static_cast<RunnerType>(m_runnerCombo->currentData().toInt());
    const bool wineLike = runnerIsWineLike(runner);
    const bool proton = runner == RunnerType::Proton;
    const bool native = runner == RunnerType::Native;

    m_runnerPathEdit->setEnabled(!native);
    m_prefixEdit->setEnabled(wineLike);
    m_prefixBrowseButton->setVisible(wineLike);
    m_wineD3DCheck->setEnabled(proton);
    m_nativeWaylandCheck->setEnabled(proton);
    m_steamOverlayCheck->setEnabled(proton);
    m_steamRuntimeCheck->setEnabled(proton);
    m_overridesEdit->setEnabled(wineLike);

    // The build chooser lists every Wine or Proton Vanta found so the user can
    // pick the exact one for a game instead of typing a path. It is hidden for
    // runners that do not use a Wine-like build.
    m_runnerBuildCombo->setVisible(wineLike);
    if (m_runnerBuildLabel)
        m_runnerBuildLabel->setVisible(wineLike);
    if (wineLike) {
        populateBuildCombo(runner);
        applyBuildSelectionUi();
    } else {
        m_runnerBrowseButton->setVisible(runner == RunnerType::Custom);
    }

    switch (runner) {
    case RunnerType::Native:
        m_runnerPathEdit->setPlaceholderText(QStringLiteral("Not used for native launches"));
        m_runnerHelp->setText(QStringLiteral("The executable is started directly on Linux. Windows .exe files need Wine or Proton."));
        break;
    case RunnerType::Wine:
        m_runnerPathEdit->setPlaceholderText(QStringLiteral("Wine executable; leave blank for system wine"));
        m_prefixEdit->setPlaceholderText(QStringLiteral("Optional; the default prefix is <game folder>/Vanta Prefix/pfx"));
        m_runnerHelp->setText(QStringLiteral("Wine receives WINEPREFIX pointing at the prefix's pfx folder and a forced DXVK override chain."));
        break;
    case RunnerType::Proton:
        m_runnerPathEdit->setPlaceholderText(QStringLiteral("Proton folder or its proton script"));
        m_prefixEdit->setPlaceholderText(QStringLiteral("Optional; the default is <game folder>/Vanta Prefix"));
        m_runnerHelp->setText(QStringLiteral("Proton runs with the prefix folder as STEAM_COMPAT_DATA_PATH. Its pfx subfolder is used for teardown."));
        break;
    case RunnerType::Flatpak:
        m_runnerPathEdit->setPlaceholderText(QStringLiteral("Flatpak application ID, for example com.example.Game"));
        m_runnerHelp->setText(QStringLiteral("Vanta runs the game with flatpak run <application ID>. Game arguments are appended."));
        break;
    case RunnerType::Custom:
        m_runnerPathEdit->setPlaceholderText(QStringLiteral("Launcher executable and optional arguments, for example: mangohud"));
        m_runnerHelp->setText(QStringLiteral("The custom command is executed with the optional executable and game arguments appended."));
        break;
    }

    // The BepInEx line means something different per runner, and Flatpak cannot
    // inject the preloader at all, so the option says so instead of presenting a
    // Windows-only override for a game that will never use it.
    if (native) {
        m_bepInExCheck->setEnabled(true);
        m_bepInExCheck->setText(QStringLiteral("BepInEx compatibility (doorstop launcher when detected)"));
        m_bepInExCheck->setToolTip(QStringLiteral(
            "A native Linux game loads BepInEx through the run_bepinex.sh doorstop launcher. "
            "Vanta uses that script when it is present next to the game."));
    } else if (wineLike) {
        m_bepInExCheck->setEnabled(true);
        m_bepInExCheck->setText(QStringLiteral("BepInEx compatibility (winhttp=n,b when detected)"));
        m_bepInExCheck->setToolTip(QStringLiteral(
            "Adds the winhttp=n,b DLL override for the launch when BepInEx files are detected."));
    } else {
        m_bepInExCheck->setEnabled(false);
        m_bepInExCheck->setText(QStringLiteral("BepInEx compatibility (not available for this runner)"));
        m_bepInExCheck->setToolTip(QStringLiteral(
            "BepInEx can only be loaded for Native, Wine and Proton launches."));
    }
}

void AddEditDialog::refreshDetection(bool applyDetectedValues)
{
    if (!m_detectionStatus)
        return;
    const QString executable = m_executableEdit->text().trimmed();
    const QString executableDirectory = executable.isEmpty()
        ? QString() : QFileInfo(executable).absolutePath();
    // The game folder is always the executable's folder; there is no separate field.
    const QString directory = executableDirectory;
    const OnlineFixInfo info = OnlineFixDetector::detect(executableDirectory, directory);
    m_detectionHint->setText(QStringLiteral("Detection is based on filenames and INI values. It does not prove online functionality."));

    if (!info.detected) {
        m_detectionStatus->setText(QStringLiteral("No OnlineFix signatures detected"));
        m_detectionDetails->setText(directory.isEmpty()
            ? QStringLiteral("Choose the game executable to inspect its folder.")
            : QStringLiteral("Inspection looked at the executable folder. OnlineFix files are never fabricated or downloaded."));
        return;
    }

    m_detectionStatus->setText(QStringLiteral("OnlineFix detected: %1").arg(fixKindName(info.kind)));
    QStringList details;
    details.append(QStringLiteral("%1 matching file(s), %2 override module(s).")
                       .arg(info.files.size()).arg(info.extraModules.size()));
    if (!info.realAppId.isEmpty())
        details.append(QStringLiteral("Real app ID: %1").arg(info.realAppId));
    if (!info.fakeAppId.isEmpty())
        details.append(QStringLiteral("Fake app ID: %1").arg(info.fakeAppId));
    if (!info.fixPath.isEmpty())
        details.append(QStringLiteral("SteamFix folder: %1").arg(info.fixPath));
    if (!info.photonLauncherDir.isEmpty())
        details.append(QStringLiteral("Photon launcher folder: %1").arg(info.photonLauncherDir));
    m_detectionDetails->setText(details.join(QLatin1Char('\n')));

    if (applyDetectedValues) {
        if (m_realAppIdEdit->text().trimmed().isEmpty() && !info.realAppId.isEmpty())
            m_realAppIdEdit->setText(info.realAppId);
        if (m_fakeAppIdEdit->text().trimmed().isEmpty() && !info.fakeAppId.isEmpty())
            m_fakeAppIdEdit->setText(info.fakeAppId);
        if (m_fixPathEdit->text().trimmed().isEmpty() && !info.fixPath.isEmpty())
            m_fixPathEdit->setText(info.fixPath);
        if (m_photonDirectoryEdit->text().trimmed().isEmpty() && !info.photonLauncherDir.isEmpty())
            m_photonDirectoryEdit->setText(info.photonLauncherDir);
    }
}

void AddEditDialog::populateBuildCombo(RunnerType runner)
{
    const QString previous = m_runnerBuildCombo->currentData().toString();
    const QString currentPath = m_runnerPathEdit->text().trimmed();

    const QList<RunnerEntry> entries = runner == RunnerType::Proton
        ? RunnerDetector::protonEntries(m_settings)
        : RunnerDetector::wineEntries(m_settings);

    m_runnerBuildCombo->blockSignals(true);
    m_runnerBuildCombo->clear();
    // A blank path is meaningful for Wine: it asks the launch plan to resolve
    // the system wine at start time. Giving it a named entry keeps the combo
    // from showing "Custom path..." for a perfectly normal configuration.
    if (runner == RunnerType::Wine)
        m_runnerBuildCombo->addItem(QStringLiteral("System Wine (chosen at launch)"), QString());
    for (const RunnerEntry &entry : entries) {
        QString label = entry.name;
        if (runner == RunnerType::Proton) {
            // Proton builds, especially the community ones, are told apart by
            // their version string rather than by the folder name.
            const QString version = RunnerDetector::protonVersion(entry.path);
            if (!version.isEmpty() && !label.contains(version, Qt::CaseInsensitive))
                label += QStringLiteral(" · %1").arg(version);
        }
        m_runnerBuildCombo->addItem(label, entry.path);
    }
    m_runnerBuildCombo->addItem(QStringLiteral("Custom path…"), QLatin1String(kCustomBuild));
    if (entries.isEmpty())
        m_runnerBuildCombo->setItemText(m_runnerBuildCombo->count() - 1,
                                        QStringLiteral("Custom path… (no builds were detected)"));

    int index = m_runnerBuildCombo->findData(currentPath);
    if (index < 0 && previous != QLatin1String(kCustomBuild))
        index = m_runnerBuildCombo->findData(previous);
    if (index < 0)
        index = m_runnerBuildCombo->count() - 1;
    m_runnerBuildCombo->setCurrentIndex(index);
    m_runnerBuildCombo->blockSignals(false);
}

void AddEditDialog::onBuildChanged()
{
    const QString data = m_runnerBuildCombo->currentData().toString();
    if (data == QLatin1String(kCustomBuild)) {
        // The path field becomes editable again so a build outside the searched
        // folders can still be entered.
        m_runnerPathEdit->setEnabled(true);
        m_runnerBrowseButton->setVisible(true);
        m_runnerPathEdit->setFocus(Qt::OtherFocusReason);
        return;
    }
    m_runnerPathEdit->setText(data);
    m_runnerPathEdit->setEnabled(false);
    m_runnerBrowseButton->setVisible(false);
}

void AddEditDialog::applyBuildSelectionUi()
{
    const bool custom = m_runnerBuildCombo->currentData().toString() == QLatin1String(kCustomBuild);
    m_runnerPathEdit->setEnabled(custom);
    m_runnerBrowseButton->setVisible(custom);
}

void AddEditDialog::browseExecutable()
{
    const QString start = m_executableEdit->text().isEmpty()
        ? QDir::homePath() : QFileInfo(m_executableEdit->text()).absolutePath();
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Choose game executable"), start,
        QStringLiteral("Windows executables (*.exe);;All files (*)"));
    if (file.isEmpty())
        return;
    m_executableEdit->setText(file);
    const QFileInfo info(file);
    if (m_nameEdit->text().trimmed().isEmpty())
        m_nameEdit->setText(info.completeBaseName());
    applyDetectedRunner(file);
    refreshDetection(false);
}

// A Windows binary is always launched through Proton, because nobody adds a game
// here to open a Windows utility. A native binary stays native unless the user
// already chose Wine or Proton on purpose.
void AddEditDialog::applyDetectedRunner(const QString &executable)
{
    const QFileInfo info(executable);
    if (executable.trimmed().isEmpty() || !info.exists()) {
        if (m_detectedRunner)
            m_detectedRunner->setVisible(false);
        return;
    }

    const bool windowsBinary = info.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) == 0
        || GameScanner::isWindowsExecutable(info.absoluteFilePath());

    const auto selectRunner = [this](RunnerType runner) {
        const int index = m_runnerCombo->findData(static_cast<int>(runner));
        if (index >= 0 && m_runnerCombo->currentIndex() != index)
            m_runnerCombo->setCurrentIndex(index);
    };
    const auto selectPlatform = [this](Platform platform) {
        const int index = m_platformCombo->findData(static_cast<int>(platform));
        if (index >= 0)
            m_platformCombo->setCurrentIndex(index);
    };

    if (windowsBinary) {
        const auto current = static_cast<RunnerType>(m_runnerCombo->currentData().toInt());
        if (current != RunnerType::Wine && current != RunnerType::Proton
            && current != RunnerType::Flatpak && current != RunnerType::Custom) {
            selectRunner(RunnerType::Proton);
        }
        selectPlatform(Platform::Windows);
        if (m_detectedRunner) {
            m_detectedRunner->setText(
                QStringLiteral("Windows executable detected — Proton selected. Change the runner in the Runner tab if needed."));
            m_detectedRunner->setVisible(true);
        }
    } else {
        const auto current = static_cast<RunnerType>(m_runnerCombo->currentData().toInt());
        if (current == RunnerType::Proton)
            selectRunner(RunnerType::Native);
        selectPlatform(Platform::Linux);
        if (m_detectedRunner) {
            m_detectedRunner->setText(
                QStringLiteral("Native Linux executable detected — Native selected."));
            m_detectedRunner->setVisible(true);
        }
    }
    updateRunnerUi();
}

void AddEditDialog::browseWorkingDirectory()
{
    const QString start = m_workingDirectoryEdit->text().isEmpty()
        ? QFileInfo(m_executableEdit->text().trimmed()).absolutePath() : m_workingDirectoryEdit->text();
    const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose working folder"), start);
    if (!directory.isEmpty())
        m_workingDirectoryEdit->setText(directory);
}

void AddEditDialog::browseCover()
{
    const QString start = m_coverEdit->text().isEmpty()
        ? QFileInfo(m_executableEdit->text().trimmed()).absolutePath() : m_coverEdit->text();
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Choose cover image"), start,
        QStringLiteral("Images (*.png *.jpg *.jpeg *.webp *.bmp *.svg);;All files (*)"));
    if (!file.isEmpty())
        m_coverEdit->setText(file);
}

void AddEditDialog::fetchCover()
{
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        if (m_coverStatus) {
            m_coverStatus->setProperty("class", QStringLiteral("danger"));
            m_coverStatus->setText(QStringLiteral("Give the game a name first so SteamGridDB can be searched."));
            m_coverStatus->setVisible(true);
            m_coverStatus->style()->unpolish(m_coverStatus);
            m_coverStatus->style()->polish(m_coverStatus);
        }
        m_nameEdit->setFocus(Qt::OtherFocusReason);
        return;
    }

    if (!m_coverFetcher)
        m_coverFetcher = new CoverFetcher(this);

    if (m_coverStatus) {
        m_coverStatus->setProperty("class", QStringLiteral("fieldHelp"));
        m_coverStatus->setText(QStringLiteral("Searching SteamGridDB for “%1”…").arg(name));
        m_coverStatus->setVisible(true);
        m_coverStatus->style()->unpolish(m_coverStatus);
        m_coverStatus->style()->polish(m_coverStatus);
    }
    m_fetchCoverButton->setEnabled(false);

    // Save under the real game id so the file the dialog stores is the same one a
    // later automatic fetch would target.
    m_coverFetcher->listCoverArt(m_game.id, name);
}

void AddEditDialog::browseRunnerPath()
{
    const auto runner = static_cast<RunnerType>(m_runnerCombo->currentData().toInt());
    if (runner == RunnerType::Proton) {
        const QString start = m_runnerPathEdit->text().isEmpty() ? QDir::homePath() : m_runnerPathEdit->text();
        const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose Proton folder"), start);
        if (!directory.isEmpty())
            m_runnerPathEdit->setText(directory);
        return;
    }
    const QString start = m_runnerPathEdit->text().isEmpty() ? QDir::homePath() : m_runnerPathEdit->text();
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Choose runner executable"), start);
    if (!file.isEmpty())
        m_runnerPathEdit->setText(file);
}

void AddEditDialog::browsePrefix()
{
    const QString start = m_prefixEdit->text().isEmpty()
        ? QFileInfo(m_executableEdit->text().trimmed()).absolutePath() : m_prefixEdit->text();
    const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose prefix folder"), start);
    if (!directory.isEmpty())
        m_prefixEdit->setText(directory);
}

void AddEditDialog::showError(const QString &message, int tabIndex)
{
    m_errorLabel->setText(message);
    m_errorLabel->setVisible(true);
    if (tabIndex >= 0 && tabIndex < m_tabs->count())
        m_tabs->setCurrentIndex(tabIndex);
}

void AddEditDialog::validateAndAccept()
{
    m_errorLabel->setVisible(false);
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        showError(QStringLiteral("Give the game a name."), 0);
        m_nameEdit->setFocus();
        return;
    }

    QString executable = m_executableEdit->text().trimmed();
    const QString workingDirectory = m_workingDirectoryEdit->text().trimmed();

    const auto runner = static_cast<RunnerType>(m_runnerCombo->currentData().toInt());

    if (executable.isEmpty()) {
        showError(QStringLiteral("Choose the game executable."), 0);
        m_executableEdit->setFocus();
        return;
    }
    {
        const QFileInfo info(executable);
        if (!info.exists() || !info.isFile()) {
            showError(QStringLiteral("The executable does not exist: %1").arg(executable), 0);
            return;
        }
        executable = info.absoluteFilePath();
    }

    // The game folder is never asked for: it is the executable's parent folder.
    QString directory = QFileInfo(executable).absolutePath();
    if (!QFileInfo(directory).isDir() && !workingDirectory.isEmpty())
        directory = workingDirectory;

    if (!executable.isEmpty()) {
        const QFileInfo executableInfo(executable);
        const bool windowsExecutable =
            executable.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
            || GameScanner::isWindowsExecutable(executable);
        if (runner == RunnerType::Native && windowsExecutable) {
            showError(QStringLiteral("A Windows .exe cannot run through the Native runner. Choose Wine or Proton in the Runner tab."), 1);
            return;
        }
        if (runner == RunnerType::Native && !windowsExecutable && !executableInfo.isExecutable()) {
            showError(QStringLiteral("The native executable is not marked executable."), 0);
            return;
        }
    }

    if (!workingDirectory.isEmpty()) {
        const QFileInfo workingInfo(workingDirectory);
        if (!workingInfo.exists() || !workingInfo.isDir()) {
            showError(QStringLiteral("The working folder does not exist: %1").arg(workingDirectory), 0);
            return;
        }
    }

    const QString cover = m_coverEdit->text().trimmed();
    if (!cover.isEmpty() && !QFileInfo(cover).isFile()) {
        showError(QStringLiteral("The cover image does not exist: %1").arg(cover), 0);
        return;
    }

    const QString runnerPath = m_runnerPathEdit->text().trimmed();
    if (runner == RunnerType::Wine && !runnerPath.isEmpty()) {
        const QFileInfo runnerInfo(runnerPath);
        if (!runnerInfo.exists() && QStandardPaths::findExecutable(runnerPath).isEmpty()) {
            showError(QStringLiteral("The Wine executable does not exist: %1").arg(runnerPath), 1);
            return;
        }
    } else if (runner == RunnerType::Proton) {
        QString reason;
        if (!RunnerDetector::validateProton(runnerPath, &reason)) {
            showError(reason.isEmpty() ? QStringLiteral("Choose a Proton folder or proton script.") : reason, 1);
            return;
        }
    } else if (runner == RunnerType::Flatpak && runnerPath.isEmpty()) {
        showError(QStringLiteral("Enter the Flatpak application ID, for example com.example.Game."), 1);
        return;
    } else if (runner == RunnerType::Custom) {
        const QStringList custom = QProcess::splitCommand(runnerPath);
        if (custom.isEmpty()) {
            showError(QStringLiteral("Enter a custom launcher command."), 1);
            return;
        }
        const QString first = custom.first();
        if (QFileInfo(first).isAbsolute()) {
            if (!QFileInfo(first).isFile()) {
                showError(QStringLiteral("The custom launcher does not exist: %1").arg(first), 1);
                return;
            }
        } else if (QStandardPaths::findExecutable(first).isEmpty()) {
            showError(QStringLiteral("The custom launcher was not found on PATH: %1").arg(first), 1);
            return;
        }
    }

    const QString prefix = m_prefixEdit->text().trimmed();
    if (!prefix.isEmpty() && QFileInfo::exists(prefix) && !QFileInfo(prefix).isDir()) {
        showError(QStringLiteral("The prefix path is not a folder: %1").arg(prefix), 1);
        return;
    }

    const QString overrides = m_overridesEdit->text().trimmed();
    QString overrideError;
    if (!UiHelp::validateDllOverrides(overrides, &overrideError)) {
        showError(overrideError, 1);
        return;
    }

    const QString realAppId = m_realAppIdEdit->text().trimmed();
    const QString fakeAppId = m_fakeAppIdEdit->text().trimmed();
    const QRegularExpression digits(QStringLiteral("^\\d*$"));
    if (!realAppId.isEmpty() && !digits.match(realAppId).hasMatch()) {
        showError(QStringLiteral("The real app ID must contain digits only."), 2);
        return;
    }
    if (!fakeAppId.isEmpty() && !digits.match(fakeAppId).hasMatch()) {
        showError(QStringLiteral("The fake app ID must contain digits only."), 2);
        return;
    }

    QMap<QString, QString> environment;
    QString environmentError;
    if (!UiHelp::parseEnvironment(m_environmentEdit->toPlainText(), &environment, &environmentError)) {
        showError(environmentError, 3);
        return;
    }
    for (auto it = environment.constBegin(); it != environment.constEnd(); ++it) {
        if (LaunchPlanBuilder::isControlledEnvironmentKey(it.key())) {
            showError(QStringLiteral("%1 is controlled by Vanta and cannot be stored per game. Set it through the Runner options instead.").arg(it.key()), 3);
            return;
        }
    }

    Game game = m_game;
    game.name = name;
    game.source = m_sourceCombo->currentText().trimmed();
    if (game.source.isEmpty())
        game.source = QStringLiteral("Manual");
    game.gameDirectory = directory;
    game.executable = executable;
    game.launchArguments = m_argumentsEdit->text();
    game.preArguments = m_preArgumentsEdit->text();
    game.workingDirectory = workingDirectory;
    game.runner = runner;
    game.platform = static_cast<Platform>(m_platformCombo->currentData().toInt());
    game.runnerPath = runnerPath;
    game.winePrefix = prefix;
    game.wineD3D = m_wineD3DCheck->isChecked();
    game.nativeWayland = m_nativeWaylandCheck->isChecked();
    game.steamOverlay = m_steamOverlayCheck->isChecked();
    game.useSteamRuntime = m_steamRuntimeCheck->isChecked();
    game.bepinexCompatibility = m_bepInExCheck->isChecked();
    game.wineDllOverrides = overrides;
    game.coverPath = cover;
    game.favorite = m_favoriteCheck->isChecked();
    game.onlineFixMode = static_cast<OnlineFixMode>(m_onlineFixModeCombo->currentData().toInt());
    game.realAppId = realAppId;
    game.fakeAppId = fakeAppId;
    game.fixPath = m_fixPathEdit->text().trimmed();
    game.photonLauncherDir = m_photonDirectoryEdit->text().trimmed();
    game.environment = environment;
    game.modifiedAt = QDateTime::currentDateTime();
    if (m_adding) {
        if (game.id.isEmpty())
            game.id = Game::create().id;
        if (!game.addedAt.isValid())
            game.addedAt = QDateTime::currentDateTime();
    }
    m_game = game;
    accept();
}
