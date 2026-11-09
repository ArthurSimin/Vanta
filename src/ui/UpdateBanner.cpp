#include "UpdateBanner.h"
#include "IconFactory.h"
#include "ThemeManager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
// The release title is often just the version again ("Vanta 1.0.3"), which the
// headline already says. Only prose the headline lacks is worth the space.
bool titleAddsDetail(const QString &name, const QString &tagName, const QString &version)
{
    QString title = name.trimmed();
    if (title.isEmpty() || title.compare(tagName, Qt::CaseInsensitive) == 0)
        return false;
    static const QString prefix = QStringLiteral("Vanta ");
    if (title.startsWith(prefix, Qt::CaseInsensitive))
        title = title.mid(prefix.size()).trimmed();
    return !title.isEmpty() && title.compare(version, Qt::CaseInsensitive) != 0;
}
}

UpdateBanner::UpdateBanner(ThemeManager *theme, QWidget *parent)
    : QFrame(parent)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("updateBanner"));
    buildUi();
    refreshIcons();
    if (m_theme)
        connect(m_theme, &ThemeManager::themeChanged, this, &UpdateBanner::refreshIcons);
    hideBanner();
}

void UpdateBanner::buildUi()
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 10, 12, 10);
    layout->setSpacing(12);

    m_glyph = new QLabel(this);
    m_glyph->setFixedSize(18, 18);
    layout->addWidget(m_glyph, 0, Qt::AlignTop);

    auto *texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(2);

    m_headline = new QLabel(this);
    QFont headlineFont = m_headline->font();
    headlineFont.setWeight(QFont::Bold);
    m_headline->setFont(headlineFont);
    m_headline->setWordWrap(true);
    texts->addWidget(m_headline);

    m_detail = new QLabel(this);
    m_detail->setProperty("class", QStringLiteral("muted"));
    m_detail->setWordWrap(true);
    texts->addWidget(m_detail);
    layout->addLayout(texts, 1);

    m_openButton = new QPushButton(QStringLiteral("Open release page"), this);
    m_openButton->setObjectName(QStringLiteral("updateBannerOpen"));
    m_openButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makePrimary(m_openButton);
    connect(m_openButton, &QPushButton::clicked, this, [this] {
        if (m_url.isEmpty())
            return;
        emit openReleaseRequested(m_url);
    });
    layout->addWidget(m_openButton, 0, Qt::AlignVCenter);

    m_dismissButton = new QToolButton(this);
    m_dismissButton->setObjectName(QStringLiteral("updateBannerDismiss"));
    m_dismissButton->setCursor(Qt::PointingHandCursor);
    m_dismissButton->setToolTip(QStringLiteral("Hide this notification until the next launch"));
    m_dismissButton->setIconSize(QSize(14, 14));
    connect(m_dismissButton, &QToolButton::clicked, this, [this] {
        hideBanner();
        emit dismissed();
    });
    layout->addWidget(m_dismissButton, 0, Qt::AlignVCenter);
}

void UpdateBanner::showRelease(const UpdateRelease &release, const QString &currentVersion)
{
    if (!release.isValid())
        return;

    m_url = release.htmlUrl;
    const QString version = UpdateChecker::normaliseVersion(release.tagName);
    m_headline->setText(version.isEmpty()
                            ? QStringLiteral("A newer Vanta release is available")
                            : QStringLiteral("Vanta %1 is available").arg(version));

    const QString current = UpdateChecker::normaliseVersion(currentVersion);
    QString detail = current.isEmpty()
        ? QStringLiteral("The update is installed through your distribution's package manager, so Vanta only "
                         "opens the release page.")
        : QStringLiteral("You are running %1. The update is installed through your distribution's package manager, "
                         "so Vanta only opens the release page.")
              .arg(current);

    // The release title is optional prose; show it only when it adds something
    // the tag does not already say.
    if (titleAddsDetail(release.name, release.tagName, version))
        detail = QStringLiteral("%1 — %2").arg(release.name.trimmed(), detail);
    m_detail->setText(detail);

    show();
}

void UpdateBanner::hideBanner()
{
    hide();
    m_url.clear();
    m_headline->clear();
    m_detail->clear();
}

bool UpdateBanner::isShowing() const
{
    return isVisible();
}

void UpdateBanner::refreshIcons()
{
    const ThemeColors colors = m_theme ? m_theme->colors() : ThemeColors();
    m_glyph->setPixmap(Icons::icon(Glyph::Download, colors.accent, 18).pixmap(18, 18));
    Icons::setButtonIcon(m_dismissButton, Glyph::Close, colors.muted, 14);
}
