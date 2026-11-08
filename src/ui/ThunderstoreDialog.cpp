#include "ThunderstoreDialog.h"

#include "IconFactory.h"
#include "ThemeManager.h"

#include <QAbstractItemView>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace {
constexpr int kDownloadsRole = Qt::UserRole;
constexpr int kSearchRole = Qt::UserRole + 1;

class DownloadItem : public QTableWidgetItem
{
public:
    DownloadItem(const QString &text, qint64 count)
        : QTableWidgetItem(text)
    {
        setData(kDownloadsRole, count);
    }

    bool operator<(const QTableWidgetItem &other) const override
    {
        return data(kDownloadsRole).toLongLong() < other.data(kDownloadsRole).toLongLong();
    }
};

void repolish(QWidget *widget)
{
    if (!widget)
        return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

bool isBepInExPack(const ThunderstoreVersion &version)
{
    return version.name.compare(QStringLiteral("BepInExPack"), Qt::CaseInsensitive) == 0
        || version.fullName.startsWith(QStringLiteral("BepInEx-BepInExPack"), Qt::CaseInsensitive);
}

bool packageMatches(const ThunderstorePackage &package, const QString &owner, const QString &name)
{
    return package.owner.compare(owner, Qt::CaseInsensitive) == 0
        && package.name.compare(name, Qt::CaseInsensitive) == 0;
}

QString packageSearchText(const ThunderstorePackage &package)
{
    QString text = package.fullName + QLatin1Char(' ') + package.owner + QLatin1Char(' ')
        + package.name + QLatin1Char(' ') + package.categories.join(QLatin1Char(' '));
    if (!package.versions.isEmpty())
        text += QLatin1Char(' ') + package.versions.first().description;
    return text.toLower();
}
}

ThunderstoreDialog::ThunderstoreDialog(const Game &game, const BepInExInstall &install,
                                       ThemeManager *theme, QWidget *parent)
    : VantaDialog(QStringLiteral("Thunderstore"), theme, parent)
    , m_game(game)
    , m_install(install)
    , m_theme(theme)
{
    m_target = install.isValid() ? ModInstaller::targetFor(install)
                                 : ModInstaller::targetForGame(game.effectiveDirectory());
    setObjectName(QStringLiteral("thunderstoreDialog"));
    setMinimumSize(1000, 660);
    resize(1120, 760);

    m_thunderstore = new Thunderstore(this);
    buildUi();

    connect(m_thunderstore, &Thunderstore::packagesLoaded,
            this, &ThunderstoreDialog::handlePackages);
    connect(m_thunderstore, &Thunderstore::packageLoaded,
            this, &ThunderstoreDialog::handlePackage);
    connect(m_thunderstore, &Thunderstore::downloadProgress,
            this, &ThunderstoreDialog::updateProgress);
    connect(m_thunderstore, &Thunderstore::downloadFinished,
            this, &ThunderstoreDialog::handleDownload);
    connect(m_thunderstore, &Thunderstore::failed,
            this, &ThunderstoreDialog::handleFailure);

    refreshIcons();
    if (m_theme)
        connect(m_theme, &ThemeManager::themeChanged, this, &ThunderstoreDialog::refreshIcons);

    if (!m_target.isValid() || !QFileInfo::exists(m_game.effectiveDirectory())) {
        setStatus(QStringLiteral("The game folder is not available, so mods cannot be installed."), true);
        return;
    }

    const QString saved = Thunderstore::communitySlug(m_game.thunderstoreCommunity);
    if (!saved.isEmpty()) {
        startLoading(saved, false);
        return;
    }

    m_candidates = Thunderstore::communityCandidates(m_game.name);
    m_candidateIndex = 0;
    if (m_candidates.isEmpty()) {
        m_searchField->setEnabled(true);
        setStatus(QStringLiteral("Enter the Thunderstore community slug for this game and choose Load."), false);
        return;
    }
    startLoading(m_candidates.first(), true);
}

