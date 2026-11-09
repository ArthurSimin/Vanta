#include "IconFactory.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QScreen>
#include <QSvgRenderer>
#include <QImageReader>
#include <QtMath>

namespace {
void drawBase(QPainter &painter, const QRectF &rect, const QColor &color)
{
    QPen pen(color, qMax(1.4, rect.width() * 0.085));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
}

void drawGlyph(QPainter &painter, Glyph glyph, const QRectF &rect, const QColor &color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    drawBase(painter, rect, color);
    const qreal x = rect.left();
    const qreal y = rect.top();
    const qreal w = rect.width();
    const qreal h = rect.height();

    switch (glyph) {
    case Glyph::Plus:
        painter.drawLine(QPointF(x + w * 0.22, y + h * 0.5), QPointF(x + w * 0.78, y + h * 0.5));
        painter.drawLine(QPointF(x + w * 0.5, y + h * 0.22), QPointF(x + w * 0.5, y + h * 0.78));
        break;
    case Glyph::Search:
        painter.drawEllipse(QRectF(x + w * 0.16, y + h * 0.14, w * 0.56, h * 0.56));
        painter.drawLine(QPointF(x + w * 0.63, y + h * 0.63), QPointF(x + w * 0.86, y + h * 0.86));
        break;
    case Glyph::Filter:
        painter.drawLine(QPointF(x + w * 0.16, y + h * 0.25), QPointF(x + w * 0.84, y + h * 0.25));
        painter.drawLine(QPointF(x + w * 0.27, y + h * 0.47), QPointF(x + w * 0.73, y + h * 0.47));
        painter.drawLine(QPointF(x + w * 0.4, y + h * 0.69), QPointF(x + w * 0.6, y + h * 0.69));
        break;
    case Glyph::Grid:
        painter.drawRoundedRect(QRectF(x + w * 0.15, y + h * 0.15, w * 0.28, h * 0.28), 2, 2);
        painter.drawRoundedRect(QRectF(x + w * 0.57, y + h * 0.15, w * 0.28, h * 0.28), 2, 2);
        painter.drawRoundedRect(QRectF(x + w * 0.15, y + h * 0.57, w * 0.28, h * 0.28), 2, 2);
        painter.drawRoundedRect(QRectF(x + w * 0.57, y + h * 0.57, w * 0.28, h * 0.28), 2, 2);
        break;
    case Glyph::List:
        for (int row = 0; row < 3; ++row) {
            const qreal yy = y + h * (0.28 + row * 0.22);
            painter.drawEllipse(QPointF(x + w * 0.2, yy), w * 0.035, w * 0.035);
            painter.drawLine(QPointF(x + w * 0.34, yy), QPointF(x + w * 0.82, yy));
        }
        break;
    case Glyph::ChevronDown:
        painter.drawLine(QPointF(x + w * 0.25, y + h * 0.38), QPointF(x + w * 0.5, y + h * 0.64));
        painter.drawLine(QPointF(x + w * 0.5, y + h * 0.64), QPointF(x + w * 0.75, y + h * 0.38));
        break;
    case Glyph::ChevronRight:
        painter.drawLine(QPointF(x + w * 0.38, y + h * 0.25), QPointF(x + w * 0.64, y + h * 0.5));
        painter.drawLine(QPointF(x + w * 0.64, y + h * 0.5), QPointF(x + w * 0.38, y + h * 0.75));
        break;
    case Glyph::More:
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        for (int index = 0; index < 3; ++index)
            painter.drawEllipse(QPointF(x + w * (0.25 + 0.25 * index), y + h * 0.5), w * 0.065, w * 0.065);
        break;
    case Glyph::Gamepad: {
        QPainterPath path;
        path.moveTo(x + w * 0.28, y + h * 0.36);
        path.cubicTo(x + w * 0.08, y + h * 0.36, x + w * 0.02, y + h * 0.7, x + w * 0.16, y + h * 0.78);
        path.cubicTo(x + w * 0.28, y + h * 0.86, x + w * 0.34, y + h * 0.64, x + w * 0.5, y + h * 0.64);
        path.cubicTo(x + w * 0.66, y + h * 0.64, x + w * 0.72, y + h * 0.86, x + w * 0.84, y + h * 0.78);
        path.cubicTo(x + w * 0.98, y + h * 0.7, x + w * 0.92, y + h * 0.36, x + w * 0.72, y + h * 0.36);
        path.closeSubpath();
        painter.drawPath(path);
        painter.drawLine(QPointF(x + w * 0.3, y + h * 0.5), QPointF(x + w * 0.3, y + h * 0.64));
        painter.drawLine(QPointF(x + w * 0.23, y + h * 0.57), QPointF(x + w * 0.37, y + h * 0.57));
        painter.setBrush(color);
        painter.drawEllipse(QPointF(x + w * 0.68, y + h * 0.53), w * 0.025, w * 0.025);
        painter.drawEllipse(QPointF(x + w * 0.75, y + h * 0.61), w * 0.025, w * 0.025);
        break;
    }
    case Glyph::Clock:
        painter.drawEllipse(QRectF(x + w * 0.14, y + h * 0.14, w * 0.72, h * 0.72));
        painter.drawLine(QPointF(x + w * 0.5, y + h * 0.28), QPointF(x + w * 0.5, y + h * 0.52));
        painter.drawLine(QPointF(x + w * 0.5, y + h * 0.52), QPointF(x + w * 0.68, y + h * 0.64));
        break;
    case Glyph::Star: {
        QPainterPath path;
        path.moveTo(x + w * 0.5, y + h * 0.12);
        for (int index = 1; index < 10; ++index) {
            const qreal angle = -M_PI / 2.0 + index * M_PI / 5.0;
            const qreal radius = (index % 2 == 0) ? w * 0.4 : w * 0.17;
            path.lineTo(x + w * 0.5 + qCos(angle) * radius, y + h * 0.52 + qSin(angle) * radius);
        }
        path.closeSubpath();
        painter.drawPath(path);
        break;
    }
    case Glyph::Folder: {
        QPainterPath path;
        path.moveTo(x + w * 0.12, y + h * 0.3);
        path.lineTo(x + w * 0.4, y + h * 0.3);
        path.lineTo(x + w * 0.48, y + h * 0.4);
        path.lineTo(x + w * 0.86, y + h * 0.4);
        path.lineTo(x + w * 0.82, y + h * 0.78);
        path.lineTo(x + w * 0.16, y + h * 0.78);
        path.closeSubpath();
        painter.drawPath(path);
        break;
    }
    case Glyph::Warning: {
        QPainterPath path;
        path.moveTo(x + w * 0.5, y + h * 0.12);
        path.lineTo(x + w * 0.9, y + h * 0.82);
        path.lineTo(x + w * 0.1, y + h * 0.82);
        path.closeSubpath();
        painter.drawPath(path);
        painter.drawLine(QPointF(x + w * 0.5, y + h * 0.35), QPointF(x + w * 0.5, y + h * 0.6));
        painter.setBrush(color);
        painter.drawEllipse(QPointF(x + w * 0.5, y + h * 0.71), w * 0.025, w * 0.025);
        break;
    }
    case Glyph::Settings:
        painter.drawEllipse(QRectF(x + w * 0.32, y + h * 0.32, w * 0.36, h * 0.36));
        for (int index = 0; index < 8; ++index) {
            const qreal angle = index * M_PI / 4.0;
            const QPointF inner(x + w * 0.5 + qCos(angle) * w * 0.25, y + h * 0.5 + qSin(angle) * h * 0.25);
            const QPointF outer(x + w * 0.5 + qCos(angle) * w * 0.42, y + h * 0.5 + qSin(angle) * h * 0.42);
            painter.drawLine(inner, outer);
        }
        break;
    case Glyph::Info:
        painter.drawEllipse(QRectF(x + w * 0.14, y + h * 0.14, w * 0.72, h * 0.72));
        painter.setBrush(color);
        painter.drawEllipse(QPointF(x + w * 0.5, y + h * 0.33), w * 0.035, w * 0.035);
        painter.drawLine(QPointF(x + w * 0.5, y + h * 0.47), QPointF(x + w * 0.5, y + h * 0.7));
        break;
    case Glyph::Restart:
        painter.drawArc(QRectF(x + w * 0.18, y + h * 0.18, w * 0.64, h * 0.64), 35 * 16, 290 * 16);
        painter.drawLine(QPointF(x + w * 0.77, y + h * 0.22), QPointF(x + w * 0.83, y + h * 0.43));
        painter.drawLine(QPointF(x + w * 0.77, y + h * 0.22), QPointF(x + w * 0.57, y + h * 0.29));
        break;
    case Glyph::Windows:
        painter.drawLine(QPointF(x + w * 0.5, y + h * 0.18), QPointF(x + w * 0.5, y + h * 0.82));
        painter.drawLine(QPointF(x + w * 0.15, y + h * 0.5), QPointF(x + w * 0.85, y + h * 0.5));
        painter.drawLine(QPointF(x + w * 0.18, y + h * 0.22), QPointF(x + w * 0.5, y + h * 0.18));
        painter.drawLine(QPointF(x + w * 0.5, y + h * 0.18), QPointF(x + w * 0.82, y + h * 0.22));
        painter.drawLine(QPointF(x + w * 0.18, y + h * 0.78), QPointF(x + w * 0.5, y + h * 0.82));
        painter.drawLine(QPointF(x + w * 0.5, y + h * 0.82), QPointF(x + w * 0.82, y + h * 0.78));
        break;
    case Glyph::Linux:
        painter.drawRoundedRect(QRectF(x + w * 0.13, y + h * 0.17, w * 0.74, h * 0.66), 4, 4);
        painter.drawLine(QPointF(x + w * 0.3, y + h * 0.4), QPointF(x + w * 0.42, y + h * 0.5));
        painter.drawLine(QPointF(x + w * 0.42, y + h * 0.5), QPointF(x + w * 0.3, y + h * 0.6));
        painter.drawLine(QPointF(x + w * 0.54, y + h * 0.62), QPointF(x + w * 0.72, y + h * 0.62));
        break;
    case Glyph::Wine: {
        QPainterPath path;
        path.moveTo(x + w * 0.28, y + h * 0.18);
        path.lineTo(x + w * 0.72, y + h * 0.18);
        path.lineTo(x + w * 0.66, y + h * 0.54);
        path.cubicTo(x + w * 0.6, y + h * 0.8, x + w * 0.4, y + h * 0.8, x + w * 0.34, y + h * 0.54);
        path.closeSubpath();
        painter.drawPath(path);
        painter.drawLine(QPointF(x + w * 0.5, y + h * 0.78), QPointF(x + w * 0.5, y + h * 0.9));
        painter.drawLine(QPointF(x + w * 0.34, y + h * 0.9), QPointF(x + w * 0.66, y + h * 0.9));
        break;
    }
    case Glyph::Proton:
        painter.drawRoundedRect(QRectF(x + w * 0.13, y + h * 0.13, w * 0.74, h * 0.74), 5, 5);
        painter.drawLine(QPointF(x + w * 0.36, y + h * 0.3), QPointF(x + w * 0.36, y + h * 0.7));
        painter.drawLine(QPointF(x + w * 0.36, y + h * 0.3), QPointF(x + w * 0.58, y + h * 0.3));
        painter.drawArc(QRectF(x + w * 0.42, y + h * 0.28, w * 0.28, h * 0.3), 90 * 16, -180 * 16);
        break;
    case Glyph::Flatpak:
        painter.drawRoundedRect(QRectF(x + w * 0.14, y + h * 0.14, w * 0.72, h * 0.72), 13, 13);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(x + w * 0.36, y + h * 0.4), w * 0.05, w * 0.05);
        painter.drawEllipse(QPointF(x + w * 0.64, y + h * 0.4), w * 0.05, w * 0.05);
        painter.drawEllipse(QPointF(x + w * 0.5, y + h * 0.64), w * 0.05, w * 0.05);
        break;
    case Glyph::Terminal:
        painter.drawRoundedRect(QRectF(x + w * 0.12, y + h * 0.17, w * 0.76, h * 0.66), 3, 3);
        painter.drawLine(QPointF(x + w * 0.3, y + h * 0.38), QPointF(x + w * 0.4, y + h * 0.48));
        painter.drawLine(QPointF(x + w * 0.4, y + h * 0.48), QPointF(x + w * 0.3, y + h * 0.58));
        painter.drawLine(QPointF(x + w * 0.54, y + h * 0.6), QPointF(x + w * 0.7, y + h * 0.6));
        break;
    case Glyph::Play: {
        QPainterPath path;
        path.moveTo(x + w * 0.32, y + h * 0.16);
        path.lineTo(x + w * 0.84, y + h * 0.5);
        path.lineTo(x + w * 0.32, y + h * 0.84);
        path.closeSubpath();
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawPath(path);
        break;
    }
    case Glyph::Stop:
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(x + w * 0.26, y + h * 0.26, w * 0.48, h * 0.48), 2, 2);
        break;
    case Glyph::Pencil:
        painter.drawLine(QPointF(x + w * 0.25, y + h * 0.75), QPointF(x + w * 0.7, y + h * 0.3));
        painter.drawLine(QPointF(x + w * 0.7, y + h * 0.3), QPointF(x + w * 0.82, y + h * 0.42));
        painter.drawLine(QPointF(x + w * 0.82, y + h * 0.42), QPointF(x + w * 0.37, y + h * 0.82));
        painter.drawLine(QPointF(x + w * 0.2, y + h * 0.82), QPointF(x + w * 0.37, y + h * 0.82));
        break;
    case Glyph::Scan:
        painter.drawEllipse(QRectF(x + w * 0.22, y + h * 0.2, w * 0.5, h * 0.5));
        painter.drawLine(QPointF(x + w * 0.62, y + h * 0.6), QPointF(x + w * 0.84, y + h * 0.82));
        painter.drawLine(QPointF(x + w * 0.5, y + h * 0.3), QPointF(x + w * 0.5, y + h * 0.42));
        painter.drawLine(QPointF(x + w * 0.5, y + h * 0.48), QPointF(x + w * 0.5, y + h * 0.6));
        painter.drawLine(QPointF(x + w * 0.38, y + h * 0.45), QPointF(x + w * 0.62, y + h * 0.45));
        break;
    case Glyph::Trash:
        painter.drawLine(QPointF(x + w * 0.22, y + h * 0.3), QPointF(x + w * 0.78, y + h * 0.3));
        painter.drawLine(QPointF(x + w * 0.4, y + h * 0.2), QPointF(x + w * 0.6, y + h * 0.2));
        painter.drawLine(QPointF(x + w * 0.3, y + h * 0.3), QPointF(x + w * 0.36, y + h * 0.82));
        painter.drawLine(QPointF(x + w * 0.7, y + h * 0.3), QPointF(x + w * 0.64, y + h * 0.82));
        painter.drawLine(QPointF(x + w * 0.36, y + h * 0.82), QPointF(x + w * 0.64, y + h * 0.82));
        break;
    case Glyph::External:
        painter.drawLine(QPointF(x + w * 0.2, y + h * 0.8), QPointF(x + w * 0.8, y + h * 0.2));
        painter.drawLine(QPointF(x + w * 0.44, y + h * 0.2), QPointF(x + w * 0.8, y + h * 0.2));
        painter.drawLine(QPointF(x + w * 0.8, y + h * 0.2), QPointF(x + w * 0.8, y + h * 0.56));
        painter.drawLine(QPointF(x + w * 0.2, y + h * 0.8), QPointF(x + w * 0.2, y + h * 0.4));
        painter.drawLine(QPointF(x + w * 0.2, y + h * 0.8), QPointF(x + w * 0.6, y + h * 0.8));
        break;
    case Glyph::Image:
        painter.drawRoundedRect(QRectF(x + w * 0.14, y + h * 0.16, w * 0.72, h * 0.68), 3, 3);
        painter.drawEllipse(QPointF(x + w * 0.36, y + h * 0.36), w * 0.06, w * 0.06);
        painter.drawLine(QPointF(x + w * 0.22, y + h * 0.74), QPointF(x + w * 0.44, y + h * 0.5));
        painter.drawLine(QPointF(x + w * 0.44, y + h * 0.5), QPointF(x + w * 0.78, y + h * 0.74));
        break;
    case Glyph::Check:
        painter.drawLine(QPointF(x + w * 0.2, y + h * 0.5), QPointF(x + w * 0.42, y + h * 0.72));
        painter.drawLine(QPointF(x + w * 0.42, y + h * 0.72), QPointF(x + w * 0.82, y + h * 0.27));
        break;
    case Glyph::Shield: {
        QPainterPath path;
        path.moveTo(x + w * 0.5, y + h * 0.1);
        path.lineTo(x + w * 0.82, y + h * 0.24);
        path.lineTo(x + w * 0.76, y + h * 0.62);
        path.cubicTo(x + w * 0.7, y + h * 0.78, x + w * 0.57, y + h * 0.87, x + w * 0.5, y + h * 0.9);
        path.cubicTo(x + w * 0.43, y + h * 0.87, x + w * 0.3, y + h * 0.78, x + w * 0.24, y + h * 0.62);
        path.lineTo(x + w * 0.18, y + h * 0.24);
        path.closeSubpath();
        painter.drawPath(path);
        break;
    }
    case Glyph::Steam:
    case Glyph::Gog:
    case Glyph::Epic:
    case Glyph::Ea:
    case Glyph::Ubisoft: {
        QString letter = QStringLiteral("?");
        switch (glyph) {
        case Glyph::Steam: letter = QStringLiteral("S"); break;
        case Glyph::Gog: letter = QStringLiteral("G"); break;
        case Glyph::Epic: letter = QStringLiteral("E"); break;
        case Glyph::Ea: letter = QStringLiteral("EA"); break;
        case Glyph::Ubisoft: letter = QStringLiteral("U"); break;
        default: break;
        }
        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(qMax(7, qRound(h * (letter.size() > 1 ? 0.4 : 0.56))));
        painter.setFont(font);
        painter.setPen(color);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(x + w * 0.08, y + h * 0.08, w * 0.84, h * 0.84));
        painter.drawText(QRectF(x, y, w, h), Qt::AlignCenter, letter);
        break;
    }
    case Glyph::Close:
        painter.drawLine(QPointF(x + w * 0.26, y + h * 0.26), QPointF(x + w * 0.74, y + h * 0.74));
        painter.drawLine(QPointF(x + w * 0.74, y + h * 0.26), QPointF(x + w * 0.26, y + h * 0.74));
        break;
    case Glyph::Download:
        painter.drawLine(QPointF(x + w * 0.5, y + h * 0.16), QPointF(x + w * 0.5, y + h * 0.64));
        painter.drawLine(QPointF(x + w * 0.3, y + h * 0.46), QPointF(x + w * 0.5, y + h * 0.66));
        painter.drawLine(QPointF(x + w * 0.7, y + h * 0.46), QPointF(x + w * 0.5, y + h * 0.66));
        painter.drawLine(QPointF(x + w * 0.2, y + h * 0.82), QPointF(x + w * 0.8, y + h * 0.82));
        break;
    }
    painter.restore();
}

