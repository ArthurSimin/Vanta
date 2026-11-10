#include "LogsDialog.h"

#include "I18n.h"
#include "IconFactory.h"
#include "ThemeManager.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

LogsDialog::LogsDialog(const QString &gameId, const QString &gameName, ThemeManager *theme,
                       QWidget *parent)
    : VantaDialog(Vanta::tr("Logs"), theme, parent)
    , m_gameId(gameId)
    , m_gameName(gameName)
    , m_theme(theme)
{
    buildUi();
    refreshIcons();
    if (m_theme)
        connect(m_theme, &ThemeManager::themeChanged, this, &LogsDialog::refreshIcons);
    reload();
}

void LogsDialog::selectLog(const QString &path)
{
    m_pendingPath = path;
    if (m_logCombo)
        reload();
}

void LogsDialog::buildUi()
{
    bodyLayout()->addWidget(UiHelp::helpLabel(
        Vanta::tr("Every launch writes the composed command and everything the runner printed here, so you "
                       "can see what happened after the game has already closed."),
        this));

    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(8);
    topRow->addWidget(new QLabel(Vanta::tr("Launch"), this));
    m_logCombo = new QComboBox(this);
    m_logCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(m_logCombo, &QComboBox::currentIndexChanged, this, [this](int) { updateSummary(); });
    topRow->addWidget(m_logCombo, 1);

    m_refreshButton = new QPushButton(Vanta::tr("Refresh"), this);
    m_refreshButton->setCursor(Qt::PointingHandCursor);
    m_refreshButton->setAutoDefault(false);
    connect(m_refreshButton, &QPushButton::clicked, this, &LogsDialog::reload);
    topRow->addWidget(m_refreshButton);
    bodyLayout()->addLayout(topRow);

    m_summaryLabel = UiHelp::mutedLabel(QString(), this);
    m_summaryLabel->setWordWrap(true);
    bodyLayout()->addWidget(m_summaryLabel);

    auto *terminal = new QFrame(this);
    terminal->setObjectName(QStringLiteral("terminalPanel"));
    auto *terminalLayout = new QVBoxLayout(terminal);
    terminalLayout->setContentsMargins(0, 0, 0, 0);
    terminalLayout->setSpacing(0);

    auto *titleBar = new QWidget(terminal);
    titleBar->setObjectName(QStringLiteral("terminalTitleBar"));
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(14, 9, 14, 9);
    titleLayout->setSpacing(8);
    m_pathLabel = new QLabel(titleBar);
    m_pathLabel->setObjectName(QStringLiteral("terminalPath"));
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titleLayout->addWidget(m_pathLabel, 1);
    terminalLayout->addWidget(titleBar);

    m_view = new QPlainTextEdit(terminal);
    m_view->setObjectName(QStringLiteral("terminalView"));
    m_view->setReadOnly(true);
    m_view->setProperty("monospace", true);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_view->setUndoRedoEnabled(false);
    m_view->setMinimumHeight(320);
    terminalLayout->addWidget(m_view, 1);
    bodyLayout()->addWidget(terminal, 1);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    m_folderButton = new QPushButton(Vanta::tr("Open folder"), this);
    m_folderButton->setCursor(Qt::PointingHandCursor);
    m_folderButton->setAutoDefault(false);
    connect(m_folderButton, &QPushButton::clicked, this, &LogsDialog::openLogFolder);
    footer->addWidget(m_folderButton);

    m_clearButton = new QPushButton(Vanta::tr("Clear logs"), this);
    m_clearButton->setCursor(Qt::PointingHandCursor);
    m_clearButton->setAutoDefault(false);
    connect(m_clearButton, &QPushButton::clicked, this, &LogsDialog::clearAll);
    footer->addWidget(m_clearButton);
    footer->addStretch(1);

    m_closeButton = new QPushButton(Vanta::tr("Close"), this);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makePrimary(m_closeButton);
    m_closeButton->setDefault(true);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(m_closeButton);
    bodyLayout()->addLayout(footer);

    resize(880, 640);
}

void LogsDialog::refreshIcons()
{
    if (!m_theme)
        return;
    const ThemeColors &colors = m_theme->colors();
    Icons::setButtonIcon(m_refreshButton, Glyph::Restart, colors.text, 16);
    Icons::setButtonIcon(m_folderButton, Glyph::Folder, colors.text, 16);
    Icons::setButtonIcon(m_clearButton, Glyph::Trash, colors.danger, 16);
    Icons::setButtonIcon(m_closeButton, Glyph::Check, QColor(Qt::white), 16);
}

