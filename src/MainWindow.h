// ---------------------------------------------------------------------------
// MainWindow.h
// ---------------------------------------------------------------------------
#pragma once

#include <QHash>
#include <QMainWindow>
#include <QPair>
#include <QSet>
#include <QVector>

#include "core/Settings.h"
#include "core/TdmsCore.h"

class QAction;
class QActionGroup;
class QLabel;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace tdms {

class Plot3DPanel;
class PlotPanel;
class PropertyPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openPath(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    // --- construction -------------------------------------------------------
    void createActions();
    void createMenus();
    void createToolBar();
    void createCentralWidgets();

    // --- settings / theme ---------------------------------------------------
    void openSettings();
    void applySettings();
    void refreshIcons();
    void updateThemeMenu();
    void updateAxisActions();
    void setTheme(ThemeMode mode);

    // --- document -----------------------------------------------------------
    void newFile();
    void openFile();
    bool openDocument(const QString& path);
    void closeFile();
    bool saveFile();
    bool saveFileAs();
    bool maybeSave();
    void markDirty();

    // --- views --------------------------------------------------------------
    void rebuildTree();
    void updateProperties();
    void updateActions();
    void updateStatus();
    void updateTitle();
    void refreshPlot();

    // --- operations ---------------------------------------------------------
    void importExternalData();
    void mergeFilesDialog();
    void exportData();
    void addGroup();
    void deleteSelection();
    void renameSelection();

    // --- slots --------------------------------------------------------------
    void onCurrentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onPropertyEdited(const QString& name, DDCDataType type, const QVariant& value);
    void onPropertyCreated(const QString& name, DDCDataType type, const QVariant& value);
    void showStatus(const QString& message);

    // --- helpers ------------------------------------------------------------
    QString channelKey(int group, int channel) const;
    QVector<double> loadChannelValues(int group, int channel, bool* ok);
    void clearCache();
    QString selectedPathText() const;
    int selectedGroup() const { return m_selKind == ObjKind::Group || m_selKind == ObjKind::Channel ? m_selGroup : -1; }
    int selectedChannel() const { return m_selKind == ObjKind::Channel ? m_selChannel : -1; }

    struct ExternalSeries {
        QString name;
        QString unit;
        QVector<double> x;
        QVector<double> y;
    };

    Document m_doc;
    QVector<ExternalSeries> m_external;

    QTreeWidget* m_tree = nullptr;
    PlotPanel* m_plot = nullptr;
    Plot3DPanel* m_plot3D = nullptr;
    QTabWidget* m_plotTabs = nullptr;
    PropertyPanel* m_props = nullptr;

    QLabel* m_statusFile = nullptr;
    QLabel* m_statusSelection = nullptr;
    QLabel* m_statusCursor = nullptr;

    QAction* m_actSave = nullptr;
    QAction* m_actSaveAs = nullptr;
    QAction* m_actClose = nullptr;
    QAction* m_actExport = nullptr;
    QAction* m_actImport = nullptr;
    QAction* m_actMerge = nullptr;
    QAction* m_actAddGroup = nullptr;
    QAction* m_actDelete = nullptr;
    QAction* m_actRename = nullptr;
    QAction* m_actRefresh = nullptr;
    QAction* m_actSettings = nullptr;
    QAction* m_actRestoreDefaults = nullptr;
    QAction* m_actThemeSystem = nullptr;
    QAction* m_actThemeLight = nullptr;
    QAction* m_actThemeDark = nullptr;
    QActionGroup* m_themeGroup = nullptr;
    // Order: bottom, top, left, right.
    QVector<QAction*> m_axisActions;

    QVector<QPair<QAction*, QString>> m_iconBindings;
    bool m_iconsDark = true;

    QSet<QString> m_checkedKeys;
    QHash<QString, QVector<double>> m_valueCache;

    ObjKind m_selKind = ObjKind::File;
    int m_selGroup = -1;
    int m_selChannel = -1;
    int m_selExternal = -1;   // index of selected external series (-2 = container)

    bool m_updatingTree = false;
};

} // namespace tdms
