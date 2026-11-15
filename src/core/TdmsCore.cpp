// ---------------------------------------------------------------------------
// TdmsCore.cpp
// ---------------------------------------------------------------------------
#include "TdmsCore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>
#include <cmath>
#include <cstring>

namespace tdms {

// ===========================================================================
// small helpers
// ===========================================================================
QString lastErrorText(int code)
{
    return QStringLiteral("%1 (错误码 %2)")
        .arg(QString::fromLocal8Bit(DDC_GetLibraryErrorDescription(code)))
        .arg(code);
}

// NI TDM C DLL 17.0 的 DDC_OpenFileEx / DDC_CreateFile 有两个坑：
//  1) 它只接受 *相对* 路径。传绝对路径时不会识别盘符，而是照样拼到当前工作目录
//     后面，得到 "C:\cwd\C:/abs/dir/file.tdx" 这种非法路径并返回 -6211
//     (DDC_StorageCouldNotBeOpened)；文件同目录下的 .tdm.log 会打印这条拼错的路径。
//  2) 路径参数按本地 ANSI 编码解析，传 UTF-8 时中文文件名会得到 -6209（文件不存在）。
// 这里统一改成"临时切到目标目录 + 只传文件名（toLocal8Bit）"，调用完立刻恢复
// 原来的工作目录。句柄内部保存的是展开后的绝对路径，因此切回 cwd 不影响之后的
// DDC_SaveFile —— 这一点由 coretest 的 absolute paths 用例盯着。
template <typename Callable>
static int callWithFileName(const QString& path, Callable&& call)
{
    const QFileInfo fi(path);
    const QString previous = QDir::currentPath();
    const bool switched = QDir::setCurrent(fi.absolutePath());
    const QByteArray fileName = (switched ? fi.fileName() : fi.absoluteFilePath()).toLocal8Bit();
    const int rc = call(fileName.constData());
    if (switched)
        QDir::setCurrent(previous);
    return rc;
}

static size_t typeSize(DDCDataType type)
{
    switch (type) {
    case DDC_UInt8:  return 1;
    case DDC_Int16:  return 2;
    case DDC_Int32:  return 4;
    case DDC_Float:  return 4;
    case DDC_Double: return 8;
    default:         return 0;
    }
}

QString dataTypeName(DDCDataType type)
{
    switch (type) {
    case DDC_UInt8:     return QStringLiteral("UInt8");
    case DDC_Int16:     return QStringLiteral("Int16");
    case DDC_Int32:     return QStringLiteral("Int32");
    case DDC_Float:     return QStringLiteral("Float");
    case DDC_Double:    return QStringLiteral("Double");
    case DDC_String:    return QStringLiteral("String");
    case DDC_Timestamp: return QStringLiteral("Timestamp");
    default:            return QStringLiteral("Unknown");
    }
}

QStringList supportedTypeNames()
{
    return { QStringLiteral("Double"), QStringLiteral("Float"), QStringLiteral("Int32"),
             QStringLiteral("Int16"), QStringLiteral("UInt8"), QStringLiteral("String"),
             QStringLiteral("Timestamp") };
}

bool isNumericDataType(DDCDataType type)
{
    return type == DDC_UInt8 || type == DDC_Int16 || type == DDC_Int32
        || type == DDC_Float || type == DDC_Double;
}

bool dataTypeFromName(const QString& name, DDCDataType* out)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("double"))    { *out = DDC_Double;    return true; }
    if (n == QLatin1String("float"))     { *out = DDC_Float;     return true; }
    if (n == QLatin1String("int32"))     { *out = DDC_Int32;     return true; }
    if (n == QLatin1String("int16"))     { *out = DDC_Int16;     return true; }
    if (n == QLatin1String("uint8"))     { *out = DDC_UInt8;     return true; }
    if (n == QLatin1String("string"))    { *out = DDC_String;    return true; }
    if (n == QLatin1String("timestamp")) { *out = DDC_Timestamp; return true; }
    return false;
}

