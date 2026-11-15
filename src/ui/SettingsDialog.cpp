// ---------------------------------------------------------------------------
// SettingsDialog.cpp
// ---------------------------------------------------------------------------
#include "SettingsDialog.h"

#include "Icons.h"
#include "Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace tdms {

namespace {

QLabel* hintLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("hintLabel"));
    label->setWordWrap(true);
    return label;
}

QLabel* sectionLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    return label;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("首选项"));
    setWindowIcon(icons::get(QStringLiteral("settings")));
    resize(720, 470);

    m_categories = new QListWidget(this);
    m_categories->setObjectName(QStringLiteral("categoryList"));
    m_categories->setFixedWidth(150);
    m_categories->setIconSize(QSize(18, 18));
    m_categories->setSelectionMode(QAbstractItemView::SingleSelection);
    m_categories->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_pages = new QStackedWidget(this);

    auto addPage = [this](const QString& title, const QString& icon, QWidget* page) {
        auto* item = new QListWidgetItem(icons::get(icon), title, m_categories);
        item->setSizeHint(QSize(0, 34));
        m_pages->addWidget(page);
    };

    addPage(tr("外观"), QStringLiteral("sun"), buildAppearancePage());
    addPage(tr("绘图"), QStringLiteral("chart"), buildPlotPage());
    addPage(tr("三维"), QStringLiteral("cube"), build3DPage());
    addPage(tr("数据与导出"), QStringLiteral("table"), buildExportPage());

    connect(m_categories, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_categories->setCurrentRow(0);

    auto* content = new QHBoxLayout;
    content->setSpacing(12);
    content->addWidget(m_categories);
    content->addWidget(m_pages, 1);

    auto* buttons = new QDialogButtonBox(this);
    m_applyButton = buttons->addButton(tr("应用"), QDialogButtonBox::ApplyRole);
    buttons->addButton(tr("确定"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("取消"), QDialogButtonBox::RejectRole);
    auto* defaultsButton = buttons->addButton(tr("恢复默认"), QDialogButtonBox::ResetRole);
    defaultsButton->setIcon(icons::get(QStringLiteral("reset")));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(12);
    layout->addLayout(content, 1);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_applyButton, &QPushButton::clicked, this, &SettingsDialog::apply);
    connect(defaultsButton, &QPushButton::clicked, this, &SettingsDialog::restoreDefaults);

    m_result = Settings::instance().values();
    loadValues(m_result);
    m_pages->setFocus();
}

