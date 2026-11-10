#include "GameSelectionBar.h"
#include "I18n.h"
#include "IconFactory.h"
#include "ThemeManager.h"

#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace {
constexpr int kCoverWidth = 42;
constexpr int kCoverHeight = 56;

QString playtimeText(const Game &game)
{
    if (game.playtimeSeconds <= 0)
        return {};
    return Vanta::tr("%1 played").arg(formatPlaytime(game.playtimeSeconds));
}

// A Custom runner may be a complete command line with no game executable at
// all, so an empty executable field only means a launch will fail for the other
// runners. A path that is set but gone is always a problem.
bool executableMissing(const Game &game)
{
    if (!game.executable.trimmed().isEmpty())
        return !game.executableExists();
    return game.runner != RunnerType::Custom;
}

QString lastPlayedText(const Game &game)
{
    if (!game.lastPlayed.isValid())
        return Vanta::tr("Never played");
    return Vanta::tr("Last played %1").arg(formatLastPlayed(game.lastPlayed));
}
}

GameSelectionBar::GameSelectionBar(ThemeManager *theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("selectionBar"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *line = new QFrame(this);
    line->setObjectName(QStringLiteral("separator"));
    line->setFixedHeight(1);
    root->addWidget(line);
    m_body = new QWidget(this);
    m_body->setObjectName(QStringLiteral("selectionBarBody"));
    auto *layout = new QHBoxLayout(m_body);
    layout->setContentsMargins(22, 10, 22, 10);
    layout->setSpacing(13);

    m_cover = new QLabel(m_body);
    m_cover->setFixedSize(kCoverWidth, kCoverHeight);
    m_cover->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_cover, 0, Qt::AlignVCenter);

    auto *texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(2);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    m_name = new QLabel(m_body);
    QFont nameFont = m_name->font();
    nameFont.setPixelSize(15);
    nameFont.setWeight(QFont::DemiBold);
    m_name->setFont(nameFont);
    titleRow->addWidget(m_name, 0, Qt::AlignVCenter);

    m_fixToggle = new QToolButton(m_body);
    m_fixToggle->setObjectName(QStringLiteral("selectionBarFix"));
    m_fixToggle->setText(QStringLiteral("OnlineFix"));
    m_fixToggle->setCheckable(true);
    m_fixToggle->setChecked(true);
    m_fixToggle->setCursor(Qt::PointingHandCursor);
    m_fixToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_fixToggle->setFixedHeight(22);
    m_fixToggle->setToolTip(Vanta::tr(
        "Apply the OnlineFix configuration for this launch. Vanta backs up every file it touches, "
        "and detection is not proof that a fix works."));
    QFont chipFont = m_fixToggle->font();
    chipFont.setPixelSize(10);
    chipFont.setBold(true);
    m_fixToggle->setFont(chipFont);
    m_fixToggle->setVisible(false);
    connect(m_fixToggle, &QToolButton::toggled, this, [this](bool on) {
        m_fixToggle->setText(on ? QStringLiteral("OnlineFix") : Vanta::tr("OnlineFix off"));
    });
    titleRow->addWidget(m_fixToggle, 0, Qt::AlignVCenter);
    titleRow->addStretch(1);
    texts->addLayout(titleRow);

    m_meta = new QLabel(m_body);
    m_meta->setProperty("class", QStringLiteral("muted"));
    QFont metaFont = m_meta->font();
    metaFont.setPixelSize(12);
    m_meta->setFont(metaFont);
    texts->addWidget(m_meta);
    layout->addLayout(texts, 1);

    m_launchButton = new QPushButton(m_body);
    m_launchButton->setObjectName(QStringLiteral("selectionBarLaunch"));
    m_launchButton->setCursor(Qt::PointingHandCursor);
    m_launchButton->setMinimumWidth(126);
    m_launchButton->setMinimumHeight(36);
    connect(m_launchButton, &QPushButton::clicked, this, [this] {
        if (m_game.id.isEmpty())
            return;
        if (m_running)
            emit stopRequested(m_game.id);
        else
            emit launchRequested(m_game.id, onlineFixEnabled());
    });
    layout->addWidget(m_launchButton, 0, Qt::AlignVCenter);

    m_menuButton = new QToolButton(m_body);
    m_menuButton->setObjectName(QStringLiteral("selectionBarMenu"));
    m_menuButton->setCursor(Qt::PointingHandCursor);
    m_menuButton->setToolTip(Vanta::tr("More actions for this game"));
    m_menuButton->setIconSize(QSize(14, 14));
    m_menuButton->setFixedSize(30, 36);
    connect(m_menuButton, &QToolButton::clicked, this, [this] {
        if (m_game.id.isEmpty())
            return;
        emit menuRequested(m_game.id, m_menuButton->mapToGlobal(
                                          QPoint(0, m_menuButton->height())));
    });
    layout->addWidget(m_menuButton, 0, Qt::AlignVCenter);

    root->addWidget(m_body);

    // A child widget cannot use setWindowOpacity, so the fade rides on an opacity
    // effect while the height animation drives the layout above it.
    m_opacity = new QGraphicsOpacityEffect(this);
    m_opacity->setOpacity(0.0);
    setGraphicsEffect(m_opacity);

    m_reveal = new QVariantAnimation(this);
    m_reveal->setDuration(220);
    m_reveal->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_reveal, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        const qreal progress = qBound(0.0, value.toReal(), 1.0);
        setMaximumHeight(qRound(m_fullHeight * progress));
        if (m_opacity)
            m_opacity->setOpacity(progress);
    });
    connect(m_reveal, &QVariantAnimation::finished, this, [this] {
        if (m_reveal->endValue().toReal() < 0.5) {
            setMaximumHeight(0);
            setVisible(false);
        } else {
            setMaximumHeight(QWIDGETSIZE_MAX);
        }
        if (m_opacity)
            m_opacity->setOpacity(1.0);
    });

    setMaximumHeight(0);
    setGame(Game());
    refreshTheme();
}