// --- per-kind property plumbing -------------------------------------------
namespace {

struct Handles {
    DDCFileHandle file = nullptr;
    DDCChannelGroupHandle group = nullptr;
    DDCChannelHandle channel = nullptr;
};

int getStringLength(ObjKind kind, const Handles& h, const char* name, unsigned int* len)
{
    switch (kind) {
    case ObjKind::File:    return DDC_GetFileStringPropertyLength(h.file, name, len);
    case ObjKind::Group:   return DDC_GetChannelGroupStringPropertyLength(h.group, name, len);
    case ObjKind::Channel: return DDC_GetChannelStringPropertyLength(h.channel, name, len);
    }
    return DDC_InvalidArgument;
}

int getRawProperty(ObjKind kind, const Handles& h, const char* name, void* value, size_t size)
{
    switch (kind) {
    case ObjKind::File:    return DDC_GetFileProperty(h.file, name, value, size);
    case ObjKind::Group:   return DDC_GetChannelGroupProperty(h.group, name, value, size);
    case ObjKind::Channel: return DDC_GetChannelProperty(h.channel, name, value, size);
    }
    return DDC_InvalidArgument;
}

int getPropertyNames(ObjKind kind, const Handles& h, unsigned int* count)
{
    switch (kind) {
    case ObjKind::File:    return DDC_GetNumFileProperties(h.file, count);
    case ObjKind::Group:   return DDC_GetNumChannelGroupProperties(h.group, count);
    case ObjKind::Channel: return DDC_GetNumChannelProperties(h.channel, count);
    }
    return DDC_InvalidArgument;
}

int getPropertyNameAt(ObjKind kind, const Handles& h, size_t index, QString* name)
{
    size_t len = 0;
    int rc = 0;
    switch (kind) {
    case ObjKind::File:
        rc = DDC_GetFilePropertyNameLengthFromIndex(h.file, index, &len);
        break;
    case ObjKind::Group:
        rc = DDC_GetChannelGroupPropertyNameLengthFromIndex(h.group, index, &len);
        break;
    case ObjKind::Channel:
        rc = DDC_GetChannelPropertyNameLengthFromIndex(h.channel, index, &len);
        break;
    }
    if (rc < 0)
        return rc;

    QByteArray buf(int(len) + 2, '\0');
    switch (kind) {
    case ObjKind::File:
        rc = DDC_GetFilePropertyNameFromIndex(h.file, index, buf.data(), size_t(buf.size()));
        break;
    case ObjKind::Group:
        rc = DDC_GetChannelGroupPropertyNameFromIndex(h.group, index, buf.data(), size_t(buf.size()));
        break;
    case ObjKind::Channel:
        rc = DDC_GetChannelPropertyNameFromIndex(h.channel, index, buf.data(), size_t(buf.size()));
        break;
    }
    if (rc < 0)
        return rc;
    *name = QString::fromUtf8(buf.constData());
    return 0;
}

int getPropertyType(ObjKind kind, const Handles& h, const char* name, DDCDataType* type)
{
    switch (kind) {
    case ObjKind::File:    return DDC_GetFilePropertyType(h.file, name, type);
    case ObjKind::Group:   return DDC_GetChannelGroupPropertyType(h.group, name, type);
    case ObjKind::Channel: return DDC_GetChannelPropertyType(h.channel, name, type);
    }
    return DDC_InvalidArgument;
}

int propertyExists(ObjKind kind, const Handles& h, const char* name, int* exists)
{
    switch (kind) {
    case ObjKind::File:    return DDC_FilePropertyExists(h.file, name, exists);
    case ObjKind::Group:   return DDC_ChannelGroupPropertyExists(h.group, name, exists);
    case ObjKind::Channel: return DDC_ChannelPropertyExists(h.channel, name, exists);
    }
    return DDC_InvalidArgument;
}

int getTimestampProperty(ObjKind kind, const Handles& h, const char* name, QDateTime* out)
{
    unsigned int y = 0, mo = 0, d = 0, hr = 0, mi = 0, s = 0, wd = 0;
    double ms = 0.0;
    int rc = 0;
    switch (kind) {
    case ObjKind::File:
        rc = DDC_GetFilePropertyTimestampComponents(h.file, name, &y, &mo, &d, &hr, &mi, &s, &ms, &wd);
        break;
    case ObjKind::Group:
        rc = DDC_GetChannelGroupPropertyTimestampComponents(h.group, name, &y, &mo, &d, &hr, &mi, &s, &ms, &wd);
        break;
    case ObjKind::Channel:
        rc = DDC_GetChannelPropertyTimestampComponents(h.channel, name, &y, &mo, &d, &hr, &mi, &s, &ms, &wd);
        break;
    }
    if (rc < 0)
        return rc;
    *out = QDateTime(QDate(int(y), int(mo), int(d)), QTime(int(hr), int(mi), int(s), int(ms)));
    return 0;
}

// Writes a property. When `create` is true a new property is created, otherwise
// the existing one is updated (the SDK requires the correct typed entry point).
int writeProperty(ObjKind kind, const Handles& h, const QString& propName,
                  DDCDataType type, const QVariant& value, bool create)
{
    const QByteArray name = propName.toUtf8();

    switch (type) {
    case DDC_String: {
        const QByteArray text = value.toString().toUtf8();
        switch (kind) {
        case ObjKind::File:
            return create ? DDC_CreateFilePropertyString(h.file, name.constData(), text.constData())
                          : DDC_SetFilePropertyString(h.file, name.constData(), text.constData());
        case ObjKind::Group:
            return create ? DDC_CreateChannelGroupPropertyString(h.group, name.constData(), text.constData())
                          : DDC_SetChannelGroupPropertyString(h.group, name.constData(), text.constData());
        case ObjKind::Channel:
            return create ? DDC_CreateChannelPropertyString(h.channel, name.constData(), text.constData())
                          : DDC_SetChannelPropertyString(h.channel, name.constData(), text.constData());
        }
        break;
    }
    case DDC_UInt8: {
        const unsigned char v = static_cast<unsigned char>(value.toUInt());
        switch (kind) {
        case ObjKind::File:
            return create ? DDC_CreateFilePropertyUInt8(h.file, name.constData(), v)
                          : DDC_SetFilePropertyUInt8(h.file, name.constData(), v);
        case ObjKind::Group:
            return create ? DDC_CreateChannelGroupPropertyUInt8(h.group, name.constData(), v)
                          : DDC_SetChannelGroupPropertyUInt8(h.group, name.constData(), v);
        case ObjKind::Channel:
            return create ? DDC_CreateChannelPropertyUInt8(h.channel, name.constData(), v)
                          : DDC_SetChannelPropertyUInt8(h.channel, name.constData(), v);
        }
        break;
    }
    case DDC_Int16: {
        const short v = static_cast<short>(value.toInt());
        switch (kind) {
        case ObjKind::File:
            return create ? DDC_CreateFilePropertyInt16(h.file, name.constData(), v)
                          : DDC_SetFilePropertyInt16(h.file, name.constData(), v);
        case ObjKind::Group:
            return create ? DDC_CreateChannelGroupPropertyInt16(h.group, name.constData(), v)
                          : DDC_SetChannelGroupPropertyInt16(h.group, name.constData(), v);
        case ObjKind::Channel:
            return create ? DDC_CreateChannelPropertyInt16(h.channel, name.constData(), v)
                          : DDC_SetChannelPropertyInt16(h.channel, name.constData(), v);
        }
        break;
    }
    case DDC_Int32: {
        const long v = static_cast<long>(value.toInt());
        switch (kind) {
        case ObjKind::File:
            return create ? DDC_CreateFilePropertyInt32(h.file, name.constData(), v)
                          : DDC_SetFilePropertyInt32(h.file, name.constData(), v);
        case ObjKind::Group:
            return create ? DDC_CreateChannelGroupPropertyInt32(h.group, name.constData(), v)
                          : DDC_SetChannelGroupPropertyInt32(h.group, name.constData(), v);
        case ObjKind::Channel:
            return create ? DDC_CreateChannelPropertyInt32(h.channel, name.constData(), v)
                          : DDC_SetChannelPropertyInt32(h.channel, name.constData(), v);
        }
        break;
    }
    case DDC_Float: {
        const float v = value.toFloat();
        switch (kind) {
        case ObjKind::File:
            return create ? DDC_CreateFilePropertyFloat(h.file, name.constData(), v)
                          : DDC_SetFilePropertyFloat(h.file, name.constData(), v);
        case ObjKind::Group:
            return create ? DDC_CreateChannelGroupPropertyFloat(h.group, name.constData(), v)
                          : DDC_SetChannelGroupPropertyFloat(h.group, name.constData(), v);
        case ObjKind::Channel:
            return create ? DDC_CreateChannelPropertyFloat(h.channel, name.constData(), v)
                          : DDC_SetChannelPropertyFloat(h.channel, name.constData(), v);
        }
        break;
    }
    case DDC_Double: {
        const double v = value.toDouble();
        switch (kind) {
        case ObjKind::File:
            return create ? DDC_CreateFilePropertyDouble(h.file, name.constData(), v)
                          : DDC_SetFilePropertyDouble(h.file, name.constData(), v);
        case ObjKind::Group:
            return create ? DDC_CreateChannelGroupPropertyDouble(h.group, name.constData(), v)
                          : DDC_SetChannelGroupPropertyDouble(h.group, name.constData(), v);
        case ObjKind::Channel:
            return create ? DDC_CreateChannelPropertyDouble(h.channel, name.constData(), v)
                          : DDC_SetChannelPropertyDouble(h.channel, name.constData(), v);
        }
        break;
    }
    case DDC_Timestamp: {
        const QDateTime dt = value.toDateTime();
        const QDate d = dt.date();
        const QTime t = dt.time();
        const unsigned int yr = static_cast<unsigned int>(d.year());
        const unsigned int mo = static_cast<unsigned int>(d.month());
        const unsigned int da = static_cast<unsigned int>(d.day());
        const unsigned int hh = static_cast<unsigned int>(t.hour());
        const unsigned int mi = static_cast<unsigned int>(t.minute());
        const unsigned int ss = static_cast<unsigned int>(t.second());
        const double ms = t.msec();
        switch (kind) {
        case ObjKind::File:
            return create ? DDC_CreateFilePropertyTimestampComponents(h.file, name.constData(), yr, mo, da, hh, mi, ss, ms)
                          : DDC_SetFilePropertyTimestampComponents(h.file, name.constData(), yr, mo, da, hh, mi, ss, ms);
        case ObjKind::Group:
            return create ? DDC_CreateChannelGroupPropertyTimestampComponents(h.group, name.constData(), yr, mo, da, hh, mi, ss, ms)
                          : DDC_SetChannelGroupPropertyTimestampComponents(h.group, name.constData(), yr, mo, da, hh, mi, ss, ms);
        case ObjKind::Channel:
            return create ? DDC_CreateChannelPropertyTimestampComponents(h.channel, name.constData(), yr, mo, da, hh, mi, ss, ms)
                          : DDC_SetChannelPropertyTimestampComponents(h.channel, name.constData(), yr, mo, da, hh, mi, ss, ms);
        }
        break;
    }
    }
    return DDC_InvalidDataType;
}

QVariant readPropertyValue(ObjKind kind, const Handles& h, const QString& propName,
                           DDCDataType type, int* err)
{
    const QByteArray name = propName.toUtf8();
    auto finish = [err](int code) {
        if (err)
            *err = code;
    };

    if (type == DDC_String) {
        unsigned int len = 0;
        int rc = getStringLength(kind, h, name.constData(), &len);
        if (rc < 0) { finish(rc); return QVariant(); }
        QByteArray buf(int(len) + 2, '\0');
        rc = getRawProperty(kind, h, name.constData(), buf.data(), size_t(buf.size()));
        if (rc < 0) { finish(rc); return QVariant(); }
        finish(0);
        return QString::fromUtf8(buf.constData());
    }

    if (type == DDC_Timestamp) {
        QDateTime dt;
        const int rc = getTimestampProperty(kind, h, name.constData(), &dt);
        if (rc < 0) { finish(rc); return QVariant(); }
        finish(0);
        return dt;
    }

    const size_t size = typeSize(type);
    if (size == 0) { finish(DDC_InvalidDataType); return QVariant(); }

    char buf[8] = { 0 };
    const int rc = getRawProperty(kind, h, name.constData(), buf, size);
    if (rc < 0) { finish(rc); return QVariant(); }
    finish(0);

    switch (type) {
    case DDC_UInt8:  return int(*reinterpret_cast<unsigned char*>(buf));
    case DDC_Int16:  return int(*reinterpret_cast<short*>(buf));
    case DDC_Int32:  return int(*reinterpret_cast<long*>(buf));
    case DDC_Float:  return double(*reinterpret_cast<float*>(buf));
    case DDC_Double: return *reinterpret_cast<double*>(buf);
    default:         return QVariant();
    }
}

QString uniqueName(const QString& base, const QSet<QString>& taken)
{
    if (!taken.contains(base))
        return base;
    for (int i = 2; i < 100000; ++i) {
        const QString candidate = QStringLiteral("%1 (%2)").arg(base).arg(i);
        if (!taken.contains(candidate))
            return candidate;
    }
    return base + QStringLiteral("_new");
}

// TDMS writes a "<file>.tdms_index" sidecar, TDM writes "<file>.tdx".
QString sidecarIndexPath(const QString& path)
{
    return path + QStringLiteral("_index");
}

QString sidecarTdxPath(const QString& path)
{
    const QFileInfo info(path);
    if (info.suffix().compare(QLatin1String("tdm"), Qt::CaseInsensitive) != 0)
        return QString();
    return info.path() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".tdx");
}