QPixmap renderGlyph(Glyph glyph, const QColor &color, int size, qreal dpr)
{
    const int pixels = qMax(1, qRound(size * dpr));
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(dpr);
    QPainter painter(&pixmap);
    drawGlyph(painter, glyph, QRectF(0, 0, size, size), color);
    painter.end();
    return pixmap;
}

Glyph glyphForSource(const QString &source)
{
    if (source == QStringLiteral("Steam")) return Glyph::Steam;
    if (source == QStringLiteral("GOG")) return Glyph::Gog;
    if (source == QStringLiteral("Epic Games Store")) return Glyph::Epic;
    if (source == QStringLiteral("EA App")) return Glyph::Ea;
    if (source == QStringLiteral("Ubisoft Connect")) return Glyph::Ubisoft;
    return Glyph::Folder;
}

void drawCoverPlaceholder(QPainter &painter, const QRectF &rect, const QString &initials,
                          const QColor &accentSoft, const QColor &accent)
{
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    gradient.setColorAt(0.0, accentSoft.darker(115));
    gradient.setColorAt(0.58, accentSoft);
    gradient.setColorAt(1.0, accent.darker(130));
    painter.fillRect(rect, gradient);

    painter.save();
    painter.setPen(Qt::NoPen);
    QColor glow = accent;
    glow.setAlpha(48);
    painter.setBrush(glow);
    painter.drawEllipse(QPointF(rect.left() + rect.width() * 0.78, rect.top() + rect.height() * 0.2),
                        rect.width() * 0.55, rect.width() * 0.55);
    painter.setBrush(QColor(255, 255, 255, 18));
    painter.drawEllipse(QPointF(rect.left() + rect.width() * 0.18, rect.top() + rect.height() * 0.82),
                        rect.width() * 0.65, rect.width() * 0.65);

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(qMax(18, qRound(rect.width() * 0.27)));
    painter.setFont(font);
    QColor textColor = QColor(255, 255, 255, 220);
    painter.setPen(textColor);
    painter.drawText(rect, Qt::AlignCenter, initials);
    painter.restore();
}

