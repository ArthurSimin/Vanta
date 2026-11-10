#include "GameCard.h"
#include "Common.h"
#include "I18n.h"
#include "IconFactory.h"
#include "ThemeManager.h"

#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QFileInfo>
#include <QResizeEvent>
#include <QStyleOption>
#include <QtMath>

GameCard::GameCard(ThemeManager *theme, QWidget *parent)
    : QAbstractButton(parent)
    , m_theme(theme)
{
    setCursor(Qt::PointingHandCursor);
    // The grid is the default view, so a card that cannot take focus leaves the
    // whole library unreachable from the keyboard.
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnimation->setDuration(150);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_selectionAnimation = new QPropertyAnimation(this, "selectionProgress", this);
    m_selectionAnimation->setDuration(160);
    m_selectionAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_pulseTimer.setInterval(50);
    connect(&m_pulseTimer, &QTimer::timeout, this, QOverload<>::of(&GameCard::update));
    updateFixedSize();
}

QSize GameCard::sizeForCover(CoverSize size)
{
    int width = 200;
    if (size == CoverSize::Small)
        width = 170;
    else if (size == CoverSize::Large)
        width = 240;
    const int coverWidth = width - 18;
    const int coverHeight = qRound(coverWidth * 4.0 / 3.0);
    return QSize(width, coverHeight + 58);
}

QSize GameCard::sizeHint() const
{
    return sizeForCover(m_coverSize);
}

void GameCard::setGame(const Game &game)
{
    m_game = game;
    setToolTip(game.name);
    updatePlaceholder();
    update();
}

void GameCard::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    if (running) {
        m_runClock.start();
        m_pulseTimer.start();
    } else {
        m_pulseTimer.stop();
    }
    update();
}

void GameCard::setSelectedState(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    animateSelection(selected ? 1.0 : 0.0);
    update();
}

void GameCard::setCoverSize(CoverSize size)
{
    if (m_coverSize == size)
        return;
    m_coverSize = size;
    updateFixedSize();
    updatePlaceholder();
    update();
}

void GameCard::setShowBadges(bool show)
{
    m_showBadges = show;
    update();
}

void GameCard::setAnimationsEnabled(bool enabled)
{
    m_animations = enabled;
    if (!enabled) {
        m_hoverAnimation->stop();
        m_selectionAnimation->stop();
        m_hoverProgress = underMouse() ? 1.0 : 0.0;
        update();
    }
}

void GameCard::refreshTheme()
{
    updatePlaceholder();
    update();
}

void GameCard::setHoverProgress(qreal value)
{
    m_hoverProgress = value;
    update();
}

void GameCard::setSelectionProgress(qreal value)
{
    m_selectionProgress = value;
    update();
}

void GameCard::animateHover(qreal target)
{
    if (!m_animations) {
        setHoverProgress(target);
        return;
    }
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(target);
    m_hoverAnimation->start();
}

void GameCard::animateSelection(qreal target)
{
    if (!m_animations) {
        setSelectionProgress(target);
        return;
    }
    m_selectionAnimation->stop();
    m_selectionAnimation->setStartValue(m_selectionProgress);
    m_selectionAnimation->setEndValue(target);
    m_selectionAnimation->start();
}

void GameCard::enterEvent(QEnterEvent *event)
{
    QAbstractButton::enterEvent(event);
    animateHover(1.0);
}

void GameCard::leaveEvent(QEvent *event)
{
    QAbstractButton::leaveEvent(event);
    animateHover(0.0);
}

void GameCard::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        emit keyboardActivated(m_game.id);
        event->accept();
        return;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
        emit focusMoveRequested(m_game.id, event->key());
        event->accept();
        return;
    default:
        break;
    }
    QAbstractButton::keyPressEvent(event);
}

void GameCard::focusInEvent(QFocusEvent *event)
{
    QAbstractButton::focusInEvent(event);
    update();
}

void GameCard::focusOutEvent(QFocusEvent *event)
{
    QAbstractButton::focusOutEvent(event);
    update();
}

void GameCard::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit activated(m_game.id);
        event->accept();
        return;
    }
    QAbstractButton::mouseDoubleClickEvent(event);
}

void GameCard::contextMenuEvent(QContextMenuEvent *event)
{
    emit contextMenuRequested(m_game.id, event->globalPos());
    event->accept();
}

void GameCard::resizeEvent(QResizeEvent *event)
{
    QAbstractButton::resizeEvent(event);
    updatePlaceholder();
}

