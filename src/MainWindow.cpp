// ---------------------------------------------------------------------------
// MainWindow.cpp
// ---------------------------------------------------------------------------
#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "core/TimeUtil.h"
#include "ui/ExportDialog.h"
#include "ui/Icons.h"
#include "ui/ImportDialog.h"
#include "ui/MergeDialog.h"
#include "ui/Plot3DPanel.h"
#include "ui/PlotPanel.h"
#include "ui/PropertyPanel.h"
#include "ui/SettingsDialog.h"
#include "ui/Theme.h"

namespace tdms {

namespace {

constexpr int kRoleKind = Qt::UserRole;
constexpr int kRoleGroup = Qt::UserRole + 1;
constexpr int kRoleChannel = Qt::UserRole + 2;
constexpr int kRoleKey = Qt::UserRole + 3;
constexpr int kRoleExternal = Qt::UserRole + 4;

constexpr int kKindExternalSeries = -1;
constexpr int kKindExternalRoot = -2;

const QColor kCurveColors[] = {
    QColor("#4ea1ff"), QColor("#ffb74d"), QColor("#66bb6a"), QColor("#ef5350"),
    QColor("#ba68c8"), QColor("#26c6da"), QColor("#ffd54f"), QColor("#8d6e63"),
    QColor("#90a4ae"), QColor("#f06292")
};
constexpr int kCurveColorCount = int(sizeof(kCurveColors) / sizeof(kCurveColors[0]));

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("TDMS Viewer"));
    resize(1440, 900);

    m_iconsDark = theme::isDark();

    createActions();
    createMenus();
    createToolBar();
    createCentralWidgets();

    m_statusFile = new QLabel(tr("未打开文件"), this);
    m_statusSelection = new QLabel(QString(), this);
    m_statusCursor = new QLabel(QString(), this);
    m_statusCursor->setMinimumWidth(230);
    statusBar()->addWidget(m_statusFile, 1);
    statusBar()->addPermanentWidget(m_statusSelection);
    statusBar()->addPermanentWidget(m_statusCursor);

    updateTitle();
    updateActions();
    updateStatus();
    showStatus(tr("就绪：打开或新建 TDM / TDMS 文件开始工作。"));

    connect(&Settings::instance(), &Settings::changed, this, &MainWindow::applySettings);

    // Initial sync (tab visibility, axis menu checks, legend, ...).
    applySettings();

    // Follow the OS colour scheme while "跟随系统" is selected.
    if (QGuiApplication* gui = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(gui->styleHints(), &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme) {
                    if (Settings::instance().values().theme == ThemeMode::System)
                        applySettings();
                });
    }
}

MainWindow::~MainWindow() = default;

// ===========================================================================
// UI construction
// ===========================================================================
void MainWindow::createActions()
{
    auto makeAction = [this](const QString& text, const QString& iconName,
                             const QKeySequence& shortcut, auto&& slot) {
        auto* action = new QAction(text, this);
        if (!iconName.isEmpty()) {
            action->setIcon(icons::get(iconName));
            m_iconBindings.append(qMakePair(action, iconName));
        }
        if (!shortcut.isEmpty())
            action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    auto* actNew = makeAction(tr("新建文件…"), QStringLiteral("new"), QKeySequence::New,
                              [this] { newFile(); });
    auto* actOpen = makeAction(tr("打开文件…"), QStringLiteral("open"), QKeySequence::Open,
                               [this] { openFile(); });
    m_actSave = makeAction(tr("保存"), QStringLiteral("save"), QKeySequence::Save,
                           [this] { saveFile(); });
    m_actSaveAs = makeAction(tr("另存为…"), QStringLiteral("save-as"), QKeySequence::SaveAs,
                             [this] { saveFileAs(); });
    m_actClose = makeAction(tr("关闭文件"), QStringLiteral("close"), QKeySequence::Close,
                            [this] { closeFile(); });

    m_actImport = makeAction(tr("导入外部数据…"), QStringLiteral("import"),
                             QKeySequence(QStringLiteral("Ctrl+I")),
                             [this] { importExternalData(); });
    m_actMerge = makeAction(tr("合并 TDM/TDMS 文件…"), QStringLiteral("merge"),
                            QKeySequence(QStringLiteral("Ctrl+M")),
                            [this] { mergeFilesDialog(); });
    m_actExport = makeAction(tr("导出 TXT / CSV…"), QStringLiteral("export"),
                             QKeySequence(QStringLiteral("Ctrl+E")),
                             [this] { exportData(); });
    auto* actQuit = makeAction(tr("退出"), QStringLiteral("quit"),
                               QKeySequence(QStringLiteral("Ctrl+Q")),
                               [this] { close(); });

    m_actAddGroup = makeAction(tr("添加分组"), QStringLiteral("add-group"),
                               QKeySequence(QStringLiteral("Ctrl+G")),
                               [this] { addGroup(); });
    m_actRename = makeAction(tr("重命名"), QStringLiteral("rename"), QKeySequence(Qt::Key_F2),
                             [this] { renameSelection(); });
    m_actDelete = makeAction(tr("删除所选"), QStringLiteral("delete"), QKeySequence::Delete,
                             [this] { deleteSelection(); });
    m_actRefresh = makeAction(tr("刷新属性"), QStringLiteral("refresh"), QKeySequence(Qt::Key_F5),
                              [this] { updateProperties(); });

    auto* actFit = makeAction(tr("自动缩放"), QStringLiteral("fit"),
                              QKeySequence(QStringLiteral("Ctrl+0")),
                              [this] { m_plot->autoScale(); });

    m_actSettings = makeAction(tr("首选项…"), QStringLiteral("settings"),
                               QKeySequence(QStringLiteral("Ctrl+,")),
                               [this] { openSettings(); });
    m_actRestoreDefaults = makeAction(
        tr("恢复默认设置"), QStringLiteral("reset"), QKeySequence(), [this] {
            Settings::instance().resetToDefaults();
            showStatus(tr("已恢复默认设置。"));
        });

    m_themeGroup = new QActionGroup(this);
    m_themeGroup->setExclusive(true);
    auto makeThemeAction = [this](const QString& text, const QString& iconName, ThemeMode mode) {
        auto* action = new QAction(text, this);
        action->setCheckable(true);
        action->setIcon(icons::get(iconName));
        m_iconBindings.append(qMakePair(action, iconName));
        m_themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode] { setTheme(mode); });
        return action;
    };
    m_actThemeSystem = makeThemeAction(tr("跟随系统"), QStringLiteral("monitor"), ThemeMode::System);
    m_actThemeLight = makeThemeAction(tr("浅色（正常颜色）"), QStringLiteral("sun"), ThemeMode::Light);
    m_actThemeDark = makeThemeAction(tr("深色"), QStringLiteral("moon"), ThemeMode::Dark);

    auto* actAbout = makeAction(tr("关于"), QStringLiteral("info"), QKeySequence(), [this] {
        QMessageBox::about(this, tr("关于 TDMS Viewer"),
            tr("<h3>TDMS Viewer 1.1</h3>"
               "<p>基于 Qt %1 与 NI TDM C DLL 构建的 TDM / TDMS 浏览与编辑工具。</p>"
               "<p>支持树形结构浏览、属性编辑、QWT 多通道曲线（绝对 / 相对时间轴）、"
               "TXT/CSV 导入导出、以及两个 TDM/TDMS 文件的合并。</p>"
               "<p style='color:#8b98ab'>TDM 库版权归 National Instruments 所有。</p>")
                .arg(QString::fromLatin1(qVersion())));
    });

    // keep them owned by the window
    actNew->setObjectName(QStringLiteral("actNew"));
    actOpen->setObjectName(QStringLiteral("actOpen"));
    actQuit->setObjectName(QStringLiteral("actQuit"));
    actFit->setObjectName(QStringLiteral("actFit"));
    actAbout->setObjectName(QStringLiteral("actAbout"));

    m_actSave->setObjectName(QStringLiteral("actSave"));
    m_actSaveAs->setObjectName(QStringLiteral("actSaveAs"));
    m_actClose->setObjectName(QStringLiteral("actClose"));
    m_actImport->setObjectName(QStringLiteral("actImport"));
    m_actMerge->setObjectName(QStringLiteral("actMerge"));
    m_actExport->setObjectName(QStringLiteral("actExport"));
    m_actAddGroup->setObjectName(QStringLiteral("actAddGroup"));
    m_actRename->setObjectName(QStringLiteral("actRename"));
    m_actDelete->setObjectName(QStringLiteral("actDelete"));
    m_actRefresh->setObjectName(QStringLiteral("actRefresh"));
    m_actSettings->setObjectName(QStringLiteral("actSettings"));
}

