#include "GameDetailsDialog.h"
#include "IconFactory.h"
#include "LaunchPlan.h"
#include "ThemeManager.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {
QLabel *chipLabel(const QString &text, QWidget *parent, bool accent = false)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("chip", true);
    if (accent)
        label->setProperty("accent", true);
    label->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return label;
}

QLineEdit *pathField(const QString &text, QWidget *parent)
{
    auto *field = new QLineEdit(parent);
    field->setReadOnly(true);
    field->setText(text);
    field->setCursorPosition(0);
    field->setToolTip(text);
    return field;
}
}

GameDetailsDialog::GameDetailsDialog(const Game &game, const ScanResult *scanResult, bool running,
                                     ThemeManager *theme, QWidget *parent)
    : VantaDialog(QStringLiteral("Game details"), theme, parent)
    , m_game(game)
    , m_running(running)
    , m_theme(theme)
{
    if (scanResult) {
        m_scan = *scanResult;
        m_hasScan = true;
    }
    setObjectName(QStringLiteral("gameDetailsDialog"));
    setMinimumSize(780, 620);
    resize(840, 720);
    buildUi();
    updateActionButtons();
    refreshIcons();
    if (m_theme)
        connect(m_theme, &ThemeManager::themeChanged, this, &GameDetailsDialog::refreshIcons);
}

void GameDetailsDialog::setLogsAvailable(bool available)
{
    m_logsAvailable = available;
    if (m_logsButton)
        m_logsButton->setVisible(available);
}

void GameDetailsDialog::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    updateActionButtons();
}

void GameDetailsDialog::updateActionButtons()
{
    m_playButton->setVisible(!m_running);
    m_stopButton->setVisible(m_running);
}

void GameDetailsDialog::setAction(Action action)
{
    m_action = action;
    accept();
}

