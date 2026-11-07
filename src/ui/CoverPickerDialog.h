#pragma once

#include "Common.h"
#include "CoverFetcher.h"

#include <QList>
#include <QPixmap>
#include <QString>

class QLabel;
class QListWidget;
class QPushButton;
class ThemeManager;

// Lets the user pick which SteamGridDB artwork to use instead of silently taking
// the first result. Thumbnails stream in as they arrive so the dialog stays
// responsive on a slow connection.
class CoverPickerDialog : public VantaDialog
{
    Q_OBJECT

public:
    CoverPickerDialog(const QString &gameName, const QString &matchedName,
                      const QList<CoverArt> &artworks, ThemeManager *theme,
                      QWidget *parent = nullptr);

    // Null when the dialog was cancelled.
    CoverArt selectedArt() const;

    // Feeds a decoded thumbnail for the matching artwork.
    void applyThumbnail(const QString &gridId, const QByteArray &payload);

private:
    void refreshIcons();
    void updateSelection();

    ThemeManager *m_theme = nullptr;
    QList<CoverArt> m_artworks;
    QListWidget *m_list = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_details = nullptr;
    QLabel *m_emptyIcon = nullptr;
    QPushButton *m_useButton = nullptr;
};
