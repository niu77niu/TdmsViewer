// ---------------------------------------------------------------------------
// Settings.h - application wide preferences.
//
// The preferences are stored in a plain INI file on disk - the Windows
// registry is never touched.  Resolution order:
//   1. the TDMSVIEWER_CONFIG environment variable (full file path)
//   2. <application dir>/config/TdmsViewer.ini  (portable, preferred)
//   3. %APPDATA%/TdmsViewer/TdmsViewer.ini       (fallback, read-only installs)
// ---------------------------------------------------------------------------
#pragma once

#include <QObject>
#include <QString>

namespace tdms {

enum class ThemeMode { System = 0, Light = 1, Dark = 2 };

// How the X axis of the plot is labelled.
enum class TimeAxisMode { Relative = 0, Absolute = 1, SampleIndex = 2 };

// What the upper X axis shows.
enum class TopAxisMode { FollowBottom = 0, Relative = 1, Absolute = 2, SampleIndex = 3 };

// Rendering style of the 3D traces.
enum class Trace3DStyle { Tube = 0, Lines = 1, Dots = 2 };

struct AppSettings {
    // appearance
    ThemeMode theme = ThemeMode::System;

    // plot
    TimeAxisMode xAxisMode = TimeAxisMode::Relative;
    bool gridVisible = true;
    bool legendVisible = true;
    bool antialiasing = true;
    double lineWidth = 1.6;
    bool assumeUnitSampleInterval = true;   // channels without wf_increment: assume 1 s

    // axes (all four sides are independent)
    bool showAxisBottom = true;
    bool showAxisTop = true;
    bool showAxisLeft = true;
    bool showAxisRight = true;
    TopAxisMode topAxisMode = TopAxisMode::Absolute;

    // 3D view
    bool show3DView = true;
    Trace3DStyle trace3DStyle = Trace3DStyle::Lines;
    bool trace3DNormalize = true;   // scale every channel to 0..1 before drawing

    // export defaults
    QString exportFormat = QStringLiteral("csv");
    int exportPrecision = 15;
    bool exportIncludeIndex = true;
    bool exportIncludeMeta = true;
};

class Settings : public QObject
{
    Q_OBJECT
public:
    static Settings& instance();

    const AppSettings& values() const { return m_values; }
    static AppSettings defaults() { return AppSettings(); }

    // Normalises, persists and emits changed().
    void setValues(const AppSettings& values);
    // Normalises and applies for this session only - the stored file is untouched.
    // Used by the --theme / --axis command line switches.
    void overrideValues(const AppSettings& values);
    void resetToDefaults();
    void reload();

    // Absolute path of the INI file currently in use (created on first save).
    static QString configFilePath();
    static QString configDirectory();
    // True when the file lives next to the executable (portable mode).
    static bool isPortableConfig();

    static QString themeName(ThemeMode mode);
    static QString axisName(TimeAxisMode mode);
    static QString topAxisName(TopAxisMode mode);
    static QString trace3DStyleName(Trace3DStyle style);

signals:
    void changed();

private:
    Settings();
    void load();
    void save() const;

    AppSettings m_values;
};

} // namespace tdms
