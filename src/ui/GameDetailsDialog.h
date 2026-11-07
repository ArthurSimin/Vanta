#pragma once

#include "Common.h"
#include "Game.h"
#include "Scanner.h"

class QPushButton;
class ThemeManager;

class GameDetailsDialog : public VantaDialog
{
    Q_OBJECT

public:
    enum class Action {
        None,
        Launch,
        Stop,
        Edit,
        OnlineFix,
        Scan,
        OpenFolder,
        OpenPrefix
    };

    GameDetailsDialog(const Game &game, const ScanResult *scanResult, bool running,
                      ThemeManager *theme, QWidget *parent = nullptr);

    Action requestedAction() const { return m_action; }
    void setRunning(bool running);

private:
    void buildUi();
    void refreshIcons();
    void updateActionButtons();
    void setAction(Action action);
    QString compatibilityExplanation() const;

    Game m_game;
    ScanResult m_scan;
    bool m_hasScan = false;
    bool m_running = false;
    Action m_action = Action::None;
    ThemeManager *m_theme = nullptr;

    QPushButton *m_playButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_onlineFixButton = nullptr;
    QPushButton *m_scanButton = nullptr;
    QPushButton *m_folderButton = nullptr;
    QPushButton *m_prefixButton = nullptr;
    QPushButton *m_closeButton = nullptr;
};
