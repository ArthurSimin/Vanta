#include "TutorialOverlay.h"
#include "Common.h"
#include "IconFactory.h"
#include "ThemeManager.h"
#include "Version.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace {
constexpr int kSpotlightRadius = 14;
constexpr int kSpotlightPadding = 6;
constexpr int kBubbleWidth = 400;
}

TutorialOverlay::TutorialOverlay(ThemeManager *theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("tutorialOverlay"));
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFocusPolicy(Qt::StrongFocus);
    buildUi();
    refreshIcons();
    hide();
    if (QWidget *host = parentWidget())
        host->installEventFilter(this);
    if (m_theme)
        connect(m_theme, &ThemeManager::themeChanged, this, &TutorialOverlay::refreshIcons);
}

void TutorialOverlay::buildUi()
{
    m_bubble = new QFrame(this);
    m_bubble->setObjectName(QStringLiteral("tutorialBubble"));
    m_bubble->setFixedWidth(kBubbleWidth);
    auto *layout = new QVBoxLayout(m_bubble);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(10);

    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(12);
    m_logo = new QLabel(m_bubble);
    m_logo->setPixmap(Icons::logoPixmap(46));
    m_logo->setFixedSize(46, 46);
    m_logo->setVisible(false);
    topRow->addWidget(m_logo, 0, Qt::AlignTop);

    auto *titleColumn = new QVBoxLayout;
    titleColumn->setSpacing(3);
    m_counter = new QLabel(m_bubble);
    m_counter->setProperty("class", QStringLiteral("muted"));
    titleColumn->addWidget(m_counter);
    m_title = new QLabel(m_bubble);
    QFont titleFont = m_title->font();
    titleFont.setPixelSize(18);
    titleFont.setWeight(QFont::Bold);
    m_title->setFont(titleFont);
    m_title->setWordWrap(true);
    titleColumn->addWidget(m_title);
    topRow->addLayout(titleColumn, 1);
    layout->addLayout(topRow);

    m_body = new QLabel(m_bubble);
    m_body->setWordWrap(true);
    m_body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_body);

    m_hint = new QLabel(m_bubble);
    m_hint->setProperty("class", QStringLiteral("fieldHelp"));
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    m_skipButton = new QPushButton(QStringLiteral("Skip tutorial"), m_bubble);
    m_skipButton->setObjectName(QStringLiteral("tutorialSkip"));
    m_skipButton->setCursor(Qt::PointingHandCursor);
    connect(m_skipButton, &QPushButton::clicked, this, [this] {
        m_active = false;
        hide();
        emit skipped();
        emit finished();
    });
    footer->addWidget(m_skipButton);
    footer->addStretch(1);

    m_backButton = new QPushButton(QStringLiteral("Back"), m_bubble);
    m_backButton->setCursor(Qt::PointingHandCursor);
    connect(m_backButton, &QPushButton::clicked, this, [this] {
        if (m_index > 0)
            showStep(m_index - 1);
    });
    footer->addWidget(m_backButton);

    m_nextButton = new QPushButton(QStringLiteral("Next"), m_bubble);
    m_nextButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makePrimary(m_nextButton);
    connect(m_nextButton, &QPushButton::clicked, this, [this] {
        if (m_index + 1 < m_steps.size())
            showStep(m_index + 1);
        else
            finish();
    });
    footer->addWidget(m_nextButton);
    layout->addLayout(footer);
}

void TutorialOverlay::setSteps(const QList<Step> &steps)
{
    m_steps = steps;
    m_index = 0;
}

void TutorialOverlay::start(int index)
{
    if (m_steps.isEmpty())
        return;
    m_active = true;
    if (QWidget *host = parentWidget())
        setGeometry(host->rect());
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
    showStep(qBound(0, index, m_steps.size() - 1));
}

void TutorialOverlay::finish()
{
    if (!m_active)
        return;
    m_active = false;
    hide();
    emit finished();
}

void TutorialOverlay::showStep(int index)
{
    if (m_steps.isEmpty())
        return;
    m_index = qBound(0, index, m_steps.size() - 1);
    const Step &step = m_steps.at(m_index);

    m_logo->setVisible(step.showLogo);
    if (step.showLogo)
        m_logo->setPixmap(Icons::logoPixmap(46));
    m_title->setText(step.title);
    m_body->setText(step.body);
    m_hint->setText(step.targetHint);
    m_hint->setVisible(!step.targetHint.isEmpty());
    m_counter->setText(QStringLiteral("Step %1 of %2").arg(m_index + 1).arg(m_steps.size()));
    m_backButton->setEnabled(m_index > 0);
    m_nextButton->setText(m_index + 1 == m_steps.size() ? QStringLiteral("Finish")
                                                        : QStringLiteral("Next"));
    if (m_theme)
        Icons::setButtonIcon(m_nextButton, m_index + 1 == m_steps.size() ? Glyph::Check : Glyph::ChevronRight,
                             QColor(Qt::white), 16);

    positionBubble();
    update();
}

