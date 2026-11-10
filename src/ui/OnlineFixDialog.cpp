#include "OnlineFixDialog.h"
#include "I18n.h"
#include "IconFactory.h"
#include "ThemeManager.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {
QLineEdit *readOnlyField(const QString &text, QWidget *parent)
{
    auto *field = new QLineEdit(parent);
    field->setReadOnly(true);
    field->setText(text);
    field->setCursorPosition(0);
    field->setToolTip(text);
    return field;
}

QLabel *chip(const QString &text, QWidget *parent, bool accent = false)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("chip", true);
    if (accent)
        label->setProperty("accent", true);
    label->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return label;
}
}

OnlineFixDialog::OnlineFixDialog(const Game &game, const OnlineFixInfo &info, Mode mode,
                                 ThemeManager *theme, QWidget *parent)
    : VantaDialog(mode == Mode::LaunchPrompt ? Vanta::tr("OnlineFix launch policy")
                                             : QStringLiteral("OnlineFix"),
                  theme, parent)
    , m_game(game)
    , m_info(info)
    , m_mode(mode)
    , m_theme(theme)
{
    setObjectName(mode == Mode::LaunchPrompt ? QStringLiteral("onlineFixPromptDialog")
                                             : QStringLiteral("onlineFixDialog"));
    if (mode == Mode::LaunchPrompt) {
        setMinimumSize(620, 480);
        resize(680, 540);
        buildPromptUi();
    } else {
        setMinimumSize(760, 620);
        resize(820, 720);
        buildManageUi();
    }
    refreshIcons();
    if (m_theme)
        connect(m_theme, &ThemeManager::themeChanged, this, &OnlineFixDialog::refreshIcons);
}

OnlineFixDialog::LaunchChoice OnlineFixDialog::promptForLaunch(const Game &game, const OnlineFixInfo &info,
                                                               ThemeManager *theme, QWidget *parent)
{
    OnlineFixDialog dialog(game, info, Mode::LaunchPrompt, theme, parent);
    if (dialog.exec() != QDialog::Accepted)
        return LaunchChoice::Cancel;
    return dialog.m_launchChoice;
}

