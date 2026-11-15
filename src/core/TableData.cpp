// ---------------------------------------------------------------------------
// TableData.cpp
// ---------------------------------------------------------------------------
#include "TableData.h"

#include <QFile>
#include <QRegularExpression>
#include <QStringConverter>
#include <QVector>
#include <algorithm>
#include <limits>

namespace tdms {
namespace {

QString decodeFileContent(const QByteArray& bytes, QString* encoding)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder.decode(bytes);
    if (!decoder.hasError()) {
        if (encoding)
            *encoding = QStringLiteral("UTF-8");
    } else {
        text = QString::fromLocal8Bit(bytes);
        if (encoding)
            *encoding = QStringLiteral("本地编码");
    }
    if (!text.isEmpty() && text.at(0) == QChar(0xFEFF))
        text.remove(0, 1);
    return text;
}

bool isCommentLine(const QString& line)
{
    const QString trimmed = line.trimmed();
    return trimmed.startsWith(QLatin1Char('#'));
}

// Splits one record, honouring double quotes ("a,b" and "" escapes).
void splitRecord(const QString& line, QChar delimiter, bool whitespaceMode, QStringList* fields)
{
    fields->clear();
    if (whitespaceMode) {
        QString current;
        bool inQuotes = false;
        for (const QChar& c : line) {
            if (c == QLatin1Char('"')) {
                inQuotes = !inQuotes;
                continue;
            }
            if (!inQuotes && (c == QLatin1Char(' ') || c == QLatin1Char('\t'))) {
                if (!current.isEmpty()) {
                    fields->append(current);
                    current.clear();
                }
                continue;
            }
            current.append(c);
        }
        if (!current.isEmpty())
            fields->append(current);
        return;
    }

    QString current;
    bool inQuotes = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    current.append(QLatin1Char('"'));
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                current.append(c);
            }
            continue;
        }
        if (c == QLatin1Char('"')) {
            inQuotes = true;
        } else if (c == delimiter) {
            fields->append(current.trimmed());
            current.clear();
        } else {
            current.append(c);
        }
    }
    fields->append(current.trimmed());
}

int countOutsideQuotes(const QString& line, QChar delimiter)
{
    int count = 0;
    bool inQuotes = false;
    for (const QChar& c : line) {
        if (c == QLatin1Char('"'))
            inQuotes = !inQuotes;
        else if (!inQuotes && c == delimiter)
            ++count;
    }
    return count;
}

QChar detectDelimiter(const QStringList& lines)
{
    const QVector<QChar> candidates = { QChar(u'\t'), QChar(u','), QChar(u';'), QChar(u'|') };
    QChar best;
    int bestScore = 0;

    for (const QChar& candidate : candidates) {
        QVector<int> counts;
        for (const QString& line : lines) {
            if (line.trimmed().isEmpty())
                continue;
            counts.append(countOutsideQuotes(line, candidate));
            if (counts.size() >= 20)
                break;
        }
        if (counts.isEmpty())
            continue;
        const int expected = counts.first();
        if (expected <= 0)
            continue;
        int matches = 0;
        for (int c : counts)
            if (c == expected)
                ++matches;
        const int score = matches * 100 + expected;
        if (score > bestScore) {
            bestScore = score;
            best = candidate;
        }
    }
    return best;
}

void splitHeaderUnit(const QString& raw, QString* name, QString* unit)
{
    QString text = raw.trimmed();
    *unit = QString();

    if (text.endsWith(QLatin1Char(']'))) {
        const int open = text.lastIndexOf(QLatin1Char('['));
        if (open > 0) {
            *unit = text.mid(open + 1, text.size() - open - 2).trimmed();
            text = text.left(open).trimmed();
        }
    } else if (text.endsWith(QLatin1Char(')'))) {
        const int open = text.lastIndexOf(QLatin1Char('('));
        if (open > 0) {
            *unit = text.mid(open + 1, text.size() - open - 2).trimmed();
            text = text.left(open).trimmed();
        }
    }
    *name = text.isEmpty() ? raw.trimmed() : text;
}

bool parseNumber(const QString& raw, QChar delimiter, double* out)
{
    QString text = raw.trimmed();
    if (text.isEmpty())
        return false;

    bool ok = false;
    *out = text.toDouble(&ok);
    if (ok)
        return true;

    // European style numbers in semicolon separated files: 1.234,56
    if (delimiter == QLatin1Char(';') && text.contains(QLatin1Char(','))) {
        QString normalized = text;
        normalized.remove(QLatin1Char('.'));
        normalized.replace(QLatin1Char(','), QLatin1Char('.'));
        *out = normalized.toDouble(&ok);
        if (ok)
            return true;
    }

    // Values like "1 234.5" or with a trailing unit
    QString compact = text;
    compact.remove(QLatin1Char(' '));
    *out = compact.toDouble(&ok);
    return ok;
}

} // namespace

QStringList delimiterChoices()
{
    return { QStringLiteral("自动检测"), QStringLiteral("制表符 (Tab)"), QStringLiteral("逗号 (,)"),
             QStringLiteral("分号 (;)"), QStringLiteral("竖线 (|)"), QStringLiteral("空格") };
}

