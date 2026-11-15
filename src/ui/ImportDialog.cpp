// ---------------------------------------------------------------------------
// ImportDialog.cpp
// ---------------------------------------------------------------------------
#include "ImportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace tdms {

ImportDialog::ImportDialog(const QString& filePath, QWidget* parent)
    : QDialog(parent)
    , m_filePath(filePath)
{
    setWindowTitle(tr("导入外部数据"));
    setModal(true);
    resize(760, 520);

    // --- options ------------------------------------------------------------
    auto* optionsBox = new QGroupBox(tr("解析选项"), this);
    auto* optionsLayout = new QHBoxLayout(optionsBox);

    m_delimiterCombo = new QComboBox(this);
    m_delimiterCombo->addItems(delimiterChoices());

    m_headerCheck = new QCheckBox(tr("首行为列标题"), this);
    m_headerCheck->setChecked(true);

    auto* reloadButton = new QPushButton(tr("重新解析"), this);
    reloadButton->setObjectName(QStringLiteral("secondaryButton"));

    optionsLayout->addWidget(new QLabel(tr("分隔符"), this));
    optionsLayout->addWidget(m_delimiterCombo, 1);
    optionsLayout->addWidget(m_headerCheck);
    optionsLayout->addWidget(reloadButton);
    optionsLayout->addStretch();

    // --- column table -------------------------------------------------------
    m_tableWidget = new QTableWidget(this);
    m_tableWidget->setColumnCount(4);
    m_tableWidget->setHorizontalHeaderLabels(
        { tr("导入"), tr("列名"), tr("类型"), tr("示例值") });
    m_tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_tableWidget->verticalHeader()->setVisible(false);
    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableWidget->setAlternatingRowColors(true);
    m_tableWidget->setColumnWidth(1, 180);

    // --- target -------------------------------------------------------------
    auto* targetBox = new QGroupBox(tr("导入目标"), this);
    auto* targetLayout = new QFormLayout(targetBox);

    m_writeRadio = new QRadioButton(tr("写入当前文件（新建分组）"), this);
    m_writeRadio->setChecked(true);
    m_plotRadio = new QRadioButton(tr("仅用于绘图（不写入文件）"), this);

    m_groupEdit = new QLineEdit(this);
    m_groupEdit->setText(QFileInfo(filePath).completeBaseName());

    m_xCombo = new QComboBox(this);

    targetLayout->addRow(m_writeRadio);
    targetLayout->addRow(m_plotRadio);
    targetLayout->addRow(tr("分组名称"), m_groupEdit);
    targetLayout->addRow(tr("X 轴数据"), m_xCombo);

    // --- footer -------------------------------------------------------------
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("hintLabel"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(tr("导入"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(optionsBox);
    layout->addWidget(m_tableWidget, 1);
    layout->addWidget(targetBox);
    layout->addWidget(m_summary);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(reloadButton, &QPushButton::clicked, this, &ImportDialog::reload);
    connect(m_delimiterCombo, &QComboBox::currentIndexChanged, this, &ImportDialog::reload);
    connect(m_headerCheck, &QCheckBox::toggled, this, &ImportDialog::reload);
    connect(m_tableWidget, &QTableWidget::itemChanged, this, [this] { updateSummary(); });
    connect(m_plotRadio, &QRadioButton::toggled, this, [this](bool on) {
        m_groupEdit->setEnabled(!on);
        updateSummary();
    });


    reload();
}

bool ImportDialog::parseOptions(ImportOptions* options) const
{
    options->firstRowHasHeader = m_headerCheck->isChecked();
    options->skipRows = 0;
    const QString choice = m_delimiterCombo->currentText();
    options->delimiter = (choice == delimiterChoices().first()) ? QString() : choice;
    return true;
}

void ImportDialog::reload()
{
    ImportOptions options;
    parseOptions(&options);

    QString error;
    if (!parseDataFile(m_filePath, options, &m_table, &error)) {
        QMessageBox::warning(this, tr("解析失败"), error);
        m_tableWidget->setRowCount(0);
        m_summary->setText(error);
        if (m_okButton)
            m_okButton->setEnabled(false);
        return;
    }
    if (m_okButton)
        m_okButton->setEnabled(true);

    m_loading = true;
    m_tableWidget->setRowCount(m_table.columns.size());
    for (int row = 0; row < m_table.columns.size(); ++row) {
        const DataColumn& col = m_table.columns.at(row);

        auto* checkItem = new QTableWidgetItem;
        checkItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        checkItem->setCheckState(col.numeric ? Qt::Checked : Qt::Unchecked);
        if (!col.numeric) {
            checkItem->setFlags(Qt::ItemIsSelectable);
            checkItem->setToolTip(tr("该列包含非数值内容，无法作为曲线数据导入。"));
        }
        m_tableWidget->setItem(row, 0, checkItem);

        auto* nameItem = new QTableWidgetItem(col.name);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_tableWidget->setItem(row, 1, nameItem);

        QString typeText = col.numeric ? tr("数值") : tr("文本");
        if (!col.unit.isEmpty())
            typeText += QStringLiteral(" · %1").arg(col.unit);
        auto* typeItem = new QTableWidgetItem(typeText);
        typeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (!col.numeric)
            typeItem->setForeground(QColor(QStringLiteral("#9a7b5f")));
        m_tableWidget->setItem(row, 2, typeItem);

        QStringList samples;
        const int count = qMin(5, col.size());
        for (int i = 0; i < count; ++i) {
            if (col.numeric) {
                const double v = col.values.at(i);
                samples.append(std::isnan(v) ? QStringLiteral("-") : QString::number(v, 'g', 6));
            } else {
                samples.append(col.texts.value(i));
            }
        }
        auto* sampleItem = new QTableWidgetItem(samples.join(QStringLiteral("  |  ")));
        sampleItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        sampleItem->setForeground(QColor(QStringLiteral("#8b98ab")));
        m_tableWidget->setItem(row, 3, sampleItem);
    }
    m_loading = false;

    m_xCombo->clear();
    m_xCombo->addItem(tr("点序号（默认）"), -1);
    for (int i = 0; i < m_table.columns.size(); ++i) {
        if (m_table.columns.at(i).numeric)
            m_xCombo->addItem(m_table.columns.at(i).name, i);
    }

    QStringList warnings = m_table.warnings;
    m_summary->setText(tr("共 %1 行、%2 列；分隔符：%3；编码：%4%5")
                           .arg(m_table.rowCount)
                           .arg(m_table.columns.size())
                           .arg(m_table.delimiterUsed, m_table.encodingUsed,
                                warnings.isEmpty() ? QString()
                                                   : QStringLiteral("；") + warnings.first()));
    updateSummary();
}

void ImportDialog::updateSummary()
{
    if (m_loading)
        return;
    const int selected = selectedColumns().size();
    const QString target = m_plotRadio->isChecked() ? tr("仅绘图") : tr("写入文件");
    m_summary->setText(tr("共 %1 行、%2 列；已选择 %3 个数值列；目标：%4")
                           .arg(m_table.rowCount)
                           .arg(m_table.columns.size())
                           .arg(selected)
                           .arg(target));
    if (m_okButton)
        m_okButton->setEnabled(selected > 0 && !m_table.columns.isEmpty());
}

QVector<int> ImportDialog::selectedColumns() const
{
    QVector<int> result;
    for (int row = 0; row < m_tableWidget->rowCount(); ++row) {
        const QTableWidgetItem* item = m_tableWidget->item(row, 0);
        if (item && item->checkState() == Qt::Checked && row < m_table.columns.size()
            && m_table.columns.at(row).numeric)
            result.append(row);
    }
    return result;
}

QString ImportDialog::groupName() const
{
    return m_groupEdit->text().trimmed();
}

bool ImportDialog::writeToDocument() const
{
    return m_writeRadio->isChecked();
}

int ImportDialog::xColumn() const
{
    return m_xCombo->currentData().toInt();
}

QString ImportDialog::delimiter() const
{
    return m_delimiterCombo->currentText();
}

bool ImportDialog::firstRowHeader() const
{
    return m_headerCheck->isChecked();
}

} // namespace tdms