// Resource path of the official brand mark for a launcher source, or an empty
// string when the source has no dedicated logo.
QString brandResource(const QString &source)
{
    if (source == QStringLiteral("Steam")) return QStringLiteral(":/icons/brand/steam.svg");
    if (source == QStringLiteral("GOG")) return QStringLiteral(":/icons/brand/gog.svg");
    if (source == QStringLiteral("Epic Games Store")) return QStringLiteral(":/icons/brand/epic.svg");
    if (source == QStringLiteral("EA App")) return QStringLiteral(":/icons/brand/ea.svg");
    if (source == QStringLiteral("Ubisoft Connect")) return QStringLiteral(":/icons/brand/ubisoft.svg");
    // Runner marks share the brand renderer with launcher sources, so a runner
    // row can show the real Proton logo instead of a generic glyph.
    if (source == QStringLiteral("Proton")) return QStringLiteral(":/icons/brand/proton.svg");
    return {};
}

// Renders a brand SVG and recolours it so the mark follows the theme instead of
// keeping its own palette. Returns a null pixmap when the resource is missing.
QPixmap renderBrand(const QString &source, const QColor &color, int size, qreal dpr)
{
    const QString resource = brandResource(source);
    if (resource.isEmpty() || size <= 0)
        return {};
    QSvgRenderer renderer(resource);
    if (!renderer.isValid())
        return {};

    const int pixels = qMax(1, qRound(size * dpr));
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(dpr);
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        renderer.render(&painter, QRectF(0, 0, size, size));
        // SourceIn keeps the logo's alpha and replaces every colour with `color`.
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(QRectF(0, 0, size, size), color);
    }
    return pixmap;
}
}