void GameDetailsDialog::buildUi()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(2, 2, 8, 2);
    layout->setSpacing(16);

    auto *hero = new QHBoxLayout;
    hero->setSpacing(20);

    auto *cover = new QLabel(content);
    cover->setFixedSize(150, 200);
    cover->setAlignment(Qt::AlignCenter);
    const ThemeColors colors = m_theme ? m_theme->colors() : ThemeColors();
    cover->setPixmap(Icons::roundedCover(m_game.coverPath, QSize(150, 200),
                                         initialsForGame(m_game.name), colors.accentSoft,
                                         colors.accent, 13));
    hero->addWidget(cover, 0, Qt::AlignTop);

    auto *summary = new QVBoxLayout;
    summary->setSpacing(8);

    auto *title = new QLabel(m_game.name.isEmpty() ? QStringLiteral("Untitled game") : m_game.name, content);
    title->setProperty("class", QStringLiteral("pageTitle"));
    title->setWordWrap(true);
    summary->addWidget(title);

    auto *chips = new QHBoxLayout;
    chips->setSpacing(6);
    chips->addWidget(chipLabel(m_game.source.isEmpty() ? QStringLiteral("Manual") : m_game.source,
                               content, true));
    chips->addWidget(chipLabel(runnerName(m_game.runner), content));
    chips->addWidget(chipLabel(platformName(m_game.platform), content));
    if (m_game.favorite)
        chips->addWidget(chipLabel(QStringLiteral("Favorite"), content));
    if (m_running)
        chips->addWidget(chipLabel(QStringLiteral("Running"), content, true));
    chips->addStretch(1);
    summary->addLayout(chips);

    auto *stats = new QLabel(QStringLiteral("Playtime: %1    ·    Last played: %2    ·    Added: %3")
                                 .arg(formatPlaytime(m_game.playtimeSeconds),
                                      formatLastPlayed(m_game.lastPlayed),
                                      formatLastPlayed(m_game.addedAt)),
                             content);
    stats->setProperty("class", QStringLiteral("muted"));
    stats->setWordWrap(true);
    summary->addWidget(stats);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(8);
    m_playButton = new QPushButton(QStringLiteral("Play"), content);
    m_playButton->setCursor(Qt::PointingHandCursor);
    // A details sheet must not launch or stop a game on Enter. When Play is
    // hidden for a running game Qt promoted Stop to the implicit default, so a
    // stray Enter killed the session.
    m_playButton->setAutoDefault(false);
    UiHelp::makePrimary(m_playButton);
    connect(m_playButton, &QPushButton::clicked, this, [this] { setAction(Action::Launch); });
    actions->addWidget(m_playButton);

    m_stopButton = new QPushButton(QStringLiteral("Stop"), content);
    m_stopButton->setCursor(Qt::PointingHandCursor);
    m_stopButton->setAutoDefault(false);
    UiHelp::makeDanger(m_stopButton);
    connect(m_stopButton, &QPushButton::clicked, this, [this] { setAction(Action::Stop); });
    actions->addWidget(m_stopButton);

    m_editButton = new QPushButton(QStringLiteral("Edit"), content);
    m_editButton->setCursor(Qt::PointingHandCursor);
    m_editButton->setAutoDefault(false);
    connect(m_editButton, &QPushButton::clicked, this, [this] { setAction(Action::Edit); });
    actions->addWidget(m_editButton);

    m_onlineFixButton = new QPushButton(QStringLiteral("OnlineFix…"), content);
    m_onlineFixButton->setCursor(Qt::PointingHandCursor);
    m_onlineFixButton->setAutoDefault(false);
    connect(m_onlineFixButton, &QPushButton::clicked, this, [this] { setAction(Action::OnlineFix); });
    actions->addWidget(m_onlineFixButton);

    m_modsButton = new QPushButton(QStringLiteral("Mods…"), content);
    m_modsButton->setCursor(Qt::PointingHandCursor);
    m_modsButton->setAutoDefault(false);
    m_modsButton->setVisible(m_game.platform == Platform::Windows || runnerSupportsBepInEx(m_game.runner));
    connect(m_modsButton, &QPushButton::clicked, this, [this] { setAction(Action::Mods); });
    actions->addWidget(m_modsButton);

    m_scanButton = new QPushButton(QStringLiteral("Scan"), content);
    m_scanButton->setCursor(Qt::PointingHandCursor);
    m_scanButton->setAutoDefault(false);
    connect(m_scanButton, &QPushButton::clicked, this, [this] { setAction(Action::Scan); });
    actions->addWidget(m_scanButton);
    actions->addStretch(1);
    summary->addLayout(actions);
    summary->addStretch(1);
    hero->addLayout(summary, 1);
    layout->addLayout(hero);

    layout->addWidget(UiHelp::separator(content));

    auto *pathsPanel = new QFrame(content);
    pathsPanel->setObjectName(QStringLiteral("infoPanel"));
    auto *pathsLayout = new QVBoxLayout(pathsPanel);
    pathsLayout->setContentsMargins(16, 14, 16, 14);
    pathsLayout->setSpacing(10);
    pathsLayout->addWidget(UiHelp::sectionLabel(QStringLiteral("Installation"), pathsPanel));
    auto *pathsForm = new QFormLayout;
    pathsForm->setHorizontalSpacing(14);
    pathsForm->setVerticalSpacing(8);
    pathsForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    pathsForm->addRow(QStringLiteral("Executable"), pathField(m_game.executable, pathsPanel));
    pathsForm->addRow(QStringLiteral("Game folder"), pathField(m_game.gameDirectory, pathsPanel));
    pathsForm->addRow(QStringLiteral("Working folder"), pathField(m_game.effectiveWorkingDirectory(), pathsPanel));
    if (runnerIsWineLike(m_game.runner)) {
        pathsForm->addRow(QStringLiteral("Runner path"), pathField(m_game.runnerPath, pathsPanel));
        pathsForm->addRow(QStringLiteral("Prefix"), pathField(LaunchPlanBuilder::effectivePrefix(m_game), pathsPanel));
    }
    if (!m_game.launchArguments.trimmed().isEmpty())
        pathsForm->addRow(QStringLiteral("Arguments"), pathField(m_game.launchArguments, pathsPanel));
    pathsLayout->addLayout(pathsForm);
    layout->addWidget(pathsPanel);

    auto *compatPanel = new QFrame(content);
    compatPanel->setObjectName(QStringLiteral("compatibilityPanel"));
    auto *compatLayout = new QVBoxLayout(compatPanel);
    compatLayout->setContentsMargins(16, 14, 16, 14);
    compatLayout->setSpacing(9);
    auto *compatHeader = new QHBoxLayout;
    compatHeader->addWidget(UiHelp::sectionLabel(QStringLiteral("Compatibility scan"), compatPanel));
    compatHeader->addStretch(1);
    const ScanState state = m_hasScan ? m_scan.state : m_game.compatibility;
    auto *stateLabel = new QLabel(scanStateLabel(state), compatPanel);
    stateLabel->setProperty("class", scanNeedsAttention(state) ? QStringLiteral("warning")
        : (scanIsDetected(state) ? QStringLiteral("success") : QStringLiteral("muted")));
    compatHeader->addWidget(stateLabel);
    compatLayout->addLayout(compatHeader);

    auto *explanation = UiHelp::helpLabel(compatibilityExplanation(), compatPanel);
    compatLayout->addWidget(explanation);

    if (m_hasScan) {
        if (!m_scan.executablePath.isEmpty()) {
            compatLayout->addWidget(UiHelp::helpLabel(
                QStringLiteral("Selected executable: %1 (%2)")
                    .arg(m_scan.executablePath,
                         m_scan.executableArchitecture.isEmpty() ? QStringLiteral("unknown architecture")
                                                                 : m_scan.executableArchitecture),
                compatPanel));
        }
        if (!m_scan.components.isEmpty()) {
            auto *table = new QTableWidget(compatPanel);
            table->setObjectName(QStringLiteral("componentTable"));
            table->setColumnCount(3);
            table->setHorizontalHeaderLabels({QStringLiteral("Component"), QStringLiteral("Category"),
                                              QStringLiteral("Architecture")});
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            table->setSelectionBehavior(QAbstractItemView::SelectRows);
            table->setSelectionMode(QAbstractItemView::SingleSelection);
            table->setShowGrid(false);
            table->verticalHeader()->setVisible(false);
            table->verticalHeader()->setDefaultSectionSize(30);
            table->horizontalHeader()->setHighlightSections(false);
            table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
            table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
            table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            const int count = qMin<int>(m_scan.components.size(), 200);
            table->setRowCount(count);
            for (int row = 0; row < count; ++row) {
                const CompatibilityComponent &component = m_scan.components.at(row);
                table->setItem(row, 0, new QTableWidgetItem(component.name));
                table->setItem(row, 1, new QTableWidgetItem(component.category));
                table->setItem(row, 2, new QTableWidgetItem(component.architecture));
            }
            table->setMinimumHeight(qMin(260, 34 + count * 30));
            compatLayout->addWidget(table);
            if (m_scan.components.size() > count)
                compatLayout->addWidget(UiHelp::helpLabel(
                    QStringLiteral("Showing %1 of %2 detected components.").arg(count).arg(m_scan.components.size()),
                    compatPanel));
        }
        if (!m_scan.warnings.isEmpty()) {
            compatLayout->addWidget(UiHelp::helpLabel(
                QStringLiteral("Warnings: %1").arg(m_scan.warnings.join(QStringLiteral(" "))), compatPanel));
        }
        if (!m_scan.missingPaths.isEmpty()) {
            compatLayout->addWidget(UiHelp::helpLabel(
                QStringLiteral("Missing paths: %1").arg(m_scan.missingPaths.join(QStringLiteral(", "))),
                compatPanel));
        }
        if (m_scan.scannedAt.isValid()) {
            compatLayout->addWidget(UiHelp::helpLabel(
                QStringLiteral("Last scanned: %1").arg(formatLastPlayed(m_scan.scannedAt)), compatPanel));
        }
    }
    compatLayout->addWidget(UiHelp::helpLabel(
        QStringLiteral("Detection is informational only. Vanta does not execute detected components by itself and "
                       "cannot confirm that a game or a fix will work."),
        compatPanel));
    layout->addWidget(compatPanel);

    const bool fixDetected = (m_hasScan && m_scan.onlineFix.detected) || m_game.fixKind != FixKind::Unknown;
    if (fixDetected) {
        auto *fixPanel = new QFrame(content);
        fixPanel->setObjectName(QStringLiteral("diagnosticsPanel"));
        auto *fixLayout = new QVBoxLayout(fixPanel);
        fixLayout->setContentsMargins(16, 14, 16, 14);
        fixLayout->setSpacing(7);
        fixLayout->addWidget(UiHelp::sectionLabel(QStringLiteral("OnlineFix files"), fixPanel));
        const FixKind kind = m_hasScan ? m_scan.onlineFix.kind : m_game.fixKind;
        fixLayout->addWidget(UiHelp::helpLabel(
            QStringLiteral("Detected: %1. Detection never means the fix is functional. Vanta only writes OnlineFix "
                           "configuration files after creating backups, and applies DLL overrides to the launch "
                           "environment without storing them.")
                .arg(fixKindName(kind)),
            fixPanel));
        const int fileCount = m_hasScan ? m_scan.onlineFix.files.size() : 0;
        if (fileCount > 0)
            fixLayout->addWidget(UiHelp::helpLabel(QStringLiteral("%1 signature file(s) detected.").arg(fileCount),
                                                   fixPanel));
        if (m_game.onlineFixMode == OnlineFixMode::Disabled) {
            // Detection is still worth reporting; what changes is that the
            // launch path ignores it, and the panel has to say so.
            fixLayout->addWidget(UiHelp::helpLabel(
                QStringLiteral("OnlineFix is disabled for this game, so a launch does not apply this configuration."),
                fixPanel));
        } else if (!m_hasScan || !m_scan.onlineFix.hasOnlineFixIni) {
            fixLayout->addWidget(UiHelp::helpLabel(
                QStringLiteral("Use the OnlineFix dialog to review detection, available launch overrides, and "
                               "per-game policy."),
                fixPanel));
        }
        layout->addWidget(fixPanel);
    }

    layout->addStretch(1);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    bodyLayout()->addWidget(scroll, 1);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    m_logsButton = new QPushButton(QStringLiteral("Logs…"), this);
    m_logsButton->setCursor(Qt::PointingHandCursor);
    m_logsButton->setVisible(m_logsAvailable);
    connect(m_logsButton, &QPushButton::clicked, this, [this] { setAction(Action::Logs); });
    footer->addWidget(m_logsButton);

    m_folderButton = new QPushButton(QStringLiteral("Open folder"), this);
    m_folderButton->setCursor(Qt::PointingHandCursor);
    connect(m_folderButton, &QPushButton::clicked, this, [this] { setAction(Action::OpenFolder); });
    footer->addWidget(m_folderButton);

    m_prefixButton = new QPushButton(QStringLiteral("Open prefix"), this);
    m_prefixButton->setCursor(Qt::PointingHandCursor);
    m_prefixButton->setVisible(runnerIsWineLike(m_game.runner));
    connect(m_prefixButton, &QPushButton::clicked, this, [this] { setAction(Action::OpenPrefix); });
    footer->addWidget(m_prefixButton);
    footer->addStretch(1);

    m_closeButton = new QPushButton(QStringLiteral("Close"), this);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    m_closeButton->setDefault(true);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(m_closeButton);
    bodyLayout()->addLayout(footer);
}

