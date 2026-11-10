#include "ModManagerDialog.h"

#include "I18n.h"
#include "IconFactory.h"
#include "ModInstaller.h"
#include "ThemeManager.h"
#include "ThunderstoreDialog.h"

#include <QAbstractItemView>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {
QLabel *chipLabel(const QString &text, QWidget *parent, bool accent = false)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("chip", true);
    if (accent)
        label->setProperty("accent", true);
    label->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return label;
}

void repolish(QWidget *widget)
{
    if (!widget)
        return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QString warningText(const QStringList &warnings)
{
    QStringList lines;
    for (const QString &warning : warnings) {
        if (!warning.trimmed().isEmpty())
            lines.append(warning.trimmed());
    }
    return lines.join(QLatin1Char('\n'));
}
}

ModManagerDialog::ModManagerDialog(const Game &game, ThemeManager *theme, QWidget *parent)
    : VantaDialog(Vanta::tr("Mods"), theme, parent)
    , m_game(game)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("modManagerDialog"));
    setMinimumSize(900, 640);
    resize(960, 720);
    buildUi();
    refresh();
    refreshIcons();
    if (m_theme)
        connect(m_theme, &ThemeManager::themeChanged, this, &ModManagerDialog::refreshIcons);
}

void ModManagerDialog::buildUi()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(2, 2, 8, 2);
    layout->setSpacing(14);

    auto *title = new QLabel(m_game.name.isEmpty() ? Vanta::tr("Mods") : m_game.name, content);
    title->setProperty("class", QStringLiteral("pageTitle"));
    title->setWordWrap(true);
    layout->addWidget(title);

    layout->addWidget(UiHelp::helpLabel(
        Vanta::tr("Vanta lists the plugin and patcher DLLs inside this game's BepInEx folder. "
                       "Enabling or disabling a mod only renames its file between .dll and .dll.disabled; "
                       "the DLL contents are never changed or deleted."),
        content));

    auto *chips = new QHBoxLayout;
    chips->setSpacing(6);
    m_generationChip = chipLabel(Vanta::tr("Detecting…"), content, true);
    m_enabledChip = chipLabel(Vanta::tr("0 enabled"), content);
    m_disabledChip = chipLabel(Vanta::tr("0 disabled"), content);
    m_doorstopChip = chipLabel(Vanta::tr("Doorstop"), content);
    chips->addWidget(m_generationChip);
    chips->addWidget(m_enabledChip);
    chips->addWidget(m_disabledChip);
    chips->addWidget(m_doorstopChip);
    chips->addStretch(1);
    layout->addLayout(chips);

    auto *infoPanel = new QFrame(content);
    infoPanel->setObjectName(QStringLiteral("infoPanel"));
    auto *infoLayout = new QVBoxLayout(infoPanel);
    infoLayout->setContentsMargins(16, 14, 16, 14);
    infoLayout->setSpacing(10);
    infoLayout->addWidget(UiHelp::sectionLabel(Vanta::tr("BepInEx installation"), infoPanel));
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(8);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_rootField = new QLineEdit(infoPanel);
    m_rootField->setReadOnly(true);
    m_rootField->setCursorPosition(0);
    form->addRow(Vanta::tr("Folder"), m_rootField);
    m_doorstopField = new QLineEdit(infoPanel);
    m_doorstopField->setReadOnly(true);
    m_doorstopField->setCursorPosition(0);
    form->addRow(Vanta::tr("Doorstop"), m_doorstopField);
    infoLayout->addLayout(form);
    layout->addWidget(infoPanel);

    m_warningLabel = new QLabel(content);
    m_warningLabel->setProperty("class", QStringLiteral("warning"));
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setVisible(false);
    layout->addWidget(m_warningLabel);

    m_feedbackLabel = new QLabel(content);
    m_feedbackLabel->setWordWrap(true);
    m_feedbackLabel->setVisible(false);
    layout->addWidget(m_feedbackLabel);

    m_table = new QTableWidget(content);
    m_table->setObjectName(QStringLiteral("modTable"));
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({Vanta::tr("Mod"), Vanta::tr("Kind"),
                                        Vanta::tr("State"), Vanta::tr("File")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setShowGrid(false);
    m_table->setSortingEnabled(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(32);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->setMinimumHeight(280);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_populating || !item || item->column() != 0)
            return;
        const QString path = item->data(Qt::UserRole).toString();
        if (path.isEmpty())
            return;
        toggleMod(path, item->checkState() == Qt::Checked);
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            &ModManagerDialog::updateSelectionActions);
    // The item check box is mouse-only by default. Space on a selected row is
    // handled through an event filter because the table consumes the key itself.
    m_table->installEventFilter(this);
    layout->addWidget(m_table, 1);

    m_emptyLabel = UiHelp::helpLabel(QString(), content);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(m_emptyLabel, 1);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    bodyLayout()->addWidget(scroll, 1);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    m_addButton = new QPushButton(Vanta::tr("Add mod file…"), this);
    m_addButton->setCursor(Qt::PointingHandCursor);
    UiHelp::makePrimary(m_addButton);
    m_addButton->setAutoDefault(false);
    connect(m_addButton, &QPushButton::clicked, this, &ModManagerDialog::addLocalMod);
    footer->addWidget(m_addButton);

    m_toggleButton = new QPushButton(Vanta::tr("Toggle"), this);
    m_toggleButton->setCursor(Qt::PointingHandCursor);
    m_toggleButton->setAutoDefault(false);
    m_toggleButton->setEnabled(false);
    connect(m_toggleButton, &QPushButton::clicked, this, &ModManagerDialog::toggleSelected);
    footer->addWidget(m_toggleButton);

    m_thunderstoreButton = new QPushButton(Vanta::tr("Browse Thunderstore…"), this);
    m_thunderstoreButton->setCursor(Qt::PointingHandCursor);
    m_thunderstoreButton->setAutoDefault(false);
    connect(m_thunderstoreButton, &QPushButton::clicked, this, &ModManagerDialog::openThunderstore);
    footer->addWidget(m_thunderstoreButton);

    m_openFolderButton = new QPushButton(Vanta::tr("Open folder"), this);
    m_openFolderButton->setCursor(Qt::PointingHandCursor);
    m_openFolderButton->setAutoDefault(false);
    connect(m_openFolderButton, &QPushButton::clicked, this, &ModManagerDialog::openModFolder);
    footer->addWidget(m_openFolderButton);

    m_refreshButton = new QPushButton(Vanta::tr("Refresh"), this);
    m_refreshButton->setCursor(Qt::PointingHandCursor);
    m_refreshButton->setAutoDefault(false);
    connect(m_refreshButton, &QPushButton::clicked, this, &ModManagerDialog::refresh);
    footer->addWidget(m_refreshButton);
    footer->addStretch(1);

    m_closeButton = new QPushButton(Vanta::tr("Close"), this);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    // The mod list clears its selection after every refresh, so a stale Enter
    // could only ever fall through to Add mod file. Enter closes the dialog.
    m_closeButton->setDefault(true);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(m_closeButton);
    bodyLayout()->addLayout(footer);
}

