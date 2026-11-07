#pragma once

#include "Common.h"

class ThemeManager;

class AboutDialog : public VantaDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(ThemeManager *theme, QWidget *parent = nullptr);

private:
    void buildUi();
    void refreshIcons();

    ThemeManager *m_theme = nullptr;
};
