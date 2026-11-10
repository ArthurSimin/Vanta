#include "SettingsDialog.h"
#include "CoverFetcher.h"
#include "I18n.h"
#include "IconFactory.h"
#include "Library.h"
#include "LibraryImportDialog.h"
#include "RunnerDetector.h"
#include "RunnerInstallDialog.h"
#include "Storage.h"
#include "ThemeManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
QScrollArea *scrollPage(QWidget *content, QWidget *parent)
{
    auto *area = new QScrollArea(parent);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setWidget(content);
    return area;
}

QFormLayout *formFor(QWidget *parent)
{
    auto *form = new QFormLayout(parent);
    form->setContentsMargins(18, 16, 18, 16);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(11);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return form;
}
}

SettingsDialog::SettingsDialog(const AppSettings &settings, ThemeManager *theme, GameLibrary *library, QWidget *parent)
    : VantaDialog(Vanta::tr("Settings"), theme, parent)
    , m_settings(settings)
    , m_theme(theme)
    , m_library(library)
{
    setObjectName(QStringLiteral("settingsDialog"));
    setMinimumSize(760, 620);
    resize(820, 720);
    m_originalTheme = m_theme ? m_theme->choice() : settings.theme;
    buildUi();
    loadFromSettings();
    refreshIcons();
    refreshRunnerSummary();
    if (m_theme) {
        connect(m_theme, &ThemeManager::themeChanged, this, &SettingsDialog::refreshIcons);
        connect(m_themeCombo, &QComboBox::currentIndexChanged, this, &SettingsDialog::applyThemePreview);
        connect(this, &QDialog::finished, this, [this](int result) {
            if (result != QDialog::Accepted)
                applyThemeChoice(m_originalTheme);
        });
    }
}

void SettingsDialog::applyThemeChoice(ThemeChoice choice)
{
    if (!m_theme)
        return;
    m_theme->setChoice(choice);
    m_theme->apply();
    applyPalette();
    refreshIcons();
}

void SettingsDialog::applyThemePreview()
{
    if (!m_theme || !m_themeCombo)
        return;
    const int index = m_themeCombo->currentIndex();
    if (index < 0)
        return;
    applyThemeChoice(static_cast<ThemeChoice>(m_themeCombo->itemData(index).toInt()));
}