void GameCard::changeEvent(QEvent *event)
{
    QAbstractButton::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
        updatePlaceholder();
}

void GameCard::updateFixedSize()
{
    setFixedSize(sizeForCover(m_coverSize));
}

QRect GameCard::coverRect() const
{
    const int side = 9;
    const int coverWidth = width() - side * 2;
    const int coverHeight = qRound(coverWidth * 4.0 / 3.0);
    return QRect(side, side, coverWidth, coverHeight);
}

void GameCard::updatePlaceholder()
{
    if (!m_theme || m_game.name.isEmpty()) {
        m_placeholder = {};
        return;
    }
    const QRect cover = coverRect();
    const qreal dpr = devicePixelRatioF();
    const QSize pixels(qRound(cover.width() * dpr), qRound(cover.height() * dpr));
    m_placeholder = Icons::placeholderCover(pixels, initialsForGame(m_game.name),
                                            m_theme->colors().accentSoft, m_theme->colors().accent);
    m_placeholder.setDevicePixelRatio(dpr);
}

void GameCard::drawBadge(QPainter &painter, const QRect &rect, const QString &text,
                         const QColor &background, const QColor &foreground) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(rect, 6, 6);
    QFont font = painter.font();
    font.setPixelSize(10);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(foreground);
    painter.drawText(rect.adjusted(6, 0, -6, 0), Qt::AlignCenter, text);
    painter.restore();
}

