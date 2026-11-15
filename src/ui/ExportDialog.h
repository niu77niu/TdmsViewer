// ---------------------------------------------------------------------------
// ExportDialog.h - export TXT / CSV options.
// ---------------------------------------------------------------------------
#pragma once

#include <QDialog>

#include "core/TdmsCore.h"

class QCheckBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

namespace tdms {

class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    enum Scope { ScopeChannel = 0, ScopeGroup = 1, ScopeFile = 2 };

    ExportDialog(bool hasChannel, bool hasGroup, const QString& suggestedName,
                 QWidget* parent = nullptr);

    Scope scope() const;
    ExportSettings settings() const;
    QString outputPath() const;

private:
    void browse();
    void updateUi();

    QRadioButton* m_scopeChannel = nullptr;
    QRadioButton* m_scopeGroup = nullptr;
    QRadioButton* m_scopeFile = nullptr;
    QRadioButton* m_csv = nullptr;
    QRadioButton* m_txt = nullptr;
    QCheckBox* m_includeIndex = nullptr;
    QCheckBox* m_includeMeta = nullptr;
    QSpinBox* m_precision = nullptr;
    QLineEdit* m_path = nullptr;
    QString m_suggestedName;
    bool m_userEdited = false;
};

} // namespace tdms
