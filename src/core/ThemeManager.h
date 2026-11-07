#pragma once

#include "Types.h"

#include <QColor>
#include <QObject>
#include <QPalette>

struct ThemeColors
{
    bool dark = true;
    QColor window;
    QColor surface;
    QColor surfaceRaised;
    QColor sidebar;
    QColor border;
    QColor borderStrong;
    QColor text;
    QColor muted;
    QColor accent;
    QColor accentHover;
    QColor accentPressed;
    QColor accentSoft;
    QColor selection;
    QColor hover;
    QColor danger;
    QColor warning;
    QColor success;
    QColor tooltip;
    QColor shadow;
};

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    explicit ThemeManager(QObject *parent = nullptr);

    ThemeChoice choice() const { return m_choice; }
    bool isDark() const { return m_dark; }
    bool animationsEnabled() const { return m_animations; }
    const ThemeColors &colors() const { return m_colors; }

    void setChoice(ThemeChoice choice);
    void setAnimationsEnabled(bool enabled);
    void apply();

signals:
    void themeChanged();
    void animationsChanged(bool enabled);
    void choiceChanged(ThemeChoice choice);

private slots:
    void systemSchemeChanged();

private:
    void resolveColors();
    void resolveSystemColors();
    QString styleSheet() const;
    void applyPalette();

    ThemeChoice m_choice = ThemeChoice::System;
    bool m_dark = true;
    bool m_animations = true;
    ThemeColors m_colors;
    QPalette m_systemPalette;
};
