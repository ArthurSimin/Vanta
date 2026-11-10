#include "CoverPickerDialog.h"
#include "I18n.h"
#include "IconFactory.h"
#include "ThemeManager.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kTileWidth = 132;
constexpr int kTileHeight = 198;

// Placeholder tile so the grid has the right shape before thumbnails arrive.
QPixmap placeholderTile(const QSize &size, const ThemeColors &colors, int index)
{
    QPixmap pixmap(size * 2);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(colors.border, 1));
    painter.setBrush(colors.surfaceRaised);
    painter.drawRoundedRect(QRectF(QPointF(0.5, 0.5), QSizeF(size) - QSizeF(1, 1)), 8, 8);
    painter.setPen(colors.muted);
    painter.drawText(QRectF(QPointF(0, 0), QSizeF(size)), Qt::AlignCenter,
                     QString::number(index + 1));
    return pixmap;
}
}

CoverPickerDialog::CoverPickerDialog(const QString &gameName, const QString &matchedName,
                                     const QList<CoverArt> &artworks, ThemeManager *theme,
                                     QWidget *parent)
    : VantaDialog(Vanta::tr("Choose cover art"), theme, parent)
    , m_theme(theme)
    , m_artworks(artworks)
{
    setObjectName(QStringLiteral("coverPickerDialog"));
    setMinimumSize(900, 620);
    resize(960, 700);

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(12);

    auto *headline = new QLabel(
        Vanta::tr("%1 artwork(s) for “%2”").arg(artworks.size()).arg(matchedName), content);
    QFont headlineFont = headline->font();
    headlineFont.setPixelSize(16);
    headlineFont.setWeight(QFont::DemiBold);
    headline->setFont(headlineFont);
    headline->setWordWrap(true);
    layout->addWidget(headline);

    layout->addWidget(UiHelp::helpLabel(
        Vanta::tr("You asked for “%1”. Pick the artwork Vanta should use; the preview on the right "
                  "shows the full-size image before you commit.").arg(gameName),
        content));

    auto *columns = new QHBoxLayout;
    columns->setSpacing(14);

    m_list = new QListWidget(content);
    m_list->setObjectName(QStringLiteral("coverArtList"));
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWrapping(true);
    m_list->setSpacing(8);
    m_list->setIconSize(QSize(kTileWidth, kTileHeight));
    m_list->setGridSize(QSize(kTileWidth + 24, kTileHeight + 26));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(true);
    QFont tileFont = m_list->font();
    tileFont.setPixelSize(10);
    m_list->setFont(tileFont);
    columns->addWidget(m_list, 1);

    const ThemeColors colors = themeManager() ? themeManager()->colors() : ThemeColors();
    for (int index = 0; index < m_artworks.size(); ++index) {
        const CoverArt &art = m_artworks.at(index);
        auto *item = new QListWidgetItem(m_list);
        item->setIcon(QIcon(placeholderTile(QSize(kTileWidth, kTileHeight), colors, index)));
        item->setData(Qt::UserRole, art.gridId);
        QString label = art.width > 0 && art.height > 0
            ? QStringLiteral("%1×%2").arg(art.width).arg(art.height)
            : Vanta::tr("Artwork %1").arg(index + 1);
        if (!art.author.isEmpty())
            label += QStringLiteral(" · %1").arg(art.author);
        item->setText(label);
        item->setTextAlignment(Qt::AlignHCenter);
        item->setToolTip(art.author.isEmpty()
                             ? Vanta::tr("Artwork %1").arg(index + 1)
                             : Vanta::tr("Artwork %1 by %2").arg(index + 1).arg(art.author));
    }

    auto *previewPanel = new QFrame(content);
    previewPanel->setObjectName(QStringLiteral("infoPanel"));
    auto *previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(14, 14, 14, 14);
    previewLayout->setSpacing(10);

    m_preview = new QLabel(previewPanel);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(260, 390);
    m_preview->setScaledContents(false);
    previewLayout->addWidget(m_preview, 1);

    m_details = UiHelp::helpLabel(QString(), previewPanel);
    m_details->setAlignment(Qt::AlignHCenter);
    m_details->setWordWrap(true);
    previewLayout->addWidget(m_details);
    columns->addWidget(previewPanel, 0);

    layout->addLayout(columns, 1);
    bodyLayout()->addWidget(content, 1);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(9);
    auto *cancel = new QPushButton(Vanta::tr("Cancel"), this);
    cancel->setCursor(Qt::PointingHandCursor);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(cancel);
    footer->addStretch(1);
    m_useButton = new QPushButton(Vanta::tr("Use this artwork"), this);
    m_useButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makePrimary(m_useButton);
    connect(m_useButton, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(m_useButton);
    bodyLayout()->addLayout(footer);

    connect(m_list, &QListWidget::currentRowChanged, this, [this] { updateSelection(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { accept(); });

    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
    updateSelection();

    if (m_theme)
        connect(m_theme, &ThemeManager::themeChanged, this, &CoverPickerDialog::refreshIcons);
}

void CoverPickerDialog::updateSelection()
{
    const int row = m_list->currentRow();
    const bool valid = row >= 0 && row < m_artworks.size();
    m_useButton->setEnabled(valid);
    if (!valid) {
        m_preview->clear();
        m_details->clear();
        return;
    }
    const CoverArt &art = m_artworks.at(row);
    const QIcon icon = m_list->item(row)->icon();
    if (!icon.isNull())
        m_preview->setPixmap(icon.pixmap(QSize(260, 390)));

    QStringList details;
    if (art.width > 0 && art.height > 0)
        details.append(QStringLiteral("%1×%2").arg(art.width).arg(art.height));
    if (!art.author.isEmpty())
        details.append(Vanta::tr("by %1").arg(art.author));
    if (art.upvotes > 0)
        details.append(Vanta::tr("%1 upvote(s)").arg(art.upvotes));
    m_details->setText(details.join(QStringLiteral(" · ")));
}

void CoverPickerDialog::applyThumbnail(const QString &gridId, const QByteArray &payload)
{
    if (gridId.isEmpty() || payload.isEmpty())
        return;
    QPixmap pixmap;
    if (!pixmap.loadFromData(payload))
        return;
    if (pixmap.width() > kTileWidth * 2) {
        pixmap = pixmap.scaled(kTileWidth * 2, kTileHeight * 2, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
    }
    pixmap.setDevicePixelRatio(2.0);

    for (int row = 0; row < m_artworks.size(); ++row) {
        if (m_artworks.at(row).gridId != gridId)
            continue;
        m_list->item(row)->setIcon(QIcon(pixmap));
        if (m_list->currentRow() == row)
            updateSelection();
        return;
    }
}

CoverArt CoverPickerDialog::selectedArt() const
{
    const int row = m_list ? m_list->currentRow() : -1;
    if (row < 0 || row >= m_artworks.size())
        return {};
    return m_artworks.at(row);
}

void CoverPickerDialog::refreshIcons()
{
    if (!m_theme)
        return;
    const ThemeColors colors = m_theme->colors();
    for (int row = 0; row < m_list->count() && row < m_artworks.size(); ++row) {
        // Only re-render tiles that still show the placeholder.
        const QPixmap current = m_list->item(row)->icon().pixmap(QSize(kTileWidth, kTileHeight));
        if (current.isNull())
            m_list->item(row)->setIcon(QIcon(placeholderTile(QSize(kTileWidth, kTileHeight), colors, row)));
    }
    updateSelection();
}
