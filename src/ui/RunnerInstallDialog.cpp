#include "RunnerInstallDialog.h"
#include "IconFactory.h"
#include "RunnerInstaller.h"
#include "ThemeManager.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
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
    m_installWineButton->setProperty("packageAvailable", manager.available);
    m_installWineButton->setEnabled(manager.available);
    connect(m_installWineButton, &QPushButton::clicked, m_installer, &RunnerInstaller::installSystemWine);
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
    connect(m_wineButton, &QPushButton::clicked, m_installer, &RunnerInstaller::downloadPortableWine);
    wineRow->addWidget(m_wineButton, 0, Qt::AlignVCenter);
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
    UiHelp::makePrimary(m_protonButton);
    connect(m_protonButton, &QPushButton::clicked, m_installer, &RunnerInstaller::downloadProton);
    protonRow->addWidget(m_protonButton, 0, Qt::AlignVCenter);
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
    m_cancelButton->setVisible(false);
    connect(m_cancelButton, &QPushButton::clicked, m_installer, &RunnerInstaller::cancel);
    buttons->addWidget(m_cancelButton);
    m_closeButton = new QPushButton(QStringLiteral("Close"), this);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(m_closeButton);
    bodyLayout()->addLayout(buttons);
}

void RunnerInstallDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_installWineButton->setEnabled(!busy && m_installWineButton->property("packageAvailable").toBool());
    m_wineButton->setEnabled(!busy);
    m_protonButton->setEnabled(!busy);
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
    emit runnersChanged();
}

void RunnerInstallDialog::showFailure(const QString &message)
{
    setBusy(false);
    m_progress->setVisible(false);
    m_sizeLabel->setVisible(false);
    setStatus(message, QStringLiteral("danger"));
    emit runnersChanged();
}

void RunnerInstallDialog::refreshIcons()
{
    if (!m_theme)
        return;
    const ThemeColors &colors = m_theme->colors();
    Icons::setButtonIcon(m_installWineButton, Glyph::Download, colors.text, 16);
    Icons::setButtonIcon(m_wineButton, Glyph::Wine, colors.text, 16);
    Icons::setButtonIcon(m_protonButton, Glyph::Proton, QColor(Qt::white), 16);
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