void SettingsDialog::buildUi()
{
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("settingsTabs"));
    bodyLayout()->addWidget(tabs, 1);

    auto *appearance = new QWidget;
    auto *appearanceForm = formFor(appearance);
    m_themeCombo = new QComboBox(appearance);
    m_themeCombo->addItem(themeChoiceDisplayName(ThemeChoice::System), static_cast<int>(ThemeChoice::System));
    m_themeCombo->addItem(themeChoiceDisplayName(ThemeChoice::Light), static_cast<int>(ThemeChoice::Light));
    m_themeCombo->addItem(themeChoiceDisplayName(ThemeChoice::Dark), static_cast<int>(ThemeChoice::Dark));
    m_themeCombo->addItem(themeChoiceDisplayName(ThemeChoice::SystemColors), static_cast<int>(ThemeChoice::SystemColors));
    appearanceForm->addRow(Vanta::tr("Theme"), m_themeCombo);
    appearanceForm->addRow(UiHelp::helpLabel(
        Vanta::tr("System (light or dark) follows the desktop color scheme reported by Qt. "
                  "Follow system colors reads the palette of the active desktop theme directly, so a "
                  "Breeze Dark or Breeze Light scheme is reproduced with its own window, text and accent "
                  "colors instead of Vanta's built-in palette."),
        appearance));

    m_languageCombo = new QComboBox(appearance);
    m_languageCombo->addItem(Vanta::tr("System default"), QStringLiteral("system"));
    m_languageCombo->addItem(QStringLiteral("English"), QStringLiteral("en"));
    for (const I18n::Language &language : I18n::languages())
        m_languageCombo->addItem(language.nativeName, language.code);
    appearanceForm->addRow(Vanta::tr("Language"), m_languageCombo);
    appearanceForm->addRow(UiHelp::helpLabel(
        Vanta::tr("Translations come from the community. The interface switches to the new language when the "
                  "settings are saved."),
        appearance));

    m_animationsCheck = new QCheckBox(Vanta::tr("Enable subtle hover and selection animations"), appearance);
    appearanceForm->addRow(QString(), m_animationsCheck);

    m_coverSizeCombo = new QComboBox(appearance);
    m_coverSizeCombo->addItem(coverSizeDisplayName(CoverSize::Small), static_cast<int>(CoverSize::Small));
    m_coverSizeCombo->addItem(coverSizeDisplayName(CoverSize::Medium), static_cast<int>(CoverSize::Medium));
    m_coverSizeCombo->addItem(coverSizeDisplayName(CoverSize::Large), static_cast<int>(CoverSize::Large));
    appearanceForm->addRow(Vanta::tr("Cover size"), m_coverSizeCombo);

    m_viewModeCombo = new QComboBox(appearance);
    m_viewModeCombo->addItem(viewModeDisplayName(ViewMode::Grid), static_cast<int>(ViewMode::Grid));
    m_viewModeCombo->addItem(viewModeDisplayName(ViewMode::List), static_cast<int>(ViewMode::List));
    appearanceForm->addRow(Vanta::tr("Default view"), m_viewModeCombo);

    m_sortCombo = new QComboBox(appearance);
    m_sortCombo->addItem(sortModeDisplayName(SortMode::Name), static_cast<int>(SortMode::Name));
    m_sortCombo->addItem(sortModeDisplayName(SortMode::LastPlayed), static_cast<int>(SortMode::LastPlayed));
    m_sortCombo->addItem(sortModeDisplayName(SortMode::Playtime), static_cast<int>(SortMode::Playtime));
    appearanceForm->addRow(Vanta::tr("Sort by"), m_sortCombo);

    m_ascendingCheck = new QCheckBox(Vanta::tr("Sort ascending"), appearance);
    appearanceForm->addRow(QString(), m_ascendingCheck);

    m_badgesCheck = new QCheckBox(Vanta::tr("Show runner and platform badges on cards"), appearance);
    appearanceForm->addRow(QString(), m_badgesCheck);

    m_rememberSidebarCheck = new QCheckBox(Vanta::tr("Remember the selected sidebar filter"), appearance);
    appearanceForm->addRow(QString(), m_rememberSidebarCheck);

    m_rememberGroupsCheck = new QCheckBox(Vanta::tr("Remember collapsed sidebar groups"), appearance);
    appearanceForm->addRow(QString(), m_rememberGroupsCheck);

    m_replayButton = new QPushButton(Vanta::tr("Show the tutorial again"), appearance);
    m_replayButton->setCursor(Qt::PointingHandCursor);
    // Without this Qt makes the first created button the dialog's implicit
    // default, so Enter on the Appearance tab replayed the tutorial instead of
    // saving. Save and Cancel are set below.
    m_replayButton->setAutoDefault(false);
    connect(m_replayButton, &QPushButton::clicked, this, [this] {
        m_replayTutorial = true;
        m_settings.tutorialCompleted = false;
    });
    appearanceForm->addRow(QString(), m_replayButton);
    tabs->addTab(scrollPage(appearance, tabs), Vanta::tr("Appearance"));

    auto *library = new QWidget;
    auto *libraryForm = formFor(library);
    m_autoScanCheck = new QCheckBox(Vanta::tr("Scan new games automatically when they are added"), library);
    libraryForm->addRow(QString(), m_autoScanCheck);
    m_rescanCheck = new QCheckBox(Vanta::tr("Rescan when the game folder changed since the last scan"), library);
    libraryForm->addRow(QString(), m_rescanCheck);

    m_depthSpin = new QSpinBox(library);
    m_depthSpin->setRange(2, 24);
    m_depthSpin->setSuffix(Vanta::tr(" levels"));
    libraryForm->addRow(Vanta::tr("Scan depth"), m_depthSpin);

    m_limitSpin = new QSpinBox(library);
    m_limitSpin->setRange(1000, 250000);
    m_limitSpin->setSingleStep(1000);
    m_limitSpin->setSuffix(Vanta::tr(" entries"));
    libraryForm->addRow(Vanta::tr("Scan entry limit"), m_limitSpin);

    libraryForm->addRow(UiHelp::helpLabel(
        Vanta::tr("Scans run in the background and are read-only, except for confirmed OnlineFix INI updates and "
                  "the Photon Newtonsoft.Json link. Lower limits keep very large folders responsive."),
        library));
    tabs->addTab(scrollPage(library, tabs), Vanta::tr("Scanning"));

    auto *onlineFix = new QWidget;
    auto *fixForm = formFor(onlineFix);
    m_onlineFixPromptCombo = new QComboBox(onlineFix);
    m_onlineFixPromptCombo->addItem(Vanta::tr("Ask before every launch"),
                                    static_cast<int>(OnlineFixPrompt::Ask));
    m_onlineFixPromptCombo->addItem(Vanta::tr("Always apply when detected"),
                                    static_cast<int>(OnlineFixPrompt::AlwaysApply));
    m_onlineFixPromptCombo->addItem(Vanta::tr("Never apply"),
                                    static_cast<int>(OnlineFixPrompt::NeverApply));
    fixForm->addRow(Vanta::tr("Launch policy"), m_onlineFixPromptCombo);

    m_requireSteamCheck = new QCheckBox(Vanta::tr("Require Steam to be running before an OnlineFix launch"),
                                        onlineFix);
    fixForm->addRow(QString(), m_requireSteamCheck);

    fixForm->addRow(UiHelp::helpLabel(
        Vanta::tr("Per-game modes override this policy. OnlineFix overrides are applied to the launch environment "
                  "only and are never saved into the game's stored environment. Configuration files are backed up "
                  "before they are changed. Vanta cannot verify that a fix works."),
        onlineFix));
    tabs->addTab(scrollPage(onlineFix, tabs), QStringLiteral("OnlineFix"));

    auto *runners = new QWidget;
    auto *runnersForm = formFor(runners);

    auto *wineRow = new QWidget(runners);
    auto *wineLayout = new QHBoxLayout(wineRow);
    wineLayout->setContentsMargins(0, 0, 0, 0);
    wineLayout->setSpacing(7);
    m_winePathEdit = new QLineEdit(wineRow);
    m_winePathEdit->setPlaceholderText(Vanta::tr("Optional custom Wine executable or build folder"));
    wineLayout->addWidget(m_winePathEdit, 1);
    auto *wineBrowse = new QPushButton(Vanta::tr("Browse…"), wineRow);
    wineBrowse->setCursor(Qt::PointingHandCursor);
    UiHelp::makeFlat(wineBrowse);
    connect(wineBrowse, &QPushButton::clicked, this, &SettingsDialog::browseWine);
    wineLayout->addWidget(wineBrowse);
    runnersForm->addRow(Vanta::tr("Custom Wine"), wineRow);

    m_protonList = new QListWidget(runners);
    m_protonList->setObjectName(QStringLiteral("protonPathList"));
    m_protonList->setMinimumHeight(110);
    m_protonList->setSelectionMode(QAbstractItemView::SingleSelection);
    runnersForm->addRow(Vanta::tr("Custom Proton paths"), m_protonList);

    auto *protonButtons = new QHBoxLayout;
    protonButtons->setSpacing(7);
    auto *addProton = new QPushButton(Vanta::tr("Add folder…"), runners);
    addProton->setCursor(Qt::PointingHandCursor);
    connect(addProton, &QPushButton::clicked, this, &SettingsDialog::addProtonPath);
    protonButtons->addWidget(addProton);
    auto *removeProton = new QPushButton(Vanta::tr("Remove"), runners);
    removeProton->setCursor(Qt::PointingHandCursor);
    connect(removeProton, &QPushButton::clicked, this, &SettingsDialog::removeProtonPath);
    protonButtons->addWidget(removeProton);
    protonButtons->addStretch(1);
    runnersForm->addRow(QString(), protonButtons);

    m_runnersButton = new QPushButton(Vanta::tr("Download or install runners…"), runners);
    m_runnersButton->setCursor(Qt::PointingHandCursor);
    connect(m_runnersButton, &QPushButton::clicked, this, &SettingsDialog::openRunnersDialog);
    runnersForm->addRow(QString(), m_runnersButton);

    m_wineLogCombo = new QComboBox(runners);
    m_wineLogCombo->addItem(wineLogLevelDisplayName(WineLogLevel::Off), static_cast<int>(WineLogLevel::Off));
    m_wineLogCombo->addItem(wineLogLevelDisplayName(WineLogLevel::Errors), static_cast<int>(WineLogLevel::Errors));
    m_wineLogCombo->addItem(wineLogLevelDisplayName(WineLogLevel::Warnings), static_cast<int>(WineLogLevel::Warnings));
    m_wineLogCombo->addItem(wineLogLevelDisplayName(WineLogLevel::Trace), static_cast<int>(WineLogLevel::Trace));
    runnersForm->addRow(Vanta::tr("Wine log detail"), m_wineLogCombo);
    runnersForm->addRow(UiHelp::helpLabel(
        Vanta::tr("Controls how much Wine writes to the launch log. “Errors only” keeps real failures visible "
                  "without the fixme noise. Every launch keeps the full output in the log viewer, so raising "
                  "this is only useful while diagnosing a problem."),
        runners));

    m_runnerSummary = UiHelp::helpLabel(QString(), runners);
    m_runnerSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    runnersForm->addRow(m_runnerSummary);

    runnersForm->addRow(UiHelp::helpLabel(
        Vanta::tr("Vanta also detects runners installed by Steam, Lutris, Heroic and Bottles, including Flatpak "
                  "installations. Detected runners are available in the game editor without extra setup."),
        runners));
    tabs->addTab(scrollPage(runners, tabs), Vanta::tr("Runners"));

    auto *covers = new QWidget;
    auto *coversForm = formFor(covers);
    m_fetchCoversCheck = new QCheckBox(
        Vanta::tr("Fetch missing cover art automatically when a game is added"), covers);
    coversForm->addRow(QString(), m_fetchCoversCheck);

    m_coverApiKeyEdit = new QLineEdit(covers);
    m_coverApiKeyEdit->setPlaceholderText(CoverFetcher::defaultApiKey());
    m_coverApiKeyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    coversForm->addRow(Vanta::tr("SteamGridDB API key"), m_coverApiKeyEdit);
    coversForm->addRow(UiHelp::helpLabel(
        Vanta::tr("Covers come from SteamGridDB. Vanta ships with a working key, so the field can stay "
                  "empty; paste your own key from steamgriddb.com/profile/preferences/api if you want the "
                  "lookups to use your account instead. Names are matched fuzzily, so a typo like “Testris” "
                  "still resolves to “Tetris”."),
        covers));

    auto *coverDirectoryRow = new QHBoxLayout;
    coverDirectoryRow->setSpacing(8);
    auto *coverDirectory = new QLabel(QDir::toNativeSeparators(CoverFetcher::coversDirectory()), covers);
    coverDirectory->setProperty("class", QStringLiteral("muted"));
    coverDirectory->setTextInteractionFlags(Qt::TextSelectableByMouse);
    coverDirectoryRow->addWidget(coverDirectory, 1);
    auto *openCovers = new QPushButton(Vanta::tr("Open folder"), covers);
    openCovers->setCursor(Qt::PointingHandCursor);
    UiHelp::makeFlat(openCovers);
    connect(openCovers, &QPushButton::clicked, this, [] {
        UiHelp::openContainingDirectory(CoverFetcher::coversDirectory());
    });
    coverDirectoryRow->addWidget(openCovers);
    coversForm->addRow(Vanta::tr("Cover folder"), coverDirectoryRow);
    tabs->addTab(scrollPage(covers, tabs), Vanta::tr("Covers"));

    auto *storage = new QWidget;
    auto *storageForm = formFor(storage);
    const Storage paths;
    m_dataPaths = new QLabel(storage);
    m_dataPaths->setWordWrap(true);
    m_dataPaths->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_dataPaths->setText(Vanta::tr("Library: %1\nSettings: %2\nScan cache: %3")
                             .arg(paths.libraryPath(), paths.settingsPath(), paths.scanCachePath()));
    storageForm->addRow(Vanta::tr("Data files"), m_dataPaths);
    storageForm->addRow(UiHelp::helpLabel(
        Vanta::tr("Vanta stores everything under the XDG config and cache directories using atomic writes. "
                  "Library and settings files can be copied to another machine to move your setup."),
        storage));

    auto *importPanel = new QFrame(storage);
    importPanel->setObjectName(QStringLiteral("infoPanel"));
    auto *importLayout = new QVBoxLayout(importPanel);
    importLayout->setContentsMargins(16, 14, 16, 14);
    importLayout->setSpacing(8);
    importLayout->addWidget(UiHelp::sectionLabel(Vanta::tr("External library porting"), importPanel));
    importLayout->addWidget(UiHelp::helpLabel(
        Vanta::tr("Scan your machine for installed games from Lutris and Heroic Games Launcher, "
                  "and import them into Vanta automatically."),
        importPanel));
    m_portButton = new QPushButton(Vanta::tr("Port external libraries…"), importPanel);
    m_portButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makePrimary(m_portButton);
    connect(m_portButton, &QPushButton::clicked, this, &SettingsDialog::openImportDialog);
    importLayout->addWidget(m_portButton, 0, Qt::AlignLeft);
    storageForm->addRow(importPanel);

    tabs->addTab(scrollPage(storage, tabs), Vanta::tr("Data"));

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch(1);
    m_cancelButton = new QPushButton(Vanta::tr("Cancel"), this);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setAutoDefault(false);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(m_cancelButton);
    m_saveButton = new QPushButton(Vanta::tr("Save settings"), this);
    m_saveButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makePrimary(m_saveButton);
    m_saveButton->setDefault(true);
    connect(m_saveButton, &QPushButton::clicked, this, &SettingsDialog::saveAndAccept);
    buttons->addWidget(m_saveButton);
    bodyLayout()->addLayout(buttons);
}