void MainWindow::createMenus()
{
    auto* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(findChild<QAction*>(QStringLiteral("actNew")));
    fileMenu->addAction(findChild<QAction*>(QStringLiteral("actOpen")));
    fileMenu->addSeparator();
    fileMenu->addAction(m_actSave);
    fileMenu->addAction(m_actSaveAs);
    fileMenu->addAction(m_actClose);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actImport);
    fileMenu->addAction(m_actMerge);
    fileMenu->addAction(m_actExport);
    fileMenu->addSeparator();
    fileMenu->addAction(findChild<QAction*>(QStringLiteral("actQuit")));

    auto* editMenu = menuBar()->addMenu(tr("编辑(&E)"));
    editMenu->addAction(m_actAddGroup);
    editMenu->addAction(m_actRename);
    editMenu->addAction(m_actDelete);
    editMenu->addSeparator();
    editMenu->addAction(m_actRefresh);

    auto* viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    viewMenu->addAction(findChild<QAction*>(QStringLiteral("actFit")));
    viewMenu->addSeparator();

    auto* axisMenu = viewMenu->addMenu(icons::get(QStringLiteral("axes")), tr("坐标轴"));
    const QStringList axisTitles = { tr("下轴（X）"), tr("上轴（X）"),
                                     tr("左轴（Y）"), tr("右轴（Y）") };
    for (int i = 0; i < axisTitles.size(); ++i) {
        auto* action = axisMenu->addAction(axisTitles.at(i));
        action->setCheckable(true);
        connect(action, &QAction::toggled, this, [this, i](bool on) {
            AppSettings values = Settings::instance().values();
            switch (i) {
            case 0: values.showAxisBottom = on; break;
            case 1: values.showAxisTop = on; break;
            case 2: values.showAxisLeft = on; break;
            default: values.showAxisRight = on; break;
            }
            Settings::instance().setValues(values);
        });
        m_axisActions.append(action);
    }

    viewMenu->addSeparator();
    auto* show3D = viewMenu->addAction(icons::get(QStringLiteral("cube")), tr("显示三维视图"));
    show3D->setCheckable(true);
    show3D->setChecked(Settings::instance().values().show3DView);
    connect(show3D, &QAction::toggled, this, [this](bool on) {
        AppSettings values = Settings::instance().values();
        values.show3DView = on;
        Settings::instance().setValues(values);
    });

    auto* settingsMenu = menuBar()->addMenu(tr("设置(&S)"));
    settingsMenu->addAction(m_actSettings);
    settingsMenu->addSeparator();
    auto* themeMenu = settingsMenu->addMenu(icons::get(QStringLiteral("sun")), tr("主题"));
    themeMenu->addAction(m_actThemeSystem);
    themeMenu->addAction(m_actThemeLight);
    themeMenu->addAction(m_actThemeDark);
    settingsMenu->addSeparator();
    settingsMenu->addAction(m_actRestoreDefaults);

    auto* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(findChild<QAction*>(QStringLiteral("actAbout")));

    updateThemeMenu();
}

void MainWindow::createToolBar()
{
    auto* toolbar = addToolBar(tr("主工具栏"));
    toolbar->setObjectName(QStringLiteral("mainToolBar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(24, 24));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setToolTip(tr("主工具栏"));
    toolbar->addAction(findChild<QAction*>(QStringLiteral("actNew")));
    toolbar->addAction(findChild<QAction*>(QStringLiteral("actOpen")));
    toolbar->addAction(m_actSave);
    toolbar->addSeparator();
    toolbar->addAction(m_actImport);
    toolbar->addAction(m_actMerge);
    toolbar->addAction(m_actExport);

    // The settings entry sits at the far right of the toolbar.
    auto* spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    auto* settingsButton = new QToolButton(toolbar);
    settingsButton->setDefaultAction(m_actSettings);
    settingsButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    settingsButton->setToolTip(tr("首选项（Ctrl+,）"));
    toolbar->addWidget(settingsButton);
}

void MainWindow::createCentralWidgets()
{
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({ tr("文件结构"), tr("信息") });
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->resizeSection(1, 118);
    m_tree->setMinimumWidth(300);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setIndentation(12);

    m_plot = new PlotPanel(this);
    m_plot3D = new Plot3DPanel(this);
    m_plotTabs = new QTabWidget(this);
    m_plotTabs->setObjectName(QStringLiteral("plotTabs"));
    m_plotTabs->setDocumentMode(true);
    m_plotTabs->addTab(m_plot, icons::get(QStringLiteral("chart")), tr("二维曲线"));
    m_plotTabs->addTab(m_plot3D, icons::get(QStringLiteral("cube")), tr("三维视图"));
    m_props = new PropertyPanel(this);

    auto* leftPanel = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    auto* treeHeader = new QLabel(tr("TDM / TDMS 结构"), leftPanel);
    treeHeader->setObjectName(QStringLiteral("treeHeader"));
    treeHeader->setFixedHeight(30);
    leftLayout->addWidget(treeHeader);
    leftLayout->addWidget(m_tree, 1);

    auto* rightSplitter = new QSplitter(Qt::Vertical, this);
    rightSplitter->addWidget(m_plotTabs);
    rightSplitter->addWidget(m_props);
    rightSplitter->setStretchFactor(0, 3);
    rightSplitter->setStretchFactor(1, 1);
    rightSplitter->setSizes({ 600, 300 });

    auto* mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->addWidget(leftPanel);
    mainSplitter->addWidget(rightSplitter);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({ 420, 1020 });
    setCentralWidget(mainSplitter);

    connect(m_tree, &QTreeWidget::currentItemChanged, this, &MainWindow::onCurrentItemChanged);
    connect(m_tree, &QTreeWidget::itemChanged, this, &MainWindow::onItemChanged);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &MainWindow::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction(m_actAddGroup);
        menu.addAction(m_actImport);
        menu.addSeparator();
        menu.addAction(m_actRename);
        menu.addAction(m_actDelete);
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
    });

    connect(m_props, &PropertyPanel::valueEdited, this, &MainWindow::onPropertyEdited);
    connect(m_props, &PropertyPanel::createRequested, this, &MainWindow::onPropertyCreated);
    connect(m_props, &PropertyPanel::refreshRequested, this, &MainWindow::updateProperties);
    connect(m_props, &PropertyPanel::statusMessage, this, &MainWindow::showStatus);
    connect(m_plot, &PlotPanel::cursorMoved, this, [this](const QString& text) {
        m_statusCursor->setText(text);
    });
    connect(m_plot3D, &Plot3DPanel::cursorMoved, this, [this](const QString& text) {
        m_statusCursor->setText(text);
    });
}

