// ---------------------------------------------------------------------------
// Theme.cpp
// ---------------------------------------------------------------------------
#include "Theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QHash>
#include <QPalette>
#include <QStringList>
#include <QStyleHints>
#include <algorithm>

namespace tdms {
namespace theme {

namespace {

const char* kStyleSheetTemplate = R"QSS(
QWidget {
    background: @window;
    color: @windowText;
    font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
    font-size: 9.5pt;
}
QMainWindow::separator { background: @border; width: 3px; height: 3px; }

QMenuBar { background: @menubar; border-bottom: 1px solid @border; }
QMenuBar::item { padding: 4px 10px; background: transparent; }
QMenuBar::item:selected { background: @hover; border-radius: 4px; }
QMenu { background: @menuBg; border: 1px solid @border; padding: 5px; }
QMenu::item { padding: 5px 26px 5px 26px; border-radius: 4px; }
QMenu::item:selected { background: @selection; color: @selectionText; }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }
QMenu::icon { padding-left: 6px; }

QToolBar { background: @panel; border: none; border-bottom: 1px solid @border; spacing: 4px; padding: 3px; }
QToolBar::separator { background: @border; width: 1px; margin: 4px 6px; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 5px; padding: 5px 7px; }
QToolButton:hover { background: @hover; border-color: @borderStrong; }
QToolButton:pressed { background: @pressed; }
QToolButton:checked { background: @selection; border-color: @accent; color: @selectionText; }

QTreeWidget, QTableWidget, QTableView, QListWidget {
    background: @base;
    alternate-background-color: @altBase;
    border: 1px solid @border;
    selection-background-color: @selection;
    selection-color: @selectionText;
    outline: none;
}
QTreeWidget::item, QTableWidget::item { padding: 3px 4px; border: none; }
QTreeWidget::item:hover, QTableWidget::item:hover { background: @hover; }
QTreeWidget::item:selected, QTableWidget::item:selected {
    background: @selection; color: @selectionText;
}
QHeaderView::section {
    background: @headerBg;
    color: @headerText;
    border: none;
    border-right: 1px solid @border;
    border-bottom: 1px solid @border;
    padding: 5px 6px;
    font-weight: 600;
}
QTableCornerButton::section { background: @headerBg; border: none; }

QListWidget#categoryList {
    background: @panel;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 4px;
}
QListWidget#categoryList::item { padding: 7px 8px; border-radius: 5px; }
QListWidget#categoryList::item:selected { background: @selection; color: @selectionText; }

QLabel#treeHeader {
    background: @headerBg;
    color: @headerText;
    font-weight: 600;
    border-bottom: 1px solid @border;
    padding-left: 10px;
}
QLabel#hintLabel { color: @muted; }
QLabel#propTitle { font-weight: 600; color: @windowText; }

QCheckBox::indicator, QTreeWidget::indicator {
    width: 14px; height: 14px;
    border: 1px solid @checkBorder;
    border-radius: 3px;
    background: @inputBg;
}
QCheckBox::indicator:checked, QTreeWidget::indicator:checked {
    background: @accent;
    border-color: @accent;
    image: url(:/theme/check.svg);
}
QRadioButton { spacing: 7px; }
QRadioButton::indicator {
    width: 13px; height: 13px;
    border: 1px solid @checkBorder;
    border-radius: 7px;
    background: @inputBg;
}
QRadioButton::indicator:hover { border-color: @accent; }
QRadioButton::indicator:checked {
    border: 1px solid @accent;
    background: @accent;
    image: url(:/theme/dot.svg);
}

QStatusBar { background: @menubar; border-top: 1px solid @border; color: @muted; }
QStatusBar QLabel { color: @muted; }

QSplitter::handle { background: @border; }
QSplitter::handle:hover { background: @accent; }

QScrollBar:vertical { background: @scrollBg; width: 11px; margin: 0; }
QScrollBar::handle:vertical { background: @scrollHandle; min-height: 24px; border-radius: 5px; }
QScrollBar::handle:vertical:hover { background: @scrollHandleHover; }
QScrollBar:horizontal { background: @scrollBg; height: 11px; margin: 0; }
QScrollBar::handle:horizontal { background: @scrollHandle; min-width: 24px; border-radius: 5px; }
QScrollBar::handle:horizontal:hover { background: @scrollHandleHover; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QPushButton {
    background: @buttonBg; color: @buttonText;
    border: 1px solid @buttonBorder; border-radius: 5px; padding: 5px 14px;
}
QPushButton:hover { background: @buttonHover; }
QPushButton:pressed { background: @pressed; }
QPushButton:disabled { color: @muted; background: @buttonDisabled; border-color: @border; }

QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background: @inputBg; color: @windowText;
    border: 1px solid @buttonBorder; border-radius: 4px; padding: 3px 6px;
    selection-background-color: @selection; selection-color: @selectionText;
}
QComboBox::drop-down {
    subcontrol-origin: padding; subcontrol-position: center right;
    border: none; width: 18px;
}
QComboBox::down-arrow {
    image: url(@chevronDown);
    width: 12px; height: 12px;
    margin-right: 4px;
}

