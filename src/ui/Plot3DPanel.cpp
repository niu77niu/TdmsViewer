// ---------------------------------------------------------------------------
// Plot3DPanel.cpp
// ---------------------------------------------------------------------------
#include "Plot3DPanel.h"

#include "Icons.h"
#include "Theme.h"
#include "core/Settings.h"

#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

#include <qwt3d_coordsys.h>
#include <qwt3d_line3d.h>
#include <qwt3d_plot.h>
#include <qwt3d_theme.h>
#include <qwt3d_types.h>

namespace tdms {

namespace {

const int kMaxPointsPerChannel = 4000;
// Scatter markers are drawn as fixed-size GL points, so a few thousand of them
// along one channel overlap into what looks like a solid stroke.  Keep the
// marker count low enough that neighbouring dots stay visually separate.
const int kMaxDotsPerChannel = 450;
const double kDotPixelSize = 1.8;

RGBA toRGBA(const QColor& color, double alpha = 1.0)
{
    return RGBA(color.redF(), color.greenF(), color.blueF(), alpha);
}

Qwt3DLine::LineStyle toLineStyle(Trace3DStyle style)
{
    switch (style) {
    case Trace3DStyle::Lines: return Qwt3DLine::Lines;
    case Trace3DStyle::Dots:  return Qwt3DLine::Dots;
    case Trace3DStyle::Tube:  break;
    }
    return Qwt3DLine::Tube;
}

} // namespace

Plot3DPanel::Plot3DPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bar = new QToolBar(this);
    bar->setIconSize(QSize(24, 24));
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    bar->setMovable(false);

    m_actReset = bar->addAction(tr("重置视角"));
    connect(m_actReset, &QAction::triggered, this, [this] { resetView(); });

    m_actNormalize = bar->addAction(tr("幅值归一化"));
    m_actNormalize->setCheckable(true);
    m_actNormalize->setToolTip(tr("把每个通道的幅值缩放到 0~1，便于在小信号与大信号之间比较形状"));
    connect(m_actNormalize, &QAction::toggled, this, [this](bool on) {
        if (m_syncing || Settings::instance().values().trace3DNormalize == on)
            return;
        AppSettings values = Settings::instance().values();
        values.trace3DNormalize = on;
        Settings::instance().setValues(values);
    });

    bar->addSeparator();

    // Icon-only toolbar: the combo keeps its text but explains itself via tooltip.
    m_styleCombo = new QComboBox(this);
    m_styleCombo->addItem(tr("管线"), int(Trace3DStyle::Tube));
    m_styleCombo->addItem(tr("细线"), int(Trace3DStyle::Lines));
    m_styleCombo->addItem(tr("散点"), int(Trace3DStyle::Dots));
    m_styleCombo->setFixedWidth(96);
    m_styleCombo->setToolTip(tr("三维曲线线型：管线 / 细线 / 散点"));
    connect(m_styleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing || index < 0)
            return;
        const Trace3DStyle style = Trace3DStyle(m_styleCombo->itemData(index).toInt());
        if (Settings::instance().values().trace3DStyle == style)
            return;
        AppSettings values = Settings::instance().values();
        values.trace3DStyle = style;
        Settings::instance().setValues(values);
    });
    bar->addWidget(m_styleCombo);

    bar->addSeparator();
    m_actImage = bar->addAction(tr("导出图片"));
    connect(m_actImage, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("导出三维视图"), QStringLiteral("plot3d.png"),
            tr("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;位图 (*.bmp)"));
        if (path.isEmpty())
            return;
        QString error;
        if (!exportImage(path, &error))
            QMessageBox::warning(this, tr("导出失败"), error);
    });

    layout->addWidget(bar);

    m_plot = new Qwt3DPlot(this);
    m_plot->setTitle(QString());
    m_plot->showColorLegend(false);
    m_plot->enableMouse(true);
    m_plot->enableKeyboard(true);
    layout->addWidget(m_plot, 1);

    applySettings();
    resetView();
}

Plot3DPanel::~Plot3DPanel() = default;

// ---------------------------------------------------------------------------
void Plot3DPanel::applySettings()
{
    const AppSettings& s = Settings::instance().values();

    m_syncing = true;
    if (m_actNormalize)
        m_actNormalize->setChecked(s.trace3DNormalize);
    if (m_styleCombo) {
        const int index = m_styleCombo->findData(int(s.trace3DStyle));
        if (index >= 0)
            m_styleCombo->setCurrentIndex(index);
    }
    m_syncing = false;

    applyTheme();
    refreshToolIcons();
    updateAxisLabels();
    rebuild();
}

void Plot3DPanel::applyTheme()
{
    const theme::Colors& c = theme::colors();
    const bool dark = theme::isDark();

    m_plot->applyTheme(dark ? Qwt3DTheme::Dark : Qwt3DTheme::Default);
    m_plot->setBackgroundColor(toRGBA(c.canvas));
    m_plot->setTitleColor(toRGBA(c.windowText));
    m_plot->setTitleFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::DemiBold);

    if (auto* coords = m_plot->coordinates()) {
        coords->setAxesColor(toRGBA(c.muted));
        coords->setNumberColor(toRGBA(c.windowText));
        coords->setLabelColor(toRGBA(c.windowText));
        coords->setGridLinesColor(toRGBA(c.grid));
        coords->setStyle(BOX);
        coords->adjustNumbers(4);
        coords->adjustLabels(6);
        for (auto& axis : coords->axes) {
            axis.setMajors(5);
            axis.setMinors(2);
        }
    }
}

