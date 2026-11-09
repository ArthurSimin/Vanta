#include "RunnerInstallDialog.h"
#include "IconFactory.h"
#include "RunnerInstaller.h"
#include "ThemeManager.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

RunnerInstallDialog::RunnerInstallDialog(ThemeManager *theme, QWidget *parent)
    : VantaDialog(QStringLiteral("Download runners"), theme, parent)
    , m_theme(theme)
    , m_installer(new RunnerInstaller(this))
{
    setObjectName(QStringLiteral("runnerInstallDialog"));
    setMinimumSize(680, 620);
    resize(720, 700);
    buildUi();

    connect(m_installer, &RunnerInstaller::busyChanged, this, &RunnerInstallDialog::setBusy);
    connect(m_installer, &RunnerInstaller::progress, this, [this](const QString &message) {
        setStatus(message);
    });
    connect(m_installer, &RunnerInstaller::downloadProgress, this, &RunnerInstallDialog::updateDownloadProgress);
    connect(m_installer, &RunnerInstaller::finished, this, &RunnerInstallDialog::showCompleted);
    connect(m_installer, &RunnerInstaller::failed, this, &RunnerInstallDialog::showFailure);
    connect(m_installer, &RunnerInstaller::cancelled, this, [this] {
        setBusy(false);
        setStatus(QStringLiteral("The operation was cancelled. Nothing else was changed."), QStringLiteral("muted"));
    });
    if (m_theme) {
        connect(m_theme, &ThemeManager::themeChanged, this, &RunnerInstallDialog::refreshIcons);
    }
    refreshIcons();
}

