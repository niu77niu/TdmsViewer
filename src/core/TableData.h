// ---------------------------------------------------------------------------
// TableData.h - parsing of delimited text files (TXT / CSV) for import.
// ---------------------------------------------------------------------------
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace tdms {

struct ImportOptions {
    QString delimiter;          // empty = auto detect
    bool firstRowHasHeader = true;
    int skipRows = 0;
};

struct DataColumn {
    QString name;
    QString unit;
    bool numeric = false;
    QVector<double> values;
    QStringList texts;
    int emptyCount = 0;

    int size() const { return numeric ? values.size() : texts.size(); }
};

struct DataTable {
    QVector<DataColumn> columns;
    int rowCount = 0;
    QString delimiterUsed;
    QString encodingUsed;
    QStringList warnings;
};

bool parseDataFile(const QString& path, const ImportOptions& options,
                   DataTable* table, QString* error);

// "#" comment lines at the top of exported files are ignored during import.
QStringList delimiterChoices();

} // namespace tdms
