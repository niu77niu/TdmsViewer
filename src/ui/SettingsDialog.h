// ---------------------------------------------------------------------------
// SettingsDialog.h - preferences (appearance / plot / export defaults).
// ---------------------------------------------------------------------------
#pragma once

#include <QDialog>

#include "core/Settings.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;

namespace tdms {

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // Last values confirmed with OK / Apply.
    const AppSettings& result() const { return m_result; }

public slots:
    void accept() override;

private:
    QWidget* buildAppearancePage();
    QWidget* buildPlotPage();
    QWidget* build3DPage();
    QWidget* buildExportPage();

    void loadValues(const AppSettings& values);
    AppSettings collect() const;
    void apply();
    void restoreDefaults();
    void openConfigFolder();

    QListWidget* m_categories = nullptr;
    QStackedWidget* m_pages = nullptr;
    QPushButton* m_applyButton = nullptr;

    QRadioButton* m_themeSystem = nullptr;
    QRadioButton* m_themeLight = nullptr;
    QRadioButton* m_themeDark = nullptr;

    QRadioButton* m_axisRelative = nullptr;
    QRadioButton* m_axisAbsolute = nullptr;
    QRadioButton* m_axisSample = nullptr;

    QCheckBox* m_axisBottom = nullptr;
    QCheckBox* m_axisTop = nullptr;
    QCheckBox* m_axisLeft = nullptr;
    QCheckBox* m_axisRight = nullptr;
    QComboBox* m_topAxisMode = nullptr;

    QCheckBox* m_grid = nullptr;
    QCheckBox* m_legend = nullptr;
    QCheckBox* m_antialias = nullptr;
    QCheckBox* m_assumeUnitInterval = nullptr;
    QDoubleSpinBox* m_lineWidth = nullptr;

    QCheckBox* m_enable3D = nullptr;
    QCheckBox* m_normalize3D = nullptr;
    QComboBox* m_style3D = nullptr;

    QLabel* m_configPath = nullptr;

    QComboBox* m_exportFormat = nullptr;
    QSpinBox* m_precision = nullptr;
    QCheckBox* m_includeIndex = nullptr;
    QCheckBox* m_includeMeta = nullptr;

    AppSettings m_result;
};

} // namespace tdms