void SettingsDialog::loadFromSettings()
{
    int index = m_themeCombo->findData(static_cast<int>(m_settings.theme));
    m_themeCombo->setCurrentIndex(index >= 0 ? index : 0);
    index = m_languageCombo->findData(m_settings.language);
    m_languageCombo->setCurrentIndex(index >= 0 ? index : 0);
    m_animationsCheck->setChecked(m_settings.animations);
    index = m_coverSizeCombo->findData(static_cast<int>(m_settings.coverSize));
    m_coverSizeCombo->setCurrentIndex(index >= 0 ? index : 1);
    index = m_viewModeCombo->findData(static_cast<int>(m_settings.viewMode));
    m_viewModeCombo->setCurrentIndex(index >= 0 ? index : 0);
    index = m_sortCombo->findData(static_cast<int>(m_settings.sortMode));
    m_sortCombo->setCurrentIndex(index >= 0 ? index : 0);
    m_ascendingCheck->setChecked(m_settings.ascending);
    m_badgesCheck->setChecked(m_settings.showBadges);
    m_rememberSidebarCheck->setChecked(m_settings.rememberSidebarSelection);
    m_rememberGroupsCheck->setChecked(m_settings.rememberCollapsedGroups);
    m_autoScanCheck->setChecked(m_settings.automaticScanning);
    m_rescanCheck->setChecked(m_settings.rescanOnDirectoryChange);
    m_depthSpin->setValue(m_settings.scanDepth);
    m_limitSpin->setValue(m_settings.scanEntryLimit);
    index = m_onlineFixPromptCombo->findData(static_cast<int>(m_settings.onlineFixPrompt));
    m_onlineFixPromptCombo->setCurrentIndex(index >= 0 ? index : 0);
    m_requireSteamCheck->setChecked(m_settings.requireSteamForOnlineFix);
    m_winePathEdit->setText(m_settings.customWinePath);
    m_fetchCoversCheck->setChecked(m_settings.fetchCoversAutomatically);
    m_coverApiKeyEdit->setText(m_settings.steamGridDbApiKey);
    index = m_wineLogCombo->findData(static_cast<int>(m_settings.wineLogLevel));
    m_wineLogCombo->setCurrentIndex(index >= 0 ? index : 1);
    m_protonList->clear();
    for (const QString &path : m_settings.customProtonPaths)
        m_protonList->addItem(path);
}