void ModManagerDialog::refresh()
{
    m_install = BepInEx::detect(m_game.effectiveDirectory(), m_game.executable);
    updateDetection();
    updateTable();
}

void ModManagerDialog::refreshIcons()
{
    if (!m_theme)
        return;
    const ThemeColors &colors = m_theme->colors();
    Icons::setButtonIcon(m_addButton, Glyph::Plus, QColor(Qt::white), 16);
    Icons::setButtonIcon(m_toggleButton, Glyph::Check, colors.text, 16);
    Icons::setButtonIcon(m_thunderstoreButton, Glyph::Download, colors.text, 16);
    Icons::setButtonIcon(m_openFolderButton, Glyph::Folder, colors.text, 16);
    Icons::setButtonIcon(m_refreshButton, Glyph::Restart, colors.text, 16);
    Icons::setButtonIcon(m_closeButton, Glyph::Check, colors.text, 16);
}

void ModManagerDialog::updateDetection()
{
    const bool detected = m_install.isValid();
    m_generationChip->setText(detected ? m_install.generation : Vanta::tr("BepInEx not detected"));
    m_generationChip->setProperty("accent", detected);
    repolish(m_generationChip);

    m_enabledChip->setVisible(detected);
    m_disabledChip->setVisible(detected);
    m_doorstopChip->setVisible(detected);
    m_enabledChip->setText(Vanta::tr("%1 enabled").arg(m_install.enabledModCount()));
    m_disabledChip->setText(Vanta::tr("%1 disabled").arg(m_install.disabledModCount()));

    if (detected) {
        const bool doorstopReady = !m_install.doorstopMarkers.isEmpty();
        m_doorstopChip->setText(doorstopReady ? Vanta::tr("Doorstop ready")
                                              : Vanta::tr("Doorstop missing"));
        m_rootField->setText(QDir::toNativeSeparators(m_install.root));
        m_rootField->setToolTip(m_rootField->text());
        m_doorstopField->setText(m_install.doorstopMarkers.isEmpty()
                                     ? Vanta::tr("No doorstop files were found in the game folder.")
                                     : m_install.doorstopMarkers.join(QStringLiteral(", ")));
        m_doorstopField->setToolTip(m_doorstopField->text());
        const QString warnings = warningText(m_install.warnings);
        m_warningLabel->setText(warnings);
        m_warningLabel->setVisible(!warnings.isEmpty());
        m_thunderstoreButton->setText(Vanta::tr("Browse Thunderstore…"));
    } else {
        m_rootField->setText(Vanta::tr("Not detected"));
        m_rootField->setToolTip(QString());
        m_doorstopField->clear();
        m_doorstopField->setToolTip(QString());
        m_warningLabel->setText(
            Vanta::tr("Vanta did not find a BepInEx folder for this game. You can still import a local "
                           "package or install the BepInEx pack from Thunderstore; Vanta will create the "
                           "conventional BepInEx folder inside the game directory."));
        m_warningLabel->setVisible(true);
        m_thunderstoreButton->setText(Vanta::tr("Get BepInEx and mods…"));
    }

    if (!detected) {
        m_emptyLabel->setText(
            Vanta::tr("No BepInEx installation was detected. A Wine or Proton game usually needs the "
                           "BepInEx pack before plugin DLLs can load, and a native game needs the doorstop "
                           "launcher script. Use “Get BepInEx and mods…” or import a package that "
                           "contains BepInEx."));
    } else if (m_install.mods.isEmpty()) {
        m_emptyLabel->setText(
            Vanta::tr("The BepInEx installation contains no plugin or patcher DLLs yet. Use “Add mod file…” "
                           "to import a .dll or .zip package, or browse Thunderstore."));
    } else {
        m_emptyLabel->clear();
    }
    m_emptyLabel->setVisible(m_install.mods.isEmpty());
}

