// ---------------------------------------------------------------------------
// TdmsCore.h - thin, Qt-friendly wrapper around the NI TDM C DLL
//              (nilibddc). Handles TDM (XML) and TDMS (streaming) files.
// ---------------------------------------------------------------------------
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include "nilibddc.h"

namespace tdms {

enum class ObjKind { File = 0, Group = 1, Channel = 2 };

// --- data type helpers ------------------------------------------------------
QString dataTypeName(DDCDataType type);
QStringList supportedTypeNames();
bool isNumericDataType(DDCDataType type);
bool dataTypeFromName(const QString& name, DDCDataType* out);

struct PropEntry {
    QString name;
    DDCDataType type = DDC_String;
    QVariant value;
    bool readOnly = false;
};

struct ChannelInfo {
    QString name;
    QString description;
    QString unit;
    DDCDataType type = DDC_Double;
    quint64 count = 0;
    DDCChannelHandle handle = nullptr;

    // NI waveform timing, read from the standard channel properties.
    // increment == 0 means the channel carries no timing information.
    QDateTime startTime;          // wf_start_time
    double increment = 0.0;       // wf_increment (seconds between samples)
    double startOffset = 0.0;     // wf_start_offset (seconds)

    bool hasTimeInfo() const { return increment > 0.0; }
    bool plottable() const { return isNumericDataType(type); }
};

struct GroupInfo {
    QString name;
    QString description;
    DDCChannelGroupHandle handle = nullptr;
    QVector<ChannelInfo> channels;
};

struct ExportSettings {
    bool csv = true;
    bool includeIndex = true;
    bool includeMeta = true;
    int precision = 15;

    QString delimiter() const { return csv ? QStringLiteral(",") : QStringLiteral("\t"); }
};

// ---------------------------------------------------------------------------
// Document: owns one open TDM/TDMS file and a cached structure view.
// ---------------------------------------------------------------------------
class Document
{
public:
    Document();
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    bool open(const QString& path, bool readOnly, QString* error);
    bool create(const QString& path, const QString& fileType, QString* error);
    void close();

    bool isOpen() const { return m_file != nullptr; }
    bool isReadOnly() const { return m_readOnly; }
    bool isDirty() const { return m_dirty; }
    void setDirty(bool dirty) { m_dirty = dirty; }

    QString path() const { return m_path; }
    QString fileType() const { return m_fileType; }
    QString title() const;              // "title" property, fallback to file name
    QDateTime fileDateTime() const;     // "datetime" property, invalid when missing
    const QVector<GroupInfo>& groups() const { return m_groups; }

    bool save(QString* error);
    bool saveAs(const QString& newPath, QString* error);

    // --- properties ---------------------------------------------------------
    QVector<PropEntry> properties(ObjKind kind, int group, int channel) const;
    bool setProperty(ObjKind kind, int group, int channel, const QString& name,
                     DDCDataType type, const QVariant& value, QString* error);
    bool createProperty(ObjKind kind, int group, int channel, const QString& name,
                        DDCDataType type, const QVariant& value, QString* error);

    // --- channel data -------------------------------------------------------
    // Numeric channels: `values` filled. Text/time channels: `labels` filled.
    bool readChannel(int group, int channel, QVector<double>* values,
                     QStringList* labels, QString* error) const;

    // --- structure editing --------------------------------------------------
    bool addGroup(const QString& name, const QString& description, int* index, QString* error);
    bool addChannel(int group, const QString& name, const QString& description,
                    const QString& unit, DDCDataType type, const QVector<double>& values,
                    QString* error);
    bool renameChannel(int group, int channel, const QString& newName, QString* error);
    // Writes wf_start_time / wf_increment / wf_start_offset on the channel.
    bool setChannelTiming(int group, int channel, const QDateTime& startTime,
                          double increment, double startOffset, QString* error);
    bool removeChannel(int group, int channel, QString* error);
    bool removeGroup(int group, QString* error);
    bool addChannelFromText(int group, const QString& name, const QString& unit,
                            const QStringList& textValues, QString* error);

    // --- export -------------------------------------------------------------
    bool exportChannel(int group, int channel, const QString& filePath,
                       const ExportSettings& settings, QString* error) const;
    bool exportGroup(int group, const QString& filePath,
                     const ExportSettings& settings, QString* error) const;
    bool exportAllToDir(const QString& dirPath, const ExportSettings& settings,
                        QStringList* writtenFiles, QString* error) const;

    void* rawHandle(ObjKind kind, int group, int channel) const;

private:
    bool loadStructure(QString* error);
    GroupInfo* groupAt(int index);
    const GroupInfo* groupAt(int index) const;
    ChannelInfo* channelAt(int group, int channel);
    const ChannelInfo* channelAt(int group, int channel) const;

    DDCFileHandle m_file = nullptr;
    QString m_path;
    QString m_fileType;
    bool m_readOnly = false;
    bool m_dirty = false;
    QVector<GroupInfo> m_groups;
};

// ---------------------------------------------------------------------------
// Merge two TDM/TDMS files into a new one.
//   mode 0: append groups (duplicate names get a suffix)
//   mode 1: merge channels into groups with the same name
// `groupsA` / `groupsB` limit which groups are copied (empty = all).
// ---------------------------------------------------------------------------
bool mergeFiles(const QString& pathA, const QString& pathB, const QString& outPath,
                int mode, const QStringList& groupsA, const QStringList& groupsB,
                QString* error, QStringList* log);

// Rebuild `srcPath` into `dstPath` without the object (group or channel).
bool rebuildWithout(const QString& srcPath, const QString& dstPath,
                    ObjKind dropKind, int dropGroup, int dropChannel,
                    QString* error);

QString lastErrorText(int code);

} // namespace tdms