void ThunderstoreDialog::buildUi()
{
    bodyLayout()->addWidget(UiHelp::helpLabel(
        QStringLiteral("Browse the public package list of a Thunderstore community and install a release with "
                       "its declared dependencies. Vanta downloads packages only, never games. Files are backed "
                       "up before an existing file is replaced."),
        this));

    auto *communityRow = new QHBoxLayout;
    communityRow->setSpacing(8);
    communityRow->addWidget(new QLabel(QStringLiteral("Community"), this));
    m_communityField = new QLineEdit(this);
    m_communityField->setPlaceholderText(
        QStringLiteral("bopl-battle or https://thunderstore.io/c/bopl-battle/"));
    m_communityField->setClearButtonEnabled(true);
    connect(m_communityField, &QLineEdit::returnPressed, this, &ThunderstoreDialog::loadCommunity);
    communityRow->addWidget(m_communityField, 1);

    m_autoButton = new QPushButton(QStringLiteral("Detect"), this);
    m_autoButton->setCursor(Qt::PointingHandCursor);
    connect(m_autoButton, &QPushButton::clicked, this, [this] {
        m_candidates = Thunderstore::communityCandidates(m_game.name);
        m_candidateIndex = 0;
        m_autoLoading = false;
        if (m_candidates.isEmpty()) {
            setStatus(QStringLiteral("The game name does not suggest a community slug. Enter one manually."), true);
            return;
        }
        startLoading(m_candidates.first(), true);
    });
    communityRow->addWidget(m_autoButton);

    m_loadButton = new QPushButton(QStringLiteral("Load"), this);
    m_loadButton->setCursor(Qt::PointingHandCursor);
    connect(m_loadButton, &QPushButton::clicked, this, &ThunderstoreDialog::loadCommunity);
    communityRow->addWidget(m_loadButton);
    bodyLayout()->addLayout(communityRow);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(8);
    m_searchField = new QLineEdit(this);
    m_searchField->setPlaceholderText(QStringLiteral("Search packages by name, author, or description"));
    m_searchField->setClearButtonEnabled(true);
    connect(m_searchField, &QLineEdit::textChanged, this, &ThunderstoreDialog::filterTable);
    searchRow->addWidget(m_searchField, 1);

    m_linkButton = new QPushButton(QStringLiteral("Install from link…"), this);
    m_linkButton->setCursor(Qt::PointingHandCursor);
    connect(m_linkButton, &QPushButton::clicked, this, &ThunderstoreDialog::installFromLink);
    searchRow->addWidget(m_linkButton);
    bodyLayout()->addLayout(searchRow);

    m_summaryLabel = UiHelp::mutedLabel(QString(), this);
    bodyLayout()->addWidget(m_summaryLabel);

    auto *split = new QHBoxLayout;
    split->setSpacing(14);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("thunderstoreTable"));
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Package"), QStringLiteral("Author"),
                                        QStringLiteral("Version"), QStringLiteral("Downloads")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(30);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->setMinimumWidth(500);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ThunderstoreDialog::updateDetails);
    split->addWidget(m_table, 3);

    auto *detailPanel = new QFrame(this);
    detailPanel->setObjectName(QStringLiteral("infoPanel"));
    auto *detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(16, 14, 16, 14);
    detailLayout->setSpacing(9);

    m_nameLabel = new QLabel(detailPanel);
    m_nameLabel->setProperty("class", QStringLiteral("pageTitle"));
    m_nameLabel->setWordWrap(true);
    detailLayout->addWidget(m_nameLabel);

    m_authorLabel = UiHelp::mutedLabel(QString(), detailPanel);
    detailLayout->addWidget(m_authorLabel);

    m_versionCombo = new QComboBox(detailPanel);
    m_versionCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(m_versionCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { updateVersionDetails(); });
    detailLayout->addWidget(m_versionCombo);

    m_categoryLabel = UiHelp::mutedLabel(QString(), detailPanel);
    detailLayout->addWidget(m_categoryLabel);

    detailLayout->addWidget(UiHelp::sectionLabel(QStringLiteral("Description"), detailPanel));
    m_descriptionLabel = new QLabel(detailPanel);
    m_descriptionLabel->setWordWrap(true);
    m_descriptionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(m_descriptionLabel);

    detailLayout->addWidget(UiHelp::sectionLabel(QStringLiteral("Dependencies"), detailPanel));
    m_dependencyLabel = new QLabel(detailPanel);
    m_dependencyLabel->setWordWrap(true);
    m_dependencyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(m_dependencyLabel);
    m_compatibilityLabel = UiHelp::helpLabel(QString(), detailPanel);
    detailLayout->addWidget(m_compatibilityLabel);
    m_dependencyHint = UiHelp::mutedLabel(
        QStringLiteral("Listed dependencies are downloaded first; the BepInEx pack is skipped when "
                       "BepInEx is already installed."),
        detailPanel);
    m_dependencyHint->setWordWrap(true);
    detailLayout->addWidget(m_dependencyHint);
    detailLayout->addStretch(1);

    auto *detailScroll = new QScrollArea(this);
    detailScroll->setWidgetResizable(true);
    detailScroll->setFrameShape(QFrame::NoFrame);
    detailScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detailScroll->setWidget(detailPanel);

    // The install row sits outside the scroll area: long descriptions and
    // dependency lists must never push the primary action out of reach.
    auto *detailColumn = new QWidget(this);
    auto *detailColumnLayout = new QVBoxLayout(detailColumn);
    detailColumnLayout->setContentsMargins(0, 0, 0, 0);
    detailColumnLayout->setSpacing(9);
    detailColumnLayout->addWidget(detailScroll, 1);

    auto *detailButtons = new QHBoxLayout;
    detailButtons->setSpacing(8);
    m_installButton = new QPushButton(QStringLiteral("Install"), detailColumn);
    m_installButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makePrimary(m_installButton);
    connect(m_installButton, &QPushButton::clicked, this, &ThunderstoreDialog::installSelected);
    detailButtons->addWidget(m_installButton);

    m_pageButton = new QPushButton(QStringLiteral("Open page"), detailColumn);
    m_pageButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makeFlat(m_pageButton);
    connect(m_pageButton, &QPushButton::clicked, this, [this] {
        const ThunderstorePackage *package = selectedPackage();
        if (package && !package->packageUrl.isEmpty())
            QDesktopServices::openUrl(QUrl(package->packageUrl));
    });
    detailButtons->addWidget(m_pageButton);
    detailButtons->addStretch(1);
    detailColumnLayout->addLayout(detailButtons);

    detailColumn->setMinimumWidth(350);
    split->addWidget(detailColumn, 2);
    bodyLayout()->addLayout(split, 1);

    auto *statusRow = new QHBoxLayout;
    statusRow->setSpacing(10);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setProperty("class", QStringLiteral("muted"));
    m_statusLabel->setWordWrap(true);
    statusRow->addWidget(m_statusLabel, 1);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setFixedWidth(190);
    m_progress->setVisible(false);
    statusRow->addWidget(m_progress);

    m_cancelButton = new QPushButton(QStringLiteral("Cancel"), this);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setVisible(false);
    connect(m_cancelButton, &QPushButton::clicked, this, &ThunderstoreDialog::cancelOperation);
    statusRow->addWidget(m_cancelButton);
    bodyLayout()->addLayout(statusRow);

    auto *footer = new QHBoxLayout;
    footer->addStretch(1);
    m_closeButton = new QPushButton(QStringLiteral("Close"), this);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(m_closeButton);
    bodyLayout()->addLayout(footer);

    updateDetails();
}

