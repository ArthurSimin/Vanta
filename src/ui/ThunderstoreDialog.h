#pragma once

#include "BepInEx.h"
#include "Common.h"
#include "Game.h"
#include "ModInstaller.h"
#include "Thunderstore.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class ThemeManager;

// Browses the public package list of one Thunderstore community, shows package
// details, and installs the selected release together with its declared
// dependencies. Downloads are held in memory until every archive has been
// parsed, so a failed download cannot leave a half-installed package behind.
class ThunderstoreDialog : public VantaDialog
{
    Q_OBJECT

public:
    ThunderstoreDialog(const Game &game, const BepInExInstall &install,
                       ThemeManager *theme, QWidget *parent = nullptr);

signals:
    void installed();
    void communityResolved(const QString &community);

protected:
    void reject() override;

private:
    void buildUi();
    void refreshIcons();
    void startLoading(const QString &slug, bool automatic);
    void loadCommunity();
    void installFromLink();
    void handlePackages(const QString &community, const QList<ThunderstorePackage> &packages);
    void handlePackage(const ThunderstorePackage &package);
    void handleDownload(const QString &context, const QByteArray &payload);
    void handleFailure(const QString &context, const QString &message);
    void populateTable();
    void filterTable(const QString &text);
    void updateDetails();
    void updateVersionDetails();
    const ThunderstorePackage *selectedPackage() const;
    const ThunderstorePackage *packageForFullName(const QString &fullName) const;
    void installSelected();
    void beginInstall(const ThunderstorePackage &package, const ThunderstoreVersion &version);
    void startNextDownload();
    void finishInstall();
    void refreshInstallation();
    void cancelOperation();
    void setBusy(bool busy);
    void setStatus(const QString &message, bool error = false);
    void updateProgress(const QString &context, qint64 received, qint64 total);
    void clearInstallQueue();

    Game m_game;
    BepInExInstall m_install;
    ModTarget m_target;
    ThemeManager *m_theme = nullptr;
    Thunderstore *m_thunderstore = nullptr;

    QLineEdit *m_communityField = nullptr;
    QPushButton *m_loadButton = nullptr;
    QPushButton *m_autoButton = nullptr;
    QLineEdit *m_searchField = nullptr;
    QPushButton *m_linkButton = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_authorLabel = nullptr;
    QLabel *m_categoryLabel = nullptr;
    QLabel *m_descriptionLabel = nullptr;
    QLabel *m_dependencyLabel = nullptr;
    QComboBox *m_versionCombo = nullptr;
    QPushButton *m_installButton = nullptr;
    QPushButton *m_pageButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_closeButton = nullptr;

    QList<ThunderstorePackage> m_packages;
    QString m_community;
    QString m_requestedCommunity;
    QString m_detailsFullName;
    QStringList m_candidates;
    int m_candidateIndex = 0;
    bool m_autoLoading = false;
    bool m_loadingPackages = false;
    bool m_installing = false;
    bool m_busy = false;

    QString m_pendingLinkOwner;
    QString m_pendingLinkName;

    QList<ThunderstoreVersion> m_installQueue;
    QList<ModInstallPlan> m_plans;
    int m_installIndex = 0;
    bool m_installSkippedBepInEx = false;
};
