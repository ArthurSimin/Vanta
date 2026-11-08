#pragma once

#include "Common.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QWidget;
class RunnerInstaller;
class ThemeManager;

class RunnerInstallDialog : public VantaDialog
{
    Q_OBJECT

public:
    explicit RunnerInstallDialog(ThemeManager *theme, QWidget *parent = nullptr);

signals:
    void runnersChanged();

protected:
    void reject() override;

private:
    void buildUi();
    void updateButtons();
    void onInstallWineClicked();
    void onWineButtonClicked();
    void onProtonButtonClicked();
    void setBusy(bool busy);
    void setStatus(const QString &text, const QString &kind = QString());
    void updateDownloadProgress(qint64 received, qint64 total);
    void showCompleted(const QString &message);
    void showFailure(const QString &message);
    void refreshIcons();

    ThemeManager *m_theme = nullptr;
    RunnerInstaller *m_installer = nullptr;
    QLabel *m_packageStatus = nullptr;
    QLabel *m_commandLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_sizeLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QWidget *m_progressPanel = nullptr;
    QPushButton *m_installWineButton = nullptr;
    QPushButton *m_wineButton = nullptr;
    QPushButton *m_downloadWineSecondary = nullptr;
    QPushButton *m_protonButton = nullptr;
    QPushButton *m_downloadProtonSecondary = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    bool m_busy = false;
};
