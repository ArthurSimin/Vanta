#include "Sidebar.h"
#include "IconFactory.h"
#include "ThemeManager.h"
#include "Version.h"

#include <QAbstractButton>
#include <QDateTime>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPainter>
#include <QPair>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>

class SidebarItem : public QAbstractButton
{
public:
    SidebarItem(const QString &id, const QString &label, const QIcon &icon,
                const QIcon &selectedIcon, int count, ThemeManager *theme, QWidget *parent)
        : QAbstractButton(parent)
        , m_id(id)
        , m_label(label)
        , m_icon(icon)
        , m_selectedIcon(selectedIcon)
        , m_count(count)
        , m_theme(theme)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setCheckable(false);
        setToolTip(label);
        setFixedHeight(36);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    QString id() const { return m_id; }
    void setCount(int count)
    {
        if (m_count == count)
            return;
        m_count = count;
        update();
    }
    void setSelectedState(bool selected)
    {
        if (m_selected == selected)
            return;
        m_selected = selected;
        update();
    }
    QSize sizeHint() const override { return QSize(210, 36); }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        QAbstractButton::enterEvent(event);
        animateHover(1.0);
    }

    void leaveEvent(QEvent *event) override
    {
        QAbstractButton::leaveEvent(event);
        animateHover(0.0);
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const ThemeColors &colors = m_theme ? m_theme->colors() : ThemeColors();
        QRectF background = QRectF(rect()).adjusted(2.0, 1.0, -2.0, -1.0);

        if (m_selected) {
            QColor fill = colors.accentSoft;
            fill.setAlphaF(0.9);
            painter.setPen(QPen(colors.accent, 1.0));
            painter.setBrush(fill);
        } else if (m_hoverProgress > 0.001) {
            QColor fill = colors.hover;
            fill.setAlphaF(fill.alphaF() * m_hoverProgress);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
        } else {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::NoBrush);
        }
        painter.drawRoundedRect(background, 10, 10);

        if (m_selected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.accent);
            painter.drawRoundedRect(QRectF(3.0, 10.0, 3.0, height() - 20.0), 1.5, 1.5);
        }

        const QRect iconRect(15, (height() - 18) / 2, 18, 18);
        const QIcon &icon = m_selected ? m_selectedIcon : m_icon;
        icon.paint(&painter, iconRect, Qt::AlignCenter);

        QFont labelFont = font();
        labelFont.setPixelSize(13);
        labelFont.setWeight(m_selected ? QFont::DemiBold : QFont::Normal);
        painter.setFont(labelFont);
        painter.setPen(colors.text);

        const QFontMetrics metrics(labelFont);
        int chipWidth = 0;
        QString countText;
        if (m_count >= 0) {
            countText = QString::number(m_count);
            QFont countFont = font();
            countFont.setPixelSize(10);
            countFont.setBold(true);
            const QFontMetrics countMetrics(countFont);
            chipWidth = qMax(21, countMetrics.horizontalAdvance(countText) + 10);
        }
        const int textLeft = 43;
        const int textRight = width() - 12 - (chipWidth > 0 ? chipWidth + 7 : 0);
        const QRect textRect(textLeft, 0, qMax(10, textRight - textLeft), height());
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                         metrics.elidedText(m_label, Qt::ElideRight, textRect.width()));

        if (chipWidth > 0) {
            const QRectF chip(width() - 12 - chipWidth, (height() - 20.0) / 2.0, chipWidth, 20.0);
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_selected ? colors.accent : colors.hover);
            painter.drawRoundedRect(chip, 10, 10);
            QFont countFont = font();
            countFont.setPixelSize(10);
            countFont.setBold(true);
            painter.setFont(countFont);
            painter.setPen(m_selected ? QColor(Qt::white) : colors.muted);
            painter.drawText(chip, Qt::AlignCenter, countText);
        }
    }