bool parseDataFile(const QString& path, const ImportOptions& options,
                   DataTable* table, QString* error)
{
    table->columns.clear();
    table->rowCount = 0;
    table->warnings.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取文件：%1").arg(path);
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    if (bytes.isEmpty()) {
        *error = QStringLiteral("文件为空。");
        return false;
    }

    QString encoding;
    const QString text = decodeFileContent(bytes, &encoding);
    table->encodingUsed = encoding;

    QStringList allLines = text.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    while (!allLines.isEmpty() && allLines.last().trimmed().isEmpty())
        allLines.removeLast();

    // Skip the leading metadata comment block written by the exporter.
    int firstDataLine = 0;
    while (firstDataLine < allLines.size() && isCommentLine(allLines.at(firstDataLine)))
        ++firstDataLine;
    firstDataLine += qMax(0, options.skipRows);

    if (firstDataLine >= allLines.size()) {
        *error = QStringLiteral("文件中没有可解析的数据行。");
        return false;
    }

    QStringList sample;
    for (int i = firstDataLine; i < allLines.size() && sample.size() < 20; ++i) {
        if (!allLines.at(i).trimmed().isEmpty())
            sample.append(allLines.at(i));
    }

    bool whitespaceMode = false;
    QChar delimiter;
    if (options.delimiter.isEmpty()) {
        delimiter = detectDelimiter(sample);
        if (delimiter.isNull()) {
            whitespaceMode = true;
            delimiter = QChar(u'\t');
            table->delimiterUsed = QStringLiteral("空格");
        } else {
            table->delimiterUsed = QString(delimiter);
        }
    } else {
        const QString d = options.delimiter;
        if (d == QStringLiteral("制表符 (Tab)")) {
            delimiter = QChar(u'\t');
        } else if (d == QStringLiteral("逗号 (,)")) {
            delimiter = QChar(u',');
        } else if (d == QStringLiteral("分号 (;)")) {
            delimiter = QChar(u';');
        } else if (d == QStringLiteral("竖线 (|)")) {
            delimiter = QChar(u'|');
        } else if (d == QStringLiteral("空格")) {
            whitespaceMode = true;
            delimiter = QChar(u'\t');
        } else {
            delimiter = detectDelimiter(sample);
            if (delimiter.isNull()) {
                whitespaceMode = true;
                delimiter = QChar(u'\t');
            }
        }
        table->delimiterUsed = whitespaceMode ? QStringLiteral("空格") : QString(delimiter);
    }

    QVector<QStringList> records;
    int maxFields = 0;
    for (int i = firstDataLine; i < allLines.size(); ++i) {
        const QString& line = allLines.at(i);
        if (line.trimmed().isEmpty())
            continue;
        if (isCommentLine(line))
            continue;
        QStringList fields;
        splitRecord(line, delimiter, whitespaceMode, &fields);
        if (fields.isEmpty())
            continue;
        maxFields = qMax(maxFields, fields.size());
        records.append(fields);
    }

    if (records.isEmpty()) {
        *error = QStringLiteral("文件中没有可解析的数据行。");
        return false;
    }

    QStringList headerFields;
    int firstRecord = 0;
    if (options.firstRowHasHeader) {
        headerFields = records.first();
        firstRecord = 1;
    }

    const int dataRows = records.size() - firstRecord;
    table->rowCount = dataRows;

    for (int col = 0; col < maxFields; ++col) {
        DataColumn column;
        QString rawName;
        QString unit;
        if (options.firstRowHasHeader && col < headerFields.size()) {
            splitHeaderUnit(headerFields.at(col), &rawName, &unit);
        } else {
            rawName = QStringLiteral("列%1").arg(col + 1);
        }
        if (rawName.isEmpty())
            rawName = QStringLiteral("列%1").arg(col + 1);
        column.name = rawName;
        column.unit = unit;

        int nonEmpty = 0;
        int numericHits = 0;
        QVector<double> numbers;
        QStringList texts;
        numbers.reserve(dataRows);
        texts.reserve(dataRows);

        for (int r = firstRecord; r < records.size(); ++r) {
            const QStringList& fields = records.at(r);
            const QString cell = col < fields.size() ? fields.at(col) : QString();
            if (cell.trimmed().isEmpty()) {
                ++column.emptyCount;
                numbers.append(std::numeric_limits<double>::quiet_NaN());
                texts.append(QString());
                continue;
            }
            ++nonEmpty;
            double value = 0.0;
            if (parseNumber(cell, delimiter, &value)) {
                ++numericHits;
                numbers.append(value);
                texts.append(QString());
            } else {
                numbers.append(std::numeric_limits<double>::quiet_NaN());
                texts.append(cell);
            }
        }

        column.numeric = nonEmpty > 0 && numericHits == nonEmpty;
        if (column.numeric) {
            column.values = numbers;
        } else {
            column.texts = texts;
            if (numericHits > 0 && nonEmpty > numericHits)
                table->warnings.append(
                    QStringLiteral("列 \"%1\" 含非数值内容，将作为文本列处理。").arg(column.name));
        }
        table->columns.append(column);
    }

    return true;
}

} // namespace tdms
