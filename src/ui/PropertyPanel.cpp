// ---------------------------------------------------------------------------
// PropertyPanel.cpp
// ---------------------------------------------------------------------------
#include "PropertyPanel.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

namespace tdms {

namespace {

constexpr int kRoleType = Qt::UserRole;
constexpr int kRoleReadOnly = Qt::UserRole + 1;
constexpr int kRoleOriginal = Qt::UserRole + 2;

QString formatValue(const QVariant& value, DDCDataType type)
{
    if (!value.isValid())
        return QStringLiteral("(不可用)");
    switch (type) {
    case DDC_Timestamp:
        return value.toDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    case DDC_Double:
    case DDC_Float:
        return QString::number(value.toDouble(), 'g', 12);
    case DDC_UInt8:
    case DDC_Int16:
    case DDC_Int32:
        return QString::number(value.toInt());
    default:
        return value.toString();
    }
}

bool parseValue(const QString& text, DDCDataType type, QVariant* outValue, QString* error)
{
    switch (type) {
    case DDC_String:
        *outValue = text;
        return true;
    case DDC_Double:
    case DDC_Float: {
        bool ok = false;
        const double v = text.trimmed().toDouble(&ok);
        if (!ok) {
            *error = QObject::tr("请输入有效的浮点数。");
            return false;
        }
        *outValue = v;
        return true;
    }
    case DDC_Int32:
    case DDC_Int16:
    case DDC_UInt8: {
        bool ok = false;
        const qlonglong v = text.trimmed().toLongLong(&ok);
        if (!ok) {
            *error = QObject::tr("请输入有效的整数。");
            return false;
        }
        qlonglong minValue = -2147483648LL;
        qlonglong maxValue = 2147483647LL;
        if (type == DDC_Int16) { minValue = -32768; maxValue = 32767; }
        if (type == DDC_UInt8) { minValue = 0; maxValue = 255; }
        if (v < minValue || v > maxValue) {
            *error = QObject::tr("数值超出 %1 的取值范围 [%2, %3]。")
                         .arg(dataTypeName(type)).arg(minValue).arg(maxValue);
            return false;
        }
        *outValue = int(v);
        return true;
    }
    case DDC_Timestamp: {
        QDateTime dt = QDateTime::fromString(text.trimmed(), QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        if (!dt.isValid())
            dt = QDateTime::fromString(text.trimmed(), Qt::ISODateWithMs);
        if (!dt.isValid())
            dt = QDateTime::fromString(text.trimmed(), Qt::ISODate);
        if (!dt.isValid()) {
            *error = QObject::tr("时间格式应为 yyyy-MM-dd HH:mm:ss.zzz。");
            return false;
        }
        *outValue = dt;
        return true;
    }
    }
    *error = QObject::tr("不支持的数据类型。");
    return false;
}

// Simple dialog used to add a new custom property.
class AddPropertyDialog : public QDialog
{
public:
    explicit AddPropertyDialog(QWidget* parent)
        : QDialog(parent)
    {
        setWindowTitle(tr("添加属性"));
        setModal(true);

        m_name = new QLineEdit(this);
        m_name->setPlaceholderText(tr("例如 operator、gain、sensor_id"));
        m_type = new QComboBox(this);
        m_type->addItems(supportedTypeNames());
        m_value = new QLineEdit(this);

        auto* form = new QFormLayout;
        form->addRow(tr("属性名"), m_name);
        form->addRow(tr("数据类型"), m_type);
        form->addRow(tr("初始值"), m_value);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto* layout = new QVBoxLayout(this);
        layout->addLayout(form);
        layout->addWidget(buttons);
        resize(380, 170);
    }

    QString propertyName() const { return m_name->text().trimmed(); }
    DDCDataType propertyType() const
    {
        DDCDataType type = DDC_String;
        dataTypeFromName(m_type->currentText(), &type);
        return type;
    }
    QString rawValue() const { return m_value->text(); }

private:
    QLineEdit* m_name = nullptr;
    QComboBox* m_type = nullptr;
    QLineEdit* m_value = nullptr;
};

} // namespace

PropertyPanel::PropertyPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    auto* headerRow = new QHBoxLayout;
    m_title = new QLabel(tr("属性"), this);
    m_title->setObjectName(QStringLiteral("propTitle"));
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* addButton = new QPushButton(tr("添加属性"), this);
    auto* refreshButton = new QPushButton(tr("刷新"), this);
    addButton->setObjectName(QStringLiteral("secondaryButton"));
    refreshButton->setObjectName(QStringLiteral("secondaryButton"));

