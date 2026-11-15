// ---------------------------------------------------------------------------
// MergeDialog.cpp
// ---------------------------------------------------------------------------
#include "MergeDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "core/TdmsCore.h"

namespace tdms {

namespace {
QTreeWidgetItem* buildFileNode(const QString& title, const QString& path,
                               const Document& doc, const QString& tag)
{
    auto* fileItem = new QTreeWidgetItem();
    fileItem->setText(0, title);
    fileItem->setText(1, doc.fileType());
    fileItem->setText(2, QFileInfo(path).fileName());
    fileItem->setFlags(Qt::ItemIsEnabled);
    QFont bold = fileItem->font(0);
    bold.setBold(true);
    fileItem->setFont(0, bold);

    for (const GroupInfo& group : doc.groups()) {
        auto* groupItem = new QTreeWidgetItem(fileItem);
        groupItem->setText(0, group.name);
        groupItem->setText(2, QObject::tr("%1 个通道").arg(group.channels.size()));
        groupItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        groupItem->setCheckState(0, Qt::Checked);
        groupItem->setData(0, Qt::UserRole, tag);

        for (const ChannelInfo& channel : group.channels) {
            auto* channelItem = new QTreeWidgetItem(groupItem);
            channelItem->setText(0, channel.name);
            channelItem->setText(1, dataTypeName(channel.type));
            channelItem->setText(2, QObject::tr("%1 点").arg(channel.count));
            channelItem->setFlags(Qt::ItemIsEnabled);
            channelItem->setForeground(0, QColor(QStringLiteral("#93a1b5")));
        }
    }
    return fileItem;
}
} // namespace

MergeDialog::MergeDialog(const QString& currentPath, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("合并 TDM / TDMS 文件"));
    setModal(true);
    resize(720, 560);

    m_pathA = new QLineEdit(this);
    m_pathB = new QLineEdit(this);
    m_outPath = new QLineEdit(this);

    auto* browseA = new QPushButton(tr("浏览…"), this);
    auto* browseB = new QPushButton(tr("浏览…"), this);
    auto* browseOut = new QPushButton(tr("浏览…"), this);
    browseA->setObjectName(QStringLiteral("secondaryButton"));
    browseB->setObjectName(QStringLiteral("secondaryButton"));
    browseOut->setObjectName(QStringLiteral("secondaryButton"));

    auto* rowA = new QHBoxLayout;
    rowA->addWidget(m_pathA, 1);
    rowA->addWidget(browseA);
    auto* rowB = new QHBoxLayout;
    rowB->addWidget(m_pathB, 1);
    rowB->addWidget(browseB);
    auto* rowOut = new QHBoxLayout;
    rowOut->addWidget(m_outPath, 1);
    rowOut->addWidget(browseOut);

    m_mode = new QComboBox(this);
    m_mode->addItem(tr("追加为独立分组（重名分组自动加序号）"));
    m_mode->addItem(tr("同名分组合并（通道追加到已有分组）"));

    auto* formBox = new QGroupBox(tr("文件与合并方式"), this);
    auto* form = new QFormLayout(formBox);
    form->addRow(tr("主文件 A"), rowA);
    form->addRow(tr("次文件 B"), rowB);
    form->addRow(tr("合并方式"), m_mode);
    form->addRow(tr("输出文件"), rowOut);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({ tr("名称"), tr("类型"), tr("说明") });
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->setAlternatingRowColors(true);

    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("hintLabel"));
    m_info->setWordWrap(true);

    m_openAfter = new QCheckBox(tr("合并完成后打开结果文件"), this);
    m_openAfter->setChecked(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(tr("开始合并"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(formBox);
    layout->addWidget(new QLabel(tr("勾选需要参与合并的分组："), this));
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_info);
    layout->addWidget(m_openAfter);
    layout->addWidget(buttons);

    connect(browseA, &QPushButton::clicked, this, [this] { chooseFile(true); });
    connect(browseB, &QPushButton::clicked, this, [this] { chooseFile(false); });
    connect(browseOut, &QPushButton::clicked, this, &MergeDialog::chooseOutput);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        validate();
        if (m_okButton->isEnabled())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this] { validate(); });


    if (!currentPath.isEmpty()) {
        m_pathA->setText(currentPath);
        loadFile(true);
    }
    validate();
}

