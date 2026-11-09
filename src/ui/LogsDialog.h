#pragma once

#include "Common.h"
#include "LaunchLogs.h"

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class ThemeManager;

// Shows the stored launch logs of one game in a terminal-style pane. Each
// launch writes an entry, so a game that closed unexpectedly can still be
// inspected after the process is gone. The pane is read-only: it never runs a
// command and never edits a log.
class LogsDialog : public VantaDialog
{
    Q_OBJECT

public:
    LogsDialog(const QString &gameId, const QString &gameName, ThemeManager *theme,
               QWidget *parent = nullptr);

    // Selects one log file before the dialog is shown. An empty path keeps the
    // newest entry selected.
    void selectLog(const QString &path);

signals:
    // Emitted after the user removed every stored log for this game.
    void logsCleared();

private:
    void buildUi();
    void refreshIcons();
    void reload();
    void updateSummary();
    void clearAll();
    void openLogFolder();
    QString currentPath() const;
    QString logDirectory() const;

    QString m_gameId;
    QString m_gameName;
    QString m_pendingPath;
    ThemeManager *m_theme = nullptr;
    QList<LaunchLogEntry> m_entries;

    QComboBox *m_logCombo = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QPlainTextEdit *m_view = nullptr;
    QLabel *m_pathLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_folderButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QPushButton *m_closeButton = nullptr;
};
