#include "Common.h"
#include "IconFactory.h"
#include "ThemeManager.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QToolButton>
#include <QScreen>
#include <QStyle>
#include <QStyleOption>
#include <QUrl>
#include <QVBoxLayout>

FlowLayout::FlowLayout(QWidget *parent, int margin, int horizontalSpacing, int verticalSpacing)
    : QLayout(parent)
    , m_horizontalSpacing(horizontalSpacing)
    , m_verticalSpacing(verticalSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem *item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
    m_items.append(item);
}

int FlowLayout::horizontalSpacing() const
{
    if (m_horizontalSpacing >= 0)
        return m_horizontalSpacing;
    return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const
{
    if (m_verticalSpacing >= 0)
        return m_verticalSpacing;
    return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

int FlowLayout::count() const
{
    return m_items.size();
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
    return m_items.value(index);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
    if (index >= 0 && index < m_items.size())
        return m_items.takeAt(index);
    return nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return {};
}

int FlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
    QObject *parentObject = parent();
    if (!parentObject)
        return -1;
    if (parentObject->isWidgetType()) {
        QWidget *widget = static_cast<QWidget *>(parentObject);
        return widget->style()->pixelMetric(pm, nullptr, widget);
    }
    return static_cast<QLayout *>(parentObject)->spacing();
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), true);
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    getContentsMargins(&left, &top, &right, &bottom);
    const QRect effectiveRect = rect.adjusted(left, top, -right, -bottom);
    int x = effectiveRect.x();
    int y = effectiveRect.y();
    int lineHeight = 0;

    for (QLayoutItem *item : m_items) {
        QWidget *widget = item->widget();
        if (widget && !widget->isVisible())
            continue;
        const QSize itemSize = item->sizeHint();
        if (x + itemSize.width() > effectiveRect.right() && lineHeight > 0) {
            x = effectiveRect.x();
            y += lineHeight + verticalSpacing();
            lineHeight = 0;
        }
        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), itemSize));
        x += itemSize.width() + horizontalSpacing();
        lineHeight = qMax(lineHeight, itemSize.height());
    }
    return y + lineHeight - rect.y() + bottom;
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem *item : m_items)
        size = size.expandedTo(item->minimumSize());
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    getContentsMargins(&left, &top, &right, &bottom);
    return size + QSize(left + right, top + bottom);
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

VantaDialog::VantaDialog(const QString &title, ThemeManager *theme, QWidget *parent)
    : QDialog(parent)
    , m_theme(theme)
{
    setWindowTitle(title);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);

    auto *outside = new QVBoxLayout(this);
    outside->setContentsMargins(24, 24, 24, 24);
    m_frame = new QFrame(this);
    m_frame->setObjectName(QStringLiteral("vantaDialogFrame"));
    outside->addWidget(m_frame);

    if (m_theme) {
        auto *shadow = new QGraphicsDropShadowEffect(m_frame);
        shadow->setBlurRadius(38);
        shadow->setOffset(0, 10);
        QColor shadowColor = m_theme->colors().shadow;
        shadow->setColor(shadowColor);
        m_frame->setGraphicsEffect(shadow);
    }

    auto *frameLayout = new QVBoxLayout(m_frame);
    frameLayout->setContentsMargins(0, 0, 0, 0);
    frameLayout->setSpacing(0);

    m_header = new QWidget(m_frame);
    m_header->setObjectName(QStringLiteral("vantaDialogHeader"));
    m_header->installEventFilter(this);
    auto *headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(24, 16, 16, 6);
    headerLayout->setSpacing(12);
    m_titleLabel = new QLabel(title, m_header);
    m_titleLabel->setObjectName(QStringLiteral("dialogTitle"));
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 3.0);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    headerLayout->addWidget(m_titleLabel, 1);

    auto *closeButton = new QToolButton(m_header);
    closeButton->setObjectName(QStringLiteral("dialogCloseButton"));
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setToolTip(QStringLiteral("Close"));
    Icons::setButtonIcon(closeButton, Glyph::Close, palette().color(QPalette::Text), 15);
    closeButton->setFixedSize(30, 30);
    connect(closeButton, &QToolButton::clicked, this, &QDialog::reject);
    headerLayout->addWidget(closeButton);
    frameLayout->addWidget(m_header);

    auto *bodyContainer = new QWidget(m_frame);
    bodyContainer->setObjectName(QStringLiteral("vantaDialogBody"));
    m_bodyLayout = new QVBoxLayout(bodyContainer);
    m_bodyLayout->setContentsMargins(24, 6, 24, 24);
    m_bodyLayout->setSpacing(12);
    frameLayout->addWidget(bodyContainer, 1);

    if (m_theme)
        connect(m_theme, &ThemeManager::themeChanged, this, &VantaDialog::applyPalette);
}

VantaDialog::~VantaDialog() = default;

void VantaDialog::setDialogTitle(const QString &title)
{
    setWindowTitle(title);
    if (m_titleLabel)
        m_titleLabel->setText(title);
}