void GameSelectionBar::setGame(const Game &game)
{
    const bool sameGame = m_game.id == game.id;
    m_game = game;
    // The height depends on what the strip shows: a game with a fix toggle is
    // taller than one without, so a measurement taken for the previous game
    // would clip the new one mid-reveal.
    if (m_reveal && m_reveal->state() != QAbstractAnimation::Running)
        m_fullHeight = 0;
    if (game.id.isEmpty())
        m_running = false;
    if (!sameGame)
        m_fixToggle->setChecked(true);
    updateContent();
    updatePrimaryButton();
    animateReveal(!game.id.isEmpty());
}

// Slides the strip up on reveal and back down on hide. The height animation is
// what makes the library view above resize smoothly rather than jump, and the
// opacity fade keeps the text from appearing mid-slide.
void GameSelectionBar::animateReveal(bool visible)
{
    if (!m_reveal)
        return;

    if (m_fullHeight <= 0)
        m_fullHeight = sizeHint().height();

    const bool animations = !m_theme || m_theme->animationsEnabled();
    if (!animations) {
        // The fade is driven by the animation's callbacks, which never run when
        // animations are off. Without setting the opacity here the strip would
        // take up its full height but paint nothing, leaving a blank band above
        // the status bar with an invisible Launch button on it.
        m_reveal->stop();
        if (m_opacity)
            m_opacity->setOpacity(visible ? 1.0 : 0.0);
        setMaximumHeight(visible ? QWIDGETSIZE_MAX : 0);
        setVisible(visible);
        return;
    }

    // Nothing to do when the strip is already in the requested state.
    if (visible && isVisible() && m_reveal->state() != QAbstractAnimation::Running)
        return;
    if (!visible && !isVisible() && m_reveal->state() != QAbstractAnimation::Running)
        return;

    m_reveal->stop();
    m_reveal->setStartValue(m_reveal->currentValue().toReal());
    m_reveal->setEndValue(visible ? 1.0 : 0.0);
    if (visible) {
        setMaximumHeight(m_fullHeight);
        setVisible(true);
    }
    m_reveal->start();
}

bool GameSelectionBar::onlineFixEnabled() const
{
    return m_game.showsOnlineFix() && m_fixToggle->isChecked();
}

