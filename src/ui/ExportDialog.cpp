// ---------------------------------------------------------------------------
// ExportDialog.cpp
// ---------------------------------------------------------------------------
#include "ExportDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/Settings.h"

namespace tdms {

ExportDialog::ExportDialog(bool hasChannel, bool hasGroup, const QString& suggestedName,
                           QWidget* parent)
    : QDialog(parent)
    , m_suggestedName(suggestedName)
{
    setWindowTitle(tr("导出为 TXT / CSV"));
    setModal(true);
    resize(560, 420);

    auto* scopeBox = new QGroupBox(tr("导出范围"), this);
    auto* scopeLayout = new QVBoxLayout(scopeBox);
    m_scopeChannel = new QRadioButton(tr("当前通道（单列数据）"), this);
    m_scopeGroup = new QRadioButton(tr("当前分组（每个通道一列）"), this);
    m_scopeFile = new QRadioButton(tr("整个文件（每个分组生成一个文件）"), this);
    scopeLayout->addWidget(m_scopeChannel);
    scopeLayout->addWidget(m_scopeGroup);
    scopeLayout->addWidget(m_scopeFile);

    if (hasChannel)
        m_scopeChannel->setChecked(true);
    else if (hasGroup)
        m_scopeGroup->setChecked(true);
    else
        m_scopeFile->setChecked(true);

    m_scopeChannel->setEnabled(hasChannel);
    m_scopeGroup->setEnabled(hasGroup);

    // The defaults come from the preferences dialog.
    const AppSettings defaults = Settings::instance().values();

    auto* formatBox = new QGroupBox(tr("文件格式"), this);
    auto* formatLayout = new QHBoxLayout(formatBox);
    m_csv = new QRadioButton(tr("CSV（逗号分隔，带 BOM，可用 Excel 直接打开）"), this);
    m_txt = new QRadioButton(tr("TXT（制表符分隔）"), this);
    m_csv->setChecked(defaults.exportFormat.compare(QLatin1String("txt"),
                                                    Qt::CaseInsensitive) != 0);
    m_txt->setChecked(!m_csv->isChecked());
    formatLayout->addWidget(m_csv);
    formatLayout->addWidget(m_txt);

    auto* optionBox = new QGroupBox(tr("内容选项"), this);
    auto* optionLayout = new QGridLayout(optionBox);
    m_includeIndex = new QCheckBox(tr("包含序号列"), this);
    m_includeIndex->setChecked(defaults.exportIncludeIndex);
    m_includeMeta = new QCheckBox(tr("包含元数据注释行（以 # 开头）"), this);
    m_includeMeta->setChecked(defaults.exportIncludeMeta);
    m_precision = new QSpinBox(this);
    m_precision->setRange(3, 17);
    m_precision->setValue(defaults.exportPrecision);
    m_precision->setSuffix(tr(" 位有效数字"));
    optionLayout->addWidget(m_includeIndex, 0, 0);
    optionLayout->addWidget(m_includeMeta, 1, 0);
    optionLayout->addWidget(new QLabel(tr("数值精度"), this), 2, 0);
    optionLayout->addWidget(m_precision, 2, 1);

    auto* pathBox = new QGroupBox(tr("输出"), this);
    auto* pathLayout = new QHBoxLayout(pathBox);
    m_path = new QLineEdit(this);
    auto* browseButton = new QPushButton(tr("浏览…"), this);
    pathLayout->addWidget(m_path, 1);
    pathLayout->addWidget(browseButton);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("导出"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(scopeBox);
    layout->addWidget(formatBox);
    layout->addWidget(optionBox);
    layout->addWidget(pathBox);
    layout->addWidget(buttons);

    connect(m_scopeChannel, &QRadioButton::toggled, this, &ExportDialog::updateUi);
    connect(m_scopeGroup, &QRadioButton::toggled, this, &ExportDialog::updateUi);
    connect(m_scopeFile, &QRadioButton::toggled, this, &ExportDialog::updateUi);
    connect(m_csv, &QRadioButton::toggled, this, &ExportDialog::updateUi);
    connect(m_txt, &QRadioButton::toggled, this, &ExportDialog::updateUi);
    connect(browseButton, &QPushButton::clicked, this, &ExportDialog::browse);
    connect(m_path, &QLineEdit::textEdited, this, [this] { m_userEdited = true; });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_path->text().trimmed().isEmpty()) {
            browse();
            if (m_path->text().trimmed().isEmpty())
                return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);


    updateUi();
}

QString ExportDialog::outputPath() const
{
    return m_path->text().trimmed();
}

ExportDialog::Scope ExportDialog::scope() const
{
    if (m_scopeChannel->isChecked())
        return ScopeChannel;
    if (m_scopeGroup->isChecked())
        return ScopeGroup;
    return ScopeFile;
}

ExportSettings ExportDialog::settings() const
{
    ExportSettings s;
    s.csv = m_csv->isChecked();
    s.includeIndex = m_includeIndex->isChecked();
    s.includeMeta = m_includeMeta->isChecked();
    s.precision = m_precision->value();
    return s;
}

void ExportDialog::updateUi()
{
    if (m_userEdited)
        return;

    const QString suffix = m_csv->isChecked() ? QStringLiteral(".csv") : QStringLiteral(".txt");
    const QString base = m_suggestedName.isEmpty() ? QStringLiteral("export") : m_suggestedName;
    if (m_scopeFile->isChecked())
        m_path->setText(QDir(QDir::homePath()).filePath(base + QStringLiteral("_导出")));
    else
        m_path->setText(QDir(QDir::homePath()).filePath(base + suffix));
}

void ExportDialog::browse()
{
    if (m_scopeFile->isChecked()) {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("选择导出目录"), m_path->text());
        if (!dir.isEmpty()) {
            m_path->setText(dir);
            m_userEdited = true;
        }
        return;
    }

    const bool csv = m_csv->isChecked();
    const QString filter = csv ? tr("CSV 文件 (*.csv)") : tr("文本文件 (*.txt)");
    QString path = QFileDialog::getSaveFileName(this, tr("导出为文件"), m_path->text(), filter);
    if (path.isEmpty())
        return;
    if (csv && !path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive))
        path += QStringLiteral(".csv");
    if (!csv && !path.endsWith(QLatin1String(".txt"), Qt::CaseInsensitive))
        path += QStringLiteral(".txt");
    m_path->setText(path);
    m_userEdited = true;
}

} // namespace tdms
