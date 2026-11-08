#pragma once

#include "Common.h"

class QLabel;
class QProgressBar;
class QPushButton;
class ThemeManager;

class LaunchProgressDialog : public VantaDialog
{
    Q_OBJECT

public:
    explicit LaunchProgressDialog(const QString &gameName, ThemeManager *theme, QWidget *parent = nullptr);

    void setMessage(const QString &message);
    void setDetail(const QString &detail);
    void setIndeterminate();
    void setProgress(int value, int maximum);
    void showFailure(const QString &message);
    void showSuccess(const QString &message);
    void finishAndClose();

    // The viewer is offered from the failure state, where the log is the only
    // thing that still explains why the process is gone.
    void setLogPath(const QString &path);

signals:
    void cancelRequested();
    void logRequested(const QString &path);

protected:
    void reject() override;

private:
    void buildUi();
    void refreshIcons();

    ThemeManager *m_theme = nullptr;
    QLabel *m_headline = nullptr;
    QLabel *m_message = nullptr;
    QLabel *m_detail = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QPushButton *m_logsButton = nullptr;
    QString m_logPath;
    bool m_finished = false;
};