private:
    void animateHover(qreal target)
    {
        if (!m_theme || !m_theme->animationsEnabled()) {
            m_hoverProgress = target;
            update();
            return;
        }
        if (m_hoverAnimation) {
            m_hoverAnimation->stop();
            m_hoverAnimation->deleteLater();
            m_hoverAnimation = nullptr;
        }
        m_hoverAnimation = new QVariantAnimation(this);
        m_hoverAnimation->setDuration(130);
        m_hoverAnimation->setStartValue(m_hoverProgress);
        m_hoverAnimation->setEndValue(target);
        m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_hoverAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_hoverProgress = value.toReal();
            update();
        });
        connect(m_hoverAnimation, &QVariantAnimation::finished, this, [this] {
            if (m_hoverAnimation) {
                m_hoverAnimation->deleteLater();
                m_hoverAnimation = nullptr;
            }
        });
        m_hoverAnimation->start();
    }

    QString m_id;
    QString m_label;
    QIcon m_icon;
    QIcon m_selectedIcon;
    int m_count = 0;
    ThemeManager *m_theme = nullptr;
    bool m_selected = false;
    qreal m_hoverProgress = 0.0;
    QVariantAnimation *m_hoverAnimation = nullptr;
};

Sidebar::Sidebar(ThemeManager *theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("sidebarSurface"));
    setFixedWidth(252);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *logoArea = new QWidget(this);
    logoArea->setObjectName(QStringLiteral("sidebarLogoArea"));
    auto *logoLayout = new QHBoxLayout(logoArea);
    logoLayout->setContentsMargins(20, 18, 16, 12);
    logoLayout->setSpacing(11);
    auto *logo = new QLabel(logoArea);
    logo->setPixmap(Icons::logoPixmap(36));
    logo->setFixedSize(36, 36);
    logoLayout->addWidget(logo, 0, Qt::AlignVCenter);
    auto *titles = new QVBoxLayout;
    titles->setContentsMargins(0, 0, 0, 0);
    titles->setSpacing(0);
    auto *name = new QLabel(QStringLiteral("Vanta"), logoArea);
    QFont nameFont = name->font();
    nameFont.setPixelSize(20);
    nameFont.setWeight(QFont::Black);
    name->setFont(nameFont);
    titles->addWidget(name);
    auto *tagline = new QLabel(QStringLiteral("GAME LIBRARY"), logoArea);
    tagline->setProperty("class", QStringLiteral("sidebarTagline"));
    QFont taglineFont = tagline->font();
    taglineFont.setPixelSize(9);
    taglineFont.setBold(true);
    tagline->setFont(taglineFont);
    titles->addWidget(tagline);
    logoLayout->addLayout(titles, 1);
    root->addWidget(logoArea);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("sidebarScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_content = new QWidget(m_scroll);
    m_content->setObjectName(QStringLiteral("sidebarContent"));
    m_groupsLayout = new QVBoxLayout(m_content);
    m_groupsLayout->setContentsMargins(10, 4, 10, 12);
    m_groupsLayout->setSpacing(1);
    m_scroll->setWidget(m_content);
    root->addWidget(m_scroll, 1);

    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("sidebarFooter"));
    auto *footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(12, 8, 12, 14);
    footerLayout->setSpacing(2);

    auto *line = new QFrame(footer);
    line->setObjectName(QStringLiteral("separator"));
    line->setFixedHeight(1);
    footerLayout->addWidget(line);
    footerLayout->addSpacing(6);

    auto makeFooterButton = [footer, footerLayout](const QString &text) {
        auto *button = new QPushButton(text, footer);
        button->setProperty("sidebarFooter", true);
        button->setCursor(Qt::PointingHandCursor);
        button->setIconSize(QSize(16, 16));
        button->setFixedHeight(34);
        footerLayout->addWidget(button);
        return button;
    };
    m_settingsButton = makeFooterButton(QStringLiteral("Settings"));
    m_tutorialButton = makeFooterButton(QStringLiteral("Tutorial"));
    m_aboutButton = makeFooterButton(QStringLiteral("About"));
    m_targets.insert(QStringLiteral("footer.settings"), m_settingsButton);
    m_targets.insert(QStringLiteral("footer.tutorial"), m_tutorialButton);
    m_targets.insert(QStringLiteral("footer.about"), m_aboutButton);
    m_labels.insert(QStringLiteral("footer.settings"), QStringLiteral("Settings"));
    m_labels.insert(QStringLiteral("footer.tutorial"), QStringLiteral("Tutorial"));
    m_labels.insert(QStringLiteral("footer.about"), QStringLiteral("About"));

    m_versionLabel = new QLabel(QStringLiteral("Version %1").arg(QStringLiteral(VANTA_VERSION)), footer);
    m_versionLabel->setProperty("class", QStringLiteral("muted"));
    m_versionLabel->setAlignment(Qt::AlignCenter);
    QFont versionFont = m_versionLabel->font();
    versionFont.setPixelSize(10);
    m_versionLabel->setFont(versionFont);
    footerLayout->addSpacing(5);
    footerLayout->addWidget(m_versionLabel);
    root->addWidget(footer);

    connect(m_settingsButton, &QPushButton::clicked, this, &Sidebar::settingsRequested);
    connect(m_tutorialButton, &QPushButton::clicked, this, &Sidebar::tutorialRequested);
    connect(m_aboutButton, &QPushButton::clicked, this, &Sidebar::aboutRequested);

    refreshTheme();
    rebuild();
}

