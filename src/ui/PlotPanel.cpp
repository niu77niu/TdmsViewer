// ---------------------------------------------------------------------------
// PlotPanel.cpp - Qwt 7 implementation.
// ---------------------------------------------------------------------------
#include "PlotPanel.h"

#include "Icons.h"
#include "Theme.h"
#include "core/TimeUtil.h"

#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>
#include <cmath>
#include <functional>

#include <qwt_interval.h>
#include <qwt_legend.h>
#include <qwt_plot.h>
#include <qwt_plot_canvas_zoomer.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_plot_panner.h>
#include <qwt_scale_div.h>
#include <qwt_scale_draw.h>
#include <qwt_scale_engine.h>
#include <qwt_text.h>

namespace tdms {

namespace {
const int kMaxPlotPoints = 200000;

QFont smallFont(int pointSize = 9)
{
    QFont f;
    f.setPointSize(pointSize);
    return f;
}
} // namespace

// ---------------------------------------------------------------------------
// A scale draw that renders one unit of values with an optional different
// time representation.  Both X axes share the very same numeric range, only
// the labels differ (bottom "12.5 s" <-> top "09:30:12.500").
// ---------------------------------------------------------------------------
class TimeScaleDraw : public QwtScaleDraw
{
public:
    void configure(TimeAxisMode valueUnit, TimeAxisMode labelMode,
                   qint64 originMSecs, double span)
    {
        m_valueUnit = valueUnit;
        m_labelMode = labelMode;
        m_originMSecs = originMSecs;
        m_span = span;
    }

    void setSpan(double span) { m_span = span; }
    void setOriginMSecs(qint64 msecs) { m_originMSecs = msecs; }

    QwtText label(double value) const override
    {
        if (!std::isfinite(value))
            return QwtText();

        QString text;
        if (m_valueUnit == TimeAxisMode::Relative && m_labelMode == TimeAxisMode::Absolute) {
            const qint64 ms = m_originMSecs + qint64(std::llround(value * 1000.0));
            text = formatAbsoluteTick(ms, m_span * 1000.0);
        } else if (m_valueUnit == TimeAxisMode::Absolute
                   && m_labelMode == TimeAxisMode::Relative) {
            const double seconds = (value - double(m_originMSecs)) / 1000.0;
            text = formatRelativeTick(seconds, m_span / 1000.0);
        } else {
            switch (m_labelMode) {
            case TimeAxisMode::Absolute:
                text = formatAbsoluteTick(qint64(std::llround(value)), m_span);
                break;
            case TimeAxisMode::SampleIndex:
                text = QString::number(qint64(std::llround(value)));
                break;
            case TimeAxisMode::Relative:
                text = formatRelativeTick(value, m_span);
                break;
            }
        }

        return QwtText(text);
    }

private:
    TimeAxisMode m_valueUnit = TimeAxisMode::Relative;
    TimeAxisMode m_labelMode = TimeAxisMode::Relative;
    qint64 m_originMSecs = 0;
    double m_span = 0.0;
};

// ---------------------------------------------------------------------------
// QwtPlot subclass: the upper X axis and (when there is only one unit) the
// right Y axis mirror their opposite side instead of scaling independently.
// ---------------------------------------------------------------------------
class AxisSyncPlot : public QwtPlot
{
public:
    explicit AxisSyncPlot(QWidget* parent = nullptr)
        : QwtPlot(parent)
    {
    }

    bool mirrorTop = true;
    bool mirrorRight = true;

    void replot() override
    {
        if (mirrorTop && isAxisVisible(QwtAxis::XTop)) {
            const QwtInterval range = axisInterval(QwtAxis::XBottom);
            setAxisScale(QwtAxis::XTop, range.minValue(), range.maxValue());
        }
        if (mirrorRight && isAxisVisible(QwtAxis::YRight)) {
            const QwtInterval range = axisInterval(QwtAxis::YLeft);
            setAxisScale(QwtAxis::YRight, range.minValue(), range.maxValue());
        }
        QwtPlot::replot();
    }
};

// ---------------------------------------------------------------------------
// Transparent cross hair + readout painted on top of the plot canvas.
// ---------------------------------------------------------------------------
class CrosshairOverlay : public QWidget
{
public:
    explicit CrosshairOverlay(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }

    void setTextProvider(std::function<QString(const QPoint&)> provider)
    {
        m_provider = std::move(provider);
    }