void ThunderstoreDialog::refreshIcons()
{
    if (!m_theme)
        return;
    const ThemeColors &colors = m_theme->colors();
    Icons::setButtonIcon(m_installButton, Glyph::Download, QColor(Qt::white), 16);
    Icons::setButtonIcon(m_pageButton, Glyph::External, colors.text, 15);
    Icons::setButtonIcon(m_loadButton, Glyph::Restart, colors.text, 15);
    Icons::setButtonIcon(m_autoButton, Glyph::Search, colors.text, 15);
    Icons::setButtonIcon(m_linkButton, Glyph::External, colors.text, 15);
    Icons::setButtonIcon(m_cancelButton, Glyph::Close, colors.text, 15);
    Icons::setButtonIcon(m_closeButton, Glyph::Check, colors.text, 16);
}

void ThunderstoreDialog::reject()
{
    if (m_busy) {
        m_thunderstore->cancel();
        m_installing = false;
        m_loadingPackages = false;
        m_autoLoading = false;
        m_pendingLinkOwner.clear();
        m_pendingLinkName.clear();
        clearInstallQueue();
    }
    VantaDialog::reject();
}

void ThunderstoreDialog::startLoading(const QString &input, bool automatic)
{
    const QString slug = Thunderstore::communitySlug(input);
    if (slug.isEmpty()) {
        setStatus(QStringLiteral("Enter a community slug such as bopl-battle, or paste a Thunderstore community link."),
                  true);
        setBusy(false);
        return;
    }

    m_requestedCommunity = slug;
    m_community = slug;
    m_communityField->setText(slug);
    m_packages.clear();
    m_detailsFullName.clear();
    populateTable();

    m_thunderstore->setCommunity(slug);
    m_loadingPackages = true;
    m_autoLoading = automatic;
    setBusy(true);
    if (automatic && m_candidates.size() > 1) {
        setStatus(QStringLiteral("Looking for a Thunderstore community for “%1” (%2 of %3)…")
                      .arg(m_game.name)
                      .arg(m_candidateIndex + 1)
                      .arg(m_candidates.size()),
                  false);
    } else {
        setStatus(QStringLiteral("Loading packages from %1…").arg(slug), false);
    }
    m_thunderstore->loadPackages();
}

void ThunderstoreDialog::loadCommunity()
{
    m_candidates.clear();
    m_candidateIndex = 0;
    m_autoLoading = false;
    m_pendingLinkOwner.clear();
    m_pendingLinkName.clear();
    startLoading(m_communityField->text(), false);
}

