#include "LaunchProgressDialog.h"
#include "IconFactory.h"
#include "ThemeManager.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

LaunchProgressDialog::LaunchProgressDialog(const QString &gameName, ThemeManager *theme, QWidget *parent)
    : VantaDialog(QStringLiteral("Launching"), theme, parent)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("launchProgressDialog"));
    setMinimumSize(560, 340);
    resize(600, 380);
    buildUi();
    m_headline->setText(gameName.isEmpty() ? QStringLiteral("Preparing launch")
                                           : QStringLiteral("Preparing %1").arg(gameName));
    refreshIcons();
    if (m_theme)
        connect(m_theme, &ThemeManager::themeChanged, this, &LaunchProgressDialog::refreshIcons);
}

void LaunchProgressDialog::buildUi()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(4, 6, 4, 2);
    layout->setSpacing(12);

    m_headline = new QLabel(content);
    QFont headlineFont = m_headline->font();
    headlineFont.setPixelSize(18);
    headlineFont.setWeight(QFont::Bold);
    m_headline->setFont(headlineFont);
    m_headline->setWordWrap(true);
    layout->addWidget(m_headline);

    m_message = new QLabel(content);
    m_message->setWordWrap(true);
    m_message->setText(QStringLiteral("Preparing the launch environment…"));
    layout->addWidget(m_message);

    m_progress = new QProgressBar(content);
    m_progress->setRange(0, 0);
    layout->addWidget(m_progress);

    auto *detailPanel = new QFrame(content);
    detailPanel->setObjectName(QStringLiteral("compatibilityPanel"));
    auto *detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(14, 12, 14, 12);
    detailLayout->setSpacing(6);
    detailLayout->addWidget(UiHelp::sectionLabel(QStringLiteral("Diagnostics"), detailPanel));
    m_detail = new QLabel(detailPanel);
    m_detail->setWordWrap(true);
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detail->setProperty("class", QStringLiteral("muted"));
    detailLayout->addWidget(m_detail);
    layout->addWidget(detailPanel, 1);

    bodyLayout()->addWidget(content, 1);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    footer->addStretch(1);
    m_cancelButton = new QPushButton(QStringLiteral("Cancel"), this);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    connect(m_cancelButton, &QPushButton::clicked, this, [this] {
        m_cancelButton->setEnabled(false);
        setMessage(QStringLiteral("Cancelling…"));
        emit cancelRequested();
    });
    footer->addWidget(m_cancelButton);
    m_closeButton = new QPushButton(QStringLiteral("Close"), this);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    m_closeButton->setVisible(false);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(m_closeButton);
    bodyLayout()->addLayout(footer);
}

void LaunchProgressDialog::setMessage(const QString &message)
{
    m_message->setText(message);
}

void LaunchProgressDialog::setDetail(const QString &detail)
{
    m_detail->setText(detail);
}

void LaunchProgressDialog::setIndeterminate()
{
    m_progress->setRange(0, 0);
}

void LaunchProgressDialog::setProgress(int value, int maximum)
{
    if (maximum <= 0) {
        m_progress->setRange(0, 0);
        return;
    }
    m_progress->setRange(0, maximum);
    m_progress->setValue(qBound(0, value, maximum));
}

void LaunchProgressDialog::showFailure(const QString &message)
{
    m_finished = true;
    m_progress->setVisible(false);
    m_message->setProperty("class", QStringLiteral("danger"));
    m_message->style()->unpolish(m_message);
    m_message->style()->polish(m_message);
    m_message->setText(message);
    m_cancelButton->setVisible(false);
    m_closeButton->setVisible(true);
}

void LaunchProgressDialog::showSuccess(const QString &message)
{
    m_finished = true;
    m_progress->setVisible(false);
    m_message->setProperty("class", QStringLiteral("success"));
    m_message->style()->unpolish(m_message);
    m_message->style()->polish(m_message);
    m_message->setText(message);
    m_cancelButton->setVisible(false);
    m_closeButton->setVisible(true);
}

void LaunchProgressDialog::finishAndClose()
{
    m_finished = true;
    accept();
}

void LaunchProgressDialog::reject()
{
    if (!m_finished) {
        m_cancelButton->setEnabled(false);
        setMessage(QStringLiteral("Cancelling…"));
        emit cancelRequested();
        return;
    }
    QDialog::reject();
}

void LaunchProgressDialog::refreshIcons()
{
    if (!m_theme)
        return;
    const ThemeColors &colors = m_theme->colors();
    Icons::setButtonIcon(m_cancelButton, Glyph::Close, colors.text, 16);
    Icons::setButtonIcon(m_closeButton, Glyph::Check, colors.text, 16);
}