void OnlineFixDialog::buildManageUi()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(2, 2, 8, 2);
    layout->setSpacing(14);

    auto *noticePanel = new QFrame(content);
    noticePanel->setObjectName(QStringLiteral("diagnosticsPanel"));
    auto *noticeLayout = new QVBoxLayout(noticePanel);
    noticeLayout->setContentsMargins(16, 14, 16, 14);
    noticeLayout->setSpacing(7);
    auto *noticeTitle = new QLabel(Vanta::tr("OnlineFix support is provided as-is"), noticePanel);
    noticeTitle->setProperty("class", QStringLiteral("warning"));
    QFont noticeFont = noticeTitle->font();
    noticeFont.setWeight(QFont::DemiBold);
    noticeTitle->setFont(noticeFont);
    noticeLayout->addWidget(noticeTitle);
    noticeLayout->addWidget(UiHelp::helpLabel(
        Vanta::tr("Vanta ports the launcher-side behavior of the OnlineFix Linux project. It can detect "
                       "OnlineFix signature files, apply the configuration changes the fix expects (creating backups "
                       "first), and compose the required Wine DLL overrides for a single launch. Vanta does not modify "
                       "game executables, does not download OnlineFix files, and cannot confirm that a fix works. "
                       "Detection is not proof of functionality."),
        noticePanel));
    layout->addWidget(noticePanel);

    auto *summaryRow = new QHBoxLayout;
    summaryRow->setSpacing(7);
    summaryRow->addWidget(chip(m_info.detected ? Vanta::tr("Detected") : Vanta::tr("Not detected"),
                              content, m_info.detected));
    summaryRow->addWidget(chip(fixKindDisplayName(m_info.kind), content));
    summaryRow->addWidget(chip(Vanta::tr("%1 file(s)").arg(m_info.files.size()), content));
    if (m_info.hasSteamFixIni)
        summaryRow->addWidget(chip(QStringLiteral("steamfix.ini"), content));
    if (m_info.hasOnlineFixIni)
        summaryRow->addWidget(chip(QStringLiteral("onlinefix.ini"), content));
    summaryRow->addStretch(1);
    layout->addLayout(summaryRow);

    if (!m_info.files.isEmpty()) {
        auto *filesPanel = new QFrame(content);
        filesPanel->setObjectName(QStringLiteral("infoPanel"));
        auto *filesLayout = new QVBoxLayout(filesPanel);
        filesLayout->setContentsMargins(16, 14, 16, 14);
        filesLayout->setSpacing(8);
        filesLayout->addWidget(UiHelp::sectionLabel(Vanta::tr("Detected signature files"), filesPanel));
        auto *table = new QTableWidget(filesPanel);
        table->setObjectName(QStringLiteral("onlineFixFileTable"));
        table->setColumnCount(3);
        table->setHorizontalHeaderLabels({Vanta::tr("File"), Vanta::tr("Matched rule"),
                                          Vanta::tr("Size")});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setShowGrid(false);
        table->verticalHeader()->setVisible(false);
        table->verticalHeader()->setDefaultSectionSize(29);
        table->horizontalHeader()->setHighlightSections(false);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        const int count = qMin<int>(m_info.files.size(), 200);
        table->setRowCount(count);
        for (int row = 0; row < count; ++row) {
            const OnlineFixFile &file = m_info.files.at(row);
            table->setItem(row, 0, new QTableWidgetItem(file.fileName));
            table->setItem(row, 1, new QTableWidgetItem(file.matchedRule));
            table->setItem(row, 2, new QTableWidgetItem(UiHelp::dataSize(file.size)));
        }
        table->setMinimumHeight(qMin(230, 34 + count * 29));
        filesLayout->addWidget(table);
        if (m_info.files.size() > count) {
            filesLayout->addWidget(UiHelp::helpLabel(
                Vanta::tr("Showing %1 of %2 detected files.").arg(count).arg(m_info.files.size()),
                filesPanel));
        }
        layout->addWidget(filesPanel);
    }

    auto *detailsPanel = new QFrame(content);
    detailsPanel->setObjectName(QStringLiteral("infoPanel"));
    auto *detailsLayout = new QVBoxLayout(detailsPanel);
    detailsLayout->setContentsMargins(16, 14, 16, 14);
    detailsLayout->setSpacing(9);
    detailsLayout->addWidget(UiHelp::sectionLabel(Vanta::tr("Per-game policy"), detailsPanel));

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(8);
    m_modeCombo = new QComboBox(detailsPanel);
    m_modeCombo->addItem(Vanta::tr("Auto — apply only when OnlineFix files are detected"),
                         static_cast<int>(OnlineFixMode::Auto));
    m_modeCombo->addItem(Vanta::tr("Force — apply launch overrides even when detection is inconclusive"),
                         static_cast<int>(OnlineFixMode::Force));
    m_modeCombo->addItem(Vanta::tr("Disabled — never apply OnlineFix behavior"),
                         static_cast<int>(OnlineFixMode::Disabled));
    const int modeIndex = m_modeCombo->findData(static_cast<int>(m_game.onlineFixMode));
    m_modeCombo->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
    form->addRow(Vanta::tr("OnlineFix mode"), m_modeCombo);

    m_realAppIdEdit = new QLineEdit(detailsPanel);
    m_realAppIdEdit->setText(!m_game.realAppId.isEmpty() ? m_game.realAppId : m_info.realAppId);
    m_realAppIdEdit->setPlaceholderText(Vanta::tr("Optional real application id"));
    form->addRow(Vanta::tr("Real app id"), m_realAppIdEdit);

    m_fakeAppIdEdit = new QLineEdit(detailsPanel);
    m_fakeAppIdEdit->setText(!m_game.fakeAppId.isEmpty() ? m_game.fakeAppId : m_info.fakeAppId);
    m_fakeAppIdEdit->setPlaceholderText(Vanta::tr("Optional replacement application id"));
    form->addRow(Vanta::tr("Fake app id"), m_fakeAppIdEdit);

    QString fixPath = m_info.fixPath;
    if (fixPath.isEmpty())
        fixPath = m_game.fixPath;
    if (fixPath.isEmpty())
        fixPath = m_game.effectiveDirectory();
    form->addRow(Vanta::tr("Fix folder"), readOnlyField(fixPath, detailsPanel));

    if (!m_info.photonLauncherDir.isEmpty()) {
        form->addRow(Vanta::tr("Photon launcher"),
                     readOnlyField(m_info.photonLauncherDir, detailsPanel));
    }
    detailsLayout->addLayout(form);
    detailsLayout->addWidget(UiHelp::helpLabel(
        Vanta::tr("Auto is the safe default. Force is only useful when detection is inconclusive; it cannot "
                       "create fix files, it only applies launch configuration. Disabled prevents both prompts and "
                       "overrides for this game."),
        detailsPanel));
    layout->addWidget(detailsPanel);

    auto *changesPanel = new QFrame(content);
    changesPanel->setObjectName(QStringLiteral("compatibilityPanel"));
    auto *changesLayout = new QVBoxLayout(changesPanel);
    changesLayout->setContentsMargins(16, 14, 16, 14);
    changesLayout->setSpacing(8);
    changesLayout->addWidget(UiHelp::sectionLabel(Vanta::tr("File changes on launch"), changesPanel));
    const QStringList planned = OnlineFixPatcher::plannedFiles(m_info);
    auto *plannedList = new QListWidget(changesPanel);
    plannedList->setObjectName(QStringLiteral("detectedFileList"));
    plannedList->setSelectionMode(QAbstractItemView::NoSelection);
    plannedList->setFocusPolicy(Qt::NoFocus);
    if (planned.isEmpty()) {
        plannedList->addItem(Vanta::tr("No file writes are planned. Launch overrides may still be applied."));
    } else {
        for (const QString &path : planned) {
            auto *item = new QListWidgetItem(QDir::toNativeSeparators(path), plannedList);
            item->setToolTip(path);
        }
    }
    plannedList->setMinimumHeight(92);
    changesLayout->addWidget(plannedList);
    changesLayout->addWidget(UiHelp::helpLabel(
        Vanta::tr("Every changed configuration file is backed up beside the original before it is written. "
                       "Photon launchers receive a Newtonsoft.Json.dll link when the expected data file and "
                       "Launcher.exe are present. No game executable or DLL is patched."),
        changesPanel));
    layout->addWidget(changesPanel);
    layout->addStretch(1);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    bodyLayout()->addWidget(scroll, 1);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    m_openFolderButton = new QPushButton(Vanta::tr("Open fix folder"), this);
    m_openFolderButton->setCursor(Qt::PointingHandCursor);
    m_openFolderButton->setAutoDefault(false);
    connect(m_openFolderButton, &QPushButton::clicked, this, &OnlineFixDialog::openFixFolder);
    footer->addWidget(m_openFolderButton);
    footer->addStretch(1);
    m_closeButton = new QPushButton(Vanta::tr("Cancel"), this);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    m_closeButton->setAutoDefault(false);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(m_closeButton);
    m_saveButton = new QPushButton(Vanta::tr("Save policy"), this);
    m_saveButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makePrimary(m_saveButton);
    m_saveButton->setDefault(true);
    connect(m_saveButton, &QPushButton::clicked, this, &OnlineFixDialog::saveAndAccept);
    footer->addWidget(m_saveButton);
    bodyLayout()->addLayout(footer);
}

