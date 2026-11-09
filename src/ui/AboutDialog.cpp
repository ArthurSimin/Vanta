#include "AboutDialog.h"
#include "IconFactory.h"
#include "ThemeManager.h"
#include "Version.h"

#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

AboutDialog::AboutDialog(ThemeManager *theme, QWidget *parent)
    : VantaDialog(QStringLiteral("About Vanta"), theme, parent)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("aboutDialog"));
    setMinimumSize(620, 560);
    resize(660, 640);
    buildUi();
    refreshIcons();
    if (m_theme)
        connect(m_theme, &ThemeManager::themeChanged, this, &AboutDialog::refreshIcons);
}

void AboutDialog::buildUi()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(2, 2, 8, 2);
    layout->setSpacing(14);

    auto *header = new QHBoxLayout;
    header->setSpacing(18);
    auto *logo = new QLabel(content);
    logo->setPixmap(Icons::logoPixmap(76));
    logo->setFixedSize(76, 76);
    header->addWidget(logo, 0, Qt::AlignTop);

    auto *titleColumn = new QVBoxLayout;
    titleColumn->setSpacing(4);
    auto *title = new QLabel(QStringLiteral("Vanta"), content);
    title->setProperty("class", QStringLiteral("pageTitle"));
    titleColumn->addWidget(title);
    auto *subtitle = new QLabel(QStringLiteral("Linux game launcher — version %1").arg(QStringLiteral(VANTA_VERSION)),
                                content);
    subtitle->setProperty("class", QStringLiteral("muted"));
    titleColumn->addWidget(subtitle);
    auto *license = new QLabel(QStringLiteral("Licensed under the GNU Affero General Public License v3.0"), content);
    license->setProperty("class", QStringLiteral("muted"));
    license->setWordWrap(true);
    titleColumn->addWidget(license);
    titleColumn->addStretch(1);
    header->addLayout(titleColumn, 1);
    layout->addLayout(header);

    layout->addWidget(UiHelp::separator(content));

    auto *intro = UiHelp::helpLabel(
        QStringLiteral("Vanta organises a local game library, launches native and Windows games through Wine or "
                       "Proton, discovers runners installed by other launchers, and reports what it finds in a game "
                       "folder. It never downloads games and never claims that a detected fix will work."),
        content);
    layout->addWidget(intro);

    auto *creditsPanel = new QFrame(content);
    creditsPanel->setObjectName(QStringLiteral("infoPanel"));
    auto *creditsLayout = new QVBoxLayout(creditsPanel);
    creditsLayout->setContentsMargins(16, 14, 16, 14);
    creditsLayout->setSpacing(8);
    creditsLayout->addWidget(UiHelp::sectionLabel(QStringLiteral("OnlineFix Linux credit"), creditsPanel));
    creditsLayout->addWidget(UiHelp::helpLabel(
        QStringLiteral("OnlineFix support in Vanta is a port of the launcher-side behaviour of OnlineFix Linux "
                       "(OFLL) by ZzEdovec, used under the GNU Affero General Public License v3.0. The upstream "
                       "project defines how OnlineFix signature files are recognised, how steamfix.ini is swapped "
                       "for FreeTP games, how ExtraProtection is written, and which Wine DLL overrides are composed. "
                       "Vanta reimplements that behaviour in C++/Qt and documents the mapping in ONLINEFIX.md."),
        creditsPanel));

    auto *repoButton = new QPushButton(QStringLiteral("Open the OnlineFix Linux repository"), creditsPanel);
    repoButton->setCursor(Qt::PointingHandCursor);
    repoButton->setAutoDefault(false);
    connect(repoButton, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/ZzEdovec/onlinefix-linux")));
    });
    creditsLayout->addWidget(repoButton, 0, Qt::AlignLeft);

    auto *repoUrl = new QLabel(QStringLiteral("https://github.com/ZzEdovec/onlinefix-linux"), creditsPanel);
    repoUrl->setProperty("monospace", true);
    repoUrl->setWordWrap(true);
    repoUrl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    creditsLayout->addWidget(repoUrl);
    layout->addWidget(creditsPanel);

    auto *componentsPanel = new QFrame(content);
    componentsPanel->setObjectName(QStringLiteral("compatibilityPanel"));
    auto *componentsLayout = new QVBoxLayout(componentsPanel);
    componentsLayout->setContentsMargins(16, 14, 16, 14);
    componentsLayout->setSpacing(7);
    componentsLayout->addWidget(UiHelp::sectionLabel(QStringLiteral("Built with"), componentsPanel));
    componentsLayout->addWidget(UiHelp::helpLabel(
        QStringLiteral("Qt %1 (Widgets, SVG, Concurrent, Network), CMake, and the Ninja build system. "
                       "Icon artwork is original to Vanta. Icons for third-party launchers are simple original "
                       "glyphs, not vendor trademarks.")
            .arg(QString::fromLatin1(qVersion())),
        componentsPanel));
    componentsLayout->addWidget(UiHelp::helpLabel(
        QStringLiteral("Vanta is an independent project. It is not affiliated with Valve, CodeWeavers, "
                       "GloriousEggroll, Kron4ek, or the authors of any game or fix."),
        componentsPanel));
    layout->addWidget(componentsPanel);
    layout->addStretch(1);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    bodyLayout()->addWidget(scroll, 1);

    auto *footer = new QHBoxLayout;
    footer->addStretch(1);
    auto *close = new QPushButton(QStringLiteral("Close"), this);
    close->setCursor(Qt::PointingHandCursor);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(close);
    bodyLayout()->addLayout(footer);
}

void AboutDialog::refreshIcons()
{
    if (!m_theme)
        return;
    const ThemeColors &colors = m_theme->colors();
    const QList<QPushButton *> buttons = findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->text().startsWith(QStringLiteral("Open the OnlineFix")))
            Icons::setButtonIcon(button, Glyph::External, colors.text, 16);
        else if (button->text() == QStringLiteral("Close"))
            Icons::setButtonIcon(button, Glyph::Check, colors.text, 16);
    }
}
