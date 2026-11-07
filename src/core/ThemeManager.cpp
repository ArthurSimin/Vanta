#include "ThemeManager.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleHints>
#include <QWidget>

namespace {
QString color(const QColor &value)
{
    return value.name(QColor::HexRgb);
}

// Pick the first installed family from a preference list so the UI does not fall
// back to DejaVu Sans on desktops that ship a modern UI font.
QString preferredFontFamily()
{
    static const QStringList candidates = {
        QStringLiteral("Inter"),
        QStringLiteral("Cantarell"),
        QStringLiteral("Noto Sans"),
        QStringLiteral("Source Sans 3"),
        QStringLiteral("Open Sans"),
        QStringLiteral("Fira Sans"),
        QStringLiteral("Ubuntu"),
        QStringLiteral("DejaVu Sans")
    };
    const QStringList installed = QFontDatabase::families();
    for (const QString &candidate : candidates) {
        if (installed.contains(candidate, Qt::CaseInsensitive))
            return candidate;
    }
    return QApplication::font().family();
}
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    m_systemPalette = QApplication::palette();
    resolveColors();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &ThemeManager::systemSchemeChanged);
#endif
}

void ThemeManager::systemSchemeChanged()
{
    if (m_choice != ThemeChoice::System && m_choice != ThemeChoice::SystemColors)
        return;
    const bool wasDark = m_dark;
    resolveColors();
    if (m_dark != wasDark || m_choice == ThemeChoice::SystemColors) {
        apply();
        emit themeChanged();
    }
}

void ThemeManager::setChoice(ThemeChoice choice)
{
    if (m_choice == choice)
        return;
    m_choice = choice;
    apply();
    emit choiceChanged(choice);
    emit themeChanged();
}

void ThemeManager::setAnimationsEnabled(bool enabled)
{
    if (m_animations == enabled)
        return;
    m_animations = enabled;
    emit animationsChanged(enabled);
}

void ThemeManager::resolveColors()
{
    if (m_choice == ThemeChoice::SystemColors) {
        resolveSystemColors();
        return;
    }
    if (m_choice == ThemeChoice::Dark) {
        m_dark = true;
    } else if (m_choice == ThemeChoice::Light) {
        m_dark = false;
    } else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
        if (scheme == Qt::ColorScheme::Dark)
            m_dark = true;
        else if (scheme == Qt::ColorScheme::Light)
            m_dark = false;
        else
            m_dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
#else
        m_dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
#endif
    }

    if (m_dark) {
        m_colors.dark = true;
        m_colors.window = QColor(QStringLiteral("#0d0a14"));
        m_colors.surface = QColor(QStringLiteral("#171120"));
        m_colors.surfaceRaised = QColor(QStringLiteral("#20172d"));
        m_colors.sidebar = QColor(QStringLiteral("#110d1a"));
        m_colors.border = QColor(QStringLiteral("#2c223b"));
        m_colors.borderStrong = QColor(QStringLiteral("#46345e"));
        m_colors.text = QColor(QStringLiteral("#f5f0fb"));
        m_colors.muted = QColor(QStringLiteral("#a99ab9"));
        m_colors.accent = QColor(QStringLiteral("#7c3aed"));
        m_colors.accentHover = QColor(QStringLiteral("#8b5cf6"));
        m_colors.accentPressed = QColor(QStringLiteral("#6d28d9"));
        m_colors.accentSoft = QColor(QStringLiteral("#2a1b43"));
        m_colors.selection = QColor(QStringLiteral("#3a2560"));
        m_colors.hover = QColor(QStringLiteral("#2a2038"));
        m_colors.danger = QColor(QStringLiteral("#f87171"));
        m_colors.warning = QColor(QStringLiteral("#fbbf24"));
        m_colors.success = QColor(QStringLiteral("#4ade80"));
        m_colors.tooltip = QColor(QStringLiteral("#261b35"));
        m_colors.shadow = QColor(0, 0, 0, 120);
    } else {
        m_colors.dark = false;
        m_colors.window = QColor(QStringLiteral("#f7f4fc"));
        m_colors.surface = QColor(QStringLiteral("#ffffff"));
        m_colors.surfaceRaised = QColor(QStringLiteral("#fbf8ff"));
        m_colors.sidebar = QColor(QStringLiteral("#f0eafb"));
        m_colors.border = QColor(QStringLiteral("#e1d8ee"));
        m_colors.borderStrong = QColor(QStringLiteral("#c8b7e2"));
        m_colors.text = QColor(QStringLiteral("#21172f"));
        m_colors.muted = QColor(QStringLiteral("#756686"));
        m_colors.accent = QColor(QStringLiteral("#7c3aed"));
        m_colors.accentHover = QColor(QStringLiteral("#6d28d9"));
        m_colors.accentPressed = QColor(QStringLiteral("#5b21b6"));
        m_colors.accentSoft = QColor(QStringLiteral("#ede4ff"));
        m_colors.selection = QColor(QStringLiteral("#e9dcff"));
        m_colors.hover = QColor(QStringLiteral("#f0e8fb"));
        m_colors.danger = QColor(QStringLiteral("#dc2626"));
        m_colors.warning = QColor(QStringLiteral("#b45309"));
        m_colors.success = QColor(QStringLiteral("#15803d"));
        m_colors.tooltip = QColor(QStringLiteral("#ffffff"));
        m_colors.shadow = QColor(35, 18, 60, 55);
    }
}

