// ---------------------------------------------------------------------------
// PlotPanel.h - Qwt 7 based multi-channel plot.
//
//  * X axis is a time axis (relative / absolute / sample index)
//  * all four axis sides exist: bottom + top (time), left + right (values)
//  * every side can be shown or hidden from the settings / view menu
// ---------------------------------------------------------------------------
#pragma once

#include <QColor>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QWidget>

#include "core/Settings.h"

class QAction;
class QComboBox;
class QwtLegend;
class QwtPlot;
class QwtPlotCurve;
class QwtPlotGrid;
class QwtPlotCanvasZoomer;
class QwtPlotPanner;

namespace tdms {

class AxisSyncPlot;
class TimeScaleDraw;
class CrosshairOverlay;

struct PlotSeries {
    QString key;                 // unique identifier
    QString title;               // legend text
    QString unit;
    // X values in axis units: seconds (relative), msecs since epoch (absolute)
    // or the sample index. Empty -> the sample index is used.
    QVector<double> x;
    QVector<double> y;
    QColor color;
};

class PlotPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PlotPanel(QWidget* parent = nullptr);
    ~PlotPanel() override;

    void setSeries(const QVector<PlotSeries>& series);
    void clear();
    void autoScale();
    void setLegendVisible(bool visible);
    void setGridVisible(bool visible);
    bool legendVisible() const;
    bool gridVisible() const;
    bool hasData() const { return !m_curves.isEmpty(); }
    bool exportImage(const QString& filePath, QString* error);

    // Applies the persisted preferences (theme, grid/legend, line width, axes).
    void applySettings();
    void setTimeAxisMode(TimeAxisMode mode);
    TimeAxisMode timeAxisMode() const { return m_axisMode; }
    // Reference instant used to describe the relative axis ("since 09:30:00").
    void setTimeOrigin(const QDateTime& origin);

signals:
    void cursorMoved(const QString& text);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QString cursorText(double x, double yLeft, double yRight, bool hasRight) const;
    void applyTheme();
    void rebuildAxisDraws();
    void applyAxisVisibility();
    void refreshAxisTitles();
    void refreshToolIcons();
    void syncAxisCombo();
    void updateSpan();
    double originMSecs() const;

    AxisSyncPlot* m_plot = nullptr;
    QwtPlotGrid* m_grid = nullptr;
    QwtLegend* m_legend = nullptr;
    QwtPlotCanvasZoomer* m_zoomer = nullptr;
    QwtPlotPanner* m_panner = nullptr;
    CrosshairOverlay* m_overlay = nullptr;
    QVector<QwtPlotCurve*> m_curves;

    QAction* m_actFit = nullptr;
    QAction* m_actGrid = nullptr;
    QAction* m_actLegend = nullptr;
    QAction* m_actImage = nullptr;
    QAction* m_actAxes = nullptr;
    QComboBox* m_axisCombo = nullptr;

    bool m_dualAxis = false;
    QString m_leftUnit;
    QString m_rightUnit;

    TimeAxisMode m_axisMode = TimeAxisMode::Relative;
    TimeScaleDraw* m_bottomDraw = nullptr;
    TimeScaleDraw* m_topDraw = nullptr;
    QDateTime m_origin;

    bool m_hasX = false;
    double m_xMin = 0.0;
    double m_xMax = 0.0;
    double m_leftMin = 0.0;
    double m_leftMax = 1.0;
    double m_rightMin = 0.0;
    double m_rightMax = 1.0;

    bool m_syncingCombo = false;
    bool m_legendVisible = true;
};

} // namespace tdms
