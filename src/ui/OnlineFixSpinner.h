#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

// The OnlineFix mark with the gear turning on its own. Qt's SVG renderer does
// not run the CSS animation in the asset, so the rotation is reproduced here:
// one anticlockwise turn every 20 seconds, matching the file. The globe and
// wrench stay still because the logo is drawn from its two layers.
class OnlineFixSpinner : public QWidget
{
    Q_OBJECT

public:
    explicit OnlineFixSpinner(QWidget *parent = nullptr);

    void setColor(const QColor &color);
    // A caller that follows the app's animations setting turns the timer off;
    // the mark then stays at a fixed angle instead of disappearing.
    void setAnimated(bool animated);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildLayers();
    void syncTimer();

    QColor m_color = QColor(Qt::white);
    bool m_animated = true;
    qreal m_angle = 0.0;
    qreal m_startAngle = 0.0;
    QElapsedTimer m_clock;
    QTimer m_timer;
    QPixmap m_gearLayer;
    QPixmap m_topLayer;
};