void Sidebar::setGames(const QList<Game> &games)
{
    m_games = games;
    rebuild();
}

void Sidebar::setCurrentItem(const QString &id)
{
    QString effective = id;
    if (effective.isEmpty() || !m_targets.contains(effective)
        || (effective.startsWith(QStringLiteral("footer.")))) {
        effective = QStringLiteral("library.games");
    }
    m_currentId = effective;
    for (SidebarItem *item : m_items)
        item->setSelectedState(item->id() == effective);
}

void Sidebar::setCollapsedGroups(const QJsonObject &collapsed)
{
    m_collapsed.clear();
    for (auto it = collapsed.constBegin(); it != collapsed.constEnd(); ++it)
        m_collapsed.insert(it.key(), it.value().toBool(false));
    for (auto it = m_groupRows.constBegin(); it != m_groupRows.constEnd(); ++it) {
        const bool hide = m_collapsed.value(it.key(), false);
        for (QWidget *row : it.value())
            row->setVisible(!hide);
    }
    updateGroupHeaders();
}

void Sidebar::refreshTheme()
{
    if (!m_theme)
        return;
    const ThemeColors &colors = m_theme->colors();
    Icons::setButtonIcon(m_settingsButton, Glyph::Settings, colors.muted, 16);
    Icons::setButtonIcon(m_tutorialButton, Glyph::Play, colors.muted, 16);
    Icons::setButtonIcon(m_aboutButton, Glyph::Info, colors.muted, 16);
    rebuild();
}

QString Sidebar::labelFor(const QString &id) const
{
    return m_labels.value(id, QStringLiteral("Library"));
}

QWidget *Sidebar::targetFor(const QString &id) const
{
    return m_targets.value(id, nullptr);
}

QJsonObject Sidebar::collapsedObject() const
{
    QJsonObject object;
    for (auto it = m_collapsed.constBegin(); it != m_collapsed.constEnd(); ++it)
        object.insert(it.key(), it.value());
    return object;
}

void Sidebar::setGroupCollapsed(const QString &groupId, bool collapsed)
{
    if (m_collapsed.value(groupId, false) == collapsed)
        return;
    m_collapsed.insert(groupId, collapsed);
    for (QWidget *row : m_groupRows.value(groupId))
        row->setVisible(!collapsed);
    updateGroupHeaders();
    emit collapsedGroupsChanged(collapsedObject());
}

void Sidebar::updateGroupHeaders()
{
    if (!m_theme)
        return;
    const QColor color = m_theme->colors().muted;
    for (auto it = m_groupHeaders.constBegin(); it != m_groupHeaders.constEnd(); ++it) {
        const bool collapsed = m_collapsed.value(it.key(), false);
        it.value()->setIcon(Icons::icon(collapsed ? Glyph::ChevronRight : Glyph::ChevronDown, color, 14));
    }
}

void Sidebar::addGroup(const QString &id, const QString &title)
{
    auto *header = new QToolButton(m_content);
    header->setObjectName(QStringLiteral("sidebarGroupHeader"));
    header->setText(title);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setIconSize(QSize(14, 14));
    header->setCursor(Qt::PointingHandCursor);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header->setFixedHeight(34);
    m_groupsLayout->addWidget(header);
    m_groupHeaders.insert(id, header);
    m_groupOrder.append(id);
    m_groupRows.insert(id, {});
    connect(header, &QToolButton::clicked, this, [this, id] {
        setGroupCollapsed(id, !m_collapsed.value(id, false));
    });
    m_targets.insert(QStringLiteral("group.") + id, header);
}