    headerRow->addWidget(m_title, 1);
    headerRow->addWidget(addButton);
    headerRow->addWidget(refreshButton);
    layout->addLayout(headerRow);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({ tr("属性名"), tr("类型"), tr("值") });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                             | QAbstractItemView::SelectedClicked
                             | QAbstractItemView::EditKeyPressed);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);

    layout->addWidget(m_table, 1);

    m_note = new QLabel(tr("双击“值”列即可修改；通道的 minimum / maximum 由数据自动计算，不可编辑。"), this);
    m_note->setObjectName(QStringLiteral("hintLabel"));
    m_note->setWordWrap(true);
    layout->addWidget(m_note);

    connect(m_table, &QTableWidget::itemChanged, this, &PropertyPanel::onItemChanged);
    connect(addButton, &QPushButton::clicked, this, &PropertyPanel::onAddClicked);
    connect(refreshButton, &QPushButton::clicked, this, &PropertyPanel::refreshRequested);

    applyTheme();
    clearContext(tr("选择左侧的文件、分组或通道以查看属性。"));
}

void PropertyPanel::applyTheme()
{
    // Colours come from the application stylesheet; only the local accents that
    // depend on the current theme are refreshed here.
}

void PropertyPanel::refreshTheme()
{
    applyTheme();
    m_table->viewport()->update();
}

void PropertyPanel::setContext(const QString& pathText, ObjKind kind, int group, int channel,
                               const QVector<PropEntry>& entries, bool readOnly)
{
    m_updating = true;
    m_kind = kind;
    m_group = group;
    m_channel = channel;
    m_readOnly = readOnly;

    m_title->setText(pathText.isEmpty() ? tr("属性") : pathText);

    m_table->setRowCount(entries.size());
    for (int row = 0; row < entries.size(); ++row) {
        const PropEntry& entry = entries.at(row);

        auto* nameItem = new QTableWidgetItem(entry.name);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        nameItem->setForeground(QColor(QStringLiteral("#8fa0b8")));

        auto* typeItem = new QTableWidgetItem(dataTypeName(entry.type));
        typeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        typeItem->setForeground(QColor(QStringLiteral("#7f8ea3")));

        const QString text = formatValue(entry.value, entry.type);
        auto* valueItem = new QTableWidgetItem(text);
        valueItem->setData(kRoleType, int(entry.type));
        valueItem->setData(kRoleReadOnly, entry.readOnly || readOnly);
        valueItem->setData(kRoleOriginal, text);

        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (!(entry.readOnly || readOnly))
            flags |= Qt::ItemIsEditable;
        valueItem->setFlags(flags);
        if (entry.readOnly || readOnly)
            valueItem->setForeground(QColor(QStringLiteral("#6f7c8f")));

        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, typeItem);
        m_table->setItem(row, 2, valueItem);
    }
    m_updating = false;
}

void PropertyPanel::clearContext(const QString& message)
{
    m_updating = true;
    m_kind = ObjKind::File;
    m_group = -1;
    m_channel = -1;
    m_table->setRowCount(0);
    m_title->setText(tr("属性"));
    if (!message.isEmpty())
        m_note->setText(message);
    m_updating = false;
}

void PropertyPanel::onItemChanged(QTableWidgetItem* item)
{
    if (m_updating || !item || item->column() != 2)
        return;

    const QString name = m_table->item(item->row(), 0)
                             ? m_table->item(item->row(), 0)->text()
                             : QString();
    const DDCDataType type = DDCDataType(item->data(kRoleType).toInt());
    const bool readOnly = item->data(kRoleReadOnly).toBool();
    if (readOnly || name.isEmpty()) {
        m_updating = true;
        item->setText(item->data(kRoleOriginal).toString());
        m_updating = false;
        emit statusMessage(tr("该属性为只读，无法修改。"));
        return;
    }

    QVariant parsed;
    QString error;
    if (!parseValue(item->text(), type, &parsed, &error)) {
        m_updating = true;
        item->setText(item->data(kRoleOriginal).toString());
        m_updating = false;
        emit statusMessage(error);
        return;
    }

    item->setData(kRoleOriginal, item->text());
    emit valueEdited(name, type, parsed);
}

void PropertyPanel::onAddClicked()
{
    if (m_readOnly) {
        emit statusMessage(tr("文件以只读方式打开，无法添加属性。"));
        return;
    }

    AddPropertyDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString name = dialog.propertyName();
    if (name.isEmpty()) {
        emit statusMessage(tr("属性名不能为空。"));
        return;
    }

    QVariant value;
    QString error;
    if (!parseValue(dialog.rawValue(), dialog.propertyType(), &value, &error)) {
        emit statusMessage(error);
        return;
    }
    emit createRequested(name, dialog.propertyType(), value);
}

} // namespace tdms