void removeSidecars(const QString& path)
{
    QFile::remove(sidecarIndexPath(path));
    const QString tdx = sidecarTdxPath(path);
    if (!tdx.isEmpty())
        QFile::remove(tdx);
}

void moveSidecars(const QString& from, const QString& to)
{
    const QString indexFrom = sidecarIndexPath(from);
    if (QFile::exists(indexFrom)) {
        QFile::remove(sidecarIndexPath(to));
        QFile::rename(indexFrom, sidecarIndexPath(to));
    }
    const QString tdxFrom = sidecarTdxPath(from);
    const QString tdxTo = sidecarTdxPath(to);
    if (!tdxFrom.isEmpty() && !tdxTo.isEmpty() && QFile::exists(tdxFrom)) {
        QFile::remove(tdxTo);
        QFile::rename(tdxFrom, tdxTo);
    }
}

QString sanitizeFileName(const QString& name)
{
    QString out = name;
    const QString bad = QStringLiteral("\\/:*?\"<>|");
    for (const QChar& c : bad)
        out.replace(c, QLatin1Char('_'));
    out = out.trimmed();
    return out.isEmpty() ? QStringLiteral("data") : out;
}

// Copies every property except "name" (groups/channels take the name at creation).
bool copyProperties(ObjKind kind, const Handles& src, const Handles& dst, QStringList* warnings)
{
    unsigned int count = 0;
    if (getPropertyNames(kind, src, &count) < 0)
        return true;

    for (unsigned int i = 0; i < count; ++i) {
        QString name;
        if (getPropertyNameAt(kind, src, i, &name) < 0)
            continue;
        if (name == QLatin1String("name") && kind != ObjKind::File)
            continue;
        // Derived channel statistics are recomputed by the library.
        if (name == QLatin1String("minimum") || name == QLatin1String("maximum"))
            continue;

        DDCDataType type = DDC_String;
        if (getPropertyType(kind, src, name.toUtf8().constData(), &type) < 0)
            continue;

        int err = 0;
        const QVariant value = readPropertyValue(kind, src, name, type, &err);
        if (err < 0)
            continue;

        int exists = 0;
        const bool alreadyThere =
            propertyExists(kind, dst, name.toUtf8().constData(), &exists) >= 0 && exists != 0;

        int rc = writeProperty(kind, dst, name, type, value, !alreadyThere);
        if (rc < 0 && !alreadyThere)
            rc = writeProperty(kind, dst, name, type, value, false);
        if (rc < 0 && warnings)
            warnings->append(QStringLiteral("属性 \"%1\" 复制失败：%2").arg(name, lastErrorText(rc)));
    }
    return true;
}

// Streams channel values from one file to another in blocks.
bool copyChannelValues(DDCChannelHandle src, DDCChannelHandle dst, DDCDataType type,
                       quint64 count, QString* error)
{
    if (count == 0)
        return true;

    if (isNumericDataType(type)) {
        const size_t elem = typeSize(type);
        const quint64 block = qMin<quint64>(65536, count);
        QByteArray buffer(int(block * elem), Qt::Uninitialized);
        quint64 offset = 0;
        while (offset < count) {
            const size_t n = size_t(qMin<quint64>(block, count - offset));
            int rc = DDC_GetDataValues(src, size_t(offset), n, buffer.data());
            if (rc < 0) {
                *error = QStringLiteral("读取通道数据失败：%1").arg(lastErrorText(rc));
                return false;
            }
            rc = DDC_AppendDataValues(dst, buffer.data(), n);
            if (rc < 0) {
                *error = QStringLiteral("写入通道数据失败：%1").arg(lastErrorText(rc));
                return false;
            }
            offset += n;
        }
        return true;
    }

    if (type == DDC_String) {
        const quint64 block = qMin<quint64>(2048, count);
        quint64 offset = 0;
        while (offset < count) {
            const size_t n = size_t(qMin<quint64>(block, count - offset));
            QVector<char*> ptrs(int(n), nullptr);
            int rc = DDC_GetDataValuesString(src, size_t(offset), n, ptrs.data());
            if (rc < 0) {
                for (char* p : ptrs) if (p) DDC_FreeMemory(p);
                *error = QStringLiteral("读取字符串通道失败：%1").arg(lastErrorText(rc));
                return false;
            }
            QVector<const char*> constPtrs(int(n), nullptr);
            for (int i = 0; i < int(n); ++i)
                constPtrs[i] = ptrs[i] ? ptrs[i] : "";
            rc = DDC_AppendDataValuesString(dst, constPtrs.data(), n);
            for (char* p : ptrs) if (p) DDC_FreeMemory(p);
            if (rc < 0) {
                *error = QStringLiteral("写入字符串通道失败：%1").arg(lastErrorText(rc));
                return false;
            }
            offset += n;
        }
        return true;
    }

    if (type == DDC_Timestamp) {
        const quint64 block = qMin<quint64>(4096, count);
        quint64 offset = 0;
        while (offset < count) {
            const size_t n = size_t(qMin<quint64>(block, count - offset));
            const int blockCount = int(n);
            QVector<unsigned int> y(blockCount), mo(blockCount), d(blockCount),
                hh(blockCount), mi(blockCount), ss(blockCount), wd(blockCount);
            QVector<double> ms(blockCount, 0.0);
            int rc = DDC_GetDataValuesTimestampComponents(
                src, size_t(offset), n, y.data(), mo.data(), d.data(), hh.data(),
                mi.data(), ss.data(), ms.data(), wd.data());
            if (rc < 0) {
                *error = QStringLiteral("读取时间戳通道失败：%1").arg(lastErrorText(rc));
                return false;
            }
            rc = DDC_AppendDataValuesTimestampComponents(
                dst, y.data(), mo.data(), d.data(), hh.data(), mi.data(),
                ss.data(), ms.data(), n);
            if (rc < 0) {
                *error = QStringLiteral("写入时间戳通道失败：%1").arg(lastErrorText(rc));
                return false;
            }
            offset += n;
        }
        return true;
    }

    *error = QStringLiteral("不支持的数据类型：%1").arg(dataTypeName(type));
    return false;
}

QString csvField(const QString& text)
{
    if (text.contains(QLatin1Char(',')) || text.contains(QLatin1Char('"'))
        || text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r'))) {
        QString escaped = text;
        escaped.replace(QLatin1Char('"'), QLatin1String("\"\""));
        return QLatin1Char('"') + escaped + QLatin1Char('"');
    }
    return text;
}

QString formatNumber(double value, int precision)
{
    if (!std::isfinite(value))
        return QStringLiteral("NaN");
    return QString::number(value, 'g', precision);
}

} // namespace

// ===========================================================================
// Document
// ===========================================================================
Document::Document() = default;

Document::~Document()
{
    close();
}

bool Document::open(const QString& path, bool readOnly, QString* error)
{
    close();

    const QFileInfo fi(path);
    QString type = fi.suffix().toLower() == QLatin1String("tdm") ? QStringLiteral("TDM")
                                                                : QStringLiteral("TDMS");

    const QByteArray typeBytes = type.toUtf8();
    DDCFileHandle file = nullptr;
    int rc = callWithFileName(path, [&](const char* fileName) {
        return DDC_OpenFileEx(fileName, typeBytes.constData(), readOnly ? 1 : 0, &file);
    });

    if (rc < 0 && !readOnly) {
        // Possibly read-only media or a locked file: retry in read-only mode.
        rc = callWithFileName(path, [&](const char* fileName) {
            return DDC_OpenFileEx(fileName, typeBytes.constData(), 1, &file);
        });
        if (rc >= 0) {
            readOnly = true;
        }
    }
    if (rc < 0) {
        if (error)
            *error = QStringLiteral("无法打开文件：%1").arg(lastErrorText(rc));
        return false;
    }

    m_file = file;
    m_path = fi.absoluteFilePath();
    m_fileType = type;
    m_readOnly = readOnly;
    m_dirty = false;

    QString loadError;
    if (!loadStructure(&loadError)) {
        if (error)
            *error = loadError;
        close();
        return false;
    }
    return true;
}