void ThemeManager::resolveSystemColors()
{
    const QPalette &palette = m_systemPalette;
    const QColor window = palette.color(QPalette::Active, QPalette::Window);
    const QColor base = palette.color(QPalette::Active, QPalette::Base);
    const QColor text = palette.color(QPalette::Active, QPalette::WindowText);
    const QColor highlight = palette.color(QPalette::Active, QPalette::Highlight);
    const QColor tooltipBase = palette.color(QPalette::Active, QPalette::ToolTipBase);

    m_dark = window.lightness() < 128;

    auto mix = [](const QColor &a, const QColor &b, qreal t) {
        const qreal clamped = qBound(0.0, t, 1.0);
        return QColor::fromRgbF(a.redF() * (1.0 - clamped) + b.redF() * clamped,
                                a.greenF() * (1.0 - clamped) + b.greenF() * clamped,
                                a.blueF() * (1.0 - clamped) + b.blueF() * clamped);
    };

    m_colors.dark = m_dark;
    m_colors.window = window;
    m_colors.surface = base;
    m_colors.surfaceRaised = mix(base, text, m_dark ? 0.07 : 0.04);
    m_colors.sidebar = m_dark ? mix(window, QColor(0, 0, 0), 0.28) : mix(window, QColor(0, 0, 0), 0.05);
    m_colors.border = mix(window, text, m_dark ? 0.16 : 0.14);
    m_colors.borderStrong = mix(window, text, m_dark ? 0.30 : 0.26);
    m_colors.text = text;
    m_colors.muted = mix(text, window, m_dark ? 0.42 : 0.40);

    const QColor accent = highlight.isValid() ? highlight : QColor(QStringLiteral("#7c3aed"));
    m_colors.accent = accent;
    m_colors.accentHover = m_dark ? mix(accent, QColor(255, 255, 255), 0.16)
                                  : mix(accent, QColor(0, 0, 0), 0.12);
    m_colors.accentPressed = mix(accent, QColor(0, 0, 0), m_dark ? 0.18 : 0.24);
    m_colors.accentSoft = mix(window, accent, m_dark ? 0.20 : 0.14);
    m_colors.selection = mix(window, accent, m_dark ? 0.38 : 0.26);
    m_colors.hover = mix(window, text, m_dark ? 0.09 : 0.06);
    m_colors.danger = m_dark ? QColor(QStringLiteral("#f87171")) : QColor(QStringLiteral("#dc2626"));
    m_colors.warning = m_dark ? QColor(QStringLiteral("#fbbf24")) : QColor(QStringLiteral("#b45309"));
    m_colors.success = m_dark ? QColor(QStringLiteral("#4ade80")) : QColor(QStringLiteral("#15803d"));
    m_colors.tooltip = tooltipBase.isValid() ? tooltipBase : m_colors.surfaceRaised;
    m_colors.shadow = QColor(0, 0, 0, m_dark ? 130 : 60);
}