void VantaDialog::applyPalette()
{
    if (!m_header)
        return;
    for (QToolButton *button : m_header->findChildren<QToolButton *>())
        Icons::setButtonIcon(button, Glyph::Close, palette().color(QPalette::Text), 15);
}

void VantaDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (QScreen *screen = this->screen()) {
        const QRect available = screen->availableGeometry();
        if (width() > available.width() - 12 || height() > available.height() - 12)
            resize(qMin(width(), available.width() - 12), qMin(height(), available.height() - 12));
        if (!available.contains(frameGeometry().center()))
            move(available.center() - rect().center());
    }
    if (m_theme && m_theme->animationsEnabled()) {
        setWindowOpacity(0.0);
        auto *fade = new QPropertyAnimation(this, "windowOpacity", this);
        fade->setDuration(150);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);
        connect(fade, &QPropertyAnimation::finished, fade, &QObject::deleteLater);
        fade->start();
    }
}

bool VantaDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_header) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_dragging = true;
                m_dragOffset = mouseEvent->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && m_dragging) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            move(mouseEvent->globalPosition().toPoint() - m_dragOffset);
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_dragging = false;
        }
    }
    return QDialog::eventFilter(watched, event);
}

namespace UiHelp {
QLabel *helpLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("class", QStringLiteral("fieldHelp"));
    label->setWordWrap(true);
    return label;
}

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("class", QStringLiteral("sectionTitle"));
    return label;
}

QLabel *mutedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("class", QStringLiteral("muted"));
    label->setWordWrap(true);
    return label;
}

QFrame *separator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("separator"));
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedHeight(1);
    return line;
}

void makePrimary(QAbstractButton *button)
{
    if (!button)
        return;
    button->setProperty("primary", true);
    button->style()->unpolish(button);
    button->style()->polish(button);
}

void makeDanger(QAbstractButton *button)
{
    if (!button)
        return;
    button->setProperty("danger", true);
    button->style()->unpolish(button);
    button->style()->polish(button);
}

void makeFlat(QAbstractButton *button)
{
    if (!button)
        return;
    button->setProperty("flat", true);
    button->style()->unpolish(button);
    button->style()->polish(button);
}

bool validateDllOverrides(const QString &text, QString *message)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (message)
            message->clear();
        return true;
    }
    const QStringList entries = trimmed.split(QLatin1Char(';'), Qt::KeepEmptyParts);
    const QRegularExpression expression(QStringLiteral("^[A-Za-z0-9_.*\\-]+=([A-Za-z]+(,[A-Za-z]+)*)?$"));
    for (int index = 0; index < entries.size(); ++index) {
        const QString entry = entries.at(index).trimmed();
        if (entry.isEmpty() && index == entries.size() - 1)
            continue;
        if (!expression.match(entry).hasMatch()) {
            if (message) {
                *message = QStringLiteral("Invalid override \"%1\". Use semicolon-separated entries such as d3d11=n,b or winmm=n.").arg(entry);
            }
            return false;
        }
    }
    if (message)
        message->clear();
    return true;
}

bool parseEnvironment(const QString &text, QMap<QString, QString> *environment, QString *message)
{
    if (!environment)
        return false;
    QMap<QString, QString> parsed;
    const QStringList lines = text.split(QLatin1Char('\n'));
    const QRegularExpression keyPattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    for (int lineNumber = 1; lineNumber <= lines.size(); ++lineNumber) {
        const QString raw = lines.at(lineNumber - 1);
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            if (message)
                *message = QStringLiteral("Environment line %1 must use KEY=VALUE.").arg(lineNumber);
            return false;
        }
        const QString key = line.left(equals).trimmed();
        if (!keyPattern.match(key).hasMatch()) {
            if (message)
                *message = QStringLiteral("Invalid environment variable name on line %1: %2").arg(lineNumber).arg(key);
            return false;
        }
        parsed.insert(key, line.mid(equals + 1));
    }
    *environment = parsed;
    if (message)
        message->clear();
    return true;
}

QString environmentToText(const QMap<QString, QString> &environment)
{
    QStringList lines;
    for (auto it = environment.constBegin(); it != environment.constEnd(); ++it)
        lines.append(QStringLiteral("%1=%2").arg(it.key(), it.value()));
    return lines.join(QLatin1Char('\n'));
}

QString dataSize(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("Unknown");
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    const double kib = bytes / 1024.0;
    if (kib < 1024.0)
        return QStringLiteral("%1 KiB").arg(kib, 0, 'f', 1);
    const double mib = kib / 1024.0;
    if (mib < 1024.0)
        return QStringLiteral("%1 MiB").arg(mib, 0, 'f', 1);
    return QStringLiteral("%1 GiB").arg(mib / 1024.0, 0, 'f', 2);
}

void openContainingDirectory(const QString &path)
{
    QFileInfo info(path);
    QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (directory.isEmpty())
        directory = QDir::homePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}
}