void RunnerInstallDialog::buildUi()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(2, 2, 8, 2);
    layout->setSpacing(14);

    layout->addWidget(UiHelp::helpLabel(
        QStringLiteral("Vanta can use your distribution's package manager, or download a portable Wine or GE-Proton build. "
                       "Downloads come directly from the official GitHub release pages and are extracted into Vanta's runner folder."),
        content));

    auto *packagePanel = new QFrame(content);
    packagePanel->setObjectName(QStringLiteral("infoPanel"));
    auto *packageLayout = new QVBoxLayout(packagePanel);
    packageLayout->setContentsMargins(16, 14, 16, 14);
    packageLayout->setSpacing(7);
    packageLayout->addWidget(UiHelp::sectionLabel(QStringLiteral("System Wine"), packagePanel));

    const PackageManagerInfo manager = RunnerInstaller::detectPackageManager();
    m_packageStatus = new QLabel(packagePanel);
    m_packageStatus->setWordWrap(true);
    if (manager.available) {
        m_packageStatus->setText(QStringLiteral("Detected package manager: %1").arg(manager.displayName));
    } else {
        m_packageStatus->setProperty("class", QStringLiteral("warning"));
        m_packageStatus->setText(QStringLiteral("No supported package manager was detected. Use the portable Wine download below."));
    }
    packageLayout->addWidget(m_packageStatus);

    m_commandLabel = new QLabel(packagePanel);
    m_commandLabel->setProperty("monospace", true);
    m_commandLabel->setWordWrap(true);
    m_commandLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_commandLabel->setText(manager.available
        ? RunnerInstaller::installCommandLine(manager)
        : QStringLiteral("No package manager command is available."));
    packageLayout->addWidget(m_commandLabel);
    packageLayout->addWidget(UiHelp::helpLabel(
        QStringLiteral("Installing a system package may show a password prompt. Vanta tries pkexec first, then sudo without asking for a password. "
                       "If neither is available, run the command above in a terminal or use the portable build."),
        packagePanel));

    m_installWineButton = new QPushButton(QStringLiteral("Install Wine with the package manager"), packagePanel);
    m_installWineButton->setCursor(Qt::PointingHandCursor);
    // This one can start an elevated package install, so Enter must not reach it.
    m_installWineButton->setAutoDefault(false);
    m_installWineButton->setProperty("packageAvailable", manager.available);
    m_installWineButton->setEnabled(manager.available);
    connect(m_installWineButton, &QPushButton::clicked, this, &RunnerInstallDialog::onInstallWineClicked);
    packageLayout->addWidget(m_installWineButton, 0, Qt::AlignLeft);
    layout->addWidget(packagePanel);

    auto *downloadPanel = new QFrame(content);
    downloadPanel->setObjectName(QStringLiteral("infoPanel"));
    auto *downloadLayout = new QVBoxLayout(downloadPanel);
    downloadLayout->setContentsMargins(16, 14, 16, 14);
    downloadLayout->setSpacing(10);
    downloadLayout->addWidget(UiHelp::sectionLabel(QStringLiteral("Portable runner downloads"), downloadPanel));

    auto *wineRow = new QHBoxLayout;
    wineRow->setSpacing(12);
    auto *wineText = new QVBoxLayout;
    wineText->setSpacing(2);
    wineText->addWidget(UiHelp::sectionLabel(QStringLiteral("Portable Wine"), downloadPanel));
    wineText->addWidget(UiHelp::helpLabel(
        QStringLiteral("Latest Kron4ek Wine build with WoW64 support. Downloads are typically around 100 MB."),
        downloadPanel));
    wineRow->addLayout(wineText, 1);
    m_wineButton = new QPushButton(QStringLiteral("Download Wine"), downloadPanel);
    m_wineButton->setCursor(Qt::PointingHandCursor);
    m_wineButton->setAutoDefault(false);
    connect(m_wineButton, &QPushButton::clicked, this, &RunnerInstallDialog::onWineButtonClicked);
    wineRow->addWidget(m_wineButton, 0, Qt::AlignVCenter);
    m_downloadWineSecondary = new QPushButton(QStringLiteral("Download build…"), downloadPanel);
    m_downloadWineSecondary->setCursor(Qt::PointingHandCursor);
    m_downloadWineSecondary->setAutoDefault(false);
    m_downloadWineSecondary->setVisible(false);
    UiHelp::makeFlat(m_downloadWineSecondary);
    connect(m_downloadWineSecondary, &QPushButton::clicked, m_installer, &RunnerInstaller::downloadPortableWine);
    wineRow->addWidget(m_downloadWineSecondary, 0, Qt::AlignVCenter);
    downloadLayout->addLayout(wineRow);

    downloadLayout->addWidget(UiHelp::separator(downloadPanel));

    auto *protonRow = new QHBoxLayout;
    protonRow->setSpacing(12);
    auto *protonText = new QVBoxLayout;
    protonText->setSpacing(2);
    protonText->addWidget(UiHelp::sectionLabel(QStringLiteral("GE-Proton"), downloadPanel));
    protonText->addWidget(UiHelp::helpLabel(
        QStringLiteral("Latest GloriousEggroll Proton build. The archive is large, usually around 550 MB."),
        downloadPanel));
    protonRow->addLayout(protonText, 1);
    m_protonButton = new QPushButton(QStringLiteral("Download Proton"), downloadPanel);
    m_protonButton->setCursor(Qt::PointingHandCursor);
    m_protonButton->setAutoDefault(false);
    UiHelp::makePrimary(m_protonButton);
    connect(m_protonButton, &QPushButton::clicked, this, &RunnerInstallDialog::onProtonButtonClicked);
    protonRow->addWidget(m_protonButton, 0, Qt::AlignVCenter);
    m_downloadProtonSecondary = new QPushButton(QStringLiteral("Download GE-Proton…"), downloadPanel);
    m_downloadProtonSecondary->setCursor(Qt::PointingHandCursor);
    m_downloadProtonSecondary->setAutoDefault(false);
    m_downloadProtonSecondary->setVisible(false);
    UiHelp::makeFlat(m_downloadProtonSecondary);
    connect(m_downloadProtonSecondary, &QPushButton::clicked, m_installer, &RunnerInstaller::downloadProton);
    protonRow->addWidget(m_downloadProtonSecondary, 0, Qt::AlignVCenter);
    downloadLayout->addLayout(protonRow);

    downloadLayout->addWidget(UiHelp::helpLabel(
        QStringLiteral("Extracted runners are detected automatically by Vanta. Steam, Lutris, Heroic and Bottles runners are also discovered."),
        downloadPanel));
    layout->addWidget(downloadPanel);

    m_progressPanel = new QFrame(content);
    m_progressPanel->setObjectName(QStringLiteral("compatibilityPanel"));
    auto *progressLayout = new QVBoxLayout(m_progressPanel);
    progressLayout->setContentsMargins(16, 14, 16, 14);
    progressLayout->setSpacing(7);
    m_statusLabel = new QLabel(m_progressPanel);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setText(QStringLiteral("Ready to install or download a runner."));
    m_statusLabel->setProperty("class", QStringLiteral("muted"));
    progressLayout->addWidget(m_statusLabel);
    m_progress = new QProgressBar(m_progressPanel);
    m_progress->setRange(0, 0);
    m_progress->setVisible(false);
    progressLayout->addWidget(m_progress);
    m_sizeLabel = new QLabel(m_progressPanel);
    m_sizeLabel->setProperty("class", QStringLiteral("muted"));
    m_sizeLabel->setVisible(false);
    progressLayout->addWidget(m_sizeLabel);
    layout->addWidget(m_progressPanel);
    layout->addStretch(1);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    bodyLayout()->addWidget(scroll, 1);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    m_cancelButton = new QPushButton(QStringLiteral("Cancel operation"), this);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setAutoDefault(false);
    m_cancelButton->setVisible(false);
    connect(m_cancelButton, &QPushButton::clicked, m_installer, &RunnerInstaller::cancel);
    buttons->addWidget(m_cancelButton);
    m_closeButton = new QPushButton(QStringLiteral("Close"), this);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    m_closeButton->setDefault(true);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(m_closeButton);
    bodyLayout()->addLayout(buttons);
    updateButtons();
}

