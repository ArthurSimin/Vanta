#include "OnlineFixSpinner.h"
#include "IconFactory.h"

#include <QHideEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>

#include <cmath>

namespace {
constexpr int kRotationMs = 20000; // the asset's linear 20-second turn
constexpr int kFrameMs = 33;
}

OnlineFixSpinner::OnlineFixSpinner(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_timer.setInterval(kFrameMs);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        const qreal elapsed = m_clock.isValid() ? static_cast<qreal>(m_clock.elapsed()) : 0.0;
        m_angle = m_startAngle - 360.0 * (elapsed / kRotationMs);
        // Keep the value bounded so a long-running dialog does not accumulate
        // large angles.
        m_angle = std::fmod(m_angle, 360.0);
        update();
    });
}

void OnlineFixSpinner::setColor(const QColor &color)
{
    if (m_color == color)
        return;
    m_color = color;
    rebuildLayers();
    update();
}

void OnlineFixSpinner::setAnimated(bool animated)
{
    if (m_animated == animated)
        return;
    m_animated = animated;
    syncTimer();
    update();
}

QSize OnlineFixSpinner::sizeHint() const
{
    return QSize(24, 24);
}

void OnlineFixSpinner::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    if (m_topLayer.isNull())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const qreal side = qMin(width(), height());
    const QPointF centre(width() / 2.0, height() / 2.0);
    const QPointF corner(centre.x() - side / 2.0, centre.y() - side / 2.0);

    painter.save();
    painter.translate(centre);
    painter.rotate(m_angle);
    painter.drawPixmap(corner - centre, m_gearLayer);
    painter.restore();
    painter.drawPixmap(corner, m_topLayer);
}

void OnlineFixSpinner::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    syncTimer();
}

void OnlineFixSpinner::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    syncTimer();
}

void OnlineFixSpinner::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rebuildLayers();
}

void OnlineFixSpinner::rebuildLayers()
{
    const int side = qMin(width(), height());
    if (side <= 0) {
        m_gearLayer = QPixmap();
        m_topLayer = QPixmap();
        return;
    }
    const qreal dpr = devicePixelRatioF();
    m_gearLayer = Icons::onlineFixGearPixmap(side, m_color, dpr);
    m_topLayer = Icons::onlineFixTopPixmap(side, m_color, dpr);
}

void OnlineFixSpinner::syncTimer()
{
    if (m_animated && isVisible()) {
        m_startAngle = m_angle;
        m_clock.restart();
        if (!m_timer.isActive())
            m_timer.start();
    } else {
        m_timer.stop();
        m_clock.invalidate();
    }
}