void ThunderstoreDialog::installFromLink()
{
    if (m_busy) {
        setStatus(QStringLiteral("Wait for the current operation to finish first."), true);
        return;
    }

    const QString clipboard = QGuiApplication::clipboard()->text().trimmed();
    const QString input = QInputDialog::getText(
        this, QStringLiteral("Install from link"),
        QStringLiteral("Paste a Thunderstore package link:"),
        QLineEdit::Normal, clipboard).trimmed();
    if (input.isEmpty())
        return;

    QString community;
    QString owner;
    QString name;
    if (!Thunderstore::packageFromLink(input, &community, &owner, &name)) {
        setStatus(QStringLiteral("That is not a Thunderstore package link. Paste a link such as "
                                 "https://thunderstore.io/c/bopl-battle/p/OkakTeam/GrenadeSplitter/."),
                  true);
        return;
    }

    m_pendingLinkOwner = owner;
    m_pendingLinkName = name;

    if (!community.isEmpty() && community != m_community) {
        m_candidates.clear();
        m_candidateIndex = 0;
        startLoading(community, false);
        return;
    }

    const ThunderstorePackage *existing = nullptr;
    for (const ThunderstorePackage &package : std::as_const(m_packages)) {
        if (packageMatches(package, owner, name)) {
            existing = &package;
            break;
        }
    }
    if (existing) {
        m_pendingLinkOwner.clear();
        m_pendingLinkName.clear();
        beginInstall(*existing, existing->latest());
        return;
    }

    const QString fallbackCommunity = !community.isEmpty() ? community
                                                           : Thunderstore::communitySlug(m_communityField->text());
    if (m_packages.isEmpty()) {
        m_pendingLinkOwner.clear();
        m_pendingLinkName.clear();
        if (!fallbackCommunity.isEmpty()) {
            m_candidates.clear();
            m_candidateIndex = 0;
            startLoading(fallbackCommunity, false);
        } else {
            setStatus(QStringLiteral("Load the game's Thunderstore community first, or paste a full package "
                                     "link that includes /c/<community>/."),
                      true);
        }
        return;
    }

    m_loadingPackages = true;
    setBusy(true);
    setStatus(QStringLiteral("Looking up %1/%2 on Thunderstore…").arg(owner, name), false);
    m_thunderstore->loadPackage(owner, name);
}

void ThunderstoreDialog::handlePackages(const QString &community,
                                        const QList<ThunderstorePackage> &packages)
{
    if (community != m_requestedCommunity)
        return;

    m_loadingPackages = false;
    m_autoLoading = false;
    m_candidates.clear();
    m_candidateIndex = 0;
    m_community = community;
    m_communityField->setText(community);
    m_packages = packages;
    populateTable();
    setBusy(false);

    if (packages.isEmpty()) {
        setStatus(QStringLiteral("This community has no packages Vanta can read."), true);
    } else {
        setStatus(QStringLiteral("Loaded %1 package%2 from %3.")
                      .arg(packages.size())
                      .arg(packages.size() == 1 ? QString() : QStringLiteral("s"), community),
                  false);
    }
    emit communityResolved(community);

    if (!m_pendingLinkOwner.isEmpty()) {
        const QString owner = m_pendingLinkOwner;
        const QString name = m_pendingLinkName;
        for (const ThunderstorePackage &package : std::as_const(m_packages)) {
            if (packageMatches(package, owner, name)) {
                m_pendingLinkOwner.clear();
                m_pendingLinkName.clear();
                beginInstall(package, package.latest());
                return;
            }
        }
        m_loadingPackages = true;
        setBusy(true);
        setStatus(QStringLiteral("Looking up %1/%2 on Thunderstore…").arg(owner, name), false);
        m_thunderstore->loadPackage(owner, name);
    }
}

void ThunderstoreDialog::handlePackage(const ThunderstorePackage &package)
{
    if (m_pendingLinkOwner.isEmpty() || !packageMatches(package, m_pendingLinkOwner, m_pendingLinkName)) {
        m_loadingPackages = false;
        setBusy(false);
        return;
    }

    m_pendingLinkOwner.clear();
    m_pendingLinkName.clear();
    bool added = false;
    if (!packageForFullName(package.fullName)) {
        m_packages.append(package);
        added = true;
    }
    m_loadingPackages = false;
    setBusy(false);
    if (added)
        populateTable();
    setStatus(QStringLiteral("Loaded %1 %2.").arg(package.name, package.latest().versionNumber), false);
    beginInstall(package, package.latest());
}

void ThunderstoreDialog::handleDownload(const QString &context, const QByteArray &payload)
{
    if (!m_installing)
        return;
    if (context != QStringLiteral("download:%1").arg(m_installIndex))
        return;
    if (m_installIndex < 0 || m_installIndex >= m_installQueue.size())
        return;

    const ThunderstoreVersion version = m_installQueue.at(m_installIndex);
    const QString archiveName = (version.fullName.isEmpty() ? version.name : version.fullName)
        + QStringLiteral(".zip");
    ModInstallPlan plan;
    QString error;
    if (!ModInstaller::planFromArchive(payload, archiveName, version.name, m_target, &plan, &error)) {
        m_installing = false;
        setBusy(false);
        clearInstallQueue();
        setStatus(QStringLiteral("%1 No files were written.").arg(error), true);
        return;
    }

    m_plans.append(plan);
    ++m_installIndex;
    startNextDownload();
}