void ThemeManager::apply()
{
    resolveColors();

    QFont base = QApplication::font();
    base.setFamily(preferredFontFamily());
    if (base.pointSizeF() < 9.0)
        base.setPointSizeF(10.0);
    base.setHintingPreference(QFont::PreferFullHinting);
    QApplication::setFont(base);

    applyPalette();
    qApp->setStyleSheet(styleSheet());
    const auto widgets = QApplication::allWidgets();
    for (QWidget *widget : widgets)
        widget->update();
}

void ThemeManager::applyPalette()
{
    QPalette palette = m_choice == ThemeChoice::SystemColors
        ? m_systemPalette
        : QApplication::style()->standardPalette();
    palette.setColor(QPalette::Window, m_colors.window);
    palette.setColor(QPalette::WindowText, m_colors.text);
    palette.setColor(QPalette::Base, m_colors.surface);
    palette.setColor(QPalette::AlternateBase, m_colors.surfaceRaised);
    palette.setColor(QPalette::Text, m_colors.text);
    palette.setColor(QPalette::Button, m_colors.surfaceRaised);
    palette.setColor(QPalette::ButtonText, m_colors.text);
    palette.setColor(QPalette::Highlight, m_colors.accent);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, m_colors.muted);
    palette.setColor(QPalette::Mid, m_colors.border);
    palette.setColor(QPalette::Dark, m_colors.sidebar);
    palette.setColor(QPalette::Light, m_colors.hover);
    palette.setColor(QPalette::ToolTipBase, m_colors.tooltip);
    palette.setColor(QPalette::ToolTipText, m_colors.text);
    palette.setColor(QPalette::Disabled, QPalette::Text, m_colors.muted.darker(120));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, m_colors.muted);
    QApplication::setPalette(palette);
}

