// ---------------------------------------------------------------------------
// MergeDialog.h - combine two TDM/TDMS files into a new one.
// ---------------------------------------------------------------------------
#pragma once

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace tdms {

class MergeDialog : public QDialog
{
    Q_OBJECT
public:
    struct Result {
        QString pathA;
        QString pathB;
        QString outPath;
        int mode = 0;
        QStringList groupsA;
        QStringList groupsB;
        bool openAfter = true;
    };

    explicit MergeDialog(const QString& currentPath, QWidget* parent = nullptr);

    Result result() const { return m_result; }

private:
    void chooseFile(bool primary);
    void chooseOutput();
    void loadFile(bool primary);
    void updateOutputSuggestion();
    void refreshTree();
    void validate();

    Result m_result;
    QLineEdit* m_pathA = nullptr;
    QLineEdit* m_pathB = nullptr;
    QLineEdit* m_outPath = nullptr;
    QComboBox* m_mode = nullptr;
    QTreeWidget* m_tree = nullptr;
    QCheckBox* m_openAfter = nullptr;
    QLabel* m_info = nullptr;
    QPushButton* m_okButton = nullptr;
    bool m_loading = false;
    QTreeWidgetItem* m_rootA = nullptr;
    QTreeWidgetItem* m_rootB = nullptr;
};

} // namespace tdms