QIcon Icons::icon(Glyph glyph, const QColor &color, int size)
{
    QIcon result;
    for (const int iconSize : {16, 18, 20, 24, 32, 48}) {
        result.addPixmap(renderGlyph(glyph, color, iconSize, 1.0));
        result.addPixmap(renderGlyph(glyph, color, iconSize, 2.0));
    }
    Q_UNUSED(size);
    return result;
}

QIcon Icons::themedIcon(Glyph glyph, const QColor &color, int size)
{
    return icon(glyph, color, size);
}

QIcon Icons::sourceIcon(const QString &source, const QColor &color, int size)
{
    QIcon result;
    for (const int iconSize : {16, 18, 20, 24, 32, 48}) {
        for (const qreal dpr : {1.0, 2.0}) {
            const QPixmap pixmap = renderBrand(source, color, iconSize, dpr);
            if (!pixmap.isNull())
                result.addPixmap(pixmap);
        }
    }
    if (!result.isNull())
        return result;
    // Sources without a dedicated logo keep the generic glyph.
    return icon(glyphForSource(source), color, size);
}

void Icons::setButtonIcon(QAbstractButton *button, Glyph glyph, const QColor &color, int size)
{
    if (!button)
        return;
    button->setIcon(icon(glyph, color, size));
    button->setIconSize(QSize(size, size));
}

