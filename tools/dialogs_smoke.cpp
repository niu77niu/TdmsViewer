// ---------------------------------------------------------------------------
// dialogs_smoke.cpp - renders the dialogs offscreen for layout inspection.
// Usage: tdmsdialogs_smoke [output-dir]
// ---------------------------------------------------------------------------
#include <QApplication>
#include <QDir>
#include <QFont>
#include <QListWidget>
#include <QMenu>
#include <QStyleFactory>

#include "ui/Icons.h"

#include "core/Settings.h"
#include "ui/ExportDialog.h"
#include "ui/ImportDialog.h"
#include "ui/MergeDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/Theme.h"

namespace {

void applyRound(tdms::ThemeMode mode)
{
    // Session only: the smoke test must not overwrite the user's stored preferences.
    tdms::AppSettings values = tdms::Settings::instance().values();
    values.theme = mode;
    tdms::Settings::instance().overrideValues(values);
    tdms::theme::apply(mode);
}

void renderDialogs(const QString& dir, const QString& suffix)
{
    tdms::ImportDialog import(QStringLiteral("samples/import_demo.csv"));
    import.resize(780, 540);
    import.show();
    import.grab().save(dir + QStringLiteral("/dialog_import_") + suffix
                       + QStringLiteral(".png"));
    import.close();

    tdms::ExportDialog exportDialog(true, true, QStringLiteral("SineRenamed"));
    exportDialog.resize(560, 430);
    exportDialog.show();
    exportDialog.grab().save(dir + QStringLiteral("/dialog_export_") + suffix
                             + QStringLiteral(".png"));
    exportDialog.close();

    tdms::MergeDialog merge(QStringLiteral("samples/demo.tdm"));
    merge.resize(760, 580);
    merge.show();
    merge.grab().save(dir + QStringLiteral("/dialog_merge_") + suffix
                      + QStringLiteral(".png"));
    merge.close();

    tdms::SettingsDialog prefs;
    prefs.resize(720, 470);
    prefs.show();
    auto* categories = prefs.findChild<QListWidget*>(QStringLiteral("categoryList"));
    const QStringList pageNames = { QStringLiteral("appearance"), QStringLiteral("plot"),
                                    QStringLiteral("3d"), QStringLiteral("export") };
    for (int page = 0; page < pageNames.size(); ++page) {
        if (categories)
            categories->setCurrentRow(page);
        prefs.grab().save(dir + QStringLiteral("/dialog_settings_") + pageNames.at(page)
                          + QLatin1Char('_') + suffix + QStringLiteral(".png"));
    }
    prefs.close();

    // A popup menu, to check that action icons are rendered in both themes.
    QMenu menu;
    menu.addAction(tdms::icons::get(QStringLiteral("new")), QStringLiteral("新建文件…"));
    menu.addAction(tdms::icons::get(QStringLiteral("open")), QStringLiteral("打开文件…"));
    menu.addAction(tdms::icons::get(QStringLiteral("save")), QStringLiteral("保存"));
    menu.addSeparator();
    menu.addAction(tdms::icons::get(QStringLiteral("import")), QStringLiteral("导入外部数据…"));
    menu.addAction(tdms::icons::get(QStringLiteral("merge")), QStringLiteral("合并 TDM/TDMS 文件…"));
    menu.addAction(tdms::icons::get(QStringLiteral("export")), QStringLiteral("导出 TXT / CSV…"));
    menu.addSeparator();
    menu.addAction(tdms::icons::get(QStringLiteral("settings")), QStringLiteral("首选项…"));
    menu.resize(menu.sizeHint());
    menu.grab().save(dir + QStringLiteral("/menu_") + suffix + QStringLiteral(".png"));
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TDMS Viewer"));
    QApplication::setOrganizationName(QStringLiteral("TdmsViewer"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));

    const QString dir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    QDir().mkpath(dir);

    applyRound(tdms::ThemeMode::Dark);
    renderDialogs(dir, QStringLiteral("dark"));

    applyRound(tdms::ThemeMode::Light);
    renderDialogs(dir, QStringLiteral("light"));

    return 0;
}
