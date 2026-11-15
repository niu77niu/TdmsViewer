// ---------------------------------------------------------------------------
// main.cpp - application entry point.
// ---------------------------------------------------------------------------
#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QStringList>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <QTabWidget>
#include <QTimer>

#include "MainWindow.h"
#include "core/Settings.h"
#include "ui/Theme.h"

namespace {

// Optional command line overrides, mainly used for documentation screenshots:
//   --theme light|dark|system
//   --axis relative|absolute|sample
void applyThemeArgument(const QStringList& arguments)
{
    const int index = arguments.indexOf(QStringLiteral("--theme"));
    if (index <= 0 || index + 1 >= arguments.size())
        return;

    const QString value = arguments.at(index + 1).toLower();
    tdms::AppSettings values = tdms::Settings::instance().values();
    if (value == QLatin1String("light"))
        values.theme = tdms::ThemeMode::Light;
    else if (value == QLatin1String("dark"))
        values.theme = tdms::ThemeMode::Dark;
    else if (value == QLatin1String("system"))
        values.theme = tdms::ThemeMode::System;
    else
        return;
    tdms::Settings::instance().overrideValues(values);
}

void applyAxisArgument(const QStringList& arguments)
{
    const int index = arguments.indexOf(QStringLiteral("--axis"));
    if (index <= 0 || index + 1 >= arguments.size())
        return;

    const QString value = arguments.at(index + 1).toLower();
    tdms::AppSettings values = tdms::Settings::instance().values();
    if (value == QLatin1String("absolute"))
        values.xAxisMode = tdms::TimeAxisMode::Absolute;
    else if (value == QLatin1String("sample"))
        values.xAxisMode = tdms::TimeAxisMode::SampleIndex;
    else if (value == QLatin1String("relative"))
        values.xAxisMode = tdms::TimeAxisMode::Relative;
    else
        return;
    tdms::Settings::instance().overrideValues(values);
}

} // namespace

int main(int argc, char* argv[])
{
    // Qwt 7 renders the 3D views through the modern OpenGL pipeline: request a
    // depth buffer and multisampling before the first QOpenGLWidget is created.
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TDMS Viewer"));
    QApplication::setOrganizationName(QStringLiteral("TdmsViewer"));
    QApplication::setApplicationVersion(QStringLiteral("1.1.0"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));

    // Window / taskbar icon. The multi-resolution .ico is embedded through
    // resources/app.qrc and used by Qt's icon engine as-is.
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/win/app.ico")));

    const QStringList arguments = QCoreApplication::arguments();
    applyThemeArgument(arguments);
    applyAxisArgument(arguments);

    // Palette + stylesheet of the persisted (or system) theme.
    tdms::theme::apply(tdms::Settings::instance().values().theme);

    tdms::MainWindow window;
    window.show();

    QString screenshotPath;
    const int shotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    if (shotIndex > 0 && shotIndex + 1 < arguments.size())
        screenshotPath = arguments.at(shotIndex + 1);

    for (int i = 1; i < arguments.size(); ++i) {
        const QString candidate = arguments.at(i);
        if (candidate.startsWith(QLatin1String("--"))) {
            ++i;   // skip the value of --theme / --screenshot
            continue;
        }
        if (QFileInfo::exists(candidate)) {
            window.openPath(candidate);
            break;
        }
    }

    // --tab curve|3d : select the plot tab (used for documentation screenshots).
    const int tabArg = arguments.indexOf(QStringLiteral("--tab"));
    if (tabArg > 0 && tabArg + 1 < arguments.size()) {
        const QString value = arguments.at(tabArg + 1).toLower();
        if (auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("plotTabs")))
            tabs->setCurrentIndex(value == QLatin1String("3d") ? 1 : 0);
    }

    // Hidden helper used for documentation screenshots / UI smoke tests.
    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(1500, &window, [&window, screenshotPath] {
            window.grab().save(screenshotPath);
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