// ===========================================================================
// Settings / theme
// ===========================================================================
void MainWindow::openSettings()
{
    SettingsDialog dialog(this);
    dialog.exec();
}

void MainWindow::setTheme(ThemeMode mode)
{
    AppSettings values = Settings::instance().values();
    values.theme = mode;
    Settings::instance().setValues(values);
    updateThemeMenu();
}

void MainWindow::updateThemeMenu()
{
    const ThemeMode mode = Settings::instance().values().theme;
    if (m_actThemeSystem) {
        const QSignalBlocker blocker(m_actThemeSystem);
        m_actThemeSystem->setChecked(mode == ThemeMode::System);
    }
    if (m_actThemeLight) {
        const QSignalBlocker blocker(m_actThemeLight);
        m_actThemeLight->setChecked(mode == ThemeMode::Light);
    }
    if (m_actThemeDark) {
        const QSignalBlocker blocker(m_actThemeDark);
        m_actThemeDark->setChecked(mode == ThemeMode::Dark);
    }
}

void MainWindow::refreshIcons()
{
    for (const auto& binding : m_iconBindings)
        binding.first->setIcon(icons::get(binding.second));
}

void MainWindow::applySettings()
{
    const AppSettings& values = Settings::instance().values();

    theme::apply(values.theme);
    if (theme::isDark() != m_iconsDark) {
        m_iconsDark = theme::isDark();
        refreshIcons();
    }

    m_props->refreshTheme();

    // Show / hide the 3D tab (index 1) without touching the 2D view state.
    if (m_plotTabs) {
        const int index = m_plotTabs->indexOf(m_plot3D);
        const bool present = index >= 0;
        if (values.show3DView && !present)
            m_plotTabs->addTab(m_plot3D, icons::get(QStringLiteral("cube")), tr("三维视图"));
        else if (!values.show3DView && present) {
            m_plotTabs->removeTab(index);
            if (m_plotTabs->count() > 0)
                m_plotTabs->setCurrentIndex(0);
        }
    }

    updateAxisActions();

    // Rebuilding the series resets the zoom, so only do it when the X axis
    // changed: grid / legend / line width are applied to the existing curves.
    const TimeAxisMode previousMode = m_plot->timeAxisMode();
    m_plot->applySettings();
    m_plot3D->applySettings();
    if (previousMode != values.xAxisMode)
        refreshPlot();
    else
        updateStatus();

    updateThemeMenu();
}

void MainWindow::updateAxisActions()
{
    if (m_axisActions.size() != 4)
        return;
    const AppSettings& values = Settings::instance().values();
    const bool states[4] = { values.showAxisBottom, values.showAxisTop,
                             values.showAxisLeft, values.showAxisRight };
    for (int i = 0; i < 4; ++i) {
        const QSignalBlocker blocker(m_axisActions.at(i));
        m_axisActions.at(i)->setChecked(states[i]);
    }
}

// ===========================================================================
// Document handling
// ===========================================================================
void MainWindow::newFile()
{
    if (!maybeSave())
        return;

    const QString chosen = QFileDialog::getSaveFileName(
        this, tr("新建 TDM / TDMS 文件"),
        QDir::homePath() + QStringLiteral("/new_data.tdms"),
        tr("TDMS 流文件 (*.tdms);;TDM 文件 (*.tdm)"));
    if (chosen.isEmpty())
        return;

    QString path = chosen;
    if (!path.endsWith(QLatin1String(".tdm"), Qt::CaseInsensitive)
        && !path.endsWith(QLatin1String(".tdms"), Qt::CaseInsensitive))
        path += QStringLiteral(".tdms");
    const QString type = path.endsWith(QLatin1String(".tdm"), Qt::CaseInsensitive)
        ? QStringLiteral("TDM")
        : QStringLiteral("TDMS");

    QString error;
    if (!m_doc.create(path, type, &error)) {
        QMessageBox::critical(this, tr("创建失败"), error);
        return;
    }

    clearCache();
    m_checkedKeys.clear();
    m_external.clear();
    m_selKind = ObjKind::File;
    m_selGroup = m_selChannel = -1;
    m_selExternal = -1;
    rebuildTree();
    m_plot->clear();
    updateProperties();
    updateTitle();
    updateActions();
    updateStatus();
    showStatus(tr("已创建 %1 文件：%2").arg(type, QFileInfo(path).fileName()));
}