QString GameDetailsDialog::compatibilityExplanation() const
{
    if (!m_hasScan)
        return QStringLiteral("This game has not been scanned yet. Run a scan to inspect executables, components, "
                              "prefixes, and OnlineFix signatures. Scanning is read-only.");
    switch (m_scan.state) {
    case ScanState::NotScanned:
        return QStringLiteral("No scan has been recorded for this game.");
    case ScanState::Scanning:
        return QStringLiteral("A scan is currently running. Results appear when it finishes.");
    case ScanState::NoKnownComponents:
        return QStringLiteral("No known compatibility components were detected. This is not an error: many games are "
                              "self-contained or ship files Vanta does not recognize.");
    case ScanState::ComponentsDetected:
        return QStringLiteral("%1 compatibility component(s) were detected. Use the game's runner settings to choose "
                              "how they should be used; Vanta does not enable them automatically.")
            .arg(m_scan.components.size());
    case ScanState::IncompleteInstallation:
        return QStringLiteral("The installation appears incomplete. Verify the game files and try scanning again.");
    case ScanState::IncompatibleArchitecture:
        return QStringLiteral("The selected executable has an architecture that may not work with the configured "
                              "runner. Check the executable and runner combination.");
    case ScanState::MissingRequiredFiles:
        return QStringLiteral("Some expected files are missing. Verify that the game folder contains a complete "
                              "installation.");
    case ScanState::ScanError:
        return QStringLiteral("The scan did not complete successfully: %1")
            .arg(m_scan.error.isEmpty() ? QStringLiteral("unknown error") : m_scan.error);
    }
    return QStringLiteral("No compatibility information is available.");
}

void GameDetailsDialog::refreshIcons()
{
    if (!m_theme)
        return;
    const ThemeColors &colors = m_theme->colors();
    Icons::setButtonIcon(m_playButton, Glyph::Play, QColor(Qt::white), 17);
    Icons::setButtonIcon(m_stopButton, Glyph::Stop, colors.danger, 17);
    Icons::setButtonIcon(m_editButton, Glyph::Pencil, colors.text, 16);
    Icons::setButtonIcon(m_onlineFixButton, Glyph::Shield, colors.text, 16);
    Icons::setButtonIcon(m_modsButton, Glyph::Download, colors.text, 16);
    Icons::setButtonIcon(m_logsButton, Glyph::Terminal, colors.text, 16);
    Icons::setButtonIcon(m_scanButton, Glyph::Scan, colors.text, 16);
    Icons::setButtonIcon(m_folderButton, Glyph::Folder, colors.text, 16);
    Icons::setButtonIcon(m_prefixButton, Glyph::Wine, colors.text, 16);
    Icons::setButtonIcon(m_closeButton, Glyph::Check, colors.text, 16);
}