QRect TutorialOverlay::targetRect() const
{
    if (m_steps.isEmpty())
        return {};
    const Step &step = m_steps.at(m_index);
    QWidget *target = step.target.data();
    if (!target || !target->isVisible())
        return {};
    const QPoint topLeft = target->mapTo(this, QPoint(0, 0));
    QRect rect(topLeft, target->size());
    rect = rect.adjusted(-kSpotlightPadding, -kSpotlightPadding,
                         kSpotlightPadding, kSpotlightPadding);
    return rect.intersected(this->rect().adjusted(-40, -40, 40, 40));
}

void TutorialOverlay::positionBubble()
{
    if (!m_bubble)
        return;
    const int margin = 20;
    const QRect bounds = rect().adjusted(margin, margin, -margin, -margin);
    const QRect target = targetRect();
    const QSize bubbleSize(m_bubble->width(), m_bubble->sizeHint().height());
    m_bubble->resize(bubbleSize);

    QPoint position;
    if (target.isNull()) {
        position = QPoint(bounds.center().x() - bubbleSize.width() / 2,
                          bounds.center().y() - bubbleSize.height() / 2);
    } else {
        const bool placeRight = target.right() + margin + bubbleSize.width() <= bounds.right();
        if (placeRight) {
            position.setX(target.right() + margin);
            position.setY(target.center().y() - bubbleSize.height() / 2);
        } else if (target.left() - margin - bubbleSize.width() >= bounds.left()) {
            position.setX(target.left() - margin - bubbleSize.width());
            position.setY(target.center().y() - bubbleSize.height() / 2);
        } else {
            position.setX(target.center().x() - bubbleSize.width() / 2);
            position.setY(target.bottom() + margin);
            if (position.y() + bubbleSize.height() > bounds.bottom())
                position.setY(target.top() - margin - bubbleSize.height());
        }
    }

    position.setX(qBound(bounds.left(), position.x(), qMax(bounds.left(), bounds.right() - bubbleSize.width())));
    position.setY(qBound(bounds.top(), position.y(), qMax(bounds.top(), bounds.bottom() - bubbleSize.height())));
    m_bubble->move(position);
}

void TutorialOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    positionBubble();
}

void TutorialOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        m_active = false;
        hide();
        emit skipped();
        emit finished();
        return;
    }
    if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Space || event->key() == Qt::Key_Return) {
        if (m_index + 1 < m_steps.size())
            showStep(m_index + 1);
        else
            finish();
        return;
    }
    if (event->key() == Qt::Key_Left && m_index > 0) {
        showStep(m_index - 1);
        return;
    }
    QWidget::keyPressEvent(event);
}

bool TutorialOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget()) {
        if (event->type() == QEvent::Resize && m_active)
            setGeometry(parentWidget()->rect());
        else if (event->type() == QEvent::Hide && m_active) {
            m_active = false;
            hide();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TutorialOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const ThemeColors colors = m_theme ? m_theme->colors() : ThemeColors();
    const QRect target = targetRect();

    QPainterPath overlayPath;
    overlayPath.addRect(QRectF(rect()));
    if (!target.isNull()) {
        QPainterPath hole;
        hole.addRoundedRect(QRectF(target), kSpotlightRadius, kSpotlightRadius);
        overlayPath = overlayPath.subtracted(hole);
    }

    QColor shade(0, 0, 0, colors.dark ? 178 : 132);
    painter.fillPath(overlayPath, shade);

    if (!target.isNull()) {
        QPen pen(colors.accent, 2.0);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(target), kSpotlightRadius, kSpotlightRadius);
    }
}

void TutorialOverlay::refreshIcons()
{
    if (!m_theme)
        return;
    const ThemeColors &colors = m_theme->colors();
    Icons::setButtonIcon(m_skipButton, Glyph::Close, colors.muted, 14);
    Icons::setButtonIcon(m_backButton, Glyph::ChevronRight, colors.text, 14);
    Icons::setButtonIcon(m_nextButton,
                         m_index + 1 >= m_steps.size() ? Glyph::Check : Glyph::ChevronRight,
                         QColor(Qt::white), 16);
    if (m_logo->isVisible())
        m_logo->setPixmap(Icons::logoPixmap(46));
    update();
}