    void updatePosition(const QPoint& pos)
    {
        m_pos = pos;
        m_valid = true;
        update();
    }

    void clearPosition()
    {
        if (!m_valid)
            return;
        m_valid = false;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!m_valid || !m_provider)
            return;

        const theme::Colors& c = theme::colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QPen pen(c.muted);
        pen.setStyle(Qt::DashLine);
        pen.setWidthF(1.0);
        painter.setPen(pen);
        painter.drawLine(0, m_pos.y(), width(), m_pos.y());
        painter.drawLine(m_pos.x(), 0, m_pos.x(), height());

        const QString text = m_provider(m_pos);
        if (text.isEmpty())
            return;

        QFont font = painter.font();
        font.setPointSize(9);
        painter.setFont(font);
        const QFontMetrics metrics(font);
        const QRect textRect = metrics.boundingRect(text).adjusted(-6, -3, 6, 3);

        QPoint origin = m_pos + QPoint(12, 12);
        if (origin.x() + textRect.width() > width())
            origin.setX(m_pos.x() - textRect.width() - 12);
        if (origin.y() + textRect.height() > height())
            origin.setY(m_pos.y() - textRect.height() - 12);
        origin.setX(qMax(0, origin.x()));
        origin.setY(qMax(0, origin.y()));

        const QRect box(origin, textRect.size());
        painter.setPen(QPen(c.border, 1.0));
        painter.setBrush(QColor(c.panel));
        painter.drawRoundedRect(box, 4, 4);
        painter.setPen(QPen(c.windowText));
        painter.drawText(box, Qt::AlignCenter, text);
    }

private:
    std::function<QString(const QPoint&)> m_provider;
    QPoint m_pos;
    bool m_valid = false;
};