void RunnerInstallDialog::updateButtons()
{
    const bool systemWine = RunnerInstaller::isSystemWineInstalled();
    const PackageManagerInfo manager = RunnerInstaller::detectPackageManager();
    if (systemWine) {
        const bool canRemove = manager.available && !manager.removalUnsupported;
        m_packageStatus->setProperty("class", canRemove ? QString() : QStringLiteral("warning"));
        if (!manager.available) {
            m_packageStatus->setText(
                QStringLiteral("System Wine is installed, but no supported package manager was detected."));
        } else if (manager.removalUnsupported) {
            m_packageStatus->setText(
                QStringLiteral("System Wine is installed. Removing it needs %1, which was not found.")
                    .arg(manager.uninstallToolName));
        } else {
            m_packageStatus->setText(QStringLiteral("System Wine is installed (%1).").arg(manager.displayName));
        }
        m_commandLabel->setText(canRemove ? RunnerInstaller::uninstallCommandLine(manager) : QString());
        m_installWineButton->setText(QStringLiteral("Uninstall Wine with the package manager"));
        m_installWineButton->setProperty("installed", true);
        // setBusy() re-enables from this property, so it has to track whether
        // removal is actually possible rather than just whether a manager exists.
        m_installWineButton->setProperty("packageAvailable", canRemove);
        m_installWineButton->setEnabled(canRemove && !m_busy);
        UiHelp::makeDanger(m_installWineButton);
    } else {
        if (manager.available) {
            m_packageStatus->setProperty("class", QString());
            m_packageStatus->setText(QStringLiteral("Detected package manager: %1").arg(manager.displayName));
            m_commandLabel->setText(RunnerInstaller::installCommandLine(manager));
        } else {
            m_packageStatus->setProperty("class", QStringLiteral("warning"));
            m_packageStatus->setText(QStringLiteral("No supported package manager was detected. Use the portable Wine download below."));
            m_commandLabel->setText(QStringLiteral("No package manager command is available."));
        }
        m_installWineButton->setText(QStringLiteral("Install Wine with the package manager"));
        m_installWineButton->setProperty("installed", false);
        m_installWineButton->setProperty("packageAvailable", manager.available);
        m_installWineButton->setProperty("class", QVariant());
        m_installWineButton->style()->unpolish(m_installWineButton);
        m_installWineButton->style()->polish(m_installWineButton);
        m_installWineButton->setEnabled(manager.available && !m_busy);
    }
    m_packageStatus->style()->unpolish(m_packageStatus);
    m_packageStatus->style()->polish(m_packageStatus);

    const bool portableWine = RunnerInstaller::isPortableWineInstalled();
    if (portableWine) {
        m_wineButton->setText(QStringLiteral("Uninstall Wine"));
        m_wineButton->setProperty("installed", true);
        UiHelp::makeDanger(m_wineButton);
        if (m_downloadWineSecondary)
            m_downloadWineSecondary->setVisible(true);
    } else {
        m_wineButton->setText(QStringLiteral("Download Wine"));
        m_wineButton->setProperty("installed", false);
        m_wineButton->setProperty("class", QVariant());
        m_wineButton->style()->unpolish(m_wineButton);
        m_wineButton->style()->polish(m_wineButton);
        if (m_downloadWineSecondary)
            m_downloadWineSecondary->setVisible(false);
    }

    const auto protonList = RunnerInstaller::installedProtonInstallations();
    if (!protonList.isEmpty()) {
        m_protonButton->setText(QStringLiteral("Uninstall Proton"));
        m_protonButton->setProperty("installed", true);
        UiHelp::makeDanger(m_protonButton);
        if (m_downloadProtonSecondary)
            m_downloadProtonSecondary->setVisible(true);
    } else {
        m_protonButton->setText(QStringLiteral("Download Proton"));
        m_protonButton->setProperty("installed", false);
        UiHelp::makePrimary(m_protonButton);
        if (m_downloadProtonSecondary)
            m_downloadProtonSecondary->setVisible(false);
    }
}