void ModManagerDialog::updateTable()
{
    m_populating = true;
    m_table->clearContents();
    m_table->setRowCount(m_install.mods.size());
    for (int row = 0; row < m_install.mods.size(); ++row) {
        const BepInExMod &mod = m_install.mods.at(row);
        auto *nameItem = new QTableWidgetItem(mod.name);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        nameItem->setCheckState(mod.enabled ? Qt::Checked : Qt::Unchecked);
        nameItem->setData(Qt::UserRole, mod.path);
        nameItem->setToolTip(QDir::toNativeSeparators(mod.path));
        m_table->setItem(row, 0, nameItem);

        auto *kindItem = new QTableWidgetItem(mod.patcher ? Vanta::tr("Patcher") : Vanta::tr("Plugin"));
        kindItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, 1, kindItem);

        auto *stateItem = new QTableWidgetItem(mod.enabled ? Vanta::tr("Enabled")
                                                           : Vanta::tr("Disabled"));
        stateItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, 2, stateItem);

        auto *pathItem = new QTableWidgetItem(mod.relativePath);
        pathItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        pathItem->setToolTip(QDir::toNativeSeparators(mod.path));
        m_table->setItem(row, 3, pathItem);
    }
    m_populating = false;
    m_table->setVisible(!m_install.mods.isEmpty());
    updateSelectionActions();
}