// ---------------------------------------------------------------------------
PlotPanel::PlotPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // --- toolbar ------------------------------------------------------------
    auto* bar = new QToolBar(this);
    bar->setIconSize(QSize(24, 24));
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    bar->setMovable(false);

    m_actFit = bar->addAction(tr("自动缩放"));
    connect(m_actFit, &QAction::triggered, this, [this] { autoScale(); });

    m_actGrid = bar->addAction(tr("网格"));
    m_actGrid->setCheckable(true);
    connect(m_actGrid, &QAction::toggled, this, [this](bool on) {
        setGridVisible(on);
        if (Settings::instance().values().gridVisible == on)
            return;
        AppSettings values = Settings::instance().values();
        values.gridVisible = on;
        Settings::instance().setValues(values);
    });

    m_actLegend = bar->addAction(tr("图例"));
    m_actLegend->setCheckable(true);
    connect(m_actLegend, &QAction::toggled, this, [this](bool on) {
        setLegendVisible(on);
        if (Settings::instance().values().legendVisible == on)
            return;
        AppSettings values = Settings::instance().values();
        values.legendVisible = on;
        Settings::instance().setValues(values);
    });

    bar->addSeparator();

    // The combo is the only textual element left in the icon-only toolbar; its
    // tooltip explains what the three entries mean.
    m_axisCombo = new QComboBox(this);
    m_axisCombo->addItem(tr("相对时间"), int(TimeAxisMode::Relative));
    m_axisCombo->addItem(tr("绝对时间"), int(TimeAxisMode::Absolute));
    m_axisCombo->addItem(tr("采样点序号"), int(TimeAxisMode::SampleIndex));
    m_axisCombo->setFixedWidth(112);
    m_axisCombo->setToolTip(tr("X 轴刻度方式：相对时间 / 绝对时间 / 采样点序号"));
    connect(m_axisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncingCombo || index < 0)
            return;
        const TimeAxisMode mode = TimeAxisMode(m_axisCombo->itemData(index).toInt());
        if (mode == Settings::instance().values().xAxisMode)
            return;
        AppSettings values = Settings::instance().values();
        values.xAxisMode = mode;
        Settings::instance().setValues(values);
    });
    bar->addWidget(m_axisCombo);

    bar->addSeparator();

    // --- four axis sides ----------------------------------------------------
    m_actAxes = bar->addAction(tr("坐标轴"));
    QAction* axesAction = m_actAxes;
    axesAction->setToolTip(tr("显示或隐藏上、下、左、右四条坐标轴"));
    axesAction->setIcon(icons::get(QStringLiteral("axes"), theme::iconColor(), 24));
    auto* axesMenu = new QMenu(this);
    const struct {
        const char* text;
        bool AppSettings::*member;
    } axisItems[] = {
        { QT_TR_NOOP("下轴（X）"), &AppSettings::showAxisBottom },
        { QT_TR_NOOP("上轴（X）"), &AppSettings::showAxisTop },
        { QT_TR_NOOP("左轴（Y）"), &AppSettings::showAxisLeft },
        { QT_TR_NOOP("右轴（Y）"), &AppSettings::showAxisRight },
    };
    for (const auto& item : axisItems) {
        auto* action = axesMenu->addAction(tr(item.text));
        action->setCheckable(true);
        action->setChecked(Settings::instance().values().*(item.member));
        const auto member = item.member;
        connect(action, &QAction::toggled, this, [this, member](bool on) {
            AppSettings values = Settings::instance().values();
            values.*member = on;
            Settings::instance().setValues(values);
        });
    }
    axesAction->setMenu(axesMenu);
    if (auto* button = qobject_cast<QToolButton*>(bar->widgetForAction(axesAction))) {
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }

    bar->addSeparator();

    m_actImage = bar->addAction(tr("导出图片"));
    connect(m_actImage, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("导出曲线图片"), QStringLiteral("plot.png"),
            tr("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;位图 (*.bmp)"));
        if (path.isEmpty())
            return;
        QString error;
        if (!exportImage(path, &error))
            QMessageBox::warning(this, tr("导出失败"), error);
    });

    layout->addWidget(bar);

    // --- plot ---------------------------------------------------------------
    m_plot = new AxisSyncPlot(this);
    m_plot->setAutoReplot(false);
    for (int axis = 0; axis < QwtAxis::AxisPositions; ++axis)
        m_plot->setAxisFont(axis, smallFont());
    m_plot->setAxisTitle(QwtAxis::XBottom, tr("相对时间"));
    m_plot->setAxisTitle(QwtAxis::YLeft, tr("数值"));

    m_grid = new QwtPlotGrid;
    m_grid->enableXMin(true);
    m_grid->enableYMin(true);
    m_grid->attach(m_plot);

    m_legend = new QwtLegend;
    m_legend->setMaxColumns(1);
    m_legend->setMinimumWidth(190);
    m_legend->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8));
    m_plot->insertLegend(m_legend, QwtPlot::RightLegend);

    // Qwt 7: the canvas zoomer keeps all four axes in sync by itself.
    m_zoomer = new QwtPlotCanvasZoomer(m_plot->canvas());
    m_zoomer->setRubberBandPen(QPen(QColor("#5b9dff"), 1, Qt::DashLine));

    m_panner = new QwtPlotPanner(m_plot->canvas());
    m_panner->setMouseButton(Qt::MiddleButton);

    // Cross hair overlay (replaces the Qwt 6 plot picker).
    m_overlay = new CrosshairOverlay(m_plot->canvas());
    m_overlay->setTextProvider([this](const QPoint& pos) {
        if (!m_hasX)
            return QString();
        return cursorText(m_plot->invTransform(QwtAxis::XBottom, pos.x()),
                          m_plot->invTransform(QwtAxis::YLeft, pos.y()),
                          m_plot->invTransform(QwtAxis::YRight, pos.y()),
                          m_dualAxis);
    });
    m_overlay->setGeometry(m_plot->canvas()->rect());
    m_overlay->raise();
    m_plot->canvas()->installEventFilter(this);
    m_plot->canvas()->setMouseTracking(true);

    layout->addWidget(m_plot, 1);

    applySettings();
    m_plot->replot();
}

PlotPanel::~PlotPanel() = default;

// ---------------------------------------------------------------------------
bool PlotPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_plot || !m_overlay || watched != m_plot->canvas())
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseMove: {
        auto* me = static_cast<QMouseEvent*>(event);
        m_overlay->setGeometry(m_plot->canvas()->rect());
        m_overlay->updatePosition(me->pos());
        if (!m_hasX) {
            emit cursorMoved(QString());
            break;
        }
        emit cursorMoved(cursorText(m_plot->invTransform(QwtAxis::XBottom, me->pos().x()),
                                    m_plot->invTransform(QwtAxis::YLeft, me->pos().y()),
                                    m_plot->invTransform(QwtAxis::YRight, me->pos().y()),
                                    m_dualAxis));
        break;
    }
    case QEvent::Leave:
        m_overlay->clearPosition();
        emit cursorMoved(QString());
        break;
    case QEvent::Resize:
        m_overlay->setGeometry(m_plot->canvas()->rect());
        m_overlay->raise();
        break;
    default:
        break;
    }

    // Never consume: the zoomer / panner still need these events.
    return false;
}

