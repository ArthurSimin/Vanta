#pragma once

#include "Common.h"

class QLabel;
class QPushButton;
class ThemeManager;

// Shown when an OnlineFix launch is requested while Steam is not running.
// It never starts Steam on its own: the user either asks Vanta to open Steam and
// waits for the client, or launches anyway knowing online play will not work.
class SteamPromptDialog : public VantaDialog
{
    Q_OBJECT

public:
    enum class Choice { Cancel, PlayWithoutSteam, OpenSteam };

    SteamPromptDialog(const QString &gameName, ThemeManager *theme, QWidget *parent = nullptr);

    Choice choice() const { return m_choice; }

    static Choice prompt(const QString &gameName, ThemeManager *theme, QWidget *parent = nullptr);

private:
    void refreshIcons();

    Choice m_choice = Choice::Cancel;
    ThemeManager *m_theme = nullptr;

    QLabel *m_gameLabel = nullptr;
    QLabel *m_warningIcon = nullptr;
    QPushButton *m_withoutSteamButton = nullptr;
    QPushButton *m_openSteamButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
};