void Sidebar::addItem(const QString &groupId, const QString &id, const QString &label,
                      const QIcon &icon, const QIcon &selectedIcon, int count)
{
    auto *item = new SidebarItem(id, label, icon, selectedIcon, count, m_theme, m_content);
    item->setSelectedState(id == m_currentId);
    m_groupsLayout->addWidget(item);
    m_groupRows[groupId].append(item);
    m_items.append(item);
    m_targets.insert(id, item);
    m_labels.insert(id, label);
    connect(item, &QAbstractButton::clicked, this, [this, id] {
        setCurrentItem(id);
        emit filterSelected(id);
    });
    item->setVisible(!m_collapsed.value(groupId, false));
}

void Sidebar::rebuild()
{
    if (m_rebuilding)
        return;
    m_rebuilding = true;
    const int scrollValue = m_scroll->verticalScrollBar()->value();

    m_targets.clear();
    m_targets.insert(QStringLiteral("footer.settings"), m_settingsButton);
    m_targets.insert(QStringLiteral("footer.tutorial"), m_tutorialButton);
    m_targets.insert(QStringLiteral("footer.about"), m_aboutButton);
    m_items.clear();
    m_groupHeaders.clear();
    m_groupRows.clear();
    m_groupOrder.clear();
    while (QLayoutItem *item = m_groupsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            delete widget;
        }
        delete item;
    }

    const ThemeColors &colors = m_theme ? m_theme->colors() : ThemeColors();
    const QColor muted = colors.muted;
    const QColor accent = colors.accent;
    auto glyphIcons = [&](Glyph glyph) {
        return qMakePair(Icons::icon(glyph, muted, 18), Icons::icon(glyph, accent, 18));
    };
    const int recentlyThreshold = 30;

    struct RowSpec {
        QString id;
        QString label;
        QIcon icon;
        QIcon selectedIcon;
        int count = 0;
    };

    QMap<QString, QList<RowSpec>> sections;
    RowSpec allGames;
    allGames.id = QStringLiteral("library.games");
    allGames.label = QStringLiteral("All games");
    allGames.icon = glyphIcons(Glyph::Gamepad).first;
    allGames.selectedIcon = glyphIcons(Glyph::Gamepad).second;
    allGames.count = m_games.size();
    sections[QStringLiteral("library")].append(allGames);

    RowSpec favorites = allGames;
    favorites.id = QStringLiteral("library.favorites");
    favorites.label = QStringLiteral("Favorites");
    favorites.icon = glyphIcons(Glyph::Star).first;
    favorites.selectedIcon = glyphIcons(Glyph::Star).second;
    favorites.count = 0;
    for (const Game &game : m_games) {
        if (game.favorite)
            ++favorites.count;
    }
    sections[QStringLiteral("library")].append(favorites);

    RowSpec recent = favorites;
    recent.id = QStringLiteral("library.recent");
    recent.label = QStringLiteral("Recently played");
    recent.icon = glyphIcons(Glyph::Clock).first;
    recent.selectedIcon = glyphIcons(Glyph::Clock).second;
    recent.count = 0;
    const QDateTime threshold = QDateTime::currentDateTime().addDays(-recentlyThreshold);
    for (const Game &game : m_games) {
        if (game.lastPlayed.isValid() && game.lastPlayed >= threshold)
            ++recent.count;
    }
    sections[QStringLiteral("library")].append(recent);

    QMap<QString, int> sourceCounts;
    QMap<RunnerType, int> runnerCounts;
    QMap<Platform, int> platformCounts;
    int detected = 0;
    int attention = 0;
    int notDetected = 0;
    for (const Game &game : m_games) {
        sourceCounts[game.source.isEmpty() ? QStringLiteral("Manual") : game.source]++;
        runnerCounts[game.runner]++;
        platformCounts[game.platform]++;
        if (scanNeedsAttention(game.compatibility))
            ++attention;
        if (scanIsDetected(game.compatibility))
            ++detected;
        if (game.compatibility == ScanState::NotScanned
            || game.compatibility == ScanState::NoKnownComponents) {
            ++notDetected;
        }
    }

    for (auto it = sourceCounts.constBegin(); it != sourceCounts.constEnd(); ++it) {
        const QString source = it.key();
        RowSpec row;
        row.id = QStringLiteral("source.") + source;
        row.label = source;
        row.icon = Icons::sourceIcon(source, muted, 18);
        row.selectedIcon = Icons::sourceIcon(source, accent, 18);
        row.count = it.value();
        sections[QStringLiteral("sources")].append(row);
    }

    const QList<RunnerType> runnerOrder = {
        RunnerType::Native, RunnerType::Wine, RunnerType::Proton,
        RunnerType::Flatpak, RunnerType::Custom
    };
    for (RunnerType runner : runnerOrder) {
        const int count = runnerCounts.value(runner, 0);
        if (count <= 0)
            continue;
        Glyph glyph = Glyph::Gamepad;
        switch (runner) {
        case RunnerType::Wine: glyph = Glyph::Wine; break;
        case RunnerType::Proton: glyph = Glyph::Proton; break;
        case RunnerType::Flatpak: glyph = Glyph::Flatpak; break;
        case RunnerType::Custom: glyph = Glyph::Terminal; break;
        case RunnerType::Native: glyph = Glyph::Linux; break;
        }
        RowSpec row;
        row.id = QStringLiteral("runner.") + runnerName(runner);
        row.label = runnerName(runner);
        row.icon = glyphIcons(glyph).first;
        row.selectedIcon = glyphIcons(glyph).second;
        row.count = count;
        sections[QStringLiteral("runners")].append(row);
    }

    const QList<Platform> platformOrder = {Platform::Linux, Platform::Windows};
    for (Platform platform : platformOrder) {
        const int count = platformCounts.value(platform, 0);
        if (count <= 0)
            continue;
        RowSpec row;
        row.id = QStringLiteral("platform.") + platformName(platform);
        row.label = platformName(platform);
        row.icon = glyphIcons(platform == Platform::Windows ? Glyph::Windows : Glyph::Linux).first;
        row.selectedIcon = glyphIcons(platform == Platform::Windows ? Glyph::Windows : Glyph::Linux).second;
        row.count = count;
        sections[QStringLiteral("platforms")].append(row);
    }

    if (detected > 0) {
        RowSpec row;
        row.id = QStringLiteral("compat.detected");
        row.label = QStringLiteral("Detected");
        row.icon = glyphIcons(Glyph::Shield).first;
        row.selectedIcon = glyphIcons(Glyph::Shield).second;
        row.count = detected;
        sections[QStringLiteral("compatibility")].append(row);
    }
    if (attention > 0) {
        RowSpec row;
        row.id = QStringLiteral("compat.attention");
        row.label = QStringLiteral("Needs attention");
        row.icon = glyphIcons(Glyph::Warning).first;
        row.selectedIcon = glyphIcons(Glyph::Warning).second;
        row.count = attention;
        sections[QStringLiteral("compatibility")].append(row);
    }
    if (notDetected > 0) {
        RowSpec row;
        row.id = QStringLiteral("compat.notdetected");
        row.label = QStringLiteral("Not detected");
        row.icon = glyphIcons(Glyph::Info).first;
        row.selectedIcon = glyphIcons(Glyph::Info).second;
        row.count = notDetected;
        sections[QStringLiteral("compatibility")].append(row);
    }

    const QList<QPair<QString, QString>> groupOrder = {
        {QStringLiteral("library"), QStringLiteral("Library")},
        {QStringLiteral("sources"), QStringLiteral("Sources")},
        {QStringLiteral("runners"), QStringLiteral("Runners")},
        {QStringLiteral("platforms"), QStringLiteral("Platforms")},
        {QStringLiteral("compatibility"), QStringLiteral("Compatibility")}
    };
    for (const auto &group : groupOrder) {
        const QList<RowSpec> rows = sections.value(group.first);
        if (rows.isEmpty() && group.first != QStringLiteral("library"))
            continue;
        addGroup(group.first, group.second);
        for (const RowSpec &row : rows)
            addItem(group.first, row.id, row.label, row.icon, row.selectedIcon, row.count);
    }
    m_groupsLayout->addStretch(1);
    updateGroupHeaders();

    if (!m_targets.contains(m_currentId))
        m_currentId = QStringLiteral("library.games");
    setCurrentItem(m_currentId);
    m_rebuilding = false;
    m_scroll->verticalScrollBar()->setValue(scrollValue);
}
