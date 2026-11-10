#include "SteamPromptDialog.h"
#include "I18n.h"
#include "IconFactory.h"
#include "ThemeManager.h"

#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

SteamPromptDialog::SteamPromptDialog(const QString &gameName, ThemeManager *theme, QWidget *parent)
    : VantaDialog(Vanta::tr("Steam is not running"), theme, parent)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("steamPromptDialog"));
    setMinimumWidth(720);
    resize(780, 420);

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(16);

    auto *headlineRow = new QHBoxLayout;
    headlineRow->setSpacing(14);

    m_warningIcon = new QLabel(content);
    m_warningIcon->setFixedSize(38, 38);
    m_warningIcon->setAlignment(Qt::AlignCenter);
    m_warningIcon->setProperty("class", QStringLiteral("warningChip"));
    headlineRow->addWidget(m_warningIcon, 0, Qt::AlignTop);

    auto *headlineColumn = new QVBoxLayout;
    headlineColumn->setSpacing(5);
    auto *headline = new QLabel(Vanta::tr("Online play will not work without Steam"), content);
    QFont headlineFont = headline->font();
    headlineFont.setPixelSize(17);
    headlineFont.setWeight(QFont::DemiBold);
    headline->setFont(headlineFont);
    headline->setWordWrap(true);
    headlineColumn->addWidget(headline);

    m_gameLabel = new QLabel(content);
    m_gameLabel->setProperty("class", QStringLiteral("muted"));
    m_gameLabel->setWordWrap(true);
    headlineColumn->addWidget(m_gameLabel);
    headlineRow->addLayout(headlineColumn, 1);
    layout->addLayout(headlineRow);

    auto *explainPanel = new QFrame(content);
    explainPanel->setObjectName(QStringLiteral("infoPanel"));
    auto *explainLayout = new QVBoxLayout(explainPanel);
    explainLayout->setContentsMargins(16, 14, 16, 14);
    explainLayout->setSpacing(8);
    explainLayout->addWidget(UiHelp::helpLabel(
        Vanta::tr("OnlineFix titles talk to Steam services to authenticate and to match players. "
                       "If the Steam client is closed, the game can still start and single-player content "
                       "usually works, but online features, matchmaking and friend lobbies will fail."),
        explainPanel));
    explainLayout->addWidget(UiHelp::helpLabel(
        Vanta::tr("Vanta does not start Steam on its own any more. Pick an option below: Vanta can "
                       "open Steam and wait until the client is signed in, or launch the game right now "
                       "with the OnlineFix files applied but no Steam session."),
        explainPanel));
    layout->addWidget(explainPanel);

    layout->addWidget(UiHelp::helpLabel(
        Vanta::tr("The OnlineFix configuration Vanta writes is identical in both cases; only the "
                       "Steam session differs."),
        content));
    layout->addStretch(1);

    bodyLayout()->addWidget(content, 1);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(9);
    m_cancelButton = new QPushButton(Vanta::tr("Cancel"), this);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(m_cancelButton);
    footer->addStretch(1);

    m_withoutSteamButton = new QPushButton(
        Vanta::tr("Open the game with no steam ( online will not work )"), this);
    m_withoutSteamButton->setCursor(Qt::PointingHandCursor);
    m_withoutSteamButton->setToolTip(Vanta::tr("Launch now. Online play will not work."));
    connect(m_withoutSteamButton, &QPushButton::clicked, this, [this] {
        m_choice = Choice::PlayWithoutSteam;
        accept();
    });
    footer->addWidget(m_withoutSteamButton);

    m_openSteamButton = new QPushButton(Vanta::tr("Open steam for online"), this);
    m_openSteamButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makePrimary(m_openSteamButton);
    connect(m_openSteamButton, &QPushButton::clicked, this, [this] {
        m_choice = Choice::OpenSteam;
        accept();
    });
    footer->addWidget(m_openSteamButton);
    bodyLayout()->addLayout(footer);

    m_gameLabel->setText(gameName.isEmpty()
        ? Vanta::tr("The Steam client is closed on this system.")
        : Vanta::tr("“%1” uses OnlineFix, and the Steam client is closed on this system.")
              .arg(gameName));

    refreshIcons();
    if (m_theme)
        connect(m_theme, &ThemeManager::themeChanged, this, &SteamPromptDialog::refreshIcons);
}

void SteamPromptDialog::refreshIcons()
{
    if (!m_theme || !m_warningIcon)
        return;
    m_warningIcon->setPixmap(Icons::icon(Glyph::Warning, m_theme->colors().warning, 30)
                                 .pixmap(30, 30));
}

SteamPromptDialog::Choice SteamPromptDialog::prompt(const QString &gameName, ThemeManager *theme, QWidget *parent)
{
    SteamPromptDialog dialog(gameName, theme, parent);
    if (dialog.exec() != QDialog::Accepted)
        return Choice::Cancel;
    return dialog.choice();
}