void OnlineFixDialog::buildPromptUi()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(2, 2, 8, 2);
    layout->setSpacing(14);

    auto *noticePanel = new QFrame(content);
    noticePanel->setObjectName(QStringLiteral("diagnosticsPanel"));
    auto *noticeLayout = new QVBoxLayout(noticePanel);
    noticeLayout->setContentsMargins(16, 14, 16, 14);
    noticeLayout->setSpacing(8);
    auto *title = new QLabel(Vanta::tr("Apply OnlineFix for this launch?"), noticePanel);
    QFont titleFont = title->font();
    titleFont.setPixelSize(17);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    noticeLayout->addWidget(title);
    noticeLayout->addWidget(UiHelp::helpLabel(detectionSummary(), noticePanel));
    noticeLayout->addWidget(UiHelp::helpLabel(
        Vanta::tr("If you continue, Vanta will update OnlineFix INI files when needed, create backups first, "
                       "and add the required Wine DLL overrides to this launch only. Steam may need to be started "
                       "and signed in before the game launches. OnlineFix support is not guaranteed to work, and "
                       "Vanta never modifies game binaries or downloads fix files."),
        noticePanel));
    layout->addWidget(noticePanel);

    const QStringList planned = OnlineFixPatcher::plannedFiles(m_info);
    if (!planned.isEmpty()) {
        auto *changes = new QFrame(content);
        changes->setObjectName(QStringLiteral("compatibilityPanel"));
        auto *changesLayout = new QVBoxLayout(changes);
        changesLayout->setContentsMargins(16, 14, 16, 14);
        changesLayout->setSpacing(6);
        changesLayout->addWidget(UiHelp::sectionLabel(Vanta::tr("Configuration files to update"), changes));
        for (const QString &path : planned) {
            auto *label = UiHelp::helpLabel(QDir::toNativeSeparators(path), changes);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            changesLayout->addWidget(label);
        }
        layout->addWidget(changes);
    }

    layout->addWidget(UiHelp::helpLabel(
        Vanta::tr("Choose “Launch without OnlineFix” to start the game normally. Vanta will remember only the "
                       "policy you configure for the game, not any per-launch override."),
        content));
    layout->addStretch(1);

    bodyLayout()->addWidget(content, 1);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    m_cancelButton = new QPushButton(Vanta::tr("Cancel"), this);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    // Enter belongs to the primary action. Cancel was the first button created
    // here, so Qt made it the implicit default and Enter aborted the launch.
    m_cancelButton->setAutoDefault(false);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(m_cancelButton);
    footer->addStretch(1);
    m_skipButton = new QPushButton(Vanta::tr("Launch without OnlineFix"), this);
    m_skipButton->setCursor(Qt::PointingHandCursor);
    m_skipButton->setAutoDefault(false);
    connect(m_skipButton, &QPushButton::clicked, this, [this] {
        m_launchChoice = LaunchChoice::Skip;
        accept();
    });
    footer->addWidget(m_skipButton);
    m_applyButton = new QPushButton(Vanta::tr("Apply and launch"), this);
    m_applyButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makePrimary(m_applyButton);
    m_applyButton->setDefault(true);
    connect(m_applyButton, &QPushButton::clicked, this, [this] {
        m_launchChoice = LaunchChoice::Apply;
        accept();
    });
    footer->addWidget(m_applyButton);
    bodyLayout()->addLayout(footer);
}

