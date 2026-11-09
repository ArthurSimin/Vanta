#pragma once

#include "Common.h"
#include "UpdateChecker.h"

#include <QFrame>

class QLabel;
class QPushButton;
class QToolButton;
class ThemeManager;

// A small banner that names the newest release and offers to open its page.
// It never downloads or installs anything: packages belong to the distribution,
// so the most this can do is take the user to the release.
class UpdateBanner : public QFrame
{
    Q_OBJECT

public:
    explicit UpdateBanner(ThemeManager *theme, QWidget *parent = nullptr);

    void showRelease(const UpdateRelease &release, const QString &currentVersion);
    void hideBanner();
    bool isShowing() const;

signals:
    void openReleaseRequested(const QString &url);
    void dismissed();

private:
    void buildUi();
    void refreshIcons();

    ThemeManager *m_theme = nullptr;
    QLabel *m_glyph = nullptr;
    QLabel *m_headline = nullptr;
    QLabel *m_detail = nullptr;
    QPushButton *m_openButton = nullptr;
    QToolButton *m_dismissButton = nullptr;
    QString m_url;
};