// ---------------------------------------------------------------------------
QWidget* SettingsDialog::buildAppearancePage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(10);

    layout->addWidget(sectionLabel(tr("主题"), page));

    auto* themeBox = new QGroupBox(tr("配色方案"), page);
    auto* themeLayout = new QVBoxLayout(themeBox);
    themeLayout->setSpacing(6);

    m_themeSystem = new QRadioButton(tr("跟随系统（默认）"), themeBox);
    m_themeSystem->setIcon(icons::get(QStringLiteral("monitor")));
    m_themeLight = new QRadioButton(tr("浅色（正常颜色）"), themeBox);
    m_themeLight->setIcon(icons::get(QStringLiteral("sun")));
    m_themeDark = new QRadioButton(tr("深色"), themeBox);
    m_themeDark->setIcon(icons::get(QStringLiteral("moon")));

    themeLayout->addWidget(m_themeSystem);
    themeLayout->addWidget(hintLabel(
        tr("跟随 Windows 的“应用模式”设置，系统在浅色与深色之间切换时界面同步变化。"), themeBox));
    themeLayout->addWidget(m_themeLight);
    themeLayout->addWidget(m_themeDark);

    layout->addWidget(themeBox);

    auto* note = hintLabel(
        tr("主题切换会立即应用到主窗口、绘图区与属性表；曲线颜色在浅色背景上依旧保持可辨识度。"),
        page);
    layout->addWidget(note);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::buildPlotPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(10);

    // --- X axis -------------------------------------------------------------
    auto* axisBox = new QGroupBox(tr("时间轴（X 轴）"), page);
    auto* axisLayout = new QVBoxLayout(axisBox);
    axisLayout->setSpacing(6);

    m_axisRelative = new QRadioButton(tr("相对时间：从曲线起点开始计时（s / mm:ss）"), axisBox);
    m_axisAbsolute = new QRadioButton(tr("绝对时间：显示采样发生的日期与时刻"), axisBox);
    m_axisSample = new QRadioButton(tr("采样点序号：仅当通道缺少时间信息时使用"), axisBox);
    axisLayout->addWidget(m_axisRelative);
    axisLayout->addWidget(m_axisAbsolute);
    axisLayout->addWidget(m_axisSample);
    axisLayout->addWidget(hintLabel(
        tr("时间信息取自通道属性 wf_start_time（起始时刻）、wf_increment（采样间隔）"
           "与 wf_start_offset（起始偏移）。"),
        axisBox));

    m_assumeUnitInterval = new QCheckBox(
        tr("通道缺少采样间隔时，按 1 s 间隔估算时间轴"), axisBox);
    axisLayout->addWidget(m_assumeUnitInterval);
    layout->addWidget(axisBox);

    // --- four axis sides ----------------------------------------------------
    auto* sidesBox = new QGroupBox(tr("坐标轴显示"), page);
    auto* sidesLayout = new QVBoxLayout(sidesBox);
    sidesLayout->setSpacing(6);

    auto* sidesGrid = new QGridLayout;
    sidesGrid->setHorizontalSpacing(18);
    sidesGrid->setVerticalSpacing(6);
    m_axisBottom = new QCheckBox(tr("下轴（X）"), sidesBox);
    m_axisTop = new QCheckBox(tr("上轴（X）"), sidesBox);
    m_axisLeft = new QCheckBox(tr("左轴（Y）"), sidesBox);
    m_axisRight = new QCheckBox(tr("右轴（Y）"), sidesBox);
    sidesGrid->addWidget(m_axisBottom, 0, 0);
    sidesGrid->addWidget(m_axisTop, 0, 1);
    sidesGrid->addWidget(m_axisLeft, 1, 0);
    sidesGrid->addWidget(m_axisRight, 1, 1);
    sidesLayout->addLayout(sidesGrid);

    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(8);
    topRow->addWidget(new QLabel(tr("上轴刻度："), sidesBox));
    m_topAxisMode = new QComboBox(sidesBox);
    m_topAxisMode->addItem(tr("与下轴相同"), int(TopAxisMode::FollowBottom));
    m_topAxisMode->addItem(tr("相对时间"), int(TopAxisMode::Relative));
    m_topAxisMode->addItem(tr("绝对时间"), int(TopAxisMode::Absolute));
    m_topAxisMode->addItem(tr("采样点序号"), int(TopAxisMode::SampleIndex));
    m_topAxisMode->setMaximumWidth(180);
    topRow->addWidget(m_topAxisMode);
    topRow->addStretch(1);
    sidesLayout->addLayout(topRow);

    sidesLayout->addWidget(hintLabel(
        tr("上轴与下轴共用同一刻度范围，只改变数字的写法：例如下轴显示“12.5 s”时，"
           "上轴可以同时显示“09:30:12.500”。右轴在没有第二种单位时镜像左轴的刻度。"),
        sidesBox));
    layout->addWidget(sidesBox);

    // --- display ------------------------------------------------------------
    auto* displayBox = new QGroupBox(tr("显示"), page);
    auto* form = new QFormLayout(displayBox);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_grid = new QCheckBox(tr("显示网格线"), displayBox);
    m_legend = new QCheckBox(tr("显示图例"), displayBox);
    m_antialias = new QCheckBox(tr("抗锯齿（曲线更平滑）"), displayBox);

    m_lineWidth = new QDoubleSpinBox(displayBox);
    m_lineWidth->setRange(0.5, 6.0);
    m_lineWidth->setSingleStep(0.2);
    m_lineWidth->setDecimals(1);
    m_lineWidth->setSuffix(tr(" px"));
    m_lineWidth->setMaximumWidth(120);

    form->addRow(m_grid);
    form->addRow(m_legend);
    form->addRow(m_antialias);
    form->addRow(tr("曲线线宽："), m_lineWidth);
    layout->addWidget(displayBox);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::build3DPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(10);

    auto* box = new QGroupBox(tr("三维视图"), page);
    auto* inner = new QVBoxLayout(box);
    inner->setSpacing(6);

    m_enable3D = new QCheckBox(tr("在主窗口显示“三维”标签页"), box);
    inner->addWidget(m_enable3D);
    inner->addWidget(hintLabel(
        tr("三维视图把每个已勾选的通道画成一条空间曲线：X 为时间、Y 为通道、Z 为幅值，"
           "可用鼠标左键旋转、滚轮缩放、Ctrl+拖动平移。"),
        box));

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_style3D = new QComboBox(box);
    m_style3D->addItem(tr("管线（有厚度的立体线）"), int(Trace3DStyle::Tube));
    m_style3D->addItem(tr("细线"), int(Trace3DStyle::Lines));
    m_style3D->addItem(tr("散点"), int(Trace3DStyle::Dots));
    m_style3D->setMaximumWidth(240);
    form->addRow(tr("曲线样式："), m_style3D);

    m_normalize3D = new QCheckBox(tr("幅值归一化（每条通道缩放到 0~1）"), box);
    form->addRow(m_normalize3D);
    inner->addLayout(form);

    inner->addWidget(hintLabel(
        tr("归一化只影响三维视图的显示比例，不会改动文件中的原始数据。"),
        box));

    layout->addWidget(box);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::buildExportPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(10);

    auto* box = new QGroupBox(tr("导出默认值"), page);
    auto* form = new QFormLayout(box);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_exportFormat = new QComboBox(box);
    m_exportFormat->addItem(tr("CSV（逗号分隔）"), QStringLiteral("csv"));
    m_exportFormat->addItem(tr("TXT（制表符分隔）"), QStringLiteral("txt"));
    m_exportFormat->setMaximumWidth(220);

    m_precision = new QSpinBox(box);
    m_precision->setRange(3, 17);
    m_precision->setMaximumWidth(120);

    m_includeIndex = new QCheckBox(tr("包含点序号列"), box);
    m_includeMeta = new QCheckBox(tr("包含文件 / 分组元数据注释"), box);

    form->addRow(tr("默认格式："), m_exportFormat);
    form->addRow(tr("小数位数："), m_precision);
    form->addRow(m_includeIndex);
    form->addRow(m_includeMeta);

    layout->addWidget(box);

    auto* extra = new QGroupBox(tr("数据表"), page);
    auto* extraLayout = new QVBoxLayout(extra);
    extraLayout->addWidget(hintLabel(
        tr("导出对话框中的选项会以这里的默认值初始化，单次导出仍可临时修改。"), extra));
    extraLayout->addWidget(hintLabel(
        tr("CSV 输出使用 UTF-8 编码并带 BOM，可直接用 Excel 打开。"), extra));
    layout->addWidget(extra);

    // --- configuration file -------------------------------------------------
    auto* configBox = new QGroupBox(tr("配置文件"), page);
    auto* configLayout = new QVBoxLayout(configBox);
    configLayout->setSpacing(6);

    m_configPath = new QLabel(Settings::configFilePath(), configBox);
    m_configPath->setObjectName(QStringLiteral("hintLabel"));
    m_configPath->setWordWrap(true);
    m_configPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    configLayout->addWidget(m_configPath);

    auto* openButton = new QPushButton(tr("打开所在文件夹"), configBox);
    openButton->setIcon(icons::get(QStringLiteral("open")));
    openButton->setMaximumWidth(160);
    connect(openButton, &QPushButton::clicked, this, &SettingsDialog::openConfigFolder);
    configLayout->addWidget(openButton);

    configLayout->addWidget(hintLabel(
        tr("所有首选项都保存在这个 INI 文件中（不使用注册表）。"
           "把它随程序一起复制即可携带配置；设置为只读目录时会自动改用用户配置目录。"),
        configBox));
    layout->addWidget(configBox);

    layout->addStretch(1);
    return page;
}