void MainWindow::openFile()
{
    if (!maybeSave())
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开 TDM / TDMS 文件"), QString(),
        tr("TDM / TDMS 文件 (*.tdm *.tdms);;TDM 文件 (*.tdm);;TDMS 文件 (*.tdms);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    openDocument(path);
}

bool MainWindow::openDocument(const QString& path)
{
    QString error;
    if (!m_doc.open(path, false, &error)) {
        QMessageBox::critical(this, tr("打开失败"), error);
        return false;
    }

    clearCache();
    m_checkedKeys.clear();
    m_external.clear();
    m_selKind = ObjKind::File;
    m_selGroup = m_selChannel = -1;
    m_selExternal = -1;

    rebuildTree();
    m_plot->clear();

    // Out-of-the-box convenience: plot the first plottable channels of the first group.
    if (!m_doc.groups().isEmpty()) {
        const GroupInfo& firstGroup = m_doc.groups().first();
        int added = 0;
        for (int c = 0; c < firstGroup.channels.size() && added < 3; ++c) {
            const ChannelInfo& ch = firstGroup.channels.at(c);
            if (ch.plottable() && ch.count > 0) {
                m_checkedKeys.insert(channelKey(0, c));
                ++added;
            }
        }
        if (added > 0) {
            rebuildTree();
            refreshPlot();
        }
    }

    updateProperties();
    updateTitle();
    updateActions();
    updateStatus();

    if (m_doc.isReadOnly())
        showStatus(tr("文件以只读方式打开（磁盘只读或被占用），属性修改与保存不可用。"));
    else
        showStatus(tr("已打开 %1").arg(QFileInfo(path).fileName()));
    return true;
}

void MainWindow::openPath(const QString& path)
{
    if (path.isEmpty())
        return;
    if (!maybeSave())
        return;
    openDocument(path);
}

void MainWindow::closeFile()
{
    if (!maybeSave())
        return;
    m_doc.close();
    clearCache();
    m_checkedKeys.clear();
    m_external.clear();
    m_selKind = ObjKind::File;
    m_selGroup = m_selChannel = -1;
    m_selExternal = -1;
    m_tree->clear();
    m_plot->clear();
    m_props->clearContext(tr("选择左侧的文件、分组或通道以查看属性。"));
    updateTitle();
    updateActions();
    updateStatus();
}

bool MainWindow::saveFile()
{
    if (!m_doc.isOpen())
        return false;
    QString error;
    if (!m_doc.save(&error)) {
        QMessageBox::critical(this, tr("保存失败"), error);
        return false;
    }
    updateTitle();
    showStatus(tr("已保存：%1").arg(QFileInfo(m_doc.path()).fileName()));
    return true;
}

bool MainWindow::saveFileAs()
{
    if (!m_doc.isOpen())
        return false;
    const QString suffix = m_doc.fileType().compare(QLatin1String("TDM"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("tdm")
        : QStringLiteral("tdms");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("另存为"), m_doc.path(),
        tr("TDM 文件 (*.tdm);;TDMS 文件 (*.tdms)"));
    if (path.isEmpty())
        return false;

    QString error;
    if (!m_doc.saveAs(path, &error)) {
        QMessageBox::critical(this, tr("另存为失败"), error);
        return false;
    }
    Q_UNUSED(suffix);
    updateTitle();
    showStatus(tr("已另存为：%1").arg(QFileInfo(path).fileName()));
    return true;
}

bool MainWindow::maybeSave()
{
    // Nothing open, or open without pending edits: closing is silent.
    // (Without the isDirty() check every close asked "save changes?" even for a
    // freshly opened file, which also blocked automated screenshot runs.)
    if (!m_doc.isOpen() || !m_doc.isDirty())
        return true;

    const QString name = QFileInfo(m_doc.path()).fileName();
    const auto answer = QMessageBox::question(
        this, tr("未保存的修改"),
        tr("“%1”有未保存的修改，是否保存？").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (answer == QMessageBox::Save)
        return saveFile();
    if (answer == QMessageBox::Cancel)
        return false;
    return true;
}

void MainWindow::markDirty()
{
    m_doc.setDirty(true);
    updateTitle();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    event->accept();
}

// ===========================================================================
// Tree
// ===========================================================================
QString MainWindow::channelKey(int group, int channel) const
{
    return QStringLiteral("%1:%2").arg(group).arg(channel);
}

void MainWindow::rebuildTree()
{
    const ObjKind keepKind = m_selKind;
    const int keepGroup = m_selGroup;
    const int keepChannel = m_selChannel;
    const int keepExternal = m_selExternal;

    m_updatingTree = true;
    QTreeWidgetItem* selectItem = nullptr;
    {
        const QSignalBlocker blocker(m_tree);
        m_tree->clear();

        if (m_doc.isOpen()) {
            auto* fileItem = new QTreeWidgetItem(m_tree);
            fileItem->setText(0, QFileInfo(m_doc.path()).fileName());
            fileItem->setText(1, tr("%1 · %2 组").arg(m_doc.fileType()).arg(m_doc.groups().size()));
            fileItem->setIcon(0, style()->standardIcon(QStyle::SP_DriveHDIcon));
            fileItem->setData(0, kRoleKind, int(ObjKind::File));
            QFont bold = fileItem->font(0);
            bold.setBold(true);
            fileItem->setFont(0, bold);
            if (keepKind == ObjKind::File && keepExternal < 0)
                selectItem = fileItem;

            for (int g = 0; g < m_doc.groups().size(); ++g) {
                const GroupInfo& group = m_doc.groups().at(g);
                auto* groupItem = new QTreeWidgetItem(fileItem);
                groupItem->setText(0, group.name);
                groupItem->setText(1, tr("%1 个通道").arg(group.channels.size()));
                groupItem->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
                groupItem->setExpanded(true);
                groupItem->setData(0, kRoleKind, int(ObjKind::Group));
                groupItem->setData(0, kRoleGroup, g);
                groupItem->setToolTip(0, group.name);
                if (keepKind == ObjKind::Group && keepGroup == g && keepExternal < 0)
                    selectItem = groupItem;

                for (int c = 0; c < group.channels.size(); ++c) {
                    const ChannelInfo& ch = group.channels.at(c);
                    auto* channelItem = new QTreeWidgetItem(groupItem);
                    channelItem->setText(0, ch.name);
                    QString info = dataTypeName(ch.type);
                    if (ch.count > 0)
                        info += QStringLiteral(" · ") + tr("%1 点").arg(ch.count);
                    if (!ch.unit.isEmpty())
                        info += QStringLiteral(" · ") + ch.unit;
                    channelItem->setText(1, info);
                    channelItem->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
                    channelItem->setData(0, kRoleKind, int(ObjKind::Channel));
                    channelItem->setData(0, kRoleGroup, g);
                    channelItem->setData(0, kRoleChannel, c);
                    channelItem->setData(0, kRoleKey, channelKey(g, c));
                    QString tip = QStringLiteral("%1 / %2\n%3 · %4 点")
                                      .arg(group.name, ch.name, dataTypeName(ch.type))
                                      .arg(ch.count);
                    if (ch.hasTimeInfo()) {
                        tip += QStringLiteral("\n起始 %1 · 间隔 %2")
                                   .arg(ch.startTime.isValid()
                                            ? ch.startTime.toString(
                                                  QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                                            : tr("未知"),
                                        formatSampleInterval(ch.increment));
                    } else {
                        tip += QStringLiteral("\n该通道没有时间信息（wf_increment）");
                    }
                    channelItem->setToolTip(0, tip);

                    if (ch.plottable()) {
                        channelItem->setFlags(channelItem->flags() | Qt::ItemIsUserCheckable);
                        channelItem->setCheckState(
                            0, m_checkedKeys.contains(channelKey(g, c)) ? Qt::Checked : Qt::Unchecked);
                    }
                    if (keepKind == ObjKind::Channel && keepGroup == g && keepChannel == c
                        && keepExternal < 0)
                        selectItem = channelItem;
                }
            }
            fileItem->setExpanded(true);
        }

        if (!m_external.isEmpty()) {
            auto* extRoot = new QTreeWidgetItem(m_tree);
            extRoot->setText(0, tr("外部数据（未保存）"));
            extRoot->setText(1, tr("%1 条曲线").arg(m_external.size()));
            extRoot->setIcon(0, style()->standardIcon(QStyle::SP_DirLinkIcon));
            extRoot->setData(0, kRoleKind, kKindExternalRoot);
            if (keepExternal == -2)
                selectItem = extRoot;

            for (int i = 0; i < m_external.size(); ++i) {
                const ExternalSeries& s = m_external.at(i);
                auto* item = new QTreeWidgetItem(extRoot);
                item->setText(0, s.name);
                item->setText(1, tr("%1 点").arg(s.y.size()));
                item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
                item->setData(0, kRoleKind, kKindExternalSeries);
                item->setData(0, kRoleExternal, i);
                const QString key = QStringLiteral("ext:%1").arg(i);
                item->setData(0, kRoleKey, key);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(0, m_checkedKeys.contains(key) ? Qt::Checked : Qt::Unchecked);
                if (keepExternal == i)
                    selectItem = item;
            }
            extRoot->setExpanded(true);
        }

        if (selectItem)
            m_tree->setCurrentItem(selectItem);
    }
    m_updatingTree = false;

    if (selectItem) {
        // refresh selection state after the tree was rebuilt
        onCurrentItemChanged(selectItem, nullptr);
    }
}

void MainWindow::onCurrentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous)
{
    Q_UNUSED(previous);
    if (!current) {
        m_selKind = ObjKind::File;
        m_selGroup = m_selChannel = -1;
        m_selExternal = -1;
        updateProperties();
        updateActions();
        updateStatus();
        return;
    }

    const int kind = current->data(0, kRoleKind).toInt();
    if (kind >= 0) {
        m_selKind = ObjKind(kind);
        m_selGroup = current->data(0, kRoleGroup).toInt();
        m_selChannel = current->data(0, kRoleChannel).toInt();
        m_selExternal = -1;
    } else {
        m_selKind = ObjKind::File;
        m_selGroup = m_selChannel = -1;
        m_selExternal = (kind == kKindExternalSeries) ? current->data(0, kRoleExternal).toInt()
                                                     : kKindExternalRoot;
    }

    updateProperties();
    updateActions();
    updateStatus();
}

void MainWindow::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_updatingTree || !item || column != 0)
        return;

    const QString key = item->data(0, kRoleKey).toString();
    if (key.isEmpty())
        return;

    const int kind = item->data(0, kRoleKind).toInt();
    if (kind != int(ObjKind::Channel) && kind != kKindExternalSeries)
        return;

    if (item->checkState(0) == Qt::Checked)
        m_checkedKeys.insert(key);
    else
        m_checkedKeys.remove(key);
    refreshPlot();
}

void MainWindow::onItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    if (!item)
        return;
    const int kind = item->data(0, kRoleKind).toInt();
    if (kind != int(ObjKind::Channel) && kind != kKindExternalSeries)
        return;

    const bool checked = item->checkState(0) == Qt::Checked;
    item->setCheckState(0, checked ? Qt::Unchecked : Qt::Checked);
}