void SettingsDialog::saveAndAccept()
{
    const int themeIndex = m_themeCombo->currentIndex();
    if (themeIndex >= 0)
        m_settings.theme = static_cast<ThemeChoice>(m_themeCombo->itemData(themeIndex).toInt());

    const int languageIndex = m_languageCombo->currentIndex();
    if (languageIndex >= 0)
        m_settings.language = m_languageCombo->itemData(languageIndex).toString();

    m_settings.animations = m_animationsCheck->isChecked();

    const int coverIndex = m_coverSizeCombo->currentIndex();
    if (coverIndex >= 0)
        m_settings.coverSize = static_cast<CoverSize>(m_coverSizeCombo->itemData(coverIndex).toInt());

    const int viewIndex = m_viewModeCombo->currentIndex();
    if (viewIndex >= 0)
        m_settings.viewMode = static_cast<ViewMode>(m_viewModeCombo->itemData(viewIndex).toInt());

    const int sortIndex = m_sortCombo->currentIndex();
    if (sortIndex >= 0)
        m_settings.sortMode = static_cast<SortMode>(m_sortCombo->itemData(sortIndex).toInt());

    m_settings.ascending = m_ascendingCheck->isChecked();
    m_settings.showBadges = m_badgesCheck->isChecked();
    m_settings.rememberSidebarSelection = m_rememberSidebarCheck->isChecked();
    m_settings.rememberCollapsedGroups = m_rememberGroupsCheck->isChecked();
    m_settings.automaticScanning = m_autoScanCheck->isChecked();
    m_settings.rescanOnDirectoryChange = m_rescanCheck->isChecked();
    m_settings.scanDepth = m_depthSpin->value();
    m_settings.scanEntryLimit = m_limitSpin->value();

    const int promptIndex = m_onlineFixPromptCombo->currentIndex();
    if (promptIndex >= 0)
        m_settings.onlineFixPrompt = static_cast<OnlineFixPrompt>(m_onlineFixPromptCombo->itemData(promptIndex).toInt());
    m_settings.requireSteamForOnlineFix = m_requireSteamCheck->isChecked();
    m_settings.fetchCoversAutomatically = m_fetchCoversCheck->isChecked();
    m_settings.steamGridDbApiKey = m_coverApiKeyEdit->text().trimmed();

    m_settings.customWinePath = m_winePathEdit->text().trimmed();

    const int logIndex = m_wineLogCombo->currentIndex();
    if (logIndex >= 0)
        m_settings.wineLogLevel = static_cast<WineLogLevel>(m_wineLogCombo->itemData(logIndex).toInt());

    QStringList protons;
    for (int row = 0; row < m_protonList->count(); ++row) {
        const QString path = m_protonList->item(row)->text().trimmed();
        if (!path.isEmpty() && !protons.contains(path))
            protons.append(path);
    }
    m_settings.customProtonPaths = protons;

    if (m_replayTutorial)
        m_settings.tutorialCompleted = false;
    accept();
}