QPixmap Icons::logoPixmap(int height, qreal devicePixelRatio)
{
    qreal dpr = devicePixelRatio;
    if (dpr <= 0.0) {
        dpr = qApp ? qApp->devicePixelRatio() : 1.0;
        if (dpr <= 0.0)
            dpr = 1.0;
    }
    QSvgRenderer renderer(QStringLiteral(":/icons/vanta-logo.svg"));
    if (!renderer.isValid())
        return {};
    const QSize logical(height, height);
    const QSize pixels(qRound(logical.width() * dpr), qRound(logical.height() * dpr));
    QPixmap pixmap(pixels);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(dpr);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(logical)));
    painter.end();
    return pixmap;
}

QPixmap Icons::coverPixmap(const QString &path, const QSize &pixelSize)
{
    if (path.isEmpty() || !QFileInfo::exists(path) || pixelSize.isEmpty())
        return {};
    const QFileInfo info(path);
    const QString key = QStringLiteral("vanta-cover-%1-%2-%3x%4").arg(info.absoluteFilePath())
        .arg(info.lastModified().toMSecsSinceEpoch()).arg(pixelSize.width()).arg(pixelSize.height());
    QPixmap cached;
    if (QPixmapCache::find(key, &cached))
        return cached;

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize source = reader.size();
    if (source.isValid()) {
        QSize target = source;
        target.scale(pixelSize, Qt::KeepAspectRatioByExpanding);
        reader.setScaledSize(target);
    }
    QImage image = reader.read();
    if (image.isNull())
        return {};
    QPixmap pixmap = QPixmap::fromImage(image);
    if (pixmap.size() != pixelSize)
        pixmap = pixmap.scaled(pixelSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (pixmap.width() > pixelSize.width() || pixmap.height() > pixelSize.height()) {
        const int x = (pixmap.width() - pixelSize.width()) / 2;
        const int y = (pixmap.height() - pixelSize.height()) / 2;
        pixmap = pixmap.copy(x, y, pixelSize.width(), pixelSize.height());
    }
    QPixmapCache::insert(key, pixmap);
    return pixmap;
}

QPixmap Icons::placeholderCover(const QSize &size, const QString &initials,
                                const QColor &accentSoft, const QColor &accent)
{
    if (size.isEmpty())
        return {};
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    drawCoverPlaceholder(painter, QRectF(QPointF(0, 0), QSizeF(size)), initials, accentSoft, accent);
    painter.end();
    return pixmap;
}

QPixmap Icons::roundedCover(const QString &path, const QSize &size, const QString &initials,
                            const QColor &accentSoft, const QColor &accent, qreal radius)
{
    if (size.isEmpty())
        return {};
    QPixmap source = coverPixmap(path, size);
    if (source.isNull())
        source = placeholderCover(size, initials, accentSoft, accent);
    QPixmap rounded(size);
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, size.width(), size.height()), radius, radius);
    painter.setClipPath(clip);
    painter.drawPixmap(QRect(0, 0, size.width(), size.height()), source);
    painter.end();
    return rounded;
}

QIcon Icons::coverIcon(const QString &path, const QSize &size, const QString &initials,
                       const QColor &accentSoft, const QColor &accent)
{
    QIcon iconResult;
    const QPixmap pixmap = roundedCover(path, size, initials, accentSoft, accent, 5);
    if (!pixmap.isNull())
        iconResult.addPixmap(pixmap);
    return iconResult;
}

QColor Icons::colorForName(const QString &name, const QColor &fallback)
{
    if (name == QStringLiteral("Steam")) return QColor(QStringLiteral("#66c0f4"));
    if (name == QStringLiteral("GOG")) return QColor(QStringLiteral("#b06eff"));
    if (name == QStringLiteral("Epic Games Store")) return QColor(QStringLiteral("#e8e8e8"));
    if (name == QStringLiteral("EA App")) return QColor(QStringLiteral("#ff6b5e"));
    if (name == QStringLiteral("Ubisoft Connect")) return QColor(QStringLiteral("#6da8ff"));
    return fallback;
}