void MainWindow::updateProperties()
{
    if (!m_doc.isOpen()) {
        m_props->clearContext(tr("尚未打开文件。请通过“文件 → 打开”载入 TDM / TDMS 文件。"));
        return;
    }
    if (m_selExternal >= 0) {
        m_props->clearContext(tr("外部数据为临时曲线，未被写入任何文件，因此没有 TDM 属性。"));
        return;
    }
    if (m_selExternal == kKindExternalRoot) {
        m_props->clearContext(tr("外部数据为临时曲线，可勾选后在右侧绘图区显示。"));
        return;
    }

    QString title;
    QVector<PropEntry> props;
    switch (m_selKind) {
    case ObjKind::File:
        title = tr("文件 · %1").arg(QFileInfo(m_doc.path()).fileName());
        props = m_doc.properties(ObjKind::File, -1, -1);
        break;
    case ObjKind::Group: {
        if (m_selGroup < 0 || m_selGroup >= m_doc.groups().size()) {
            m_props->clearContext(tr("选择左侧的文件、分组或通道以查看属性。"));
            return;
        }
        const GroupInfo& g = m_doc.groups().at(m_selGroup);
        title = tr("分组 · %1").arg(g.name);
        props = m_doc.properties(ObjKind::Group, m_selGroup, -1);
        break;
    }
    case ObjKind::Channel: {
        if (m_selGroup < 0 || m_selGroup >= m_doc.groups().size()) {
            m_props->clearContext(tr("选择左侧的文件、分组或通道以查看属性。"));
            return;
        }
        const GroupInfo& g = m_doc.groups().at(m_selGroup);
        if (m_selChannel < 0 || m_selChannel >= g.channels.size()) {
            m_props->clearContext(tr("选择左侧的文件、分组或通道以查看属性。"));
            return;
        }
        const ChannelInfo& c = g.channels.at(m_selChannel);
        title = tr("通道 · %1 / %2").arg(g.name, c.name);
        props = m_doc.properties(ObjKind::Channel, m_selGroup, m_selChannel);
        break;
    }
    }

    m_props->setContext(title, m_selKind, m_selGroup, m_selChannel, props, m_doc.isReadOnly());
}

// ===========================================================================
// Plot
// ===========================================================================
QVector<double> MainWindow::loadChannelValues(int group, int channel, bool* ok)
{
    *ok = false;
    const QString key = channelKey(group, channel);
    auto it = m_valueCache.constFind(key);
    if (it != m_valueCache.constEnd()) {
        *ok = true;
        return it.value();
    }

    QVector<double> values;
    QStringList labels;
    QString error;
    if (!m_doc.readChannel(group, channel, &values, &labels, &error)) {
        showStatus(error);
        return {};
    }
    m_valueCache.insert(key, values);
    *ok = true;
    return values;
}

void MainWindow::clearCache()
{
    m_valueCache.clear();
}