bool ModManagerDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_table && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Space || keyEvent->key() == Qt::Key_Return
            || keyEvent->key() == Qt::Key_Enter) {
            if (m_table->currentRow() >= 0 && m_table->currentRow() < m_install.mods.size()) {
                toggleSelected();
                return true;
            }
        }
    }
    return VantaDialog::eventFilter(watched, event);
}

void ModManagerDialog::updateSelectionActions()
{
    const int row = m_table->currentRow();
    const BepInExMod *mod = nullptr;
    if (row >= 0 && row < m_install.mods.size())
        mod = &m_install.mods.at(row);
    m_toggleButton->setEnabled(mod != nullptr);
    if (!mod) {
        m_toggleButton->setText(Vanta::tr("Toggle"));
        return;
    }
    m_toggleButton->setText(mod->enabled ? Vanta::tr("Disable") : Vanta::tr("Enable"));
    m_toggleButton->setToolTip(Vanta::tr("%1 %2")
                                   .arg(mod->enabled ? Vanta::tr("Disable") : Vanta::tr("Enable"),
                                        mod->name));
}

void ModManagerDialog::toggleSelected()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_install.mods.size())
        return;
    const BepInExMod &mod = m_install.mods.at(row);
    toggleMod(mod.path, !mod.enabled);
}

void ModManagerDialog::setFeedback(const QString &message, bool error)
{
    m_feedbackLabel->setText(message);
    m_feedbackLabel->setProperty("class", error ? QStringLiteral("warning") : QStringLiteral("success"));
    repolish(m_feedbackLabel);
    m_feedbackLabel->setVisible(!message.isEmpty());
}

void ModManagerDialog::toggleMod(const QString &path, bool enabled)
{
    const BepInExMod *mod = modForPath(path);
    if (!mod) {
        setFeedback(Vanta::tr("The selected mod is no longer present. The list was refreshed."), true);
        refresh();
        return;
    }

    QString error;
    if (!BepInEx::setModEnabled(m_install, *mod, enabled, &error)) {
        setFeedback(error.isEmpty() ? Vanta::tr("The mod could not be changed.") : error, true);
        refresh();
        return;
    }

    const QString name = mod->name;
    setFeedback(Vanta::tr("%1 is now %2.")
                    .arg(name, enabled ? Vanta::tr("enabled") : Vanta::tr("disabled")),
                false);
    refresh();
    emit modsChanged();
}