void MergeDialog::chooseFile(bool primary)
{
    const QString path = QFileDialog::getOpenFileName(
        this, primary ? tr("选择主文件 A") : tr("选择次文件 B"), QString(),
        tr("TDM / TDMS 文件 (*.tdm *.tdms);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    if (primary)
        m_pathA->setText(path);
    else
        m_pathB->setText(path);
    loadFile(primary);
    updateOutputSuggestion();
    validate();
}

void MergeDialog::chooseOutput()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("保存合并结果"), m_outPath->text(),
        tr("TDM 文件 (*.tdm);;TDMS 文件 (*.tdms)"));
    if (path.isEmpty())
        return;
    QString finalPath = path;
    if (!finalPath.endsWith(QLatin1String(".tdm"), Qt::CaseInsensitive)
        && !finalPath.endsWith(QLatin1String(".tdms"), Qt::CaseInsensitive))
        finalPath += QStringLiteral(".tdms");
    m_outPath->setText(finalPath);
    validate();
}

void MergeDialog::loadFile(bool primary)
{
    const QString path = primary ? m_pathA->text() : m_pathB->text();
    if (path.isEmpty())
        return;

    Document doc;
    QString error;
    if (!doc.open(path, true, &error)) {
        QMessageBox::warning(this, tr("无法读取文件"), error);
        return;
    }

    m_loading = true;
    if (primary) {
        delete m_rootA;
        m_rootA = buildFileNode(tr("主文件 A"), path, doc, QStringLiteral("A"));
        m_tree->insertTopLevelItem(0, m_rootA);
        m_rootA->setExpanded(true);
    } else {
        delete m_rootB;
        m_rootB = buildFileNode(tr("次文件 B"), path, doc, QStringLiteral("B"));
        m_tree->insertTopLevelItem(m_rootA ? 1 : 0, m_rootB);
        m_rootB->setExpanded(true);
    }
    m_loading = false;

    m_info->setText(tr("%1：%2，%3 个分组")
                        .arg(QFileInfo(path).fileName(), doc.fileType())
                        .arg(doc.groups().size()));
    updateOutputSuggestion();
}

void MergeDialog::updateOutputSuggestion()
{
    if (m_outPath->text().trimmed().isEmpty() && !m_pathA->text().isEmpty()) {
        const QFileInfo a(m_pathA->text());
        const QFileInfo b(m_pathB->text());
        const QString suffix = a.suffix().compare(QLatin1String("tdm"), Qt::CaseInsensitive) == 0
            ? QStringLiteral("tdm")
            : QStringLiteral("tdms");
        QString name = QStringLiteral("merged_%1").arg(a.completeBaseName());
        if (!b.fileName().isEmpty())
            name += QLatin1Char('_') + b.completeBaseName();
        name += QLatin1Char('.') + suffix;
        m_outPath->setText(a.absolutePath() + QLatin1Char('/') + name);
    }
}

void MergeDialog::validate()
{
    const bool filesOk = !m_pathA->text().trimmed().isEmpty()
        && !m_pathB->text().trimmed().isEmpty()
        && QFileInfo::exists(m_pathA->text())
        && QFileInfo::exists(m_pathB->text());
    const bool outOk = !m_outPath->text().trimmed().isEmpty();

    QStringList groupsA;
    QStringList groupsB;
    if (m_rootA) {
        for (int i = 0; i < m_rootA->childCount(); ++i) {
            QTreeWidgetItem* item = m_rootA->child(i);
            if (item->checkState(0) == Qt::Checked)
                groupsA.append(item->text(0));
        }
    }
    if (m_rootB) {
        for (int i = 0; i < m_rootB->childCount(); ++i) {
            QTreeWidgetItem* item = m_rootB->child(i);
            if (item->checkState(0) == Qt::Checked)
                groupsB.append(item->text(0));
        }
    }
    const bool groupsOk = filesOk && (!groupsA.isEmpty() || !groupsB.isEmpty());

    m_result.pathA = m_pathA->text().trimmed();
    m_result.pathB = m_pathB->text().trimmed();
    m_result.outPath = m_outPath->text().trimmed();
    m_result.mode = m_mode->currentIndex();
    m_result.groupsA = groupsA;
    m_result.groupsB = groupsB;
    m_result.openAfter = m_openAfter->isChecked();

    if (m_okButton)
        m_okButton->setEnabled(filesOk && outOk && groupsOk);

    if (filesOk && !groupsOk)
        m_info->setText(tr("请至少勾选一个分组。"));
}

} // namespace tdms