void MainWindow::refreshPlot()
{
    const AppSettings settings = Settings::instance().values();
    const TimeAxisMode mode = settings.xAxisMode;
    const bool hasDocument = m_doc.isOpen();

    static const QVector<GroupInfo> noGroups;
    const QVector<GroupInfo>& groups = hasDocument ? m_doc.groups() : noGroups;

    // Channels that are checked and can be drawn.
    struct Picked {
        int group;
        int channel;
    };
    QVector<Picked> picked;
    for (int g = 0; g < groups.size(); ++g) {
        const GroupInfo& group = groups.at(g);
        for (int c = 0; c < group.channels.size(); ++c) {
            if (!m_checkedKeys.contains(channelKey(g, c)))
                continue;
            if (!group.channels.at(c).plottable())
                continue;
            picked.append(Picked{ g, c });
        }
    }

    // Reference instant for relative time: the earliest start time of the
    // plotted channels (falls back to the file creation time).
    QDateTime origin = hasDocument ? m_doc.fileDateTime() : QDateTime();
    for (const Picked& item : picked) {
        const ChannelInfo& ch = groups.at(item.group).channels.at(item.channel);
        if (!ch.startTime.isValid())
            continue;
        if (!origin.isValid() || ch.startTime < origin)
            origin = ch.startTime;
    }
    m_plot->setTimeOrigin(mode == TimeAxisMode::Relative ? origin : QDateTime());

    // Builds the X values in the units expected by the current axis mode:
    // seconds for a relative axis, msecs since the epoch for an absolute one.
    auto buildX = [&](int count, const QDateTime& start, double increment,
                      double startOffset) -> QVector<double> {
        QVector<double> x(count);
        switch (mode) {
        case TimeAxisMode::Relative: {
            double base = 0.0;
            if (start.isValid() && origin.isValid())
                base = double(origin.msecsTo(start)) / 1000.0;
            for (int i = 0; i < count; ++i)
                x[i] = base + startOffset + i * increment;
            break;
        }
        case TimeAxisMode::Absolute: {
            qint64 baseMs = 0;
            if (start.isValid())
                baseMs = start.toMSecsSinceEpoch();
            else if (origin.isValid())
                baseMs = origin.toMSecsSinceEpoch();
            else
                baseMs = QDateTime::currentDateTime().toMSecsSinceEpoch();
            for (int i = 0; i < count; ++i)
                x[i] = double(baseMs) + (startOffset + i * increment) * 1000.0;
            break;
        }
        case TimeAxisMode::SampleIndex:
            for (int i = 0; i < count; ++i)
                x[i] = i;
            break;
        }
        return x;
    };

    QVector<PlotSeries> series;
    int colorIndex = 0;
    bool assumedTiming = false;

    for (const Picked& item : picked) {
        const GroupInfo& group = groups.at(item.group);
        const ChannelInfo& ch = group.channels.at(item.channel);

        bool ok = false;
        const QVector<double> values = loadChannelValues(item.group, item.channel, &ok);
        if (!ok || values.isEmpty())
            continue;

        PlotSeries s;
        s.key = channelKey(item.group, item.channel);
        s.unit = ch.unit;
        s.title = ch.unit.isEmpty()
            ? QStringLiteral("%1 / %2").arg(group.name, ch.name)
            : QStringLiteral("%1 / %2 [%3]").arg(group.name, ch.name, ch.unit);
        s.y = values;
        s.color = kCurveColors[colorIndex++ % kCurveColorCount];

        double increment = ch.increment;
        double offset = ch.startOffset;
        if (!(increment > 0.0)) {
            if (settings.assumeUnitSampleInterval) {
                increment = 1.0;
                offset = 0.0;
                assumedTiming = true;
            }
        }

        if (increment > 0.0)
            s.x = buildX(values.size(), ch.startTime, increment, offset);

        series.append(s);
    }

    for (int i = 0; i < m_external.size(); ++i) {
        const QString key = QStringLiteral("ext:%1").arg(i);
        if (!m_checkedKeys.contains(key))
            continue;
        const ExternalSeries& src = m_external.at(i);
        if (src.y.isEmpty())
            continue;

        PlotSeries s;
        s.key = key;
        s.unit = src.unit;
        s.title = src.unit.isEmpty()
            ? QStringLiteral("%1 (外部)").arg(src.name)
            : QStringLiteral("%1 [%2] (外部)").arg(src.name, src.unit);
        s.y = src.y;
        s.color = kCurveColors[colorIndex++ % kCurveColorCount];

        // An explicit X column is interpreted as elapsed seconds; without one
        // the samples are spread 1 s apart starting at the reference instant.
        if (!src.x.isEmpty() && src.x.size() == src.y.size()) {
            if (mode == TimeAxisMode::Absolute) {
                const qint64 baseMs = origin.isValid()
                    ? origin.toMSecsSinceEpoch()
                    : QDateTime::currentDateTime().toMSecsSinceEpoch();
                s.x.resize(src.x.size());
                for (int k = 0; k < src.x.size(); ++k)
                    s.x[k] = double(baseMs) + src.x.at(k) * 1000.0;
            } else {
                s.x = src.x;
            }
        } else {
            s.x = buildX(src.y.size(), QDateTime(), 1.0, 0.0);
        }

        series.append(s);
    }

    m_plot->setSeries(series);

    // Keep the 3D view in sync (it shares the same X values).
    QString xTitle = tr("采样点序号");
    if (mode == TimeAxisMode::Relative)
        xTitle = tr("相对时间 (s)");
    else if (mode == TimeAxisMode::Absolute)
        xTitle = tr("绝对时间");
    m_plot3D->setSeries(series, xTitle);

    updateStatus();

    if (assumedTiming && mode != TimeAxisMode::SampleIndex)
        showStatus(tr("部分通道没有 wf_increment 属性，已按 1 s 采样间隔估算时间轴。"));
}

// ===========================================================================
// Status / actions
// ===========================================================================
QString MainWindow::selectedPathText() const
{
    if (!m_doc.isOpen())
        return QString();
    if (m_selExternal >= 0 && m_selExternal < m_external.size())
        return tr("外部数据 · %1").arg(m_external.at(m_selExternal).name);
    if (m_selExternal == kKindExternalRoot)
        return tr("外部数据");
    switch (m_selKind) {
    case ObjKind::File:
        return tr("文件");
    case ObjKind::Group:
        return m_selGroup < m_doc.groups().size() ? m_doc.groups().at(m_selGroup).name : QString();
    case ObjKind::Channel: {
        if (m_selGroup >= m_doc.groups().size())
            return QString();
        const GroupInfo& g = m_doc.groups().at(m_selGroup);
        if (m_selChannel >= g.channels.size())
            return QString();
        return QStringLiteral("%1 / %2").arg(g.name, g.channels.at(m_selChannel).name);
    }
    }
    return QString();
}

