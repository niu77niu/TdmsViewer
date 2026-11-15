// ---------------------------------------------------------------------------
// PropertyPanel.h - property table for file / group / channel objects.
// ---------------------------------------------------------------------------
#pragma once

#include <QWidget>
#include <QVector>

#include "core/TdmsCore.h"

class QLabel;
class QTableWidget;
class QTableWidgetItem;

namespace tdms {

class PropertyPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PropertyPanel(QWidget* parent = nullptr);

    void setContext(const QString& pathText, ObjKind kind, int group, int channel,
                    const QVector<PropEntry>& entries, bool readOnly);
    void clearContext(const QString& message = QString());

    ObjKind kind() const { return m_kind; }
    int group() const { return m_group; }
    int channel() const { return m_channel; }

    // Re-applies theme dependent colours after the palette changed.
    void refreshTheme();

signals:
    void valueEdited(const QString& name, DDCDataType type, const QVariant& value);
    void createRequested(const QString& name, DDCDataType type, const QVariant& value);
    void refreshRequested();
    void statusMessage(const QString& message);

private slots:
    void onItemChanged(QTableWidgetItem* item);
    void onAddClicked();

private:
    void applyTheme();

    QLabel* m_title = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_note = nullptr;
    bool m_updating = false;
    bool m_readOnly = false;
    ObjKind m_kind = ObjKind::File;
    int m_group = -1;
    int m_channel = -1;
};

} // namespace tdms