// ---------------------------------------------------------------------------
void PlotPanel::applySettings()
{
    const AppSettings& s = Settings::instance().values();

    applyTheme();

    if (m_actGrid) {
        const QSignalBlocker blocker(m_actGrid);
        m_actGrid->setChecked(s.gridVisible);
    }
    if (m_actLegend) {
        const QSignalBlocker blocker(m_actLegend);
        m_actLegend->setChecked(s.legendVisible);
    }
    if (m_grid) {
        QPen gridPen(theme::gridColor());
        gridPen.setStyle(Qt::DotLine);
        m_grid->setPen(gridPen);
        m_grid->setMajorPen(gridPen);
        m_grid->setVisible(s.gridVisible);
    }
    setLegendVisible(s.legendVisible);

    for (QwtPlotCurve* curve : m_curves) {
        QPen pen = curve->pen();
        pen.setWidthF(s.lineWidth);
        curve->setPen(pen);
        curve->setRenderHint(QwtPlotItem::RenderAntialiased, s.antialiasing);
    }

    m_axisMode = s.xAxisMode;
    applyAxisVisibility();
    rebuildAxisDraws();
    syncAxisCombo();
    refreshToolIcons();
    m_plot->replot();
}

void PlotPanel::applyTheme()
{
    const theme::Colors& c = theme::colors();

    m_plot->setCanvasBackground(QBrush(c.canvas));

    QPalette pal = m_plot->palette();
    pal.setColor(QPalette::Window, c.panel);
    pal.setColor(QPalette::Base, c.panel);
    pal.setColor(QPalette::WindowText, c.windowText);
    pal.setColor(QPalette::Text, c.windowText);
    pal.setColor(QPalette::ButtonText, c.windowText);
    m_plot->setPalette(pal);

    if (m_legend) {
        QPalette legendPalette = m_legend->palette();
        legendPalette.setColor(QPalette::WindowText, c.windowText);
        legendPalette.setColor(QPalette::Text, c.windowText);
        m_legend->setPalette(legendPalette);
    }

    if (m_zoomer)
        m_zoomer->setRubberBandPen(QPen(c.accent, 1, Qt::DashLine));

    refreshAxisTitles();
    if (m_curves.isEmpty()) {
        QwtText title(tr("尚未选择通道：在左侧结构中勾选通道即可显示曲线"));
        title.setColor(c.muted);
        title.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11));
        m_plot->setTitle(title);
    }
}

void PlotPanel::refreshToolIcons()
{
    // The toolbar runs in icon-only mode at 22 px, so render at exactly that
    // size instead of letting QIcon scale a 24 px pixmap down.
    constexpr int kIconSize = 24;
    const QColor color = theme::iconColor();
    if (m_actFit)
        m_actFit->setIcon(icons::get(QStringLiteral("fit"), color, kIconSize));
    if (m_actGrid)
        m_actGrid->setIcon(icons::get(QStringLiteral("grid"), color, kIconSize));
    if (m_actLegend)
        m_actLegend->setIcon(icons::get(QStringLiteral("legend"), color, kIconSize));
    if (m_actImage)
        m_actImage->setIcon(icons::get(QStringLiteral("image"), color, kIconSize));
    if (m_actAxes)
        m_actAxes->setIcon(icons::get(QStringLiteral("axes"), color, kIconSize));
}

void PlotPanel::syncAxisCombo()
{
    if (!m_axisCombo)
        return;
    const int index = m_axisCombo->findData(int(m_axisMode));
    if (index < 0 || index == m_axisCombo->currentIndex())
        return;
    const QSignalBlocker blocker(m_axisCombo);
    m_axisCombo->setCurrentIndex(index);
}

void PlotPanel::setTimeOrigin(const QDateTime& origin)
{
    m_origin = origin;
    rebuildAxisDraws();
    refreshAxisTitles();
    m_plot->replot();
}

void PlotPanel::setTimeAxisMode(TimeAxisMode mode)
{
    if (m_axisMode == mode) {
        rebuildAxisDraws();
        return;
    }
    m_axisMode = mode;
    rebuildAxisDraws();
    autoScale();
}