void MainWindow::updateStatus()
{
    if (!m_doc.isOpen()) {
        m_statusFile->setText(tr("未打开文件"));
        m_statusSelection->setText(QString());
        return;
    }

    int channels = 0;
    for (const GroupInfo& g : m_doc.groups())
        channels += g.channels.size();

    QString text = QStringLiteral("%1 · %2 · %3 组 / %4 通道")
                       .arg(QFileInfo(m_doc.path()).fileName(), m_doc.fileType())
                       .arg(m_doc.groups().size())
                       .arg(channels);
    if (m_doc.isReadOnly())
        text += tr(" · 只读");
    m_statusFile->setText(text);
    m_statusSelection->setText(selectedPathText());
}

void MainWindow::updateTitle()
{
    QString base = QStringLiteral("TDMS Viewer");
    if (m_doc.isOpen())
        base = QFileInfo(m_doc.path()).fileName() + QStringLiteral(" - ") + base;
    setWindowTitle(base + QStringLiteral(" [*]"));
    setWindowModified(m_doc.isDirty());
}

void MainWindow::updateActions()
{
    const bool open = m_doc.isOpen();
    const bool writable = open && !m_doc.isReadOnly();

    m_actSave->setEnabled(writable);
    m_actSaveAs->setEnabled(writable);
    m_actClose->setEnabled(open);
    m_actExport->setEnabled(open);
    m_actAddGroup->setEnabled(writable);
    m_actImport->setEnabled(true);
    m_actMerge->setEnabled(true);
    m_actRefresh->setEnabled(open);

    const bool hasSelection = open && m_selExternal < 0;
    m_actRename->setEnabled(writable && hasSelection && m_selKind != ObjKind::File);
    m_actDelete->setEnabled(writable && hasSelection && m_selKind != ObjKind::File);
}

void MainWindow::showStatus(const QString& message)
{
    statusBar()->showMessage(message, 6000);
}

// ===========================================================================
// Property editing
// ===========================================================================
void MainWindow::onPropertyEdited(const QString& name, DDCDataType type, const QVariant& value)
{
    if (!m_doc.isOpen())
        return;

    QString error;
    const ObjKind kind = m_props->kind();
    const int group = m_props->group();
    const int channel = m_props->channel();

    if (!m_doc.setProperty(kind, group, channel, name, type, value, &error)) {
        QMessageBox::warning(this, tr("修改属性失败"), error);
        updateProperties();
        return;
    }

    markDirty();
    if (name == QLatin1String("name")) {
        rebuildTree();
        refreshPlot();
    } else if (name == QLatin1String("unit_string")) {
        refreshPlot();
    }
    updateProperties();
    showStatus(tr("属性 “%1” 已更新（保存后写入文件）。").arg(name));
}

void MainWindow::onPropertyCreated(const QString& name, DDCDataType type, const QVariant& value)
{
    if (!m_doc.isOpen())
        return;

    QString error;
    if (!m_doc.createProperty(m_props->kind(), m_props->group(), m_props->channel(),
                              name, type, value, &error)) {
        QMessageBox::warning(this, tr("添加属性失败"), error);
        return;
    }
    markDirty();
    updateProperties();
    showStatus(tr("已添加属性 “%1”（保存后写入文件）。").arg(name));
}