void ThunderstoreDialog::handleFailure(const QString &context, const QString &message)
{
    if (context == QStringLiteral("packages")) {
        if (m_autoLoading && m_candidateIndex + 1 < m_candidates.size()) {
            ++m_candidateIndex;
            startLoading(m_candidates.at(m_candidateIndex), true);
            return;
        }
        m_autoLoading = false;
        m_loadingPackages = false;
        m_candidates.clear();
        m_packages.clear();
        m_detailsFullName.clear();
        populateTable();
        setBusy(false);
        setStatus(message + QLatin1Char(' ') + QStringLiteral(
            "Enter the community slug or paste a Thunderstore community link, then choose Load."), true);
        m_pendingLinkOwner.clear();
        m_pendingLinkName.clear();
        return;
    }

    if (context == QStringLiteral("package")) {
        m_loadingPackages = false;
        m_pendingLinkOwner.clear();
        m_pendingLinkName.clear();
        setBusy(false);
        setStatus(message, true);
        return;
    }

    if (context.startsWith(QStringLiteral("download"))) {
        if (!m_installing)
            return;
        m_installing = false;
        setBusy(false);
        clearInstallQueue();
        setStatus(message + QLatin1Char(' ') + QStringLiteral("No files were written."), true);
    }
}

void ThunderstoreDialog::populateTable()
{
    const ThunderstorePackage *previous = selectedPackage();
    const QString previousName = previous ? previous->fullName : QString();

    m_table->blockSignals(true);
    m_table->setSortingEnabled(false);
    m_table->clearContents();
    m_table->setRowCount(m_packages.size());

    const QLocale locale;
    for (int row = 0; row < m_packages.size(); ++row) {
        const ThunderstorePackage &package = m_packages.at(row);
        const ThunderstoreVersion latest = package.latest();

        auto *nameItem = new QTableWidgetItem(
            package.deprecated ? QStringLiteral("%1 (deprecated)").arg(package.name) : package.name);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        nameItem->setData(Qt::UserRole, package.fullName);
        nameItem->setData(kSearchRole, packageSearchText(package));
        nameItem->setToolTip(package.fullName);
        m_table->setItem(row, 0, nameItem);

        auto *authorItem = new QTableWidgetItem(package.owner);
        authorItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, 1, authorItem);

        auto *versionItem = new QTableWidgetItem(latest.versionNumber);
        versionItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, 2, versionItem);

        auto *downloadItem = new DownloadItem(locale.toString(latest.downloads), latest.downloads);
        downloadItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        downloadItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, 3, downloadItem);
    }

    m_table->setSortingEnabled(true);
    m_table->sortItems(3, Qt::DescendingOrder);
    m_table->blockSignals(false);

    filterTable(m_searchField->text());

    int targetRow = -1;
    if (!previousName.isEmpty()) {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            QTableWidgetItem *item = m_table->item(row, 0);
            if (item && item->data(Qt::UserRole).toString() == previousName
                && !m_table->isRowHidden(row)) {
                targetRow = row;
                break;
            }
        }
    }
    if (targetRow < 0) {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (!m_table->isRowHidden(row)) {
                targetRow = row;
                break;
            }
        }
    }
    if (targetRow >= 0)
        m_table->selectRow(targetRow);
    else
        updateDetails();
}

void ThunderstoreDialog::filterTable(const QString &text)
{
    const QString needle = text.trimmed().toLower();
    int visible = 0;
    int firstVisible = -1;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *item = m_table->item(row, 0);
        const bool match = needle.isEmpty()
            || (item && item->data(kSearchRole).toString().contains(needle));
        m_table->setRowHidden(row, !match);
        if (match) {
            ++visible;
            if (firstVisible < 0)
                firstVisible = row;
        }
    }

    if (needle.isEmpty()) {
        m_summaryLabel->setText(m_packages.isEmpty() ? QString()
            : QStringLiteral("%1 package%2 in %3.")
                  .arg(m_packages.size())
                  .arg(m_packages.size() == 1 ? QString() : QStringLiteral("s"), m_community));
    } else {
        m_summaryLabel->setText(QStringLiteral("%1 of %2 packages match “%3”.")
                                    .arg(visible)
                                    .arg(m_packages.size())
                                    .arg(text.trimmed()));
    }

    const int current = m_table->currentRow();
    if (current < 0 || m_table->isRowHidden(current)) {
        if (firstVisible >= 0)
            m_table->selectRow(firstVisible);
        else
            updateDetails();
    }
}