bool Document::create(const QString& path, const QString& fileType, QString* error)
{
    close();

    const QFileInfo fi(path);
    if (fi.exists() && !QFile::remove(path)) {
        if (error)
            *error = QStringLiteral("目标文件已存在且无法覆盖：%1").arg(path);
        return false;
    }

    const QString type = (fileType.compare(QLatin1String("TDM"), Qt::CaseInsensitive) == 0)
                             ? QStringLiteral("TDM")
                             : QStringLiteral("TDMS");
    const QByteArray name = fi.completeBaseName().toUtf8();

    const QByteArray typeBytes = type.toUtf8();
    DDCFileHandle file = nullptr;
    const int rc = callWithFileName(path, [&](const char* fileName) {
        return DDC_CreateFile(fileName, typeBytes.constData(), name.constData(),
                              "", "", "", &file);
    });
    if (rc < 0) {
        if (error)
            *error = QStringLiteral("无法创建文件：%1").arg(lastErrorText(rc));
        return false;
    }

    m_file = file;
    m_path = fi.absoluteFilePath();
    m_fileType = type;
    m_readOnly = false;
    m_dirty = true;
    m_groups.clear();

    // Give the file a human readable title and creation time.
    DDC_SetFilePropertyString(m_file, DDC_FILE_TITLE, name.constData());
    const QDateTime now = QDateTime::currentDateTime();
    DDC_SetFilePropertyTimestampComponents(m_file, DDC_FILE_DATETIME,
        unsigned(now.date().year()), unsigned(now.date().month()), unsigned(now.date().day()),
        unsigned(now.time().hour()), unsigned(now.time().minute()), unsigned(now.time().second()),
        now.time().msec());
    return true;
}

void Document::close()
{
    if (m_file) {
        DDC_CloseFile(m_file);
        m_file = nullptr;
    }
    m_groups.clear();
    m_path.clear();
    m_fileType.clear();
    m_readOnly = false;
    m_dirty = false;
}

QString Document::title() const
{
    if (!m_file)
        return QString();
    QVariant value = readPropertyValue(ObjKind::File, Handles{ m_file, nullptr, nullptr },
                                       QStringLiteral("title"), DDC_String, nullptr);
    const QString text = value.toString();
    return text.isEmpty() ? QFileInfo(m_path).fileName() : text;
}

QDateTime Document::fileDateTime() const
{
    if (!m_file)
        return QDateTime();

    int exists = 0;
    if (DDC_FilePropertyExists(m_file, "datetime", &exists) < 0 || !exists)
        return QDateTime();

    QDateTime value;
    if (getTimestampProperty(ObjKind::File, Handles{ m_file, nullptr, nullptr },
                             "datetime", &value) < 0)
        return QDateTime();
    return value;
}

// Reads the standard NI waveform timing properties of a channel. Any missing
// property simply leaves the corresponding field at its default.
void readChannelTiming(DDCChannelHandle handle, ChannelInfo* channel)
{
    const Handles h{ nullptr, nullptr, handle };
    int exists = 0;

    if (DDC_ChannelPropertyExists(handle, "wf_increment", &exists) >= 0 && exists) {
        int err = 0;
        const QVariant v = readPropertyValue(ObjKind::Channel, h,
                                             QStringLiteral("wf_increment"), DDC_Double, &err);
        if (err >= 0)
            channel->increment = v.toDouble();
    }

    if (DDC_ChannelPropertyExists(handle, "wf_start_offset", &exists) >= 0 && exists) {
        int err = 0;
        const QVariant v = readPropertyValue(ObjKind::Channel, h,
                                             QStringLiteral("wf_start_offset"), DDC_Double, &err);
        if (err >= 0)
            channel->startOffset = v.toDouble();
    }

    if (DDC_ChannelPropertyExists(handle, "wf_start_time", &exists) >= 0 && exists) {
        QDateTime start;
        if (getTimestampProperty(ObjKind::Channel, h, "wf_start_time", &start) >= 0)
            channel->startTime = start;
    }

    if (!(channel->increment > 0.0) || !std::isfinite(channel->increment)) {
        channel->increment = 0.0;
        channel->startOffset = 0.0;
    }
}

bool Document::loadStructure(QString* error)
{
    m_groups.clear();
    if (!m_file)
        return false;

    auto readString = [](ObjKind kind, const Handles& h, const char* prop) -> QString {
        int err = 0;
        return readPropertyValue(kind, h, QString::fromLatin1(prop), DDC_String, &err).toString();
    };

    unsigned int numGroups = 0;
    int rc = DDC_GetNumChannelGroups(m_file, &numGroups);
    if (rc < 0) {
        *error = QStringLiteral("读取分组失败：%1").arg(lastErrorText(rc));
        return false;
    }

    QVector<DDCChannelGroupHandle> groupHandles(int(numGroups), nullptr);
    if (numGroups > 0) {
        rc = DDC_GetChannelGroups(m_file, groupHandles.data(), size_t(numGroups));
        if (rc < 0) {
            *error = QStringLiteral("读取分组失败：%1").arg(lastErrorText(rc));
            return false;
        }
    }

    for (int g = 0; g < int(numGroups); ++g) {
        GroupInfo info;
        info.handle = groupHandles[g];
        const Handles gh{ nullptr, groupHandles[g], nullptr };
        info.name = readString(ObjKind::Group, gh, DDC_CHANNELGROUP_NAME);
        info.description = readString(ObjKind::Group, gh, DDC_CHANNELGROUP_DESCRIPTION);
        if (info.name.isEmpty())
            info.name = QStringLiteral("Group %1").arg(g + 1);

        unsigned int numChannels = 0;
        if (DDC_GetNumChannels(groupHandles[g], &numChannels) >= 0 && numChannels > 0) {
            QVector<DDCChannelHandle> channelHandles(int(numChannels), nullptr);
            if (DDC_GetChannels(groupHandles[g], channelHandles.data(), size_t(numChannels)) >= 0) {
                for (int c = 0; c < int(numChannels); ++c) {
                    ChannelInfo ch;
                    ch.handle = channelHandles[c];
                    const Handles chh{ nullptr, nullptr, channelHandles[c] };
                    ch.name = readString(ObjKind::Channel, chh, DDC_CHANNEL_NAME);
                    ch.description = readString(ObjKind::Channel, chh, DDC_CHANNEL_DESCRIPTION);
                    ch.unit = readString(ObjKind::Channel, chh, DDC_CHANNEL_UNIT_STRING);
                    if (ch.name.isEmpty())
                        ch.name = QStringLiteral("Channel %1").arg(c + 1);

                    DDCDataType type = DDC_Double;
                    DDC_GetDataType(channelHandles[c], &type);
                    ch.type = type;

                    unsigned __int64 count = 0;
                    if (DDC_GetNumDataValues(channelHandles[c], &count) >= 0)
                        ch.count = quint64(count);

                    readChannelTiming(channelHandles[c], &ch);

                    info.channels.append(ch);
                }
            }
        }
        m_groups.append(info);
    }
    return true;
}

GroupInfo* Document::groupAt(int index)
{
    return (index >= 0 && index < m_groups.size()) ? &m_groups[index] : nullptr;
}

const GroupInfo* Document::groupAt(int index) const
{
    return (index >= 0 && index < m_groups.size()) ? &m_groups[index] : nullptr;
}

ChannelInfo* Document::channelAt(int group, int channel)
{
    GroupInfo* g = groupAt(group);
    if (!g || channel < 0 || channel >= g->channels.size())
        return nullptr;
    return &g->channels[channel];
}

const ChannelInfo* Document::channelAt(int group, int channel) const
{
    const GroupInfo* g = groupAt(group);
    if (!g || channel < 0 || channel >= g->channels.size())
        return nullptr;
    return &g->channels[channel];
}

void* Document::rawHandle(ObjKind kind, int group, int channel) const
{
    switch (kind) {
    case ObjKind::File:
        return m_file;
    case ObjKind::Group: {
        const GroupInfo* g = groupAt(group);
        return g ? g->handle : nullptr;
    }
    case ObjKind::Channel: {
        const ChannelInfo* c = channelAt(group, channel);
        return c ? c->handle : nullptr;
    }
    }
    return nullptr;
}

namespace {

Handles makeHandles(ObjKind kind, void* handle)
{
    Handles h;
    switch (kind) {
    case ObjKind::File:    h.file = static_cast<DDCFileHandle>(handle); break;
    case ObjKind::Group:   h.group = static_cast<DDCChannelGroupHandle>(handle); break;
    case ObjKind::Channel: h.channel = static_cast<DDCChannelHandle>(handle); break;
    }
    return h;
}

} // namespace