// ===========================================================================
// Operations
// ===========================================================================
void MainWindow::importExternalData()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("导入外部数据"), QString(),
        tr("数据文件 (*.txt *.csv *.tsv *.dat);;文本文件 (*.txt);;CSV 文件 (*.csv);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;

    ImportDialog dialog(path, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QVector<int> columns = dialog.selectedColumns();
    if (columns.isEmpty())
        return;

    const DataTable& table = dialog.table();

    // Optional X column: its spacing becomes the sample interval written to the
    // file, so the imported channels can be drawn on a time axis.
    QVector<double> xValues;
    const int xColumn = dialog.xColumn();
    if (xColumn >= 0 && xColumn < table.columns.size() && table.columns.at(xColumn).numeric)
        xValues = table.columns.at(xColumn).values;

    double importIncrement = 1.0;
    double importOffset = 0.0;
    if (xValues.size() >= 2) {
        const double step = xValues.at(1) - xValues.at(0);
        if (step > 0.0) {
            importIncrement = step;
            importOffset = xValues.first();
        }
    }
    const QDateTime importStart = QDateTime::currentDateTime();

    if (dialog.writeToDocument()) {
        if (!m_doc.isOpen()) {
            QMessageBox::information(this, tr("未打开文件"),
                                     tr("请先新建或打开一个 TDM / TDMS 文件，再导入数据写入文件。\n"
                                        "也可以选择“仅用于绘图”。"));
            return;
        }
        if (m_doc.isReadOnly()) {
            QMessageBox::warning(this, tr("只读文件"),
                                 tr("当前文件以只读方式打开，无法写入数据。"));
            return;
        }

        QString groupName = dialog.groupName();
        if (groupName.isEmpty())
            groupName = tr("导入数据");

        QString error;
        int groupIndex = -1;
        if (!m_doc.addGroup(groupName, tr("从 %1 导入").arg(QFileInfo(path).fileName()),
                            &groupIndex, &error)) {
            QMessageBox::critical(this, tr("导入失败"), error);
            return;
        }

        QVector<QString> newKeys;
        for (int columnIndex : columns) {
            const DataColumn& col = table.columns.at(columnIndex);
            if (!m_doc.addChannel(groupIndex, col.name, QString(), col.unit, DDC_Double,
                                  col.values, &error)) {
                QMessageBox::critical(this, tr("导入失败"), error);
                break;
            }
            const int channelIndex = m_doc.groups().at(groupIndex).channels.size() - 1;
            // Timing makes the channel plottable on the time axis.
            QString timingError;
            m_doc.setChannelTiming(groupIndex, channelIndex, importStart, importIncrement,
                                   importOffset, &timingError);
            newKeys.append(channelKey(groupIndex, channelIndex));
        }

        clearCache();
        for (const QString& key : newKeys)
            m_checkedKeys.insert(key);
        markDirty();
        rebuildTree();
        refreshPlot();
        showStatus(tr("已导入 %1 个通道到分组 “%2”，保存后写入文件。")
                       .arg(newKeys.size()).arg(groupName));
        return;
    }

    // plot-only import
    const int firstIndex = m_external.size();
    int added = 0;
    for (int columnIndex : columns) {
        const DataColumn& col = table.columns.at(columnIndex);
        ExternalSeries s;
        s.name = col.name;
        s.unit = col.unit;
        s.y = col.values;
        s.x = (xValues.size() == col.values.size()) ? xValues : QVector<double>();
        m_external.append(s);
        m_checkedKeys.insert(QStringLiteral("ext:%1").arg(firstIndex + added));
        ++added;
    }

    m_selExternal = kKindExternalRoot;
    rebuildTree();
    refreshPlot();
    showStatus(tr("已载入 %1 条外部曲线（仅显示，不写入文件）。").arg(added));
}

void MainWindow::mergeFilesDialog()
{
    if (!maybeSave())
        return;

    MergeDialog dialog(m_doc.isOpen() ? m_doc.path() : QString(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const MergeDialog::Result result = dialog.result();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    QStringList log;
    const bool ok = mergeFiles(result.pathA, result.pathB, result.outPath, result.mode,
                               result.groupsA, result.groupsB, &error, &log);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        QMessageBox::critical(this, tr("合并失败"), error);
        return;
    }

    QString message = log.isEmpty() ? tr("合并完成。") : log.join(QStringLiteral("\n"));
    message += tr("\n\n输出文件：%1").arg(result.outPath);
    QMessageBox::information(this, tr("合并完成"), message);

    if (result.openAfter)
        openDocument(result.outPath);
}

void MainWindow::exportData()
{
    if (!m_doc.isOpen())
        return;

    const bool hasChannel = m_selKind == ObjKind::Channel && m_selExternal < 0;
    const bool hasGroup = (m_selKind == ObjKind::Group || hasChannel) && m_selExternal < 0;

    QString suggested;
    if (hasChannel) {
        suggested = m_doc.groups().at(m_selGroup).channels.at(m_selChannel).name;
    } else if (hasGroup) {
        suggested = m_doc.groups().at(m_selGroup).name;
    } else {
        suggested = QFileInfo(m_doc.path()).completeBaseName();
    }

    ExportDialog dialog(hasChannel, hasGroup, suggested, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const ExportSettings settings = dialog.settings();
    const QString path = dialog.outputPath();
    QString error;

    switch (dialog.scope()) {
    case ExportDialog::ScopeChannel:
        if (!hasChannel) {
            QMessageBox::information(this, tr("无法导出"), tr("请先在左侧选择一个通道。"));
            return;
        }
        if (!m_doc.exportChannel(m_selGroup, m_selChannel, path, settings, &error)) {
            QMessageBox::critical(this, tr("导出失败"), error);
            return;
        }
        showStatus(tr("已导出通道到 %1").arg(path));
        break;
    case ExportDialog::ScopeGroup:
        if (!hasGroup) {
            QMessageBox::information(this, tr("无法导出"), tr("请先在左侧选择一个分组。"));
            return;
        }
        if (!m_doc.exportGroup(m_selGroup, path, settings, &error)) {
            QMessageBox::critical(this, tr("导出失败"), error);
            return;
        }
        showStatus(tr("已导出分组到 %1").arg(path));
        break;
    case ExportDialog::ScopeFile: {
        QStringList files;
        if (!m_doc.exportAllToDir(path, settings, &files, &error)) {
            QMessageBox::critical(this, tr("导出失败"), error);
            return;
        }
        showStatus(tr("已导出 %1 个分组文件到 %2").arg(files.size()).arg(path));
        QMessageBox::information(this, tr("导出完成"),
                                 tr("已导出 %1 个分组文件到：\n%2").arg(files.size()).arg(path));
        break;
    }
    }
}

void MainWindow::addGroup()
{
    if (!m_doc.isOpen() || m_doc.isReadOnly())
        return;

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("添加分组"), tr("分组名称："), QLineEdit::Normal,
        tr("新建分组 %1").arg(m_doc.groups().size() + 1), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    QString error;
    int index = -1;
    if (!m_doc.addGroup(name.trimmed(), QString(), &index, &error)) {
        QMessageBox::warning(this, tr("添加分组失败"), error);
        return;
    }
    markDirty();
    m_selKind = ObjKind::Group;
    m_selGroup = index;
    m_selChannel = -1;
    m_selExternal = -1;
    rebuildTree();
    updateProperties();
    showStatus(tr("已添加分组 “%1”，可通过“导入外部数据”向其中写入通道。").arg(name.trimmed()));
}

void MainWindow::renameSelection()
{
    if (!m_doc.isOpen() || m_doc.isReadOnly())
        return;

    QString currentName;
    if (m_selKind == ObjKind::Group && m_selGroup < m_doc.groups().size()) {
        currentName = m_doc.groups().at(m_selGroup).name;
    } else if (m_selKind == ObjKind::Channel && m_selGroup < m_doc.groups().size()) {
        const GroupInfo& g = m_doc.groups().at(m_selGroup);
        if (m_selChannel < g.channels.size())
            currentName = g.channels.at(m_selChannel).name;
    } else {
        QMessageBox::information(this, tr("重命名"), tr("请选择一个分组或通道。"));
        return;
    }

    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, tr("重命名"), tr("新名称："), QLineEdit::Normal, currentName, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == currentName)
        return;

    QString error;
    bool success = false;
    if (m_selKind == ObjKind::Channel)
        success = m_doc.renameChannel(m_selGroup, m_selChannel, newName.trimmed(), &error);
    else
        success = m_doc.setProperty(ObjKind::Group, m_selGroup, -1, QStringLiteral("name"),
                                    DDC_String, newName.trimmed(), &error);
    if (!success) {
        QMessageBox::warning(this, tr("重命名失败"), error);
        return;
    }

    markDirty();
    rebuildTree();
    refreshPlot();
    updateProperties();
    showStatus(tr("已重命名为 “%1”。").arg(newName.trimmed()));
}

void MainWindow::deleteSelection()
{
    if (!m_doc.isOpen() || m_doc.isReadOnly())
        return;

    if (m_selKind == ObjKind::File) {
        QMessageBox::information(this, tr("删除"), tr("请选择要删除的分组或通道。"));
        return;
    }

    QString name;
    QString question;
    if (m_selKind == ObjKind::Group) {
        name = m_doc.groups().at(m_selGroup).name;
        const int count = m_doc.groups().at(m_selGroup).channels.size();
        question = tr("确定删除分组 “%1” 及其 %2 个通道吗？").arg(name).arg(count);
    } else {
        const GroupInfo& g = m_doc.groups().at(m_selGroup);
        name = g.channels.at(m_selChannel).name;
        question = tr("确定删除通道 “%1” 吗？").arg(name);
    }

    if (QMessageBox::question(this, tr("删除确认"), question,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    bool ok = false;
    if (m_selKind == ObjKind::Group)
        ok = m_doc.removeGroup(m_selGroup, &error);
    else
        ok = m_doc.removeChannel(m_selGroup, m_selChannel, &error);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        QMessageBox::warning(this, tr("删除失败"), error);
        return;
    }

    clearCache();
    m_checkedKeys.clear();
    m_selKind = ObjKind::File;
    m_selGroup = m_selChannel = -1;
    m_selExternal = -1;
    rebuildTree();
    refreshPlot();
    updateProperties();
    updateStatus();
    showStatus(tr("已删除 “%1”（磁盘文件已重建，请核对后保存）。").arg(name));
}

} // namespace tdms
