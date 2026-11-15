// ---------------------------------------------------------------------------
// Settings.cpp - INI backed preferences (no registry access at all).
// ---------------------------------------------------------------------------
#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QtGlobal>

namespace tdms {

namespace {

int clampInt(int value, int lo, int hi)
{
    return qBound(lo, value, hi);
}

ThemeMode themeFromInt(int value)
{
    switch (value) {
    case 1: return ThemeMode::Light;
    case 2: return ThemeMode::Dark;
    default: return ThemeMode::System;
    }
}

TimeAxisMode axisFromInt(int value)
{
    switch (value) {
    case 1: return TimeAxisMode::Absolute;
    case 2: return TimeAxisMode::SampleIndex;
    default: return TimeAxisMode::Relative;
    }
}

TopAxisMode topAxisFromInt(int value)
{
    switch (value) {
    case 1: return TopAxisMode::Relative;
    case 2: return TopAxisMode::Absolute;
    case 3: return TopAxisMode::SampleIndex;
    default: return TopAxisMode::FollowBottom;
    }
}

Trace3DStyle trace3DFromInt(int value)
{
    switch (value) {
    case 1: return Trace3DStyle::Lines;
    case 2: return Trace3DStyle::Dots;
    default: return Trace3DStyle::Tube;
    }
}

// Clamps everything that a QSpinBox or an older INI file could get wrong.
AppSettings normalise(AppSettings v)
{
    v.lineWidth = qBound(0.5, v.lineWidth, 6.0);
    v.exportPrecision = clampInt(v.exportPrecision, 3, 17);
    if (v.exportFormat.compare(QLatin1String("txt"), Qt::CaseInsensitive) == 0)
        v.exportFormat = QStringLiteral("txt");
    else
        v.exportFormat = QStringLiteral("csv");
    return v;
}

// Returns true when the directory can host our INI file.
bool probeWritable(const QString& dir)
{
    if (dir.isEmpty())
        return false;
    if (!QDir().mkpath(dir))
        return false;

    QFile probe(dir + QStringLiteral("/.tdmsviewer_probe"));
    if (!probe.open(QIODevice::WriteOnly))
        return false;
    probe.close();
    probe.remove();
    return true;
}

} // namespace

QString Settings::configDirectory()
{
    const QByteArray env = qgetenv("TDMSVIEWER_CONFIG");
    if (!env.isEmpty()) {
        const QString path = QString::fromLocal8Bit(env);
        return QFileInfo(path).absolutePath();
    }

    const QString portable = QCoreApplication::applicationDirPath()
        + QStringLiteral("/config");
    if (probeWritable(portable))
        return portable;

    QString fallback = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (fallback.isEmpty())
        fallback = QCoreApplication::applicationDirPath();
    return fallback;
}

QString Settings::configFilePath()
{
    const QByteArray env = qgetenv("TDMSVIEWER_CONFIG");
    if (!env.isEmpty())
        return QFileInfo(QString::fromLocal8Bit(env)).absoluteFilePath();

    return configDirectory() + QStringLiteral("/TdmsViewer.ini");
}

bool Settings::isPortableConfig()
{
    return configFilePath()
        == QCoreApplication::applicationDirPath() + QStringLiteral("/config/TdmsViewer.ini");
}

Settings::Settings()
{
    load();
    // Create the INI file on first run so that the documented location always
    // exists and can be inspected / edited by hand.
    if (!QFile::exists(configFilePath()))
        save();
}

Settings& Settings::instance()
{
    static Settings s;
    return s;
}

QString Settings::themeName(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::Light: return QObject::tr("浅色");
    case ThemeMode::Dark:  return QObject::tr("深色");
    case ThemeMode::System: break;
    }
    return QObject::tr("跟随系统");
}

QString Settings::axisName(TimeAxisMode mode)
{
    switch (mode) {
    case TimeAxisMode::Absolute: return QObject::tr("绝对时间");
    case TimeAxisMode::SampleIndex: return QObject::tr("采样点序号");
    case TimeAxisMode::Relative: break;
    }
    return QObject::tr("相对时间");
}

QString Settings::topAxisName(TopAxisMode mode)
{
    switch (mode) {
    case TopAxisMode::Relative: return QObject::tr("相对时间");
    case TopAxisMode::Absolute: return QObject::tr("绝对时间");
    case TopAxisMode::SampleIndex: return QObject::tr("采样点序号");
    case TopAxisMode::FollowBottom: break;
    }
    return QObject::tr("与下轴相同");
}

QString Settings::trace3DStyleName(Trace3DStyle style)
{
    switch (style) {
    case Trace3DStyle::Lines: return QObject::tr("细线");
    case Trace3DStyle::Dots:  return QObject::tr("散点");
    case Trace3DStyle::Tube: break;
    }
    return QObject::tr("管线");
}

