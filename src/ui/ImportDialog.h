// ---------------------------------------------------------------------------
// ImportDialog.h - choose which columns of a TXT/CSV file to import.
// ---------------------------------------------------------------------------
#pragma once

#include <QDialog>
#include <QVector>

#include "core/TableData.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTableWidget;

namespace tdms {

class ImportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ImportDialog(const QString& filePath, QWidget* parent = nullptr);

    QVector<int> selectedColumns() const;
    QString groupName() const;
    bool writeToDocument() const;
    int xColumn() const;
    QString delimiter() const;
    const DataTable& table() const { return m_table; }
    bool firstRowHeader() const;

private:
    void reload();
    void updateSummary();
    bool parseOptions(ImportOptions* options) const;

    QString m_filePath;
    DataTable m_table;
    QComboBox* m_delimiterCombo = nullptr;
    QCheckBox* m_headerCheck = nullptr;
    QTableWidget* m_tableWidget = nullptr;
    QLineEdit* m_groupEdit = nullptr;
    QRadioButton* m_writeRadio = nullptr;
    QRadioButton* m_plotRadio = nullptr;
    QComboBox* m_xCombo = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_okButton = nullptr;
    bool m_loading = false;
};

} // namespace tdms