double PlotPanel::originMSecs() const
{
    return m_origin.isValid() ? double(m_origin.toMSecsSinceEpoch()) : 0.0;
}

void PlotPanel::applyAxisVisibility()
{
    const AppSettings& s = Settings::instance().values();

    m_plot->setAxisVisible(QwtAxis::XBottom, s.showAxisBottom);
    m_plot->setAxisVisible(QwtAxis::XTop, s.showAxisTop);
    m_plot->setAxisVisible(QwtAxis::YLeft, s.showAxisLeft);
    m_plot->setAxisVisible(QwtAxis::YRight, s.showAxisRight);

    // Top always mirrors the bottom range; the right side mirrors the left
    // unless a second unit owns it.
    m_plot->mirrorTop = s.showAxisTop;
    m_plot->mirrorRight = !m_dualAxis;
}

void PlotPanel::rebuildAxisDraws()
{
    const AppSettings& s = Settings::instance().values();

    m_bottomDraw = new TimeScaleDraw;
    m_bottomDraw->configure(m_axisMode, m_axisMode, qint64(originMSecs()), 0.0);
    m_plot->setAxisScaleEngine(QwtAxis::XBottom, new QwtLinearScaleEngine);
    m_plot->setAxisScaleDraw(QwtAxis::XBottom, m_bottomDraw);

    TimeAxisMode topMode = m_axisMode;
    switch (s.topAxisMode) {
    case TopAxisMode::FollowBottom: topMode = m_axisMode; break;
    case TopAxisMode::Relative:     topMode = TimeAxisMode::Relative; break;
    case TopAxisMode::Absolute:     topMode = TimeAxisMode::Absolute; break;
    case TopAxisMode::SampleIndex:  topMode = TimeAxisMode::SampleIndex; break;
    }

    m_topDraw = new TimeScaleDraw;
    m_topDraw->configure(m_axisMode, topMode, qint64(originMSecs()), 0.0);
    m_plot->setAxisScaleEngine(QwtAxis::XTop, new QwtLinearScaleEngine);
    m_plot->setAxisScaleDraw(QwtAxis::XTop, m_topDraw);

    m_plot->setAxisScaleEngine(QwtAxis::YLeft, new QwtLinearScaleEngine);
    m_plot->setAxisScaleDraw(QwtAxis::YLeft, new QwtScaleDraw);
    m_plot->setAxisScaleEngine(QwtAxis::YRight, new QwtLinearScaleEngine);
    m_plot->setAxisScaleDraw(QwtAxis::YRight, new QwtScaleDraw);

    updateSpan();
    refreshAxisTitles();
}

void PlotPanel::updateSpan()
{
    if (!m_bottomDraw)
        return;
    const double span = m_hasX ? (m_xMax - m_xMin) : 0.0;
    m_bottomDraw->setSpan(span);
    if (m_topDraw) {
        m_topDraw->setSpan(span);
        m_topDraw->setOriginMSecs(qint64(originMSecs()));
    }
}

void PlotPanel::refreshAxisTitles()
{
    if (!m_plot)
        return;

    const AppSettings& s = Settings::instance().values();

    m_plot->setAxisTitle(QwtAxis::YLeft,
                         m_leftUnit.isEmpty() ? tr("数值") : tr("数值 [%1]").arg(m_leftUnit));
    if (m_dualAxis && !m_rightUnit.isEmpty())
        m_plot->setAxisTitle(QwtAxis::YRight, tr("数值 [%1]").arg(m_rightUnit));
    else
        m_plot->setAxisTitle(QwtAxis::YRight, m_leftUnit.isEmpty()
                                                     ? tr("数值")
                                                     : tr("数值 [%1]").arg(m_leftUnit));

    const auto titleFor = [this](TimeAxisMode mode) {
        switch (mode) {
        case TimeAxisMode::Absolute:
            return tr("绝对时间");
        case TimeAxisMode::SampleIndex:
            return tr("采样点序号");
        case TimeAxisMode::Relative:
            break;
        }
        if (m_origin.isValid())
            return tr("相对时间（自 %1 起）").arg(m_origin.toString(QStringLiteral("HH:mm:ss")));
        return tr("相对时间 (s)");
    };

    m_plot->setAxisTitle(QwtAxis::XBottom, titleFor(m_axisMode));

    TimeAxisMode topMode = m_axisMode;
    switch (s.topAxisMode) {
    case TopAxisMode::FollowBottom: topMode = m_axisMode; break;
    case TopAxisMode::Relative:     topMode = TimeAxisMode::Relative; break;
    case TopAxisMode::Absolute:     topMode = TimeAxisMode::Absolute; break;
    case TopAxisMode::SampleIndex:  topMode = TimeAxisMode::SampleIndex; break;
    }
    m_plot->setAxisTitle(QwtAxis::XTop,
                         topMode == m_axisMode ? titleFor(m_axisMode) : titleFor(topMode));
}

