#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QSize>

class QAbstractButton;

enum class Glyph {
    Plus,
    Search,
    Filter,
    Grid,
    List,
    ChevronDown,
    ChevronRight,
    More,
    Gamepad,
    Clock,
    Star,
    Folder,
    Warning,
    Settings,
    Info,
    Restart,
    Windows,
    Linux,
    Wine,
    Proton,
    Flatpak,
    Terminal,
    Play,
    Stop,
    Pencil,
    Scan,
    Trash,
    External,
    Image,
    Check,
    Shield,
    Steam,
    Gog,
    Epic,
    Ea,
    Ubisoft,
    Close,
    Download
};

namespace Icons
{
QIcon icon(Glyph glyph, const QColor &color, int size = 20);
QIcon themedIcon(Glyph glyph, const QColor &color, int size = 20);
QIcon sourceIcon(const QString &source, const QColor &color, int size = 20);
void setButtonIcon(QAbstractButton *button, Glyph glyph, const QColor &color, int size = 18);
QPixmap logoPixmap(int height, qreal devicePixelRatio = 0.0);
QPixmap coverPixmap(const QString &path, const QSize &pixelSize);
QPixmap roundedCover(const QString &path, const QSize &size, const QString &initials,
                     const QColor &accentSoft, const QColor &accent, qreal radius = 8.0);
QPixmap placeholderCover(const QSize &size, const QString &initials,
                         const QColor &accentSoft, const QColor &accent);
QIcon coverIcon(const QString &path, const QSize &size, const QString &initials,
                const QColor &accentSoft, const QColor &accent);
QColor colorForName(const QString &name, const QColor &fallback);
}