QVector<PropEntry> Document::properties(ObjKind kind, int group, int channel) const
{
    QVector<PropEntry> list;
    void* handle = rawHandle(kind, group, channel);
    if (!handle)
        return list;

    const Handles h = makeHandles(kind, handle);
    unsigned int count = 0;
    if (getPropertyNames(kind, h, &count) < 0)
        return list;

    for (unsigned int i = 0; i < count; ++i) {
        QString name;
        if (getPropertyNameAt(kind, h, i, &name) < 0)
            continue;

        PropEntry entry;
        entry.name = name;

        DDCDataType type = DDC_String;
        if (getPropertyType(kind, h, name.toUtf8().constData(), &type) < 0)
            continue;
        entry.type = type;

        int err = 0;
        entry.value = readPropertyValue(kind, h, name, type, &err);
        if (err == DDC_PropertyIsNotAScalar) {
            entry.value = QVariant();
            entry.readOnly = true;
        } else if (err < 0) {
            entry.value = QVariant();
            entry.readOnly = true;
        }

        // Derived channel statistics cannot be edited.
        if (kind == ObjKind::Channel
            && (name == QLatin1String("minimum") || name == QLatin1String("maximum")))
            entry.readOnly = true;

        list.append(entry);
    }
    return list;
}

bool Document::setProperty(ObjKind kind, int group, int channel, const QString& name,
                           DDCDataType type, const QVariant& value, QString* error)
{
    if (m_readOnly) {
        *error = QStringLiteral("文件以只读方式打开，无法修改。");
        return false;
    }
    void* handle = rawHandle(kind, group, channel);
    if (!handle) {
        *error = QStringLiteral("无效的对象。");
        return false;
    }

    const int rc = writeProperty(kind, makeHandles(kind, handle), name, type, value, false);
    if (rc < 0) {
        if (error)
            *error = QStringLiteral("修改属性 \"%1\" 失败：%2").arg(name, lastErrorText(rc));
        return false;
    }
    m_dirty = true;

    // Keep the cached structure in sync for name/description edits.
    if (kind == ObjKind::Group && groupAt(group)) {
        if (name == QLatin1String("name"))
            groupAt(group)->name = value.toString();
        else if (name == QLatin1String("description"))
            groupAt(group)->description = value.toString();
    } else if (kind == ObjKind::Channel && channelAt(group, channel)) {
        ChannelInfo* ch = channelAt(group, channel);
        if (name == QLatin1String("name"))
            ch->name = value.toString();
        else if (name == QLatin1String("description"))
            ch->description = value.toString();
        else if (name == QLatin1String("unit_string"))
            ch->unit = value.toString();
    }
    return true;
}

bool Document::createProperty(ObjKind kind, int group, int channel, const QString& name,
                              DDCDataType type, const QVariant& value, QString* error)
{
    if (m_readOnly) {
        *error = QStringLiteral("文件以只读方式打开，无法修改。");
        return false;
    }
    void* handle = rawHandle(kind, group, channel);
    if (!handle) {
        *error = QStringLiteral("无效的对象。");
        return false;
    }
    if (name.trimmed().isEmpty()) {
        *error = QStringLiteral("属性名不能为空。");
        return false;
    }

    const int rc = writeProperty(kind, makeHandles(kind, handle), name, type, value, true);
    if (rc < 0) {
        if (error)
            *error = QStringLiteral("创建属性 \"%1\" 失败：%2").arg(name, lastErrorText(rc));
        return false;
    }
    m_dirty = true;
    return true;
}

bool Document::save(QString* error)
{
    if (!m_file) {
        *error = QStringLiteral("没有打开的文件。");
        return false;
    }
    if (m_readOnly) {
        *error = QStringLiteral("文件以只读方式打开，无法保存。");
        return false;
    }
    const int rc = DDC_SaveFile(m_file);
    if (rc < 0) {
        *error = QStringLiteral("保存失败：%1").arg(lastErrorText(rc));
        return false;
    }
    m_dirty = false;
    return true;
}

bool Document::saveAs(const QString& newPath, QString* error)
{
    if (!m_file) {
        *error = QStringLiteral("没有打开的文件。");
        return false;
    }
    if (QFileInfo(newPath).absoluteFilePath() == QFileInfo(m_path).absoluteFilePath())
        return save(error);

    if (!save(error))
        return false;

    if (QFile::exists(newPath) && !QFile::remove(newPath)) {
        *error = QStringLiteral("无法覆盖目标文件：%1").arg(newPath);
        return false;
    }
    if (!QFile::copy(m_path, newPath)) {
        *error = QStringLiteral("复制文件到新位置失败：%1").arg(newPath);
        return false;
    }

    // Move the TDMS index / TDM tdx sidecars along with the data file.
    const QString indexSource = sidecarIndexPath(m_path);
    if (QFile::exists(indexSource)) {
        QFile::remove(sidecarIndexPath(newPath));
        QFile::copy(indexSource, sidecarIndexPath(newPath));
    }
    const QString tdxSource = sidecarTdxPath(m_path);
    const QString tdxTarget = sidecarTdxPath(newPath);
    if (!tdxSource.isEmpty() && !tdxTarget.isEmpty() && QFile::exists(tdxSource)) {
        QFile::remove(tdxTarget);
        QFile::copy(tdxSource, tdxTarget);
    }

    const bool wasDirty = false;
    const QString oldPath = m_path;
    close();
    if (!open(newPath, false, error)) {
        // try to restore the original document
        QString ignored;
        open(oldPath, false, &ignored);
        return false;
    }
    m_dirty = wasDirty;
    return true;
}