void SettingsDialog::browseWine()
{
    const QString file = QFileDialog::getOpenFileName(this, Vanta::tr("Select a Wine executable"),
                                                      m_winePathEdit->text(), QStringLiteral("All files (*)"));
    if (!file.isEmpty())
        m_winePathEdit->setText(file);
}

void SettingsDialog::addProtonPath()
{
    const QString directory = QFileDialog::getExistingDirectory(this, Vanta::tr("Select a Proton build folder"),
                                                                QString(), QFileDialog::ShowDirsOnly);
    if (directory.isEmpty())
        return;
    if (m_protonList->findItems(directory, Qt::MatchExactly).isEmpty())
        m_protonList->addItem(directory);
}

void SettingsDialog::removeProtonPath()
{
    const int row = m_protonList->currentRow();
    if (row >= 0)
        delete m_protonList->takeItem(row);
}

void SettingsDialog::openRunnersDialog()
{
    RunnerInstallDialog dialog(m_theme, this);
    connect(&dialog, &RunnerInstallDialog::runnersChanged, this, [this] {
        refreshRunnerSummary();
        emit runnersChanged();
    });
    dialog.exec();
    refreshRunnerSummary();
}

void SettingsDialog::openImportDialog()
{
    if (!m_library)
        return;
    LibraryImportDialog dialog(m_library, m_theme, this);
    connect(&dialog, &LibraryImportDialog::gamesImportedList, this, [this](const QList<Game> &games) {
        if (!games.isEmpty()) {
            emit gamesImported(games);
            emit libraryChanged();
        }
    });
    dialog.exec();
}