// ---------------------------------------------------------------------------
void PlotPanel::setSeries(const QVector<PlotSeries>& series)
{
    for (QwtPlotCurve* curve : m_curves) {
        curve->detach();
        delete curve;
    }
    m_curves.clear();

    const AppSettings& settings = Settings::instance().values();
    const bool antialiased = settings.antialiasing;
    const double lineWidth = settings.lineWidth;

    QString primaryUnit;
    for (const PlotSeries& s : series) {
        if (!s.unit.isEmpty()) {
            primaryUnit = s.unit;
            break;
        }
    }

    bool needRightAxis = false;
    m_hasX = false;
    m_xMin = 0.0;
    m_xMax = 0.0;
    m_leftMin = 0.0;
    m_leftMax = 1.0;
    m_rightMin = 0.0;
    m_rightMax = 1.0;

    bool leftInit = false;
    bool rightInit = false;

    for (const PlotSeries& s : series) {
        if (s.y.isEmpty())
            continue;

        const bool useRight = !s.unit.isEmpty() && !primaryUnit.isEmpty()
            && s.unit.compare(primaryUnit, Qt::CaseInsensitive) != 0;
        if (useRight)
            needRightAxis = true;

        const int n = s.y.size();
        const bool hasOwnX = s.x.size() == n;
        const int step = (n > kMaxPlotPoints) ? int(std::ceil(double(n) / kMaxPlotPoints)) : 1;

        // Missing samples (NaN, e.g. empty CSV cells) break the curve into segments
        // instead of drawing a misleading straight line across the gap.
        QVector<QVector<QPointF>> segments;
        QVector<QPointF> segment;
        segment.reserve(qMin(n, kMaxPlotPoints));
        bool tooManyGaps = false;

        auto flushSegment = [&]() {
            if (!segment.isEmpty()) {
                segments.append(segment);
                segment.clear();
                segment.reserve(qMin(n, kMaxPlotPoints));
            }
        };

        auto xAt = [&](int index) {
            return hasOwnX ? s.x.at(index) : double(index);
        };

        for (int i = 0; i < n; i += step) {
            const double x = xAt(i);
            const double y = s.y.at(i);
            if (!std::isfinite(y)) {
                flushSegment();
                if (segments.size() >= 64) {
                    tooManyGaps = true;
                    break;
                }
                continue;
            }

            if (std::isfinite(x)) {
                if (!m_hasX) {
                    m_xMin = m_xMax = x;
                    m_hasX = true;
                } else {
                    m_xMin = qMin(m_xMin, x);
                    m_xMax = qMax(m_xMax, x);
                }
            }

            if (useRight) {
                if (!rightInit) {
                    m_rightMin = m_rightMax = y;
                    rightInit = true;
                } else {
                    m_rightMin = qMin(m_rightMin, y);
                    m_rightMax = qMax(m_rightMax, y);
                }
            } else {
                if (!leftInit) {
                    m_leftMin = m_leftMax = y;
                    leftInit = true;
                } else {
                    m_leftMin = qMin(m_leftMin, y);
                    m_leftMax = qMax(m_leftMax, y);
                }
            }

            segment.append(QPointF(x, y));
        }
        flushSegment();

        if (segments.isEmpty())
            continue;

        if (tooManyGaps) {
            QVector<QPointF> merged;
            for (const QVector<QPointF>& part : segments)
                merged += part;
            segments.clear();
            segments.append(merged);
        }

        const QColor color = s.color.isValid() ? s.color : QColor("#5b9dff");
        for (int part = 0; part < segments.size(); ++part) {
            auto* curve = new QwtPlotCurve(part == 0 ? s.title : QString());
            curve->setSamples(segments.at(part));
            QPen pen(color);
            pen.setWidthF(lineWidth);
            curve->setPen(pen);
            curve->setRenderHint(QwtPlotItem::RenderAntialiased, antialiased);
            curve->setYAxis(useRight ? QwtAxis::YRight : QwtAxis::YLeft);
            if (part > 0)
                curve->setItemAttribute(QwtPlotItem::Legend, false);
            curve->attach(m_plot);
            m_curves.append(curve);
        }
    }

    m_dualAxis = needRightAxis;

    m_leftUnit = primaryUnit;
    m_rightUnit.clear();
    if (m_dualAxis) {
        for (const PlotSeries& s : series) {
            if (!s.unit.isEmpty() && s.unit.compare(primaryUnit, Qt::CaseInsensitive) != 0) {
                m_rightUnit = s.unit;
                break;
            }
        }
    }

    applyAxisVisibility();
    updateSpan();
    refreshAxisTitles();

    QwtText title(series.isEmpty()
                      ? tr("尚未选择通道：在左侧结构中勾选通道即可显示曲线")
                      : QString());
    title.setColor(theme::mutedText());
    title.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11));
    m_plot->setTitle(title);

    autoScale();

    // Qwt re-creates the legend entries whenever the items change.
    setLegendVisible(Settings::instance().values().legendVisible);
}