void RunnerInstallDialog::onInstallWineClicked()
{
    if (m_installWineButton->property("installed").toBool()) {
        const auto reply = QMessageBox::question(
            this,
            QStringLiteral("Uninstall Wine"),
            QStringLiteral("Are you sure you want to uninstall Wine using the package manager?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply == QMessageBox::Yes)
            m_installer->uninstallSystemWine();
    } else {
        m_installer->installSystemWine();
    }
}

void RunnerInstallDialog::onWineButtonClicked()
{
    if (m_wineButton->property("installed").toBool()) {
        const auto reply = QMessageBox::question(
            this,
            QStringLiteral("Uninstall Portable Wine"),
            QStringLiteral("Are you sure you want to uninstall portable Wine?\nThis will remove the build from Vanta's runner folder."),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply == QMessageBox::Yes)
            m_installer->uninstallPortableWine();
    } else {
        m_installer->downloadPortableWine();
    }
}

void RunnerInstallDialog::onProtonButtonClicked()
{
    if (m_protonButton->property("installed").toBool()) {
        const auto protonList = RunnerInstaller::installedProtonInstallations();
        if (protonList.isEmpty()) {
            m_installer->downloadProton();
            return;
        }
        if (protonList.size() == 1) {
            const auto &p = protonList.first();
            const auto reply = QMessageBox::question(
                this,
                QStringLiteral("Uninstall Proton"),
                QStringLiteral("Are you sure you want to uninstall %1?\nLocation: %2\nThis will remove the runner files.").arg(p.first, p.second),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (reply == QMessageBox::Yes)
                m_installer->uninstallProton(p.second);
        } else {
            VantaDialog pickDialog(QStringLiteral("Uninstall Proton"), m_theme, this);
            pickDialog.setMinimumWidth(480);
            auto *pickLayout = pickDialog.bodyLayout();
            pickLayout->addWidget(UiHelp::helpLabel(
                QStringLiteral("Multiple Proton installations were detected. Select the version you wish to uninstall:"),
                &pickDialog));
            auto *combo = new QComboBox(&pickDialog);
            for (const auto &p : protonList) {
                combo->addItem(QStringLiteral("%1 (%2)").arg(p.first, p.second), p.second);
            }
            pickLayout->addWidget(combo);
            auto *btnBox = new QHBoxLayout;
            btnBox->addStretch(1);
            auto *cancelBtn = new QPushButton(QStringLiteral("Cancel"), &pickDialog);
            cancelBtn->setCursor(Qt::PointingHandCursor);
            // Enter stays on Cancel so a stray keypress cannot remove a runner.
            cancelBtn->setDefault(true);
            connect(cancelBtn, &QPushButton::clicked, &pickDialog, &QDialog::reject);
            btnBox->addWidget(cancelBtn);
            auto *uninstBtn = new QPushButton(QStringLiteral("Uninstall"), &pickDialog);
            uninstBtn->setCursor(Qt::PointingHandCursor);
            uninstBtn->setAutoDefault(false);
            uninstBtn->setProperty("class", QStringLiteral("danger"));
            connect(uninstBtn, &QPushButton::clicked, &pickDialog, &QDialog::accept);
            btnBox->addWidget(uninstBtn);
            pickLayout->addLayout(btnBox);
            if (pickDialog.exec() == QDialog::Accepted) {
                const QString selectedPath = combo->currentData().toString();
                if (!selectedPath.isEmpty())
                    m_installer->uninstallProton(selectedPath);
            }
        }
    } else {
        m_installer->downloadProton();
    }
}

void RunnerInstallDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_installWineButton->setEnabled(!busy && m_installWineButton->property("packageAvailable").toBool());
    m_wineButton->setEnabled(!busy);
    if (m_downloadWineSecondary)
        m_downloadWineSecondary->setEnabled(!busy);
    m_protonButton->setEnabled(!busy);
    if (m_downloadProtonSecondary)
        m_downloadProtonSecondary->setEnabled(!busy);
    m_cancelButton->setVisible(busy);
    m_closeButton->setEnabled(!busy);
    m_progress->setVisible(busy);
    m_sizeLabel->setVisible(busy);
    if (busy) {
        m_progress->setRange(0, 0);
        m_sizeLabel->clear();
    }
}

void RunnerInstallDialog::setStatus(const QString &text, const QString &kind)
{
    m_statusLabel->setText(text);
    m_statusLabel->setProperty("class", kind);
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

void RunnerInstallDialog::updateDownloadProgress(qint64 received, qint64 total)
{
    if (!m_busy)
        return;
    if (total > 0) {
        m_progress->setRange(0, 100);
        const qint64 percent = qBound<qint64>(qint64(0), received * 100 / total, qint64(100));
        m_progress->setValue(static_cast<int>(percent));
        m_sizeLabel->setText(QStringLiteral("%1 of %2 (%3%)")
                                 .arg(UiHelp::dataSize(received), UiHelp::dataSize(total))
                                 .arg(percent));
    } else {
        m_progress->setRange(0, 0);
        m_sizeLabel->setText(QStringLiteral("%1 downloaded").arg(UiHelp::dataSize(received)));
    }
}

void RunnerInstallDialog::showCompleted(const QString &message)
{
    setBusy(false);
    m_progress->setVisible(false);
    m_sizeLabel->setVisible(false);
    setStatus(message, QStringLiteral("success"));
    updateButtons();
    refreshIcons();
    emit runnersChanged();
}

void RunnerInstallDialog::showFailure(const QString &message)
{
    setBusy(false);
    m_progress->setVisible(false);
    m_sizeLabel->setVisible(false);
    setStatus(message, QStringLiteral("danger"));
    updateButtons();
    refreshIcons();
    emit runnersChanged();
}

void RunnerInstallDialog::refreshIcons()
{
    if (!m_theme)
        return;
    const ThemeColors &colors = m_theme->colors();
    if (m_installWineButton->property("installed").toBool())
        Icons::setButtonIcon(m_installWineButton, Glyph::Trash, colors.danger, 16);
    else
        Icons::setButtonIcon(m_installWineButton, Glyph::Download, colors.text, 16);

    if (m_wineButton->property("installed").toBool())
        Icons::setButtonIcon(m_wineButton, Glyph::Trash, colors.danger, 16);
    else
        Icons::setButtonIcon(m_wineButton, Glyph::Wine, colors.text, 16);

    if (m_protonButton->property("installed").toBool())
        Icons::setButtonIcon(m_protonButton, Glyph::Trash, colors.danger, 16);
    else {
        m_protonButton->setIcon(Icons::sourceIcon(QStringLiteral("Proton"), colors.text, 16));
        m_protonButton->setIconSize(QSize(16, 16));
    }

    if (m_downloadWineSecondary)
        Icons::setButtonIcon(m_downloadWineSecondary, Glyph::Download, colors.text, 14);
    if (m_downloadProtonSecondary)
        Icons::setButtonIcon(m_downloadProtonSecondary, Glyph::Download, colors.text, 14);

    Icons::setButtonIcon(m_cancelButton, Glyph::Close, colors.text, 16);
    Icons::setButtonIcon(m_closeButton, Glyph::Check, colors.text, 16);
}

void RunnerInstallDialog::reject()
{
    if (m_busy) {
        m_installer->cancel();
        return;
    }
    QDialog::reject();
}