void SettingsDialog::refreshRunnerSummary()
{
    if (!m_runnerSummary)
        return;
    AppSettings probe = m_settings;
    probe.customWinePath = m_winePathEdit->text().trimmed();
    const QList<RunnerEntry> wines = RunnerDetector::wineEntries(probe);
    const QList<RunnerEntry> protons = RunnerDetector::protonEntries(probe);
    QStringList lines;
    lines.append(Vanta::tr("Detected Wine builds: %1").arg(wines.size()));
    lines.append(Vanta::tr("Detected Proton builds: %1").arg(protons.size()));
    for (const RunnerEntry &entry : wines) {
        lines.append(QStringLiteral("• %1 — %2").arg(entry.name, entry.path));
    }
    for (const RunnerEntry &entry : protons) {
        lines.append(QStringLiteral("• %1 — %2").arg(entry.name, entry.path));
    }
    m_runnerSummary->setText(lines.join(QLatin1Char('\n')));
}

void SettingsDialog::refreshIcons()
{
    if (!m_theme)
        return;
    const ThemeColors &colors = m_theme->colors();
    Icons::setButtonIcon(m_replayButton, Glyph::Play, colors.text, 16);
    Icons::setButtonIcon(m_runnersButton, Glyph::Download, colors.text, 16);
    if (m_portButton)
        Icons::setButtonIcon(m_portButton, Glyph::Plus, QColor(Qt::white), 16);
    Icons::setButtonIcon(m_cancelButton, Glyph::Close, colors.text, 16);
    Icons::setButtonIcon(m_saveButton, Glyph::Check, QColor(Qt::white), 16);
}