void ModManagerDialog::addLocalMod()
{
    const QString directory = m_game.effectiveDirectory();
    if (directory.isEmpty() || !QFileInfo(directory).isDir()) {
        QMessageBox::warning(this, Vanta::tr("Game folder unavailable"),
                             Vanta::tr("The game folder is not available, so Vanta cannot install a mod."));
        return;
    }

    const QString start = m_install.isValid() ? m_install.pluginsPath : directory;
    const QString path = QFileDialog::getOpenFileName(
        this, Vanta::tr("Add mod file"), start,
        QStringLiteral("Mod packages (*.zip *.dll *.dll.disabled);;ZIP archives (*.zip);;DLL files (*.dll *.dll.disabled)"));
    if (path.isEmpty())
        return;

    const ModTarget target = m_install.isValid() ? ModInstaller::targetFor(m_install)
                                                 : ModInstaller::targetForGame(directory);
    ModInstallPlan plan;
    QString error;
    if (!ModInstaller::planFromFile(path, target, &plan, &error)) {
        QMessageBox::warning(this, Vanta::tr("Mod not installed"), error);
        return;
    }

    bool overwrite = false;
    if (!plan.conflicts.isEmpty()) {
        QStringList sample = plan.conflicts.mid(0, 8);
        QString detail = sample.join(QLatin1Char('\n'));
        if (plan.conflicts.size() > sample.size())
            detail += Vanta::tr("\nand %1 more").arg(plan.conflicts.size() - sample.size());
        const auto reply = QMessageBox::question(
            this, Vanta::tr("Replace existing files?"),
            Vanta::tr("%1 file(s) already exist with different contents:\n\n%2\n\n"
                           "Vanta copies each replaced file under BepInEx/.vanta-backups before writing. "
                           "Nothing is deleted. Continue?")
                .arg(plan.conflicts.size())
                .arg(detail),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return;
        overwrite = true;
    }

    QStringList written;
    QString backupRoot;
    if (!ModInstaller::execute(plan, target, overwrite, &written, &backupRoot, &error)) {
        QString message = error;
        if (!written.isEmpty()) {
            message += Vanta::tr(" %1 file(s) were written before the failure.").arg(written.size());
        }
        QMessageBox::critical(this,
                              written.isEmpty() ? Vanta::tr("Mod not installed")
                                                : Vanta::tr("Mod installation incomplete"),
                              message);
        refresh();
        if (!written.isEmpty())
            emit modsChanged();
        return;
    }

    const QString name = plan.displayName.isEmpty() ? plan.packageName : plan.displayName;
    QString message;
    if (written.isEmpty()) {
        message = Vanta::tr("%1 is already installed and every file matches. Nothing changed.").arg(name);
    } else {
        message = Vanta::tr("Installed %1 file(s) for %2.").arg(written.size()).arg(name);
        if (!backupRoot.isEmpty())
            message += Vanta::tr(" Backups: %1").arg(QDir::toNativeSeparators(backupRoot));
    }
    const QString warnings = warningText(plan.warnings);
    if (!warnings.isEmpty())
        message += QLatin1Char(' ') + warnings;
    setFeedback(message, false);
    refresh();
    if (!written.isEmpty())
        emit modsChanged();
}

void ModManagerDialog::openThunderstore()
{
    if (m_game.effectiveDirectory().isEmpty() || !QFileInfo(m_game.effectiveDirectory()).isDir()) {
        QMessageBox::warning(this, Vanta::tr("Game folder unavailable"),
                             Vanta::tr("The game folder is not available, so Vanta cannot install a mod."));
        return;
    }

    ThunderstoreDialog dialog(m_game, m_install, m_theme, this);
    connect(&dialog, &ThunderstoreDialog::installed, this, [this] {
        refresh();
        emit modsChanged();
    });
    connect(&dialog, &ThunderstoreDialog::communityResolved, this,
            [this](const QString &community) {
                if (community.trimmed().isEmpty() || m_game.thunderstoreCommunity == community)
                    return;
                m_game.thunderstoreCommunity = community;
                emit communityResolved(community);
            });
    dialog.exec();
    refresh();
}

void ModManagerDialog::openModFolder()
{
    const QString folder = modFolder();
    if (folder.isEmpty() || !QFileInfo::exists(folder)) {
        setFeedback(Vanta::tr("The mod folder does not exist yet."), true);
        return;
    }
    UiHelp::openContainingDirectory(folder);
}

QString ModManagerDialog::modFolder() const
{
    if (!m_install.pluginsPath.isEmpty())
        return m_install.pluginsPath;
    if (!m_install.root.isEmpty())
        return m_install.root;
    return m_game.effectiveDirectory();
}

const BepInExMod *ModManagerDialog::modForPath(const QString &path) const
{
    for (const BepInExMod &mod : m_install.mods) {
        if (mod.path == path)
            return &mod;
    }
    return nullptr;
}