QSpinBox::up-button, QDoubleSpinBox::up-button {
    subcontrol-origin: border;
    subcontrol-position: top right;
    width: 17px;
    background: @buttonBg;
    border-left: 1px solid @buttonBorder;
    border-top-right-radius: 4px;
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
    subcontrol-origin: border;
    subcontrol-position: bottom right;
    width: 17px;
    background: @buttonBg;
    border-left: 1px solid @buttonBorder;
    border-top: 1px solid @buttonBorder;
    border-bottom-right-radius: 4px;
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
    background: @buttonHover;
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
    image: url(@chevronUp);
    width: 10px; height: 10px;
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
    image: url(@chevronDown);
    width: 10px; height: 10px;
}
QComboBox QAbstractItemView {
    background: @menuBg; border: 1px solid @border;
    selection-background-color: @selection; selection-color: @selectionText;
}

QGroupBox {
    color: @windowText;
    border: 1px solid @border;
    border-radius: 6px;
    margin-top: 11px;
    padding-top: 9px;
}
QGroupBox::title {
    subcontrol-origin: margin; left: 10px; padding: 0 4px;
    color: @headerText; font-weight: 600;
}

QToolTip { background: @tooltipBg; color: @tooltipText; border: 1px solid @border; padding: 4px; }
QDialog { background: @window; }
)QSS";

Colors makeDark()
{
    Colors c;
    c.window = QColor("#181c22");
    c.windowText = QColor("#d7dee9");
    c.base = QColor("#171b21");
    c.altBase = QColor("#1b2028");
    c.border = QColor("#262c36");
    c.borderStrong = QColor("#38445a");
    c.panel = QColor("#1b2028");
    c.menubar = QColor("#141820");
    c.menuBg = QColor("#1d232c");
    c.hover = QColor("#2a3342");
    c.pressed = QColor("#232a36");
    c.headerBg = QColor("#20262f");
    c.headerText = QColor("#93a1b5");
    c.selection = QColor("#2d4a72");
    c.selectionText = QColor("#ffffff");
    c.muted = QColor("#8794a8");
    c.buttonBg = QColor("#2a3242");
    c.buttonText = QColor("#dbe3ef");
    c.buttonBorder = QColor("#3a4459");
    c.buttonHover = QColor("#333d51");
    c.buttonDisabled = QColor("#232833");
    c.inputBg = QColor("#20262f");
    c.checkBorder = QColor("#4a566b");
    c.scrollBg = QColor("#14181e");
    c.scrollHandle = QColor("#323b4a");
    c.scrollHandleHover = QColor("#40506a");
    c.accent = QColor("#5b9dff");
    c.tooltipBg = QColor("#232a35");
    c.tooltipText = QColor("#d7dee9");
    c.canvas = QColor("#12151a");
    c.grid = QColor("#2b3242");
    return c;
}

Colors makeLight()
{
    Colors c;
    c.window = QColor("#f2f4f7");
    c.windowText = QColor("#1f2733");
    c.base = QColor("#ffffff");
    c.altBase = QColor("#f7f9fb");
    c.border = QColor("#d3d9e2");
    c.borderStrong = QColor("#bcc6d4");
    c.panel = QColor("#f7f8fa");
    c.menubar = QColor("#eceff4");
    c.menuBg = QColor("#ffffff");
    c.hover = QColor("#e6ecf4");
    c.pressed = QColor("#dfe5ee");
    c.headerBg = QColor("#eef1f5");
    c.headerText = QColor("#55637a");
    c.selection = QColor("#2f6fd0");
    c.selectionText = QColor("#ffffff");
    c.muted = QColor("#6b7787");
    c.buttonBg = QColor("#ffffff");
    c.buttonText = QColor("#232a35");
    c.buttonBorder = QColor("#c8d1de");
    c.buttonHover = QColor("#eaeff6");
    c.buttonDisabled = QColor("#f0f2f5");
    c.inputBg = QColor("#ffffff");
    c.checkBorder = QColor("#a9b4c4");
    c.scrollBg = QColor("#f0f2f5");
    c.scrollHandle = QColor("#c3cad4");
    c.scrollHandleHover = QColor("#a9b3c0");
    c.accent = QColor("#1a6fd4");
    c.tooltipBg = QColor("#fdfdfe");
    c.tooltipText = QColor("#1f2733");
    c.canvas = QColor("#ffffff");
    c.grid = QColor("#dbe1e9");
    return c;
}

Colors buildColors(bool dark)
{
    return dark ? makeDark() : makeLight();
}

QPalette buildPalette(const Colors& c)
{
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.windowText);
    p.setColor(QPalette::Base, c.base);
    p.setColor(QPalette::AlternateBase, c.altBase);
    p.setColor(QPalette::Text, c.windowText);
    p.setColor(QPalette::Button, c.buttonBg);
    p.setColor(QPalette::ButtonText, c.buttonText);
    p.setColor(QPalette::BrightText, c.selectionText);
    p.setColor(QPalette::Highlight, c.selection);
    p.setColor(QPalette::HighlightedText, c.selectionText);
    p.setColor(QPalette::ToolTipBase, c.tooltipBg);
    p.setColor(QPalette::ToolTipText, c.tooltipText);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::PlaceholderText, c.muted);
    p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Dark, c.border);
    p.setColor(QPalette::Light, c.base);
    p.setColor(QPalette::Disabled, QPalette::Text, c.muted);
    p.setColor(QPalette::Disabled, QPalette::WindowText, c.muted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.muted);
    return p;
}

