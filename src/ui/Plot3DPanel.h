// ---------------------------------------------------------------------------
// Plot3DPanel.h - Qwt 7 (qwt::plot3d) view of several channels at once.
//
// Every channel becomes one 3D polyline: X = time, Y = channel offset,
// Z = amplitude (optionally normalised per channel).
// ---------------------------------------------------------------------------
#pragma once

#include <QVector>
#include <QWidget>

#include "ui/PlotPanel.h"   // PlotSeries

class QAction;
class QComboBox;
class Qwt3DPlot;
class Qwt3DLine;

namespace tdms {

class Plot3DPanel : public QWidget
{
    Q_OBJECT
public:
    explicit Plot3DPanel(QWidget* parent = nullptr);
    ~Plot3DPanel() override;

    void setSeries(const QVector<PlotSeries>& series, const QString& xAxisTitle);
    void clear();
    void applySettings();
    void resetView();
    bool exportImage(const QString& filePath, QString* error);
    bool hasData() const { return !m_lines.isEmpty(); }

signals:
    void cursorMoved(const QString& text);

private:
    void rebuild();
    void applyTheme();
    void refreshToolIcons();
    void updateAxisLabels();

    Qwt3DPlot* m_plot = nullptr;
    QVector<Qwt3DLine*> m_lines;
    QVector<PlotSeries> m_series;
    QString m_xTitle;
    QString m_zTitle;

    QAction* m_actReset = nullptr;
    QAction* m_actNormalize = nullptr;
    QAction* m_actImage = nullptr;
    QComboBox* m_styleCombo = nullptr;
    bool m_syncing = false;
};

} // namespace tdms