void Settings::load()
{
    const AppSettings def;
    QSettings store(configFilePath(), QSettings::IniFormat);

    AppSettings v = def;
    v.theme = themeFromInt(store.value(QStringLiteral("appearance/theme"),
                                       int(def.theme)).toInt());
    v.xAxisMode = axisFromInt(store.value(QStringLiteral("plot/xAxisMode"),
                                          int(def.xAxisMode)).toInt());
    v.gridVisible = store.value(QStringLiteral("plot/grid"), def.gridVisible).toBool();
    v.legendVisible = store.value(QStringLiteral("plot/legend"), def.legendVisible).toBool();
    v.antialiasing = store.value(QStringLiteral("plot/antialiasing"), def.antialiasing).toBool();
    v.lineWidth = store.value(QStringLiteral("plot/lineWidth"), def.lineWidth).toDouble();
    v.assumeUnitSampleInterval =
        store.value(QStringLiteral("plot/assumeUnitSampleInterval"),
                    def.assumeUnitSampleInterval).toBool();

    v.showAxisBottom = store.value(QStringLiteral("axes/bottom"), def.showAxisBottom).toBool();
    v.showAxisTop = store.value(QStringLiteral("axes/top"), def.showAxisTop).toBool();
    v.showAxisLeft = store.value(QStringLiteral("axes/left"), def.showAxisLeft).toBool();
    v.showAxisRight = store.value(QStringLiteral("axes/right"), def.showAxisRight).toBool();
    v.topAxisMode = topAxisFromInt(store.value(QStringLiteral("axes/topMode"),
                                               int(def.topAxisMode)).toInt());

    v.show3DView = store.value(QStringLiteral("plot3d/enabled"), def.show3DView).toBool();
    v.trace3DStyle = trace3DFromInt(store.value(QStringLiteral("plot3d/style"),
                                                int(def.trace3DStyle)).toInt());
    v.trace3DNormalize = store.value(QStringLiteral("plot3d/normalize"),
                                     def.trace3DNormalize).toBool();

    v.exportFormat = store.value(QStringLiteral("export/format"), def.exportFormat).toString();
    if (v.exportFormat.compare(QLatin1String("txt"), Qt::CaseInsensitive) != 0)
        v.exportFormat = QStringLiteral("csv");
    v.exportPrecision = store.value(QStringLiteral("export/precision"),
                                    def.exportPrecision).toInt();
    v.exportIncludeIndex = store.value(QStringLiteral("export/includeIndex"),
                                       def.exportIncludeIndex).toBool();
    v.exportIncludeMeta = store.value(QStringLiteral("export/includeMeta"),
                                      def.exportIncludeMeta).toBool();

    m_values = normalise(v);
}

void Settings::save() const
{
    QSettings store(configFilePath(), QSettings::IniFormat);
    const AppSettings& v = m_values;
    store.setValue(QStringLiteral("appearance/theme"), int(v.theme));
    store.setValue(QStringLiteral("plot/xAxisMode"), int(v.xAxisMode));
    store.setValue(QStringLiteral("plot/grid"), v.gridVisible);
    store.setValue(QStringLiteral("plot/legend"), v.legendVisible);
    store.setValue(QStringLiteral("plot/antialiasing"), v.antialiasing);
    store.setValue(QStringLiteral("plot/lineWidth"), v.lineWidth);
    store.setValue(QStringLiteral("plot/assumeUnitSampleInterval"), v.assumeUnitSampleInterval);
    store.setValue(QStringLiteral("axes/bottom"), v.showAxisBottom);
    store.setValue(QStringLiteral("axes/top"), v.showAxisTop);
    store.setValue(QStringLiteral("axes/left"), v.showAxisLeft);
    store.setValue(QStringLiteral("axes/right"), v.showAxisRight);
    store.setValue(QStringLiteral("axes/topMode"), int(v.topAxisMode));
    store.setValue(QStringLiteral("plot3d/enabled"), v.show3DView);
    store.setValue(QStringLiteral("plot3d/style"), int(v.trace3DStyle));
    store.setValue(QStringLiteral("plot3d/normalize"), v.trace3DNormalize);
    store.setValue(QStringLiteral("export/format"), v.exportFormat);
    store.setValue(QStringLiteral("export/precision"), v.exportPrecision);
    store.setValue(QStringLiteral("export/includeIndex"), v.exportIncludeIndex);
    store.setValue(QStringLiteral("export/includeMeta"), v.exportIncludeMeta);
    store.sync();
}

void Settings::setValues(const AppSettings& values)
{
    m_values = normalise(values);
    save();
    emit changed();
}

void Settings::overrideValues(const AppSettings& values)
{
    m_values = normalise(values);
    emit changed();
}

void Settings::resetToDefaults()
{
    setValues(AppSettings());
}

void Settings::reload()
{
    load();
    emit changed();
}

} // namespace tdms