const ThunderstorePackage *ThunderstoreDialog::selectedPackage() const
{
    const int row = m_table ? m_table->currentRow() : -1;
    if (row < 0 || m_table->isRowHidden(row))
        return nullptr;
    QTableWidgetItem *item = m_table->item(row, 0);
    if (!item)
        return nullptr;
    return packageForFullName(item->data(Qt::UserRole).toString());
}

const ThunderstorePackage *ThunderstoreDialog::packageForFullName(const QString &fullName) const
{
    for (const ThunderstorePackage &package : m_packages) {
        if (package.fullName.compare(fullName, Qt::CaseInsensitive) == 0)
            return &package;
    }
    return nullptr;
}

void ThunderstoreDialog::updateDetails()
{
    const ThunderstorePackage *package = selectedPackage();
    if (!package) {
        m_detailsFullName.clear();
        m_nameLabel->setText(QStringLiteral("No package selected"));
        m_nameLabel->setToolTip(QString());
        m_authorLabel->clear();
        m_categoryLabel->clear();
        m_descriptionLabel->clear();
        m_dependencyLabel->clear();
        m_versionCombo->blockSignals(true);
        m_versionCombo->clear();
        m_versionCombo->blockSignals(false);
        m_installButton->setEnabled(false);
        m_pageButton->setEnabled(false);
        return;
    }

    m_pageButton->setEnabled(!package->packageUrl.isEmpty());
    if (m_detailsFullName == package->fullName) {
        updateVersionDetails();
        return;
    }

    m_detailsFullName = package->fullName;
    m_nameLabel->setText(package->deprecated
                             ? QStringLiteral("%1 (deprecated)").arg(package->name)
                             : package->name);
    m_nameLabel->setToolTip(package->fullName);
    m_categoryLabel->setText(package->categories.isEmpty()
                                 ? QStringLiteral("Uncategorized")
                                 : package->categories.join(QStringLiteral(", ")));

    m_versionCombo->blockSignals(true);
    m_versionCombo->clear();
    for (int index = 0; index < package->versions.size(); ++index) {
        const ThunderstoreVersion &version = package->versions.at(index);
        QString label = version.versionNumber;
        if (version.dateCreated.isValid())
            label += QStringLiteral(" · ") + version.dateCreated.date().toString(QStringLiteral("yyyy-MM-dd"));
        m_versionCombo->addItem(label, index);
    }
    m_versionCombo->setCurrentIndex(0);
    m_versionCombo->blockSignals(false);
    updateVersionDetails();
}

void ThunderstoreDialog::updateVersionDetails()
{
    const ThunderstorePackage *package = selectedPackage();
    if (!package) {
        updateDetails();
        return;
    }
    const int index = m_versionCombo->currentData().toInt();
    if (index < 0 || index >= package->versions.size())
        return;

    const ThunderstoreVersion &version = package->versions.at(index);
    const QLocale locale;
    m_authorLabel->setText(QStringLiteral("by %1 · %2 downloads")
                               .arg(package->owner, locale.toString(version.downloads)));
    m_descriptionLabel->setText(version.description.isEmpty()
                                    ? QStringLiteral("This release has no description.")
                                    : version.description);
    if (version.dependencies.isEmpty()) {
        m_dependencyLabel->setText(QStringLiteral("No dependencies are declared."));
    } else {
        QStringList lines;
        for (const QString &dependency : version.dependencies)
            lines.append(QStringLiteral("• ") + dependency);
        m_dependencyLabel->setText(lines.join(QLatin1Char('\n')));
    }
    m_compatibilityLabel->setText(
        QStringLiteral("Vanta cannot tell which game version a Thunderstore release was built for. "
                       "Check the mod page when the game has received an update."));
    m_installButton->setText(QStringLiteral("Install %1").arg(version.versionNumber));
    m_installButton->setEnabled(!m_busy);
    m_versionCombo->setEnabled(!m_busy);
}

void ThunderstoreDialog::installSelected()
{
    if (m_busy)
        return;
    const ThunderstorePackage *package = selectedPackage();
    if (!package) {
        setStatus(QStringLiteral("Select a package first."), true);
        return;
    }
    const int index = m_versionCombo->currentData().toInt();
    if (index < 0 || index >= package->versions.size()) {
        setStatus(QStringLiteral("Select a release first."), true);
        return;
    }
    beginInstall(*package, package->versions.at(index));
}

