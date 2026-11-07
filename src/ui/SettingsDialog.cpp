#include "SettingsDialog.h"
#include "CoverFetcher.h"
#include "IconFactory.h"
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

SettingsDialog::SettingsDialog(const AppSettings &settings, ThemeManager *theme, QWidget *parent)
    : VantaDialog(QStringLiteral("Settings"), theme, parent)
    , m_settings(settings)
    , m_theme(theme)
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
    m_themeCombo->addItem(themeChoiceName(ThemeChoice::System), static_cast<int>(ThemeChoice::System));
    m_themeCombo->addItem(themeChoiceName(ThemeChoice::Light), static_cast<int>(ThemeChoice::Light));
    m_themeCombo->addItem(themeChoiceName(ThemeChoice::Dark), static_cast<int>(ThemeChoice::Dark));
    m_themeCombo->addItem(themeChoiceName(ThemeChoice::SystemColors), static_cast<int>(ThemeChoice::SystemColors));
    appearanceForm->addRow(QStringLiteral("Theme"), m_themeCombo);
    appearanceForm->addRow(UiHelp::helpLabel(
        QStringLiteral("System (light or dark) follows the desktop color scheme reported by Qt. "
                       "Follow system colors reads the palette of the active desktop theme directly, so a "
                       "Breeze Dark or Breeze Light scheme is reproduced with its own window, text and accent "
                       "colors instead of Vanta's built-in palette."),
        appearance));

    m_animationsCheck = new QCheckBox(QStringLiteral("Enable subtle hover and selection animations"), appearance);
    appearanceForm->addRow(QString(), m_animationsCheck);

    m_coverSizeCombo = new QComboBox(appearance);
    m_coverSizeCombo->addItem(coverSizeName(CoverSize::Small), static_cast<int>(CoverSize::Small));
    m_coverSizeCombo->addItem(coverSizeName(CoverSize::Medium), static_cast<int>(CoverSize::Medium));
    m_coverSizeCombo->addItem(coverSizeName(CoverSize::Large), static_cast<int>(CoverSize::Large));
    appearanceForm->addRow(QStringLiteral("Cover size"), m_coverSizeCombo);

    m_viewModeCombo = new QComboBox(appearance);
    m_viewModeCombo->addItem(viewModeName(ViewMode::Grid), static_cast<int>(ViewMode::Grid));
    m_viewModeCombo->addItem(viewModeName(ViewMode::List), static_cast<int>(ViewMode::List));
    appearanceForm->addRow(QStringLiteral("Default view"), m_viewModeCombo);

    m_sortCombo = new QComboBox(appearance);
    m_sortCombo->addItem(sortModeName(SortMode::Name), static_cast<int>(SortMode::Name));
    m_sortCombo->addItem(sortModeName(SortMode::LastPlayed), static_cast<int>(SortMode::LastPlayed));
    m_sortCombo->addItem(sortModeName(SortMode::Playtime), static_cast<int>(SortMode::Playtime));
    appearanceForm->addRow(QStringLiteral("Sort by"), m_sortCombo);

    m_ascendingCheck = new QCheckBox(QStringLiteral("Sort ascending"), appearance);
    appearanceForm->addRow(QString(), m_ascendingCheck);

    m_badgesCheck = new QCheckBox(QStringLiteral("Show runner and platform badges on cards"), appearance);
    appearanceForm->addRow(QString(), m_badgesCheck);

    m_rememberSidebarCheck = new QCheckBox(QStringLiteral("Remember the selected sidebar filter"), appearance);
    appearanceForm->addRow(QString(), m_rememberSidebarCheck);

    m_rememberGroupsCheck = new QCheckBox(QStringLiteral("Remember collapsed sidebar groups"), appearance);
    appearanceForm->addRow(QString(), m_rememberGroupsCheck);

    m_replayButton = new QPushButton(QStringLiteral("Show the tutorial again"), appearance);
    m_replayButton->setCursor(Qt::PointingHandCursor);
    connect(m_replayButton, &QPushButton::clicked, this, [this] {
        m_replayTutorial = true;
        m_settings.tutorialCompleted = false;
    });
    appearanceForm->addRow(QString(), m_replayButton);
    tabs->addTab(scrollPage(appearance, tabs), QStringLiteral("Appearance"));

    auto *library = new QWidget;
    auto *libraryForm = formFor(library);
    m_autoScanCheck = new QCheckBox(QStringLiteral("Scan new games automatically when they are added"), library);
    libraryForm->addRow(QString(), m_autoScanCheck);
    m_rescanCheck = new QCheckBox(QStringLiteral("Rescan when the game folder changed since the last scan"), library);
    libraryForm->addRow(QString(), m_rescanCheck);

    m_depthSpin = new QSpinBox(library);
    m_depthSpin->setRange(2, 24);
    m_depthSpin->setSuffix(QStringLiteral(" levels"));
    libraryForm->addRow(QStringLiteral("Scan depth"), m_depthSpin);

    m_limitSpin = new QSpinBox(library);
    m_limitSpin->setRange(1000, 250000);
    m_limitSpin->setSingleStep(1000);
    m_limitSpin->setSuffix(QStringLiteral(" entries"));
    libraryForm->addRow(QStringLiteral("Scan entry limit"), m_limitSpin);

    libraryForm->addRow(UiHelp::helpLabel(
        QStringLiteral("Scans run in the background and are read-only, except for confirmed OnlineFix INI updates and "
                       "the Photon Newtonsoft.Json link. Lower limits keep very large folders responsive."),
        library));
    tabs->addTab(scrollPage(library, tabs), QStringLiteral("Scanning"));

    auto *onlineFix = new QWidget;
    auto *fixForm = formFor(onlineFix);
    m_onlineFixPromptCombo = new QComboBox(onlineFix);
    m_onlineFixPromptCombo->addItem(QStringLiteral("Ask before every launch"),
                                    static_cast<int>(OnlineFixPrompt::Ask));
    m_onlineFixPromptCombo->addItem(QStringLiteral("Always apply when detected"),
                                    static_cast<int>(OnlineFixPrompt::AlwaysApply));
    m_onlineFixPromptCombo->addItem(QStringLiteral("Never apply"),
                                    static_cast<int>(OnlineFixPrompt::NeverApply));
    fixForm->addRow(QStringLiteral("Launch policy"), m_onlineFixPromptCombo);

    m_requireSteamCheck = new QCheckBox(QStringLiteral("Require Steam to be running before an OnlineFix launch"),
                                        onlineFix);
    fixForm->addRow(QString(), m_requireSteamCheck);

    fixForm->addRow(UiHelp::helpLabel(
        QStringLiteral("Per-game modes override this policy. OnlineFix overrides are applied to the launch environment "
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
    m_winePathEdit->setPlaceholderText(QStringLiteral("Optional custom Wine executable or build folder"));
    wineLayout->addWidget(m_winePathEdit, 1);
    auto *wineBrowse = new QPushButton(QStringLiteral("Browse…"), wineRow);
    wineBrowse->setCursor(Qt::PointingHandCursor);
    UiHelp::makeFlat(wineBrowse);
    connect(wineBrowse, &QPushButton::clicked, this, &SettingsDialog::browseWine);
    wineLayout->addWidget(wineBrowse);
    runnersForm->addRow(QStringLiteral("Custom Wine"), wineRow);

    m_protonList = new QListWidget(runners);
    m_protonList->setObjectName(QStringLiteral("protonPathList"));
    m_protonList->setMinimumHeight(110);
    m_protonList->setSelectionMode(QAbstractItemView::SingleSelection);
    runnersForm->addRow(QStringLiteral("Custom Proton paths"), m_protonList);

    auto *protonButtons = new QHBoxLayout;
    protonButtons->setSpacing(7);
    auto *addProton = new QPushButton(QStringLiteral("Add folder…"), runners);
    addProton->setCursor(Qt::PointingHandCursor);
    connect(addProton, &QPushButton::clicked, this, &SettingsDialog::addProtonPath);
    protonButtons->addWidget(addProton);
    auto *removeProton = new QPushButton(QStringLiteral("Remove"), runners);
    removeProton->setCursor(Qt::PointingHandCursor);
    connect(removeProton, &QPushButton::clicked, this, &SettingsDialog::removeProtonPath);
    protonButtons->addWidget(removeProton);
    protonButtons->addStretch(1);
    runnersForm->addRow(QString(), protonButtons);

    m_runnersButton = new QPushButton(QStringLiteral("Download or install runners…"), runners);
    m_runnersButton->setCursor(Qt::PointingHandCursor);
    connect(m_runnersButton, &QPushButton::clicked, this, &SettingsDialog::openRunnersDialog);
    runnersForm->addRow(QString(), m_runnersButton);

    m_runnerSummary = UiHelp::helpLabel(QString(), runners);
    m_runnerSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    runnersForm->addRow(m_runnerSummary);

    runnersForm->addRow(UiHelp::helpLabel(
        QStringLiteral("Vanta also detects runners installed by Steam, Lutris, Heroic and Bottles, including Flatpak "
                       "installations. Detected runners are available in the game editor without extra setup."),
        runners));
    tabs->addTab(scrollPage(runners, tabs), QStringLiteral("Runners"));

    auto *covers = new QWidget;
    auto *coversForm = formFor(covers);
    m_fetchCoversCheck = new QCheckBox(
        QStringLiteral("Fetch missing cover art automatically when a game is added"), covers);
    coversForm->addRow(QString(), m_fetchCoversCheck);

    m_coverApiKeyEdit = new QLineEdit(covers);
    m_coverApiKeyEdit->setPlaceholderText(CoverFetcher::defaultApiKey());
    m_coverApiKeyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    coversForm->addRow(QStringLiteral("SteamGridDB API key"), m_coverApiKeyEdit);
    coversForm->addRow(UiHelp::helpLabel(
        QStringLiteral("Covers come from SteamGridDB. Vanta ships with a working key, so the field can stay "
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
    auto *openCovers = new QPushButton(QStringLiteral("Open folder"), covers);
    openCovers->setCursor(Qt::PointingHandCursor);
    UiHelp::makeFlat(openCovers);
    connect(openCovers, &QPushButton::clicked, this, [] {
        UiHelp::openContainingDirectory(CoverFetcher::coversDirectory());
    });
    coverDirectoryRow->addWidget(openCovers);
    coversForm->addRow(QStringLiteral("Cover folder"), coverDirectoryRow);
    tabs->addTab(scrollPage(covers, tabs), QStringLiteral("Covers"));

    auto *storage = new QWidget;
    auto *storageForm = formFor(storage);
    const Storage paths;
    m_dataPaths = new QLabel(storage);
    m_dataPaths->setWordWrap(true);
    m_dataPaths->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_dataPaths->setText(QStringLiteral("Library: %1\nSettings: %2\nScan cache: %3")
                             .arg(paths.libraryPath(), paths.settingsPath(), paths.scanCachePath()));
    storageForm->addRow(QStringLiteral("Data files"), m_dataPaths);
    storageForm->addRow(UiHelp::helpLabel(
        QStringLiteral("Vanta stores everything under the XDG config and cache directories using atomic writes. "
                       "Library and settings files can be copied to another machine to move your setup."),
        storage));
    tabs->addTab(scrollPage(storage, tabs), QStringLiteral("Data"));

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch(1);
    m_cancelButton = new QPushButton(QStringLiteral("Cancel"), this);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(m_cancelButton);
    m_saveButton = new QPushButton(QStringLiteral("Save settings"), this);
    m_saveButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makePrimary(m_saveButton);
    connect(m_saveButton, &QPushButton::clicked, this, &SettingsDialog::saveAndAccept);
    buttons->addWidget(m_saveButton);
    bodyLayout()->addLayout(buttons);
}

void SettingsDialog::loadFromSettings()
{
    int index = m_themeCombo->findData(static_cast<int>(m_settings.theme));
    m_themeCombo->setCurrentIndex(index >= 0 ? index : 0);
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
    m_protonList->clear();
    for (const QString &path : m_settings.customProtonPaths)
        m_protonList->addItem(path);
}

void SettingsDialog::saveAndAccept()
{
    const int themeIndex = m_themeCombo->currentIndex();
    if (themeIndex >= 0)
        m_settings.theme = static_cast<ThemeChoice>(m_themeCombo->itemData(themeIndex).toInt());
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
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Select a Wine executable"),
                                                      m_winePathEdit->text(), QStringLiteral("All files (*)"));
    if (!file.isEmpty())
        m_winePathEdit->setText(file);
}

void SettingsDialog::addProtonPath()
{
    const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Select a Proton build folder"),
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

void SettingsDialog::refreshRunnerSummary()
{
    if (!m_runnerSummary)
        return;
    AppSettings probe = m_settings;
    probe.customWinePath = m_winePathEdit->text().trimmed();
    const QList<RunnerEntry> wines = RunnerDetector::wineEntries(probe);
    const QList<RunnerEntry> protons = RunnerDetector::protonEntries(probe);
    QStringList lines;
    lines.append(QStringLiteral("Detected Wine builds: %1").arg(wines.size()));
    lines.append(QStringLiteral("Detected Proton builds: %1").arg(protons.size()));
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
    Icons::setButtonIcon(m_cancelButton, Glyph::Close, colors.text, 16);
    Icons::setButtonIcon(m_saveButton, Glyph::Check, QColor(Qt::white), 16);
}