void Plot3DPanel::refreshToolIcons()
{
    // Matches the toolbar icon size so QIcon does not rescale the pixmaps.
    constexpr int kIconSize = 24;
    const QColor color = theme::iconColor();
    if (m_actReset)
        m_actReset->setIcon(icons::get(QStringLiteral("reset"), color, kIconSize));
    if (m_actNormalize)
        m_actNormalize->setIcon(icons::get(QStringLiteral("fit"), color, kIconSize));
    if (m_actImage)
        m_actImage->setIcon(icons::get(QStringLiteral("image"), color, kIconSize));
}

void Plot3DPanel::updateAxisLabels()
{
    if (auto* coords = m_plot->coordinates()) {
        coords->axes[X1].setLabelString(m_xTitle.isEmpty() ? QStringLiteral("Time") : m_xTitle);
        coords->axes[Y1].setLabelString(QStringLiteral("Channel"));
        coords->axes[Z1].setLabelString(m_zTitle.isEmpty() ? QStringLiteral("Amplitude") : m_zTitle);
    }
}

void Plot3DPanel::resetView()
{
    m_plot->setRotation(35.0, 0.0, 22.0);
    m_plot->setViewportShift(0.10, 0.0);
    m_plot->setScale(1.0, 1.0, 1.0);
    m_plot->setZoom(0.90);
    m_plot->update();
}

// ---------------------------------------------------------------------------
void Plot3DPanel::setSeries(const QVector<PlotSeries>& series, const QString& xAxisTitle)
{
    m_series = series;
    m_xTitle = xAxisTitle;
    rebuild();
}

void Plot3DPanel::clear()
{
    m_series.clear();
    rebuild();
}

void Plot3DPanel::rebuild()
{
    for (Qwt3DLine* line : m_lines) {
        m_plot->detach(line);
        delete line;
    }
    m_lines.clear();

    const AppSettings& s = Settings::instance().values();
    const bool normalize = s.trace3DNormalize;
    m_zTitle = normalize ? QStringLiteral("Value (0-1)") : QStringLiteral("Value");
    updateAxisLabels();

    if (m_series.isEmpty()) {
        m_plot->setTitle(tr("勾选多个通道后即可生成三维波形"));
        m_plot->setTitlePosition(0.5, 0.5, Center);
        m_plot->update();
        return;
    }

    const int channelCount = m_series.size();
    for (int channel = 0; channel < channelCount; ++channel) {
        const PlotSeries& series = m_series.at(channel);
        const int total = series.y.size();
        if (total == 0)
            continue;

        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();
        for (int i = 0; i < total; ++i) {
            const double v = series.y.at(i);
            if (!std::isfinite(v))
                continue;
            yMin = std::min(yMin, v);
            yMax = std::max(yMax, v);
        }
        if (yMin > yMax)
            continue;

        const double span = (yMax - yMin) > 0.0 ? (yMax - yMin) : 1.0;
        const int budget = (s.trace3DStyle == Trace3DStyle::Dots) ? kMaxDotsPerChannel
                                                                 : kMaxPointsPerChannel;
        const int step = std::max(1, total / budget);
        const bool hasX = series.x.size() == total;

        QVector<double> xs;
        QVector<double> ys;
        QVector<double> zs;
        xs.reserve(total / step + 1);
        ys.reserve(total / step + 1);
        zs.reserve(total / step + 1);

        for (int i = 0; i < total; i += step) {
            const double value = series.y.at(i);
            if (!std::isfinite(value))
                continue;
            double x = hasX ? series.x.at(i) : double(i);
            if (!std::isfinite(x))
                continue;
            // Absolute time is delivered in msecs - draw it as seconds.
            if (hasX && std::fabs(x) > 1e11)
                x /= 1000.0;
            xs.append(x);
            ys.append(double(channel));
            zs.append(normalize ? (value - yMin) / span : value);
        }
        if (xs.size() < 2)
            continue;

        auto* line = new Qwt3DLine();
        line->setSamples(xs, ys, zs);
        line->setLineStyle(toLineStyle(s.trace3DStyle));
        line->setColor(toRGBA(series.color.isValid() ? series.color : QColor("#5b9dff")));
        if (s.trace3DStyle == Trace3DStyle::Tube)
            line->setTubeRadius(0.0);   // automatic
        if (s.trace3DStyle == Trace3DStyle::Dots)
            line->setPointSize(kDotPixelSize);
        line->attach(m_plot);
        m_lines.append(line);
    }

    m_plot->setTitle(m_lines.isEmpty() ? tr("勾选多个通道后即可生成三维波形") : QString());
    if (m_lines.isEmpty())
        m_plot->setTitlePosition(0.5, 0.5, Center);
    m_plot->update();
}

bool Plot3DPanel::exportImage(const QString& filePath, QString* error)
{
    const QPixmap pixmap = m_plot->grab();
    if (pixmap.isNull()) {
        if (error)
            *error = tr("三维视图为空，无法导出。");
        return false;
    }
    if (!pixmap.save(filePath)) {
        if (error)
            *error = tr("无法写入图片文件：%1").arg(filePath);
        return false;
    }
    return true;
}

} // namespace tdms