void SettingsDialog::openConfigFolder()
{
    const QString path = Settings::configDirectory();
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// ---------------------------------------------------------------------------
void SettingsDialog::loadValues(const AppSettings& values)
{
    m_themeSystem->setChecked(values.theme == ThemeMode::System);
    m_themeLight->setChecked(values.theme == ThemeMode::Light);
    m_themeDark->setChecked(values.theme == ThemeMode::Dark);

    m_axisRelative->setChecked(values.xAxisMode == TimeAxisMode::Relative);
    m_axisAbsolute->setChecked(values.xAxisMode == TimeAxisMode::Absolute);
    m_axisSample->setChecked(values.xAxisMode == TimeAxisMode::SampleIndex);

    m_grid->setChecked(values.gridVisible);
    m_legend->setChecked(values.legendVisible);
    m_antialias->setChecked(values.antialiasing);
    m_assumeUnitInterval->setChecked(values.assumeUnitSampleInterval);
    m_lineWidth->setValue(values.lineWidth);

    m_axisBottom->setChecked(values.showAxisBottom);
    m_axisTop->setChecked(values.showAxisTop);
    m_axisLeft->setChecked(values.showAxisLeft);
    m_axisRight->setChecked(values.showAxisRight);
    const int topIndex = m_topAxisMode->findData(int(values.topAxisMode));
    m_topAxisMode->setCurrentIndex(topIndex >= 0 ? topIndex : 0);

    m_enable3D->setChecked(values.show3DView);
    m_normalize3D->setChecked(values.trace3DNormalize);
    const int styleIndex = m_style3D->findData(int(values.trace3DStyle));
    m_style3D->setCurrentIndex(styleIndex >= 0 ? styleIndex : 0);

    const int formatIndex = m_exportFormat->findData(values.exportFormat);
    m_exportFormat->setCurrentIndex(formatIndex >= 0 ? formatIndex : 0);
    m_precision->setValue(values.exportPrecision);
    m_includeIndex->setChecked(values.exportIncludeIndex);
    m_includeMeta->setChecked(values.exportIncludeMeta);
}

AppSettings SettingsDialog::collect() const
{
    AppSettings values = Settings::instance().values();

    if (m_themeLight->isChecked())
        values.theme = ThemeMode::Light;
    else if (m_themeDark->isChecked())
        values.theme = ThemeMode::Dark;
    else
        values.theme = ThemeMode::System;

    if (m_axisAbsolute->isChecked())
        values.xAxisMode = TimeAxisMode::Absolute;
    else if (m_axisSample->isChecked())
        values.xAxisMode = TimeAxisMode::SampleIndex;
    else
        values.xAxisMode = TimeAxisMode::Relative;

    values.gridVisible = m_grid->isChecked();
    values.legendVisible = m_legend->isChecked();
    values.antialiasing = m_antialias->isChecked();
    values.assumeUnitSampleInterval = m_assumeUnitInterval->isChecked();
    values.lineWidth = m_lineWidth->value();

    values.showAxisBottom = m_axisBottom->isChecked();
    values.showAxisTop = m_axisTop->isChecked();
    values.showAxisLeft = m_axisLeft->isChecked();
    values.showAxisRight = m_axisRight->isChecked();
    values.topAxisMode = TopAxisMode(m_topAxisMode->currentData().toInt());

    values.show3DView = m_enable3D->isChecked();
    values.trace3DNormalize = m_normalize3D->isChecked();
    values.trace3DStyle = Trace3DStyle(m_style3D->currentData().toInt());

    values.exportFormat = m_exportFormat->currentData().toString();
    values.exportPrecision = m_precision->value();
    values.exportIncludeIndex = m_includeIndex->isChecked();
    values.exportIncludeMeta = m_includeMeta->isChecked();
    return values;
}

void SettingsDialog::apply()
{
    m_result = collect();
    Settings::instance().setValues(m_result);
}

void SettingsDialog::restoreDefaults()
{
    const AppSettings def = Settings::defaults();
    loadValues(def);
    m_result = def;
    Settings::instance().setValues(def);
}

void SettingsDialog::accept()
{
    m_result = collect();
    Settings::instance().setValues(m_result);
    QDialog::accept();
}

} // namespace tdms