void PlotPanel::autoScale()
{
    if (m_hasX) {
        double margin = (m_xMax - m_xMin) * 0.005;
        if (!(margin > 0.0))
            margin = 0.5;
        m_plot->setAxisScale(QwtAxis::XBottom, m_xMin - margin, m_xMax + margin);
    }

    const auto applyY = [](QwtPlot* plot, int axis, double lo, double hi, bool valid) {
        if (!valid)
            return;
        double span = hi - lo;
        if (!(span > 0.0))
            span = (std::fabs(hi) > 0.0 ? std::fabs(hi) : 1.0);
        const double margin = span * 0.06;
        plot->setAxisScale(axis, lo - margin, hi + margin);
    };
    applyY(m_plot, QwtAxis::YLeft, m_leftMin, m_leftMax, m_hasX);
    if (m_dualAxis)
        applyY(m_plot, QwtAxis::YRight, m_rightMin, m_rightMax, m_hasX);

    m_plot->replot();
    if (m_zoomer)
        m_zoomer->setZoomBase();
}

void PlotPanel::clear()
{
    setSeries({});
    m_plot->replot();
}

void PlotPanel::setLegendVisible(bool visible)
{
    m_legendVisible = visible;
    if (!m_legend)
        return;

    // Qwt 7 re-shows the legend widget from QwtPlot::doLayout() as long as it
    // is inserted, so hiding it means detaching it from the plot instead.
    if (visible) {
        if (m_plot->legend() != m_legend) {
            m_legend->setParent(nullptr);
            m_plot->insertLegend(m_legend, QwtPlot::RightLegend);
        }
    } else if (m_plot->legend() == m_legend) {
        m_legend->setParent(nullptr);
        m_plot->insertLegend(nullptr, QwtPlot::RightLegend);
    }
    m_plot->replot();
}

void PlotPanel::setGridVisible(bool visible)
{
    if (m_grid)
        m_grid->setVisible(visible);
    m_plot->replot();
}

bool PlotPanel::legendVisible() const
{
    return m_legendVisible;
}

bool PlotPanel::gridVisible() const
{
    return m_grid && m_grid->isVisible();
}

QString PlotPanel::cursorText(double x, double yLeft, double yRight, bool hasRight) const
{
    QString xText;
    switch (m_axisMode) {
    case TimeAxisMode::Absolute:
        xText = QDateTime::fromMSecsSinceEpoch(qint64(std::llround(x)))
                    .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        break;
    case TimeAxisMode::Relative:
        xText = formatElapsed(x);
        break;
    case TimeAxisMode::SampleIndex:
        xText = tr("采样点 %1").arg(qRound64(x));
        break;
    }

    if (hasRight && !m_rightUnit.isEmpty()) {
        return QStringLiteral("%1    %2 = %3    %4 = %5")
            .arg(xText,
                 m_leftUnit.isEmpty() ? tr("数值") : m_leftUnit,
                 QString::number(yLeft, 'g', 8),
                 m_rightUnit,
                 QString::number(yRight, 'g', 8));
    }

    return QStringLiteral("%1    y = %2").arg(xText).arg(yLeft, 0, 'g', 8);
}

bool PlotPanel::exportImage(const QString& filePath, QString* error)
{
    const QPixmap pixmap = m_plot->grab();
    if (pixmap.isNull()) {
        if (error)
            *error = tr("绘图区为空，无法导出。");
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