bool Document::readChannel(int group, int channel, QVector<double>* values,
                           QStringList* labels, QString* error) const
{
    if (values)
        values->clear();
    if (labels)
        labels->clear();

    const ChannelInfo* ch = channelAt(group, channel);
    if (!ch || !ch->handle) {
        *error = QStringLiteral("无效的通道。");
        return false;
    }
    if (ch->count > 50000000ULL) {
        *error = QStringLiteral("通道数据量过大（%1 点），超出查看器处理上限。").arg(ch->count);
        return false;
    }

    const quint64 count = ch->count;
    if (count == 0) {
        if (values)
            values->clear();
        if (labels)
            labels->clear();
        return true;
    }

    if (isNumericDataType(ch->type)) {
        const size_t elem = typeSize(ch->type);
        QByteArray buffer(int(count * elem), Qt::Uninitialized);
        const int rc = DDC_GetDataValues(ch->handle, 0, size_t(count), buffer.data());
        if (rc < 0) {
            *error = QStringLiteral("读取通道数据失败：%1").arg(lastErrorText(rc));
            return false;
        }
        if (!values) {
            if (labels) {
                for (quint64 i = 0; i < count; ++i)
                    labels->append(QString::number(qulonglong(i)));
            }
            return true;
        }
        values->resize(int(count));
        const char* src = buffer.constData();
        double* dst = values->data();
        switch (ch->type) {
        case DDC_UInt8: {
            const unsigned char* p = reinterpret_cast<const unsigned char*>(src);
            for (quint64 i = 0; i < count; ++i) dst[i] = double(p[i]);
            break;
        }
        case DDC_Int16: {
            const short* p = reinterpret_cast<const short*>(src);
            for (quint64 i = 0; i < count; ++i) dst[i] = double(p[i]);
            break;
        }
        case DDC_Int32: {
            const long* p = reinterpret_cast<const long*>(src);
            for (quint64 i = 0; i < count; ++i) dst[i] = double(p[i]);
            break;
        }
        case DDC_Float: {
            const float* p = reinterpret_cast<const float*>(src);
            for (quint64 i = 0; i < count; ++i) dst[i] = double(p[i]);
            break;
        }
        default:
            std::memcpy(dst, src, size_t(count) * sizeof(double));
            break;
        }
        return true;
    }

    if (ch->type == DDC_String) {
        QStringList out;
        const quint64 block = qMin<quint64>(4096, count);
        quint64 offset = 0;
        while (offset < count) {
            const size_t n = size_t(qMin<quint64>(block, count - offset));
            QVector<char*> ptrs(int(n), nullptr);
            const int rc = DDC_GetDataValuesString(ch->handle, size_t(offset), n, ptrs.data());
            if (rc < 0) {
                for (char* p : ptrs) if (p) DDC_FreeMemory(p);
                *error = QStringLiteral("读取字符串通道失败：%1").arg(lastErrorText(rc));
                return false;
            }
            for (int i = 0; i < int(n); ++i)
                out.append(ptrs[i] ? QString::fromUtf8(ptrs[i]) : QString());
            for (char* p : ptrs) if (p) DDC_FreeMemory(p);
            offset += n;
        }
        if (labels)
            *labels = out;
        return true;
    }

    if (ch->type == DDC_Timestamp) {
        QStringList out;
        const quint64 block = qMin<quint64>(4096, count);
        quint64 offset = 0;
        while (offset < count) {
            const size_t n = size_t(qMin<quint64>(block, count - offset));
            const int blockCount = int(n);
            QVector<unsigned int> y(blockCount), mo(blockCount), d(blockCount),
                hh(blockCount), mi(blockCount), ss(blockCount), wd(blockCount);
            QVector<double> ms(blockCount, 0.0);
            const int rc = DDC_GetDataValuesTimestampComponents(
                ch->handle, size_t(offset), n, y.data(), mo.data(), d.data(), hh.data(),
                mi.data(), ss.data(), ms.data(), wd.data());
            if (rc < 0) {
                *error = QStringLiteral("读取时间戳通道失败：%1").arg(lastErrorText(rc));
                return false;
            }
            for (int i = 0; i < int(n); ++i) {
                const QDate date(static_cast<int>(y.at(i)), static_cast<int>(mo.at(i)),
                                 static_cast<int>(d.at(i)));
                const QTime time(static_cast<int>(hh.at(i)), static_cast<int>(mi.at(i)),
                                 static_cast<int>(ss.at(i)), static_cast<int>(ms.at(i)));
                out.append(QDateTime(date, time).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
            }
            offset += n;
        }
        if (labels)
            *labels = out;
        return true;
    }

    *error = QStringLiteral("不支持的数据类型：%1").arg(dataTypeName(ch->type));
    return false;
}

bool Document::addGroup(const QString& name, const QString& description, int* index, QString* error)
{
    if (m_readOnly) {
        *error = QStringLiteral("文件以只读方式打开，无法修改。");
        return false;
    }
    DDCChannelGroupHandle handle = nullptr;
    const int rc = DDC_AddChannelGroup(m_file, name.toUtf8().constData(),
                                       description.toUtf8().constData(), &handle);
    if (rc < 0) {
        *error = QStringLiteral("添加分组失败：%1").arg(lastErrorText(rc));
        return false;
    }

    GroupInfo info;
    info.handle = handle;
    info.name = name;
    info.description = description;
    m_groups.append(info);
    m_dirty = true;
    if (index)
        *index = m_groups.size() - 1;
    return true;
}

bool Document::addChannel(int group, const QString& name, const QString& description,
                          const QString& unit, DDCDataType type, const QVector<double>& values,
                          QString* error)
{
    if (m_readOnly) {
        *error = QStringLiteral("文件以只读方式打开，无法修改。");
        return false;
    }
    GroupInfo* g = groupAt(group);
    if (!g) {
        *error = QStringLiteral("无效的分组。");
        return false;
    }
    if (!isNumericDataType(type)) {
        *error = QStringLiteral("当前仅支持创建数值类型的通道。");
        return false;
    }

    DDCChannelHandle handle = nullptr;
    int rc = DDC_AddChannel(g->handle, type, name.toUtf8().constData(),
                            description.toUtf8().constData(), unit.toUtf8().constData(), &handle);
    if (rc < 0) {
        *error = QStringLiteral("添加通道失败：%1").arg(lastErrorText(rc));
        return false;
    }

    const int n = values.size();
    if (n > 0) {
        switch (type) {
        case DDC_Float: {
            QVector<float> buf(n);
            for (int i = 0; i < n; ++i) buf[i] = float(values[i]);
            rc = DDC_SetDataValuesFloat(handle, buf.data(), size_t(n));
            break;
        }
        case DDC_Int16: {
            QVector<short> buf(n);
            for (int i = 0; i < n; ++i)
                buf[i] = short(qBound(-32768.0, std::round(values[i]), 32767.0));
            rc = DDC_SetDataValuesInt16(handle, buf.data(), size_t(n));
            break;
        }
        case DDC_Int32: {
            QVector<long> buf(n);
            for (int i = 0; i < n; ++i)
                buf[i] = long(qBound(-2147483648.0, std::round(values[i]), 2147483647.0));
            rc = DDC_SetDataValuesInt32(handle, buf.data(), size_t(n));
            break;
        }
        case DDC_UInt8: {
            QVector<unsigned char> buf(n);
            for (int i = 0; i < n; ++i)
                buf[i] = static_cast<unsigned char>(qBound(0.0, std::round(values[i]), 255.0));
            rc = DDC_SetDataValuesUInt8(handle, buf.data(), size_t(n));
            break;
        }
        default: {
            QVector<double> buf(values);
            rc = DDC_SetDataValuesDouble(handle, buf.data(), size_t(n));
            break;
        }
        }
        if (rc < 0) {
            *error = QStringLiteral("写入通道数据失败：%1").arg(lastErrorText(rc));
            return false;
        }
    }

    ChannelInfo info;
    info.handle = handle;
    info.name = name;
    info.description = description;
    info.unit = unit;
    info.type = type;
    info.count = quint64(n);
    g->channels.append(info);
    m_dirty = true;
    return true;
}

bool Document::addChannelFromText(int group, const QString& name, const QString& unit,
                                  const QStringList& textValues, QString* error)
{
    if (m_readOnly) {
        *error = QStringLiteral("文件以只读方式打开，无法修改。");
        return false;
    }
    GroupInfo* g = groupAt(group);
    if (!g) {
        *error = QStringLiteral("无效的分组。");
        return false;
    }

    DDCChannelHandle handle = nullptr;
    int rc = DDC_AddChannel(g->handle, DDC_String, name.toUtf8().constData(), "",
                            unit.toUtf8().constData(), &handle);
    if (rc < 0) {
        *error = QStringLiteral("添加通道失败：%1").arg(lastErrorText(rc));
        return false;
    }

    QVector<QByteArray> encoded;
    encoded.reserve(textValues.size());
    for (const QString& s : textValues)
        encoded.append(s.toUtf8());
    QVector<const char*> ptrs(encoded.size(), nullptr);
    for (int i = 0; i < encoded.size(); ++i)
        ptrs[i] = encoded[i].constData();

    if (!ptrs.isEmpty()) {
        rc = DDC_SetDataValuesString(handle, ptrs.data(), size_t(ptrs.size()));
        if (rc < 0) {
            *error = QStringLiteral("写入字符串通道失败：%1").arg(lastErrorText(rc));
            return false;
        }
    }

    ChannelInfo info;
    info.handle = handle;
    info.name = name;
    info.unit = unit;
    info.type = DDC_String;
    info.count = quint64(textValues.size());
    g->channels.append(info);
    m_dirty = true;
    return true;
}

bool Document::renameChannel(int group, int channel, const QString& newName, QString* error)
{
    return setProperty(ObjKind::Channel, group, channel, QStringLiteral("name"),
                       DDC_String, newName, error);
}

bool Document::setChannelTiming(int group, int channel, const QDateTime& startTime,
                                double increment, double startOffset, QString* error)
{
    if (m_readOnly) {
        *error = QStringLiteral("文件以只读方式打开，无法修改。");
        return false;
    }
    ChannelInfo* ch = channelAt(group, channel);
    if (!ch) {
        *error = QStringLiteral("无效的通道。");
        return false;
    }
    if (!(increment > 0.0) || !std::isfinite(increment)) {
        *error = QStringLiteral("采样间隔必须为正数。");
        return false;
    }

    auto write = [&](const char* name, DDCDataType type, const QVariant& value) {
        int exists = 0;
        const bool present =
            DDC_ChannelPropertyExists(ch->handle, name, &exists) >= 0 && exists != 0;
        const QString propName = QString::fromLatin1(name);
        return present
            ? setProperty(ObjKind::Channel, group, channel, propName, type, value, error)
            : createProperty(ObjKind::Channel, group, channel, propName, type, value, error);
    };

    // A valid absolute start time is what makes the "absolute" axis possible;
    // without one we still keep the interval so relative plotting works.
    const QDateTime start = startTime.isValid() ? startTime : QDateTime::currentDateTime();
    if (!write("wf_start_time", DDC_Timestamp, start))
        return false;
    if (!write("wf_increment", DDC_Double, increment))
        return false;
    if (!write("wf_start_offset", DDC_Double, startOffset))
        return false;

    if (ChannelInfo* updated = channelAt(group, channel)) {
        updated->startTime = start;
        updated->increment = increment;
        updated->startOffset = startOffset;
    }
    m_dirty = true;
    return true;
}

bool Document::removeChannel(int group, int channel, QString* error)
{
    if (m_readOnly) {
        *error = QStringLiteral("文件以只读方式打开，无法修改。");
        return false;
    }
    GroupInfo* g = groupAt(group);
    ChannelInfo* ch = channelAt(group, channel);
    if (!g || !ch) {
        *error = QStringLiteral("无效的通道。");
        return false;
    }

    const int before = g->channels.size();

    // TDMS keeps already-saved segments: the SDK accepts DDC_RemoveChannel but the
    // removal is lost on save. Only the XML based TDM supports native removal.
    if (m_fileType != QLatin1String("TDMS")) {
        const int rc = DDC_RemoveChannel(ch->handle);
        if (rc >= 0) {
            unsigned int now = 0;
            if (DDC_GetNumChannels(g->handle, &now) >= 0 && int(now) < before) {
                g->channels.removeAt(channel);
                m_dirty = true;
                return true;
            }
        }
    }

    // Fall back to rebuilding the file without this channel.
    const QString original = m_path;
    const QFileInfo originalInfo(original);
    const QString temp = originalInfo.path() + QLatin1Char('/')
        + originalInfo.completeBaseName() + QStringLiteral(".rebuild.")
        + originalInfo.suffix();
    QFile::remove(temp);
    removeSidecars(temp);

    close();
    QString rebuildError;
    if (!rebuildWithout(original, temp, ObjKind::Channel, group, channel, &rebuildError)) {
        QString reopenError;
        open(original, false, &reopenError);
        QFile::remove(temp);
        *error = QStringLiteral("当前格式不支持直接删除通道，重建文件也失败：%1").arg(rebuildError);
        return false;
    }
    removeSidecars(original);
    if (!QFile::remove(original)) {
        QFile::remove(temp);
        QString reopenError;
        open(original, false, &reopenError);
        *error = QStringLiteral("无法替换原文件（可能被占用）：%1").arg(original);
        return false;
    }
    if (!QFile::rename(temp, original)) {
        QString reopenError;
        open(original, false, &reopenError);
        *error = QStringLiteral("无法重命名重建后的文件。");
        return false;
    }
    moveSidecars(temp, original);
    if (!open(original, false, error))
        return false;
    m_dirty = true;
    return true;
}

bool Document::removeGroup(int group, QString* error)
{
    if (m_readOnly) {
        *error = QStringLiteral("文件以只读方式打开，无法修改。");
        return false;
    }
    GroupInfo* g = groupAt(group);
    if (!g) {
        *error = QStringLiteral("无效的分组。");
        return false;
    }

    const int before = m_groups.size();
    if (m_fileType != QLatin1String("TDMS")) {
        const int rc = DDC_RemoveChannelGroup(g->handle);
        if (rc >= 0) {
            unsigned int now = 0;
            if (DDC_GetNumChannelGroups(m_file, &now) >= 0 && int(now) < before) {
                m_groups.removeAt(group);
                m_dirty = true;
                return true;
            }
        }
    }

    const QString original = m_path;
    const QFileInfo originalInfo(original);
    const QString temp = originalInfo.path() + QLatin1Char('/')
        + originalInfo.completeBaseName() + QStringLiteral(".rebuild.")
        + originalInfo.suffix();
    QFile::remove(temp);
    removeSidecars(temp);

    close();
    QString rebuildError;
    if (!rebuildWithout(original, temp, ObjKind::Group, group, -1, &rebuildError)) {
        QString reopenError;
        open(original, false, &reopenError);
        QFile::remove(temp);
        *error = QStringLiteral("当前格式不支持直接删除分组，重建文件也失败：%1").arg(rebuildError);
        return false;
    }
    removeSidecars(original);
    if (!QFile::remove(original)) {
        QFile::remove(temp);
        QString reopenError;
        open(original, false, &reopenError);
        *error = QStringLiteral("无法替换原文件（可能被占用）：%1").arg(original);
        return false;
    }
    if (!QFile::rename(temp, original)) {
        QString reopenError;
        open(original, false, &reopenError);
        *error = QStringLiteral("无法重命名重建后的文件。");
        return false;
    }
    moveSidecars(temp, original);
    if (!open(original, false, error))
        return false;
    m_dirty = true;
    return true;
}

// ===========================================================================
// Export
// ===========================================================================
bool Document::exportChannel(int group, int channel, const QString& filePath,
                             const ExportSettings& settings, QString* error) const
{
    const ChannelInfo* ch = channelAt(group, channel);
    if (!ch) {
        *error = QStringLiteral("无效的通道。");
        return false;
    }

    QVector<double> values;
    QStringList labels;
    if (!readChannel(group, channel, &values, &labels, error))
        return false;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("无法写入文件：%1").arg(filePath);
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    if (settings.csv)
        out.setGenerateByteOrderMark(true);

    const QString nl = QStringLiteral("\r\n");
    const QChar delim = settings.csv ? QChar(u',') : QChar(u'\t');
    const GroupInfo* g = groupAt(group);

    if (settings.includeMeta) {
        const QChar comment = QChar(u'#');
        out << comment << " TDMS Viewer export" << nl;
        out << comment << " File    : " << QFileInfo(m_path).fileName() << nl;
        out << comment << " Group   : " << (g ? g->name : QString()) << nl;
        out << comment << " Channel : " << ch->name << nl;
        out << comment << " Type    : " << dataTypeName(ch->type) << nl;
        if (!ch->unit.isEmpty())
            out << comment << " Unit    : " << ch->unit << nl;
        out << comment << " Points  : " << ch->count << nl;
        out << comment << " Exported: " << QDateTime::currentDateTime().toString(Qt::ISODate) << nl;
    }

    QString header;
    if (settings.includeIndex)
        header += QStringLiteral("Index") + delim;
    header += settings.csv ? csvField(ch->name) : ch->name;
    if (!ch->unit.isEmpty())
        header += QStringLiteral(" [%1]").arg(ch->unit);
    out << header << nl;

    const bool textChannel = !isNumericDataType(ch->type);
    const qint64 n = textChannel ? labels.size() : values.size();
    for (qint64 i = 0; i < n; ++i) {
        QString line;
        if (settings.includeIndex)
            line += QString::number(i) + delim;
        if (textChannel) {
            line += settings.csv ? csvField(labels.at(int(i))) : labels.at(int(i));
        } else {
            line += formatNumber(values.at(int(i)), settings.precision);
        }
        out << line << nl;
    }

    out.flush();
    file.close();
    return true;
}

bool Document::exportGroup(int group, const QString& filePath,
                           const ExportSettings& settings, QString* error) const
{
    const GroupInfo* g = groupAt(group);
    if (!g) {
        *error = QStringLiteral("无效的分组。");
        return false;
    }

    struct Column {
        QString name;
        QString unit;
        bool text = false;
        QVector<double> values;
        QStringList labels;
    };
    QVector<Column> columns;
    qint64 maxCount = 0;

    for (int chIndex = 0; chIndex < g->channels.size(); ++chIndex) {
        const ChannelInfo& ch = g->channels.at(chIndex);
        Column col;
        col.name = ch.name;
        col.unit = ch.unit;
        col.text = !isNumericDataType(ch.type);
        if (!readChannel(group, chIndex, &col.values, &col.labels, error))
            return false;
        maxCount = qMax<qint64>(maxCount, col.text ? col.labels.size() : col.values.size());
        columns.append(col);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("无法写入文件：%1").arg(filePath);
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    if (settings.csv)
        out.setGenerateByteOrderMark(true);

    const QString nl = QStringLiteral("\r\n");
    const QChar delim = settings.csv ? QChar(u',') : QChar(u'\t');

    if (settings.includeMeta) {
        out << "# TDMS Viewer export" << nl;
        out << "# File  : " << QFileInfo(m_path).fileName() << nl;
        out << "# Group : " << g->name << nl;
        out << "# Points: " << maxCount << nl;
        out << "# Exported: " << QDateTime::currentDateTime().toString(Qt::ISODate) << nl;
    }

    QString header;
    if (settings.includeIndex)
        header += QStringLiteral("Index") + delim;
    for (int c = 0; c < columns.size(); ++c) {
        QString field = columns[c].name;
        if (!columns[c].unit.isEmpty())
            field += QStringLiteral(" [%1]").arg(columns[c].unit);
        header += settings.csv ? csvField(field) : field;
        if (c + 1 < columns.size())
            header += delim;
    }
    out << header << nl;

    for (qint64 row = 0; row < maxCount; ++row) {
        QString line;
        if (settings.includeIndex)
            line += QString::number(row) + delim;
        for (int c = 0; c < columns.size(); ++c) {
            const Column& col = columns[c];
            if (col.text) {
                if (row < col.labels.size())
                    line += settings.csv ? csvField(col.labels.at(int(row))) : col.labels.at(int(row));
            } else if (row < col.values.size()) {
                line += formatNumber(col.values.at(int(row)), settings.precision);
            }
            if (c + 1 < columns.size())
                line += delim;
        }
        out << line << nl;
    }

    out.flush();
    file.close();
    return true;
}

bool Document::exportAllToDir(const QString& dirPath, const ExportSettings& settings,
                              QStringList* writtenFiles, QString* error) const
{
    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        *error = QStringLiteral("无法创建目录：%1").arg(dirPath);
        return false;
    }

    const QString ext = settings.csv ? QStringLiteral(".csv") : QStringLiteral(".txt");
    QSet<QString> used;
    for (int g = 0; g < m_groups.size(); ++g) {
        QString base = sanitizeFileName(m_groups[g].name);
        QString candidate = base + ext;
        int suffix = 2;
        while (used.contains(candidate) || dir.exists(candidate)) {
            candidate = QStringLiteral("%1_%2%3").arg(base).arg(suffix++).arg(ext);
        }
        used.insert(candidate);
        const QString path = dir.filePath(candidate);
        if (!exportGroup(g, path, settings, error))
            return false;
        if (writtenFiles)
            writtenFiles->append(path);
    }
    return true;
}

// ===========================================================================
// Merge / rebuild
// ===========================================================================
namespace {

struct TargetGroup {
    QString name;
    DDCChannelGroupHandle handle = nullptr;
    QSet<QString> channelNames;
};

struct TargetFile {
    DDCFileHandle file = nullptr;
    QVector<TargetGroup> groups;

    TargetGroup* find(const QString& name)
    {
        for (TargetGroup& g : groups)
            if (g.name == name)
                return &g;
        return nullptr;
    }
};

bool appendGroupTo(const Document& src, int groupIndex, TargetFile& target, int mode,
                   QString* error, QStringList* log)
{
    const GroupInfo& srcGroup = src.groups().at(groupIndex);

    TargetGroup* existing = (mode == 1) ? target.find(srcGroup.name) : nullptr;
    TargetGroup* dest = existing;
    QString groupName = srcGroup.name;

    if (!dest) {
        QSet<QString> names;
        for (const TargetGroup& g : target.groups)
            names.insert(g.name);
        groupName = uniqueName(srcGroup.name, names);

        DDCChannelGroupHandle handle = nullptr;
        int rc = DDC_AddChannelGroup(target.file, groupName.toUtf8().constData(),
                                     srcGroup.description.toUtf8().constData(), &handle);
        if (rc < 0) {
            *error = QStringLiteral("创建分组 \"%1\" 失败：%2").arg(groupName, lastErrorText(rc));
            return false;
        }
        TargetGroup tg;
        tg.name = groupName;
        tg.handle = handle;
        target.groups.append(tg);
        dest = &target.groups.last();

        const Handles srcH{ nullptr, srcGroup.handle, nullptr };
        const Handles dstH{ nullptr, handle, nullptr };
        copyProperties(ObjKind::Group, srcH, dstH, log);
    } else if (log) {
        log->append(QStringLiteral("分组 \"%1\" 已存在，通道被追加到该分组。").arg(groupName));
    }

    for (const ChannelInfo& srcChannel : srcGroup.channels) {
        const QString channelName = uniqueName(srcChannel.name, dest->channelNames);

        DDCChannelHandle channel = nullptr;
        int rc = DDC_AddChannel(dest->handle, srcChannel.type, channelName.toUtf8().constData(),
                                srcChannel.description.toUtf8().constData(),
                                srcChannel.unit.toUtf8().constData(), &channel);
        if (rc < 0) {
            *error = QStringLiteral("创建通道 \"%1\" 失败：%2").arg(channelName, lastErrorText(rc));
            return false;
        }
        dest->channelNames.insert(channelName);

        const Handles srcH{ nullptr, nullptr, srcChannel.handle };
        const Handles dstH{ nullptr, nullptr, channel };
        copyProperties(ObjKind::Channel, srcH, dstH, log);

        if (!copyChannelValues(srcChannel.handle, channel, srcChannel.type, srcChannel.count, error))
            return false;

        if (log && channelName != srcChannel.name)
            log->append(QStringLiteral("通道 \"%1\" 重命名为 \"%2\" 以避免冲突。")
                            .arg(srcChannel.name, channelName));
    }
    return true;
}

} // namespace

bool mergeFiles(const QString& pathA, const QString& pathB, const QString& outPath,
                int mode, const QStringList& groupsA, const QStringList& groupsB,
                QString* error, QStringList* log)
{
    Document a;
    Document b;
    if (!a.open(pathA, true, error))
        return false;
    if (!b.open(pathB, true, error))
        return false;

    if (QFile::exists(outPath)
        && QFileInfo(outPath).absoluteFilePath() != QFileInfo(pathA).absoluteFilePath()
        && QFileInfo(outPath).absoluteFilePath() != QFileInfo(pathB).absoluteFilePath()) {
        QFile::remove(outPath);
    }

    const QString type = outPath.endsWith(QLatin1String(".tdm"), Qt::CaseInsensitive)
                             ? QStringLiteral("TDM")
                             : QStringLiteral("TDMS");

    Document out;
    if (!out.create(outPath, type, error))
        return false;

    TargetFile target;
    target.file = static_cast<DDCFileHandle>(out.rawHandle(ObjKind::File, 0, 0));

    {
        const Handles srcH{ static_cast<DDCFileHandle>(a.rawHandle(ObjKind::File, 0, 0)), nullptr, nullptr };
        const Handles dstH{ target.file, nullptr, nullptr };
        copyProperties(ObjKind::File, srcH, dstH, log);
    }

    auto include = [](const QStringList& filter, const QString& name) {
        return filter.isEmpty() || filter.contains(name);
    };

    for (int g = 0; g < a.groups().size(); ++g) {
        if (!include(groupsA, a.groups().at(g).name))
            continue;
        if (!appendGroupTo(a, g, target, mode, error, log))
            return false;
    }
    for (int g = 0; g < b.groups().size(); ++g) {
        if (!include(groupsB, b.groups().at(g).name))
            continue;
        if (!appendGroupTo(b, g, target, mode, error, log))
            return false;
    }

    if (!out.save(error))
        return false;
    out.close();

    if (log)
        log->prepend(QStringLiteral("已合并 %1 个分组到 %2")
                         .arg(target.groups.size())
                         .arg(QFileInfo(outPath).fileName()));
    return true;
}

bool rebuildWithout(const QString& srcPath, const QString& dstPath,
                    ObjKind dropKind, int dropGroup, int dropChannel, QString* error)
{
    Document src;
    if (!src.open(srcPath, true, error))
        return false;

    const QString type = src.fileType();
    Document out;
    if (!out.create(dstPath, type, error))
        return false;

    TargetFile target;
    target.file = static_cast<DDCFileHandle>(out.rawHandle(ObjKind::File, 0, 0));

    {
        const Handles srcH{ static_cast<DDCFileHandle>(src.rawHandle(ObjKind::File, 0, 0)), nullptr, nullptr };
        const Handles dstH{ target.file, nullptr, nullptr };
        copyProperties(ObjKind::File, srcH, dstH, nullptr);
    }

    for (int g = 0; g < src.groups().size(); ++g) {
        if (dropKind == ObjKind::Group && g == dropGroup)
            continue;
        const GroupInfo& group = src.groups().at(g);

        DDCChannelGroupHandle handle = nullptr;
        int rc = DDC_AddChannelGroup(target.file, group.name.toUtf8().constData(),
                                     group.description.toUtf8().constData(), &handle);
        if (rc < 0) {
            *error = QStringLiteral("重建分组 \"%1\" 失败：%2").arg(group.name, lastErrorText(rc));
            return false;
        }
        {
            const Handles srcH{ nullptr, group.handle, nullptr };
            const Handles dstH{ nullptr, handle, nullptr };
            copyProperties(ObjKind::Group, srcH, dstH, nullptr);
        }

        for (int c = 0; c < group.channels.size(); ++c) {
            if (dropKind == ObjKind::Channel && g == dropGroup && c == dropChannel)
                continue;
            const ChannelInfo& ch = group.channels.at(c);

            DDCChannelHandle channel = nullptr;
            rc = DDC_AddChannel(handle, ch.type, ch.name.toUtf8().constData(),
                                ch.description.toUtf8().constData(),
                                ch.unit.toUtf8().constData(), &channel);
            if (rc < 0) {
                *error = QStringLiteral("重建通道 \"%1\" 失败：%2").arg(ch.name, lastErrorText(rc));
                return false;
            }
            {
                const Handles srcH{ nullptr, nullptr, ch.handle };
                const Handles dstH{ nullptr, nullptr, channel };
                copyProperties(ObjKind::Channel, srcH, dstH, nullptr);
            }
            if (!copyChannelValues(ch.handle, channel, ch.type, ch.count, error))
                return false;
        }
    }

    if (!out.save(error))
        return false;
    out.close();
    return true;
}

} // namespace tdms