void ThunderstoreDialog::beginInstall(const ThunderstorePackage &package,
                                      const ThunderstoreVersion &version)
{
    if (m_busy || m_installing)
        return;
    if (!m_target.isValid() || !QFileInfo(m_game.effectiveDirectory()).isDir()) {
        setStatus(QStringLiteral("The game folder is not available, so the package cannot be installed."), true);
        return;
    }
    if (!version.isValid() || version.downloadUrl.trimmed().isEmpty()) {
        setStatus(QStringLiteral("This release has no download address."), true);
        return;
    }
    if (package.name.compare(QStringLiteral("r2modman"), Qt::CaseInsensitive) == 0) {
        setStatus(QStringLiteral("r2modman is a mod manager application, not a BepInEx plugin, "
                                 "so Vanta does not install it into a game."), true);
        return;
    }

    QStringList missing;
    QStringList notes;
    const QList<ThunderstoreVersion> dependencies =
        Thunderstore::resolveDependencies(package, m_packages, &missing, &notes);
    // An already installed BepInEx satisfies its own dependency even if the
    // community happens to omit the pack from its package list.
    QStringList required;
    for (const QString &reference : missing) {
        const ThunderstoreDependency dependency = Thunderstore::parseDependency(reference);
        if (m_install.isValid()
            && dependency.name.compare(QStringLiteral("BepInExPack"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        required.append(reference);
    }
    if (!required.isEmpty()) {
        setStatus(QStringLiteral("This package needs %1, which is not listed in this community. "
                                 "Install it separately or choose another package.")
                      .arg(required.join(QStringLiteral(", "))),
                  true);
        return;
    }

    // The pack is a loader, not a plugin: its files belong at the game root and
    // replace the core DLLs of an existing installation. That is how BepInEx is
    // updated, but it must never happen as a side effect of "install this mod",
    // so an explicit request for the pack is confirmed on its own.
    if (isBepInExPack(version)) {
        if (!m_install.isValid()) {
            setStatus(QStringLiteral("Installing %1 %2…").arg(package.name, version.versionNumber), false);
        } else {
            const auto reply = QMessageBox::question(
                this, QStringLiteral("Replace the BepInEx loader?"),
                QStringLiteral("BepInEx is already installed.\n\n"
                               "%1 %2 copies its own core DLLs, winhttp proxy and doorstop files over the "
                               "installed loader. Plugins and their config are left alone, and every replaced "
                               "file is backed up under BepInEx/.vanta-backups first.\n\n"
                               "Continue only if you want to change the loader version.")
                    .arg(package.name, version.versionNumber),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply != QMessageBox::Yes) {
                setStatus(QStringLiteral("Nothing was changed. The installed BepInEx loader was kept."), false);
                return;
            }
        }
    }

    clearInstallQueue();
    for (const ThunderstoreVersion &dependency : dependencies) {
        if (m_install.isValid() && isBepInExPack(dependency)) {
            m_installSkippedBepInEx = true;
            continue;
        }
        m_installQueue.append(dependency);
    }
    m_installQueue.append(version);
    m_dependencyNotes = notes;
    m_installIndex = 0;
    m_installing = true;
    setBusy(true);
    setStatus(QStringLiteral("Installing %1 %2…").arg(package.name, version.versionNumber), false);
    startNextDownload();
}

void ThunderstoreDialog::startNextDownload()
{
    if (!m_installing)
        return;
    if (m_installIndex >= m_installQueue.size()) {
        finishInstall();
        return;
    }

    const ThunderstoreVersion version = m_installQueue.at(m_installIndex);
    m_progress->setRange(0, 0);
    setStatus(QStringLiteral("Downloading %1 of %2: %3 %4…")
                  .arg(m_installIndex + 1)
                  .arg(m_installQueue.size())
                  .arg(version.name, version.versionNumber),
              false);
    m_thunderstore->download(version, QStringLiteral("download:%1").arg(m_installIndex));
}

void ThunderstoreDialog::finishInstall()
{
    if (m_plans.isEmpty()) {
        m_installing = false;
        setBusy(false);
        clearInstallQueue();
        setStatus(QStringLiteral("The package produced no installable files. No files were written."), true);
        return;
    }

    ModInstallPlan merged;
    QString error;
    if (!ModInstaller::mergePlans(m_plans, m_target, &merged, &error)) {
        m_installing = false;
        setBusy(false);
        clearInstallQueue();
        setStatus(QStringLiteral("%1 No files were written.").arg(error), true);
        return;
    }

    m_installing = false;
    setBusy(false);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);

    bool overwrite = false;
    if (!merged.conflicts.isEmpty()) {
        QStringList sample = merged.conflicts.mid(0, 8);
        QString detail = sample.join(QLatin1Char('\n'));
        if (merged.conflicts.size() > sample.size())
            detail += QStringLiteral("\nand %1 more").arg(merged.conflicts.size() - sample.size());
        const auto reply = QMessageBox::question(
            this, QStringLiteral("Replace existing files?"),
            QStringLiteral("%1 file(s) already exist with different contents:\n\n%2\n\n"
                           "Vanta copies each replaced file under BepInEx/.vanta-backups before writing. "
                           "Nothing is deleted. Continue?")
                .arg(merged.conflicts.size())
                .arg(detail),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            clearInstallQueue();
            setStatus(QStringLiteral("Installation cancelled. No files were written."), false);
            return;
        }
        overwrite = true;
    }

    const bool skippedBepInEx = m_installSkippedBepInEx;
    const int packageCount = m_installQueue.size();
    const QString rootName = m_installQueue.isEmpty() || m_installQueue.last().name.isEmpty()
        ? merged.displayName
        : m_installQueue.last().name;
    QStringList written;
    QString backupRoot;
    if (!ModInstaller::execute(merged, m_target, overwrite, &written, &backupRoot, &error)) {
        refreshInstallation();
        QString message = error;
        if (!written.isEmpty())
            message += QStringLiteral(" %1 file(s) were written before the failure.").arg(written.size());
        clearInstallQueue();
        setStatus(message, true);
        if (!written.isEmpty())
            emit installed();
        return;
    }

    refreshInstallation();
    QString message;
    if (written.isEmpty()) {
        message = QStringLiteral("%1 is already installed and every file matches. Nothing changed.")
                      .arg(rootName);
    } else if (packageCount > 1) {
        message = QStringLiteral("Installed %1 file(s) for %2 and %3 dependenc%4.")
                      .arg(written.size())
                      .arg(rootName)
                      .arg(packageCount - 1)
                      .arg(packageCount - 1 == 1 ? QStringLiteral("y") : QStringLiteral("ies"));
    } else {
        message = QStringLiteral("Installed %1 file(s) for %2.").arg(written.size()).arg(rootName);
    }
    if (skippedBepInEx && !written.isEmpty())
        message += QStringLiteral(" BepInEx was already present, so its pack was not reinstalled.");
    if (!m_install.isValid())
        message += QStringLiteral(" BepInEx is still not installed, so these files will not load until it is.");
    if (!m_dependencyNotes.isEmpty()) {
        message += QLatin1Char(' ');
        message += m_dependencyNotes.join(QLatin1Char(' '));
    }
    if (!backupRoot.isEmpty())
        message += QStringLiteral(" Replaced files were backed up under %1.")
                       .arg(QDir::toNativeSeparators(backupRoot));
    clearInstallQueue();
    setStatus(message, false);
    emit installed();
}

void ThunderstoreDialog::refreshInstallation()
{
    m_install = BepInEx::detect(m_game.effectiveDirectory(), m_game.executable);
    m_target = m_install.isValid() ? ModInstaller::targetFor(m_install)
                                   : ModInstaller::targetForGame(m_game.effectiveDirectory());
}

void ThunderstoreDialog::cancelOperation()
{
    if (!m_busy)
        return;
    m_thunderstore->cancel();
    m_installing = false;
    m_loadingPackages = false;
    m_autoLoading = false;
    m_pendingLinkOwner.clear();
    m_pendingLinkName.clear();
    clearInstallQueue();
    setBusy(false);
    setStatus(QStringLiteral("The operation was cancelled. No files were written."), false);
}

void ThunderstoreDialog::clearInstallQueue()
{
    m_installQueue.clear();
    m_plans.clear();
    m_dependencyNotes.clear();
    m_installIndex = 0;
    m_installSkippedBepInEx = false;
}

void ThunderstoreDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_communityField->setEnabled(!busy);
    m_loadButton->setEnabled(!busy);
    m_autoButton->setEnabled(!busy);
    m_searchField->setEnabled(!busy);
    m_linkButton->setEnabled(!busy);
    m_table->setEnabled(!busy);
    m_versionCombo->setEnabled(!busy);
    m_installButton->setEnabled(!busy && selectedPackage() != nullptr);
    m_cancelButton->setVisible(busy);
    m_progress->setVisible(busy);
    if (!busy) {
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
        updateDetails();
    }
}

void ThunderstoreDialog::setStatus(const QString &message, bool error)
{
    m_statusLabel->setText(message);
    m_statusLabel->setProperty("class", error ? QStringLiteral("warning") : QStringLiteral("muted"));
    repolish(m_statusLabel);
    m_statusLabel->setVisible(!message.isEmpty());
}

void ThunderstoreDialog::updateProgress(const QString &context, qint64 received, qint64 total)
{
    if (!m_installing || context != QStringLiteral("download:%1").arg(m_installIndex))
        return;
    if (total > 0) {
        if (m_progress->maximum() != 100)
            m_progress->setRange(0, 100);
        const qint64 percent = received * 100 / total;
        m_progress->setValue(static_cast<int>(qBound<qint64>(qint64(0), percent, qint64(100))));
    } else if (m_progress->maximum() != 0) {
        m_progress->setRange(0, 0);
    }
}