void GameSelectionBar::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    // The meta line changes between "Running now" and the stored playtime, so it
    // has to be rebuilt too and not only the primary button.
    updateContent();
    updatePrimaryButton();
}

void GameSelectionBar::updateContent()
{
    if (m_game.id.isEmpty()) {
        m_name->clear();
        m_meta->clear();
        m_cover->clear();
        m_fixToggle->setVisible(false);
        return;
    }

    m_name->setText(m_game.name);

    m_fixToggle->setVisible(m_game.showsOnlineFix());
    m_fixToggle->setText(m_fixToggle->isChecked() ? QStringLiteral("OnlineFix")
                                                  : Vanta::tr("OnlineFix off"));

    QStringList parts;
    // While the game is open the stored last-played time is stale, and on a
    // first run it is not set at all. Reporting the live state is honest and
    // stops a running game from claiming it was never played.
    parts.append(m_running ? Vanta::tr("Running now") : lastPlayedText(m_game));
    const QString playtime = playtimeText(m_game);
    if (!playtime.isEmpty())
        parts.append(playtime);
    parts.append(Vanta::tr("%1 · %2").arg(runnerDisplayName(m_game.runner), platformDisplayName(m_game.platform)));
    if (executableMissing(m_game))
        parts.append(Vanta::tr("Executable missing"));
    m_meta->setText(parts.join(QStringLiteral("   ·   ")));

    const ThemeColors colors = m_theme ? m_theme->colors() : ThemeColors();
    const qreal dpr = devicePixelRatioF();
    const QSize pixels(qRound(kCoverWidth * dpr), qRound(kCoverHeight * dpr));
    QPixmap cover;
    if (!m_game.coverPath.isEmpty())
        cover = Icons::coverPixmap(m_game.coverPath, pixels);
    if (cover.isNull()) {
        cover = Icons::placeholderCover(pixels, initialsForGame(m_game.name),
                                       colors.accentSoft, colors.accent);
    }
    if (!cover.isNull()) {
        cover.setDevicePixelRatio(dpr);
        m_cover->setPixmap(cover);
    } else {
        m_cover->clear();
    }
}

void GameSelectionBar::updatePrimaryButton()
{
    if (m_game.id.isEmpty()) {
        m_launchButton->setText(Vanta::tr("Launch"));
        m_launchButton->setEnabled(false);
        m_menuButton->setEnabled(false);
        return;
    }
    // Launch plan building fails on a missing executable, so offering an enabled
    // Launch button only produces a dialog. The meta line already says the file
    // is gone; the button should agree with it. Stopping a running game stays
    // available because the process exists whether or not the file does now.
    const bool canLaunch = m_running || !executableMissing(m_game);
    m_launchButton->setEnabled(canLaunch);
    m_launchButton->setToolTip(canLaunch ? QString()
        : Vanta::tr("The configured executable is missing. Set the game's executable first."));
    m_menuButton->setEnabled(true);
    m_launchButton->setProperty("primary", !m_running);
    m_launchButton->setProperty("danger", m_running);
    m_launchButton->setText(m_running ? Vanta::tr("Stop") : Vanta::tr("Launch"));
    m_launchButton->style()->unpolish(m_launchButton);
    m_launchButton->style()->polish(m_launchButton);

    if (m_theme) {
        Icons::setButtonIcon(m_launchButton, m_running ? Glyph::Stop : Glyph::Play,
                             m_running ? m_theme->colors().danger : QColor(Qt::white), 15);
        Icons::setButtonIcon(m_menuButton, Glyph::ChevronDown, m_theme->colors().text, 15);
    }
}

void GameSelectionBar::refreshTheme()
{
    updateContent();
    updatePrimaryButton();
    // A theme change can alter the fonts and therefore the strip height. The
    // measurement below is only taken during an animation, so it has to be
    // invalidated while the strip is settled or the reveal would clamp to a
    // stale height and snap at the end.
    if (!m_reveal || m_reveal->state() != QAbstractAnimation::Running)
        m_fullHeight = 0;
}