void LogsDialog::reload()
{
    const QString previous = m_pendingPath.isEmpty() ? currentPath() : m_pendingPath;
    m_pendingPath.clear();

    m_entries = LaunchLogs::list(m_gameId);
    m_logCombo->blockSignals(true);
    m_logCombo->clear();
    for (const LaunchLogEntry &entry : m_entries) {
        const QString stamp = entry.finishedAt.isValid()
            ? entry.finishedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
            : Vanta::tr("Unknown time");
        QString label = stamp;
        if (entry.running)
            label += Vanta::tr("  ·  running");
        else if (entry.crashed)
            label += Vanta::tr("  ·  exited unexpectedly");
        else if (entry.stopped)
            label += Vanta::tr("  ·  stopped");
        else if (entry.exitCode < 0)
            label += Vanta::tr("  ·  never started");
        else if (entry.exitCode > 0)
            label += Vanta::tr("  ·  exit %1").arg(entry.exitCode);
        m_logCombo->addItem(label, entry.path);
    }
    m_logCombo->blockSignals(false);

    int index = -1;
    if (!previous.isEmpty())
        index = m_logCombo->findData(previous);
    if (index < 0 && m_logCombo->count() > 0)
        index = 0;
    if (index >= 0)
        m_logCombo->setCurrentIndex(index);

    updateSummary();
}

QString LogsDialog::currentPath() const
{
    if (!m_logCombo || m_logCombo->currentIndex() < 0)
        return {};
    return m_logCombo->currentData().toString();
}

void LogsDialog::updateSummary()
{
    const bool hasLogs = m_logCombo->count() > 0;
    m_logCombo->setEnabled(hasLogs);
    m_clearButton->setEnabled(hasLogs);
    // rootDirectory() is always a non-empty path; only the per-game folder tells
    // whether there is anything to open. Without the existence check the button
    // looked live for a game that never launched and then did nothing.
    const QString folder = logDirectory();
    m_folderButton->setEnabled(QFileInfo::exists(folder));
    m_folderButton->setToolTip(QFileInfo::exists(folder)
        ? Vanta::tr("Open the folder that holds this game's logs.")
        : Vanta::tr("No log folder exists yet. Start the game once and it will be created."));

    if (!hasLogs) {
        m_summaryLabel->setText(
            Vanta::tr("No launch log has been recorded for %1 yet. Start the game once and the runner "
                           "output will appear here.").arg(m_gameName));
        m_pathLabel->setText(Vanta::tr("No log selected"));
        m_view->setPlainText(QString());
        return;
    }

    const int index = m_logCombo->currentIndex();
    if (index < 0 || index >= m_entries.size())
        return;
    const LaunchLogEntry &entry = m_entries.at(index);

    QString summary = entry.finishedAt.isValid()
        ? entry.finishedAt.toString(QStringLiteral("d MMM yyyy, HH:mm:ss"))
        : Vanta::tr("Unknown time");
    if (!entry.runner.isEmpty())
        summary += Vanta::tr("  ·  %1").arg(entry.runner);
    if (entry.running) {
        summary += Vanta::tr("  ·  still running");
    } else {
        summary += entry.exitCode < 0 ? Vanta::tr("  ·  the process never started")
                                      : Vanta::tr("  ·  exit code %1").arg(entry.exitCode);
        if (entry.crashed)
            summary += Vanta::tr("  ·  the game did not close normally");
        else if (entry.stopped)
            summary += Vanta::tr("  ·  stopped from Vanta");
    }
    if (entry.bytes > 0)
        summary += Vanta::tr("  ·  %1").arg(UiHelp::dataSize(entry.bytes));
    m_summaryLabel->setText(summary);

    const QString path = entry.path;
    m_pathLabel->setText(QDir::toNativeSeparators(path));
    m_pathLabel->setToolTip(QDir::toNativeSeparators(path));

    QString error;
    const QString text = LaunchLogs::read(path, &error);
    m_view->setPlainText(error.isEmpty() ? text : error);
    // A terminal opens at the newest line, which is where a failure shows up.
    // The scrollbar range is only final after the view has laid the text out,
    // so defer by one event-loop turn instead of reading a stale maximum.
    QTimer::singleShot(0, m_view, [view = m_view] {
        QScrollBar *scroll = view->verticalScrollBar();
        scroll->setValue(scroll->maximum());
    });
}

QString LogsDialog::logDirectory() const
{
    return QDir(LaunchLogs::rootDirectory()).filePath(m_gameId);
}

void LogsDialog::openLogFolder()
{
    const QString path = currentPath();
    if (!path.isEmpty()) {
        UiHelp::openContainingDirectory(path);
        return;
    }
    const QString directory = logDirectory();
    if (!QFileInfo::exists(directory)) {
        m_summaryLabel->setText(
            Vanta::tr("No log folder exists for %1 yet. Start the game once and the runner output "
                           "will be stored there.").arg(m_gameName));
        return;
    }
    UiHelp::openContainingDirectory(directory);
}

void LogsDialog::clearAll()
{
    if (m_entries.isEmpty())
        return;
    const auto reply = QMessageBox::question(
        this, Vanta::tr("Clear launch logs?"),
        Vanta::tr("Every stored log for %1 will be deleted. Future launches still write a new log. "
                       "The game folder is not touched.").arg(m_gameName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;

    const int removed = LaunchLogs::removeAll(m_gameId);
    reload();
    m_summaryLabel->setText(Vanta::tr("Removed %1 log file(s).").arg(removed));
    emit logsCleared();
}