QString OnlineFixDialog::detectionSummary() const
{
    if (!m_info.detected) {
        if (m_game.onlineFixMode == OnlineFixMode::Force) {
            return Vanta::tr("OnlineFix mode is set to Force, but no signature files were detected. Vanta can "
                                  "still add the standard DXVK override chain to this launch, but it cannot create "
                                  "OnlineFix files or make an absent fix work.");
        }
        return Vanta::tr("No OnlineFix signature files were detected for this game.");
    }
    QString text = Vanta::tr("%1 OnlineFix signature file(s) were detected (%2).")
                       .arg(m_info.files.size())
                       .arg(fixKindDisplayName(m_info.kind));
    if (!m_info.realAppId.isEmpty() || !m_info.fakeAppId.isEmpty()) {
        text += Vanta::tr(" App IDs: real %1, fake %2.")
                    .arg(m_info.realAppId.isEmpty() ? Vanta::tr("unknown") : m_info.realAppId,
                         m_info.fakeAppId.isEmpty() ? Vanta::tr("unknown") : m_info.fakeAppId);
    }
    text += Vanta::tr(" Detection never proves that the fix is functional.");
    return text;
}

void OnlineFixDialog::saveAndAccept()
{
    const int modeIndex = m_modeCombo ? m_modeCombo->currentIndex() : -1;
    if (m_modeCombo && modeIndex >= 0)
        m_game.onlineFixMode = static_cast<OnlineFixMode>(m_modeCombo->itemData(modeIndex).toInt());
    if (m_realAppIdEdit)
        m_game.realAppId = m_realAppIdEdit->text().trimmed();
    if (m_fakeAppIdEdit)
        m_game.fakeAppId = m_fakeAppIdEdit->text().trimmed();
    if (m_info.detected) {
        if (!m_info.fixPath.isEmpty())
            m_game.fixPath = m_info.fixPath;
        if (!m_info.photonLauncherDir.isEmpty())
            m_game.photonLauncherDir = m_info.photonLauncherDir;
        if (m_game.realAppId.isEmpty())
            m_game.realAppId = m_info.realAppId;
        if (m_game.fakeAppId.isEmpty())
            m_game.fakeAppId = m_info.fakeAppId;
        m_game.fixKind = m_info.kind;
    }
    accept();
}

void OnlineFixDialog::openFixFolder()
{
    QString path = m_info.fixPath;
    if (path.isEmpty())
        path = m_game.fixPath;
    if (path.isEmpty())
        path = m_game.effectiveDirectory();
    UiHelp::openContainingDirectory(path);
}

void OnlineFixDialog::refreshIcons()
{
    if (!m_theme)
        return;
    const ThemeColors &colors = m_theme->colors();
    if (m_openFolderButton)
        Icons::setButtonIcon(m_openFolderButton, Glyph::Folder, colors.text, 16);
    if (m_saveButton)
        Icons::setButtonIcon(m_saveButton, Glyph::Check, QColor(Qt::white), 16);
    if (m_closeButton)
        Icons::setButtonIcon(m_closeButton, Glyph::Close, colors.text, 16);
    if (m_cancelButton)
        Icons::setButtonIcon(m_cancelButton, Glyph::Close, colors.text, 16);
    if (m_applyButton)
        Icons::setButtonIcon(m_applyButton, Glyph::Shield, QColor(Qt::white), 16);
    if (m_skipButton)
        Icons::setButtonIcon(m_skipButton, Glyph::Play, colors.text, 16);
}