QString buildStyleSheet(const Colors& c, bool dark)
{
    QHash<QString, QString> tokens;
    tokens.insert(QStringLiteral("@window"), c.window.name());
    tokens.insert(QStringLiteral("@windowText"), c.windowText.name());
    tokens.insert(QStringLiteral("@base"), c.base.name());
    tokens.insert(QStringLiteral("@altBase"), c.altBase.name());
    tokens.insert(QStringLiteral("@borderStrong"), c.borderStrong.name());
    tokens.insert(QStringLiteral("@border"), c.border.name());
    tokens.insert(QStringLiteral("@panel"), c.panel.name());
    tokens.insert(QStringLiteral("@menubar"), c.menubar.name());
    tokens.insert(QStringLiteral("@menuBg"), c.menuBg.name());
    tokens.insert(QStringLiteral("@hover"), c.hover.name());
    tokens.insert(QStringLiteral("@pressed"), c.pressed.name());
    tokens.insert(QStringLiteral("@headerBg"), c.headerBg.name());
    tokens.insert(QStringLiteral("@headerText"), c.headerText.name());
    tokens.insert(QStringLiteral("@selectionText"), c.selectionText.name());
    tokens.insert(QStringLiteral("@selection"), c.selection.name());
    tokens.insert(QStringLiteral("@muted"), c.muted.name());
    tokens.insert(QStringLiteral("@buttonBorder"), c.buttonBorder.name());
    tokens.insert(QStringLiteral("@buttonText"), c.buttonText.name());
    tokens.insert(QStringLiteral("@buttonHover"), c.buttonHover.name());
    tokens.insert(QStringLiteral("@buttonBg"), c.buttonBg.name());
    tokens.insert(QStringLiteral("@buttonDisabled"), c.buttonDisabled.name());
    tokens.insert(QStringLiteral("@inputBg"), c.inputBg.name());
    tokens.insert(QStringLiteral("@checkBorder"), c.checkBorder.name());
    tokens.insert(QStringLiteral("@scrollBg"), c.scrollBg.name());
    tokens.insert(QStringLiteral("@scrollHandleHover"), c.scrollHandleHover.name());
    tokens.insert(QStringLiteral("@scrollHandle"), c.scrollHandle.name());
    tokens.insert(QStringLiteral("@accent"), c.accent.name());
    tokens.insert(QStringLiteral("@tooltipBg"), c.tooltipBg.name());
    tokens.insert(QStringLiteral("@tooltipText"), c.tooltipText.name());
    tokens.insert(QStringLiteral("@chevronDown"),
                  dark ? QStringLiteral(":/theme/chevron-down-dark.svg")
                       : QStringLiteral(":/theme/chevron-down-light.svg"));
    tokens.insert(QStringLiteral("@chevronUp"),
                  dark ? QStringLiteral(":/theme/chevron-up-dark.svg")
                       : QStringLiteral(":/theme/chevron-up-light.svg"));

    QString qss = QString::fromUtf8(kStyleSheetTemplate);
    // Longest tokens first so "@border" does not eat "@borderStrong".
    QStringList keys = tokens.keys();
    std::sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) {
        return a.size() > b.size();
    });
    for (const QString& key : keys)
        qss.replace(key, tokens.value(key));
    return qss;
}

Colors g_colors = makeDark();
bool g_dark = true;
bool g_applied = false;
ThemeMode g_requested = ThemeMode::System;

bool systemIsDark()
{
    if (QGuiApplication* gui = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        const Qt::ColorScheme scheme = gui->styleHints()->colorScheme();
        if (scheme == Qt::ColorScheme::Dark)
            return true;
        if (scheme == Qt::ColorScheme::Light)
            return false;
    }
    // Fallback: judge from the platform palette.
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

} // namespace

bool isDark(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::Light: return false;
    case ThemeMode::Dark:  return true;
    case ThemeMode::System: break;
    }
    return systemIsDark();
}

bool isDark()
{
    return g_dark;
}

void apply(ThemeMode mode)
{
    const bool dark = isDark(mode);
    if (g_applied && dark == g_dark && mode == g_requested)
        return;

    g_dark = dark;
    g_requested = mode;
    g_colors = buildColors(dark);
    g_applied = true;

    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app)
        return;

    app->setPalette(buildPalette(g_colors));
    app->setStyleSheet(buildStyleSheet(g_colors, dark));
}

const Colors& colors()
{
    return g_colors;
}

QString styleSheet()
{
    return buildStyleSheet(g_colors, g_dark);
}

QColor canvasColor() { return g_colors.canvas; }
QColor gridColor() { return g_colors.grid; }
QColor iconColor() { return g_colors.windowText; }
QColor mutedText() { return g_colors.muted; }

} // namespace theme
} // namespace tdms