QString ThemeManager::styleSheet() const
{
    const QString window = color(m_colors.window);
    const QString surface = color(m_colors.surface);
    const QString raised = color(m_colors.surfaceRaised);
    const QString sidebar = color(m_colors.sidebar);
    const QString border = color(m_colors.border);
    const QString borderStrong = color(m_colors.borderStrong);
    const QString text = color(m_colors.text);
    const QString muted = color(m_colors.muted);
    const QString accent = color(m_colors.accent);
    const QString accentHover = color(m_colors.accentHover);
    const QString accentPressed = color(m_colors.accentPressed);
    const QString accentSoft = color(m_colors.accentSoft);
    const QString selection = color(m_colors.selection);
    const QString hover = color(m_colors.hover);
    const QString danger = color(m_colors.danger);
    const QString warning = color(m_colors.warning);
    const QString success = color(m_colors.success);
    const QString tooltip = color(m_colors.tooltip);

    return QStringLiteral(R"QSS(
* {
    outline: none;
}
QWidget {
    color: %TEXT%;
    selection-background-color: %ACCENT%;
    selection-color: #ffffff;
}
QMainWindow, QWidget#centralWidget, QWidget#contentArea {
    background: %WINDOW%;
}
QDialog {
    background: %SURFACE%;
}
QLabel {
    background: transparent;
}
QLabel[class="fieldHelp"] {
    color: %MUTED%;
    font-size: 11px;
}
QLabel[class="sectionTitle"] {
    font-weight: 700;
    font-size: 14px;
}
QLabel[class="pageTitle"] {
    font-weight: 800;
    font-size: 24px;
}
QLabel[class="muted"] {
    color: %MUTED%;
}
QLabel[class="warning"] {
    color: %WARNING%;
}
QLabel[class="danger"] {
    color: %DANGER%;
}
QLabel[class="success"] {
    color: %SUCCESS%;
}
QFrame#headerBar {
    background: transparent;
}
QFrame#searchContainer {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: 19px;
}
QFrame#searchContainer:focus-within {
    border: 1px solid %ACCENT%;
}
QLineEdit#searchField {
    background: transparent;
    border: none;
    border-radius: 0;
    padding: 5px 2px;
    min-height: 26px;
}
QLineEdit#searchField QToolButton {
    background: transparent;
    border: none;
    padding: 0px;
    margin: 0px;
}
QLineEdit#searchField QToolButton:hover {
    background: transparent;
}
QToolButton#searchClearButton {
    background: transparent;
    border: none;
    padding: 3px;
    margin-right: 2px;
}
QToolButton#searchClearButton:hover {
    background: %HOVER%;
    border-radius: 8px;
}
QToolButton#filterButton {
    background: transparent;
    border: none;
    border-top-right-radius: 19px;
    border-bottom-right-radius: 19px;
    padding: 7px;
    margin-right: 2px;
}
QToolButton#filterButton:hover {
    background: %HOVER%;
}
QToolButton#filterButton:checked {
    background: %ACCENTSOFT%;
}
QToolButton#addGameButton {
    background: %ACCENT%;
    color: #ffffff;
    border: none;
    border-radius: 14px;
    min-width: 38px;
    max-width: 38px;
    min-height: 38px;
    max-height: 38px;
    font-size: 22px;
    font-weight: 500;
}
QToolButton#addGameButton:hover {
    background: %ACCENTHOVER%;
}
QToolButton#addGameButton:pressed {
    background: %ACCENTPRESSED%;
}
QToolButton#viewOptionsButton, QToolButton#viewMenuButton, QToolButton#mainMenuButton {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: 13px;
    min-width: 36px;
    min-height: 36px;
    padding: 2px;
}
QToolButton#viewOptionsButton {
    border-top-right-radius: 0;
    border-bottom-right-radius: 0;
    border-right: none;
}
QToolButton#viewMenuButton {
    border-top-left-radius: 0;
    border-bottom-left-radius: 0;
    min-width: 22px;
    max-width: 22px;
}
QToolButton#viewOptionsButton:hover, QToolButton#viewMenuButton:hover, QToolButton#mainMenuButton:hover {
    background: %HOVER%;
    border-color: %BORDERSTRONG%;
}
QToolButton#viewOptionsButton:pressed, QToolButton#viewMenuButton:pressed, QToolButton#mainMenuButton:pressed {
    background: %SELECTION%;
}
QPushButton {
    background: %RAISED%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    border-radius: 17px;
    padding: 7px 17px;
    min-height: 20px;
    font-weight: 600;
}
QPushButton:hover {
    background: %HOVER%;
    border-color: %BORDERSTRONG%;
}
QPushButton:pressed {
    background: %SELECTION%;
}
QPushButton:disabled {
    color: %MUTED%;
    background: %SURFACE%;
}
QPushButton[primary="true"] {
    background: %ACCENT%;
    color: #ffffff;
    border: 1px solid %ACCENT%;
}
QPushButton[primary="true"]:hover {
    background: %ACCENTHOVER%;
    border-color: %ACCENTHOVER%;
}
QPushButton[primary="true"]:pressed {
    background: %ACCENTPRESSED%;
}
QPushButton[danger="true"] {
    color: %DANGER%;
}
QPushButton[danger="true"]:hover {
    background: %DANGER%;
    color: #ffffff;
    border-color: %DANGER%;
}
QPushButton[flat="true"] {
    background: transparent;
    border-color: transparent;
    color: %MUTED%;
}
QPushButton[flat="true"]:hover {
    background: %HOVER%;
    color: %TEXT%;
}
QToolButton {
    background: transparent;
    border: none;
    border-radius: 10px;
    padding: 5px;
}
QToolButton:hover {
    background: %HOVER%;
}
QToolButton:pressed {
    background: %SELECTION%;
}
QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox {
    background: %SURFACE%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    border-radius: 10px;
    padding: 7px 10px;
    selection-background-color: %ACCENT%;
    selection-color: #ffffff;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus {
    border: 1px solid %ACCENT%;
}
QLineEdit:disabled, QPlainTextEdit:disabled, QTextEdit:disabled, QSpinBox:disabled {
    color: %MUTED%;
    background: %SIDEBAR%;
}
QComboBox {
    background: %SURFACE%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    border-radius: 10px;
    padding: 6px 30px 6px 10px;
    min-height: 22px;
}
QComboBox:hover {
    border-color: %BORDERSTRONG%;
}
QComboBox:focus {
    border-color: %ACCENT%;
}
QComboBox::drop-down {
    border: none;
    width: 26px;
}
QComboBox QAbstractItemView {
    background: %SURFACE%;
    color: %TEXT%;
    border: 1px solid %BORDERSTRONG%;
    border-radius: 10px;
    padding: 5px;
    selection-background-color: %ACCENT%;
    selection-color: #ffffff;
    outline: none;
}
QCheckBox, QRadioButton {
    spacing: 9px;
    background: transparent;
    min-height: 22px;
}
QCheckBox::indicator, QRadioButton::indicator {
    width: 18px;
    height: 18px;
    border: 1px solid %BORDERSTRONG%;
    background: %SURFACE%;
}
QCheckBox::indicator {
    border-radius: 6px;
}
QRadioButton::indicator {
    border-radius: 9px;
}
QCheckBox::indicator:hover, QRadioButton::indicator:hover {
    border-color: %ACCENT%;
    background: %HOVER%;
}
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background: %ACCENT%;
    border: 1px solid %ACCENT%;
}
QCheckBox::indicator:checked {
    image: url(:/icons/check.svg);
}
QRadioButton::indicator:checked {
    image: url(:/icons/radio-dot.svg);
}
QCheckBox::indicator:checked:hover, QRadioButton::indicator:checked:hover {
    background: %ACCENTHOVER%;
    border-color: %ACCENTHOVER%;
}
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled {
    background: %SIDEBAR%;
    border-color: %BORDER%;
}
QCheckBox::indicator:checked:disabled, QRadioButton::indicator:checked:disabled {
    background: %BORDERSTRONG%;
    border-color: %BORDERSTRONG%;
}
QMenu {
    background: %SURFACE%;
    border: 1px solid %BORDERSTRONG%;
    border-radius: 12px;
    padding: 6px;
}
QMenu::item {
    background: transparent;
    color: %TEXT%;
    padding: 7px 28px 7px 12px;
    border-radius: 8px;
    margin: 1px 2px;
}
QMenu::item:selected {
    background: %ACCENTSOFT%;
    color: %TEXT%;
}
QMenu::item:disabled {
    color: %MUTED%;
}
QMenu::separator {
    height: 1px;
    background: %BORDER%;
    margin: 5px 8px;
}
QMenu::indicator {
    width: 16px;
    height: 16px;
    margin-left: 7px;
}
QToolTip {
    background: %TOOLTIP%;
    color: %TEXT%;
    border: 1px solid %BORDERSTRONG%;
    border-radius: 8px;
    padding: 6px 9px;
}
QScrollArea, QAbstractScrollArea {
    background: transparent;
    border: none;
}
QScrollBar:vertical {
    background: transparent;
    width: 9px;
    margin: 3px 1px;
}
QScrollBar::handle:vertical {
    background: %BORDERSTRONG%;
    min-height: 30px;
    border-radius: 4px;
}
QScrollBar::handle:vertical:hover {
    background: %ACCENT%;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: transparent;
}
QScrollBar:horizontal {
    background: transparent;
    height: 9px;
    margin: 1px 3px;
}
QScrollBar::handle:horizontal {
    background: %BORDERSTRONG%;
    min-width: 30px;
    border-radius: 4px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0px;
}
QTabWidget::pane {
    border: 1px solid %BORDER%;
    border-radius: 12px;
    background: %SURFACE%;
    top: -1px;
}
QTabBar::tab {
    background: transparent;
    color: %MUTED%;
    padding: 8px 16px;
    border-radius: 9px;
    margin-right: 4px;
    font-weight: 600;
}
QTabBar::tab:hover {
    background: %HOVER%;
    color: %TEXT%;
}
QTabBar::tab:selected {
    background: %ACCENTSOFT%;
    color: %TEXT%;
}
QTableWidget, QTableView {
    background: %SURFACE%;
    alternate-background-color: %RAISED%;
    border: 1px solid %BORDER%;
    border-radius: 14px;
    gridline-color: transparent;
    padding: 6px;
}
QTableView::item {
    padding: 6px;
    border: none;
}
QTableView::item:selected {
    background: %SELECTION%;
    color: %TEXT%;
}
QTableView::item:hover {
    background: %HOVER%;
}
QHeaderView::section {
    background: %SURFACE%;
    color: %MUTED%;
    border: none;
    border-bottom: 1px solid %BORDER%;
    padding: 10px 8px;
    font-weight: 700;
    font-size: 11px;
}
QTableCornerButton::section {
    background: %SURFACE%;
    border: none;
}
QListWidget, QTreeWidget, QTreeView, QListWidget#detectedFileList {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: 12px;
    padding: 4px;
    outline: none;
}
QListWidget::item, QTreeWidget::item {
    border-radius: 8px;
    padding: 6px;
}
QListWidget::item:selected, QTreeWidget::item:selected {
    background: %SELECTION%;
    color: %TEXT%;
}
QListWidget::item:hover, QTreeWidget::item:hover {
    background: %HOVER%;
}
QGroupBox {
    border: 1px solid %BORDER%;
    border-radius: 12px;
    margin-top: 14px;
    padding: 12px;
    background: %SURFACE%;
    font-weight: 700;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 14px;
    padding: 0 6px;
    color: %TEXT%;
}
QProgressBar {
    background: %HOVER%;
    border: none;
    border-radius: 4px;
    min-height: 7px;
    max-height: 7px;
    text-align: center;
    color: transparent;
}
QProgressBar::chunk {
    background: %ACCENT%;
    border-radius: 4px;
}
QFrame#compatibilityPanel, QFrame#diagnosticsPanel, QFrame#infoPanel {
    background: %RAISED%;
    border: 1px solid %BORDER%;
    border-radius: 13px;
}
QFrame#emptyState, QFrame#filterPopup {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: 16px;
}
QFrame#separator {
    background: %BORDER%;
    max-height: 1px;
    min-height: 1px;
}
QPlainTextEdit[monospace="true"], QTextEdit[monospace="true"] {
    font-family: "DejaVu Sans Mono", "Liberation Mono", monospace;
    font-size: 11px;
    background: %WINDOW%;
}
QLabel[monospace="true"] {
    font-family: "DejaVu Sans Mono", "Liberation Mono", monospace;
    font-size: 11px;
    background: %WINDOW%;
    border: 1px solid %BORDER%;
    border-radius: 10px;
    padding: 8px;
}
QDialogButtonBox QPushButton {
    min-width: 84px;
}
QMessageBox {
    background: %SURFACE%;
}
QMessageBox QLabel {
    color: %TEXT%;
}
QSlider::groove:horizontal {
    height: 4px;
    background: %HOVER%;
    border-radius: 2px;
}
QSlider::sub-page:horizontal {
    background: %ACCENT%;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    width: 16px;
    margin: -6px 0;
    background: %ACCENT%;
    border-radius: 8px;
}
QSpinBox::up-button, QSpinBox::down-button {
    width: 18px;
    background: transparent;
    border: none;
}
QSplitter::handle {
    background: transparent;
}
QWidget#sidebarSurface {
    background: %SIDEBAR%;
}
QWidget#sidebarLogoArea {
    background: transparent;
}
QWidget#contentHeader {
    background: transparent;
}
QWidget#gridContent {
    background: transparent;
}
QWidget#selectionBarBody {
    background: %SURFACE%;
}
QPushButton#selectionBarLaunch {
    border-radius: 15px;
    padding: 6px 18px;
}
QToolButton#selectionBarMenu {
    background: %RAISED%;
    border: 1px solid %BORDER%;
    border-radius: 15px;
    padding: 0;
}
QToolButton#selectionBarMenu:hover {
    background: %HOVER%;
    border-color: %BORDERSTRONG%;
}
QToolButton#selectionBarMenu:disabled {
    background: %SURFACE%;
    border-color: %BORDER%;
}
QLabel#selectionBarChip {
    background: %ACCENTSOFT%;
    color: %ACCENT%;
    border-radius: 9px;
    padding: 1px 8px;
}
QToolButton#selectionBarFix {
    background: transparent;
    color: %MUTED%;
    border: 1px solid %BORDER%;
    border-radius: 10px;
    padding: 1px 9px;
    font-weight: 700;
}QToolButton#selectionBarFix:hover {
    color: %TEXT%;
    border-color: %BORDERSTRONG%;
}
QToolButton#selectionBarFix:checked {
    background: %ACCENTSOFT%;
    color: %ACCENT%;
    border-color: %ACCENT%;
}
QFrame#tutorialBubble {
    background: %SURFACE%;
    border: 1px solid %BORDERSTRONG%;
    border-radius: 16px;
}
QFrame#vantaDialogFrame {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: 18px;
}
QWidget#vantaDialogHeader, QWidget#vantaDialogBody {
    background: transparent;
}
QLabel#dialogTitle {
    color: %TEXT%;
    font-weight: 800;
}
QToolButton#dialogCloseButton {
    background: transparent;
    border: none;
    border-radius: 15px;
}
QToolButton#dialogCloseButton:hover {
    background: %HOVER%;
}
QPushButton#tutorialSkip {
    background: transparent;
    border: none;
    color: %MUTED%;
    padding: 4px 8px;
    font-weight: 500;
}
QPushButton#tutorialSkip:hover {
    color: %TEXT%;
    background: %HOVER%;
}
)QSS")
        .replace(QStringLiteral("%WINDOW%"), window)
        .replace(QStringLiteral("%SURFACE%"), surface)
        .replace(QStringLiteral("%RAISED%"), raised)
        .replace(QStringLiteral("%SIDEBAR%"), sidebar)
        .replace(QStringLiteral("%BORDERSTRONG%"), borderStrong)
        .replace(QStringLiteral("%BORDER%"), border)
        .replace(QStringLiteral("%TEXT%"), text)
        .replace(QStringLiteral("%MUTED%"), muted)
        .replace(QStringLiteral("%ACCENTHOVER%"), accentHover)
        .replace(QStringLiteral("%ACCENTPRESSED%"), accentPressed)
        .replace(QStringLiteral("%ACCENTSOFT%"), accentSoft)
        .replace(QStringLiteral("%ACCENT%"), accent)
        .replace(QStringLiteral("%SELECTION%"), selection)
        .replace(QStringLiteral("%HOVER%"), hover)
        .replace(QStringLiteral("%DANGER%"), danger)
        .replace(QStringLiteral("%WARNING%"), warning)
        .replace(QStringLiteral("%SUCCESS%"), success)
        .replace(QStringLiteral("%TOOLTIP%"), tooltip);
}