void GameCard::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    const ThemeColors &colors = m_theme ? m_theme->colors() : ThemeColors();
    const QRectF backgroundRect = rect().adjusted(1, 1, -1, -1);
    QColor background = palette().color(QPalette::Base);
    background.setAlphaF(m_theme && m_theme->isDark() ? 0.72 : 0.9);
    if (m_hoverProgress > 0.0) {
        const QColor hovered = colors.surfaceRaised;
        background = QColor(
            qRound(background.red() + (hovered.red() - background.red()) * m_hoverProgress),
            qRound(background.green() + (hovered.green() - background.green()) * m_hoverProgress),
            qRound(background.blue() + (hovered.blue() - background.blue()) * m_hoverProgress));
    }
    QColor border = palette().color(QPalette::Mid);
    border.setAlpha(150);
    if (m_hoverProgress > 0.0) {
        border = QColor(
            qRound(border.red() + (colors.accentHover.red() - border.red()) * m_hoverProgress),
            qRound(border.green() + (colors.accentHover.green() - border.green()) * m_hoverProgress),
            qRound(border.blue() + (colors.accentHover.blue() - border.blue()) * m_hoverProgress));
    }
    if (m_selectionProgress > 0.0)
        border = colors.accent;

    if (m_hoverProgress > 0.0 || m_selectionProgress > 0.0) {
        QColor shadow = colors.shadow;
        shadow.setAlphaF((m_theme && m_theme->isDark() ? 0.28 : 0.12) * qMax(m_hoverProgress, m_selectionProgress));
        painter.setPen(Qt::NoPen);
        painter.setBrush(shadow);
        painter.drawRoundedRect(backgroundRect.adjusted(4, 6, -4, 2), 15, 15);
    }
    QPen backgroundPen(border, m_selectionProgress > 0.0 ? 2.0 : 1.0);
    painter.setPen(backgroundPen);
    painter.setBrush(background);
    painter.drawRoundedRect(backgroundRect, 15, 15);

    const QRect cover = coverRect();
    painter.save();
    QPainterPath clip;
    clip.addRoundedRect(QRectF(cover), 11, 11);
    painter.setClipPath(clip);
    const qreal dpr = devicePixelRatioF();
    const QSize pixelSize(qRound(cover.width() * dpr), qRound(cover.height() * dpr));
    QPixmap coverPixmap;
    if (!m_game.coverPath.isEmpty() && QFileInfo::exists(m_game.coverPath))
        coverPixmap = Icons::coverPixmap(m_game.coverPath, pixelSize);
    if (!coverPixmap.isNull()) {
        coverPixmap.setDevicePixelRatio(dpr);
        painter.drawPixmap(cover, coverPixmap);
    } else if (!m_placeholder.isNull()) {
        painter.drawPixmap(cover, m_placeholder);
    } else {
        painter.fillRect(cover, colors.accentSoft);
    }

    QLinearGradient shade(cover.topLeft(), cover.bottomLeft());
    shade.setColorAt(0.0, QColor(0, 0, 0, 26));
    shade.setColorAt(0.5, QColor(0, 0, 0, 0));
    shade.setColorAt(1.0, QColor(0, 0, 0, 100));
    painter.fillRect(cover, shade);
    painter.restore();

    if (m_showBadges) {
        QFont badgeFont = painter.font();
        badgeFont.setPixelSize(10);
        badgeFont.setBold(true);
        const QFontMetrics badgeMetrics(badgeFont);
        int leftX = cover.left() + 7;
        const int bottomY = cover.bottom() - 31;
        if (!m_game.executableExists()) {
            const QString text = Vanta::tr("Missing");
            const QRect missingRect(leftX, bottomY, badgeMetrics.horizontalAdvance(text) + 14, 24);
            drawBadge(painter, missingRect, text, QColor(QStringLiteral("#b91c1c")), Qt::white);
            leftX = missingRect.right() + 4;
        }
        if (m_game.showsOnlineFix()) {
            const QString text = QStringLiteral("OnlineFix");
            const QRect fixRect(leftX, bottomY, badgeMetrics.horizontalAdvance(text) + 14, 24);
            drawBadge(painter, fixRect, text, colors.accent, Qt::white);
        }
    }

    if (scanIsDetected(m_game.compatibility) || scanNeedsAttention(m_game.compatibility)) {
        const QColor indicator = scanNeedsAttention(m_game.compatibility) ? colors.warning : colors.success;
        const QRect indicatorRect(cover.right() - 31, cover.top() + 7, 24, 24);
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(9, 5, 16, 190));
        painter.drawRoundedRect(indicatorRect, 7, 7);
        Icons::icon(scanNeedsAttention(m_game.compatibility) ? Glyph::Warning : Glyph::Shield,
                    indicator, 14).paint(&painter, indicatorRect.adjusted(5, 5, -5, -5));
        painter.restore();
    }

    if (m_running) {
        const qreal phase = m_runClock.isValid() ? m_runClock.elapsed() / 1000.0 : 0.0;
        const qreal pulse = 0.55 + 0.45 * (0.5 + 0.5 * qSin(phase * 5.0));
        QFont font = painter.font();
        font.setPixelSize(10);
        font.setBold(true);
        const QFontMetrics runMetrics(font);
        const QString runText = Vanta::tr("Running");
        const int runWidth = qMin(cover.width() - 14, 20 + runMetrics.horizontalAdvance(runText) + 12);
        const QRect runRect(cover.left() + 7, cover.top() + 7, runWidth, 24);
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(9, 5, 16, 200));
        painter.drawRoundedRect(runRect, 7, 7);
        QColor dot = colors.success;
        dot.setAlphaF(pulse);
        painter.setBrush(dot);
        painter.drawEllipse(QPointF(runRect.left() + 12, runRect.center().y() + 1), 3.5, 3.5);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(runRect.adjusted(20, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, runText);
        painter.restore();
    }

    const QRect titleRect(10, cover.bottom() + 9, width() - 20, height() - cover.bottom() - 15);
    QFont titleFont = font();
    titleFont.setWeight(QFont::DemiBold);
    titleFont.setPixelSize(qMax(12, qRound(width() * 0.072)));
    painter.setFont(titleFont);
    painter.setPen(palette().color(QPalette::Text));
    const QFontMetrics metrics(titleFont);
    const QString title = metrics.elidedText(m_game.name, Qt::ElideRight, titleRect.width());
    painter.drawText(titleRect, Qt::AlignHCenter | Qt::AlignVCenter, title);

    if (m_selectionProgress > 0.0) {
        QColor glow = colors.accent;
        glow.setAlphaF(0.25 * m_selectionProgress);
        painter.setPen(QPen(glow, 6));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(backgroundRect.adjusted(3, 3, -3, -3), 13, 13);
        painter.setPen(QPen(colors.accent, 2));
        painter.drawRoundedRect(backgroundRect, 15, 15);
    }

    // Keyboard focus has to be visible somewhere other than the selection border,
    // which also appears for a mouse click that never gave the card focus.
    if (hasFocus()) {
        QColor ring = colors.accent;
        painter.save();
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(ring, 1.5, Qt::DashLine));
        painter.drawRoundedRect(backgroundRect.adjusted(4, 4, -4, -4), 11, 11);
        painter.restore();
    }
}
