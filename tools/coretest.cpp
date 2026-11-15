// ---------------------------------------------------------------------------
// coretest.cpp - console regression test for the core layer (no GUI).
// Run from the build directory: tdmscore_test
// ---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QTime>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringConverter>
#include <QTextStream>
#include <cmath>
#include <cstdio>

#include "core/Settings.h"
#include "core/TableData.h"
#include "core/TdmsCore.h"

using namespace tdms;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const QString& message)
{
    ++g_checks;
    if (!condition)
        ++g_failures;
    printf("%s  %s\n", condition ? "PASS" : "FAIL", message.toUtf8().constData());
}

static QVector<double> sineWave(int n, double amplitude, double step, double offset = 0.0)
{
    QVector<double> values(n);
    for (int i = 0; i < n; ++i)
        values[i] = offset + amplitude * std::sin(i * step);
    return values;
}

static QVector<double> ramp(int n, double start, double step)
{
    QVector<double> values(n);
    for (int i = 0; i < n; ++i)
        values[i] = start + i * step;
    return values;
}

static QDateTime demoStart()
{
    return QDateTime(QDate(2026, 11, 11), QTime(9, 30, 0));
}

static bool writeDemoTdm(const QString& path, QString* error)
{
    Document doc;
    if (!doc.create(path, QStringLiteral("TDM"), error))
        return false;

    const QDateTime start = demoStart();

    int group = -1;
    if (!doc.addGroup(QStringLiteral("Simulated Data"), QStringLiteral("正弦与噪声"), &group, error))
        return false;
    if (!doc.addChannel(group, QStringLiteral("Sine"), QStringLiteral("正弦信号"),
                        QStringLiteral("V"), DDC_Double, sineWave(2000, 5.0, 0.01), error))
        return false;
    if (!doc.addChannel(group, QStringLiteral("Noise"), QStringLiteral("随机噪声"),
                        QStringLiteral("V"), DDC_Double, sineWave(2000, 1.0, 1.7), error))
        return false;
    if (!doc.addChannel(group, QStringLiteral("Counter"), QStringLiteral("计数"),
                        QStringLiteral("count"), DDC_Int16, ramp(2000, 0, 1), error))
        return false;

    // NI style waveform timing so the viewer can draw a time axis.
    doc.setChannelTiming(group, 0, start, 0.01, 0.0, error);
    doc.setChannelTiming(group, 1, start, 0.01, 0.0, error);
    doc.setChannelTiming(group, 2, start, 0.005, 0.0, error);

    int group2 = -1;
    if (!doc.addGroup(QStringLiteral("Temperature"), QStringLiteral("温度湿度"), &group2, error))
        return false;
    if (!doc.addChannel(group2, QStringLiteral("Temp"), QStringLiteral("温度"),
                        QStringLiteral("degC"), DDC_Double, ramp(500, 20.0, 0.01), error))
        return false;
    if (!doc.addChannel(group2, QStringLiteral("Humidity"), QStringLiteral("湿度"),
                        QStringLiteral("%"), DDC_Double, ramp(500, 45.0, 0.02), error))
        return false;

    // The second group starts one minute later: relative time keeps both
    // groups aligned on a common origin.
    const QDateTime lateStart = start.addSecs(60);
    doc.setChannelTiming(group2, 0, lateStart, 0.2, 0.0, error);
    doc.setChannelTiming(group2, 1, lateStart, 0.2, 0.0, error);

    doc.createProperty(ObjKind::File, 0, 0, QStringLiteral("operator"), DDC_String,
                       QStringLiteral("张三"), error);
    doc.createProperty(ObjKind::File, 0, 0, QStringLiteral("runId"), DDC_Int32, 1024, error);
    doc.createProperty(ObjKind::Group, group, -1, QStringLiteral("site"), DDC_String,
                       QStringLiteral("Lab-1"), error);
    doc.createProperty(ObjKind::Group, group, -1, QStringLiteral("rate"), DDC_Double, 100.0, error);
    doc.createProperty(ObjKind::Channel, group, 0, QStringLiteral("sensor"), DDC_String,
                       QStringLiteral("ACC-01"), error);
    doc.createProperty(ObjKind::Channel, group, 0, QStringLiteral("gain"), DDC_Double, 1.25, error);

    return doc.save(error);
}

static bool writeDemoTdms(const QString& path, QString* error)
{
    Document doc;
    if (!doc.create(path, QStringLiteral("TDMS"), error))
        return false;

    int group = -1;
    if (!doc.addGroup(QStringLiteral("Simulated Data"), QStringLiteral("来自 TDMS"), &group, error))
        return false;
    if (!doc.addChannel(group, QStringLiteral("Sine2"), QStringLiteral("第二段正弦"),
                        QStringLiteral("V"), DDC_Double, sineWave(1500, 3.0, 0.02, 1.0), error))
        return false;
    if (!doc.addChannel(group, QStringLiteral("Pulse"), QStringLiteral("脉冲"),
                        QStringLiteral("V"), DDC_Float, sineWave(1500, 2.0, 0.05, 2.0), error))
        return false;

    int group2 = -1;
    if (!doc.addGroup(QStringLiteral("Status"), QStringLiteral("状态"), &group2, error))
        return false;
    if (!doc.addChannel(group2, QStringLiteral("Flags"), QStringLiteral("标志位"),
                        QString(), DDC_UInt8, ramp(300, 0, 1), error))
        return false;

    const QDateTime start = demoStart().addSecs(5);
    doc.setChannelTiming(group, 0, start, 0.001, 0.0, error);
    doc.setChannelTiming(group, 1, start, 0.002, 0.0, error);
    doc.setChannelTiming(group2, 0, start.addSecs(30), 0.1, 0.0, error);

    return doc.save(error);
}

static void testCreateAndRead(const QString& tdm, const QString& tdms)
{
    printf("\n--- create & read ---\n");
    QString error;
    check(writeDemoTdm(tdm, &error), QStringLiteral("创建 TDM 文件 %1").arg(error));
    check(writeDemoTdms(tdms, &error), QStringLiteral("创建 TDMS 文件 %1").arg(error));

    Document doc;
    check(doc.open(tdm, true, &error), QStringLiteral("打开 TDM %1").arg(error));
    check(doc.groups().size() == 2, QStringLiteral("分组数量 = 2（实际 %1）").arg(doc.groups().size()));
    check(doc.groups().at(0).channels.size() == 3,
          QStringLiteral("首个分组通道数 = 3（实际 %1）").arg(doc.groups().at(0).channels.size()));

    const ChannelInfo& timed = doc.groups().at(0).channels.at(0);
    check(timed.hasTimeInfo(), QStringLiteral("通道带有 wf_increment 时间信息"));
    check(std::fabs(timed.increment - 0.01) < 1e-9,
          QStringLiteral("采样间隔 = 0.01 s（实际 %1）").arg(timed.increment));
    check(timed.startTime.isValid()
              && timed.startTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                  == QStringLiteral("2026-11-11 09:30:00"),
          QStringLiteral("起始时刻 = %1")
              .arg(timed.startTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    check(doc.groups().at(1).channels.at(0).startTime.secsTo(timed.startTime) == -60,
          QStringLiteral("第二个分组比首个分组晚 60 s"));

    const QVector<PropEntry> fileProps = doc.properties(ObjKind::File, -1, -1);
    QString operatorValue;
    for (const PropEntry& p : fileProps) {
        if (p.name == QLatin1String("operator"))
            operatorValue = p.value.toString();
    }
    check(operatorValue == QStringLiteral("张三"),
          QStringLiteral("文件属性 operator = %1").arg(operatorValue));

    const QVector<PropEntry> channelProps = doc.properties(ObjKind::Channel, 0, 0);
    bool foundGain = false;
    for (const PropEntry& p : channelProps) {
        if (p.name == QLatin1String("gain") && qFuzzyCompare(p.value.toDouble() + 1.0, 2.25))
            foundGain = true;
    }
    check(foundGain, QStringLiteral("通道属性 gain = 1.25"));

    QVector<double> values;
    QStringList labels;
    check(doc.readChannel(0, 0, &values, &labels, &error),
          QStringLiteral("读取通道数据 %1").arg(error));
    check(values.size() == 2000, QStringLiteral("正弦通道点数 = 2000（实际 %1）").arg(values.size()));
    check(!values.isEmpty() && std::fabs(values.at(0)) < 1e-9,
          QStringLiteral("首个采样值约为 0"));

    QVector<double> intValues;
    check(doc.readChannel(0, 2, &intValues, &labels, &error), QStringLiteral("读取 Int16 通道"));
    check(intValues.size() == 2000 && std::fabs(intValues.at(1999) - 1999.0) < 0.001,
          QStringLiteral("Int16 通道末值 = 1999"));
}

static void testProperties(const QString& tdm)
{
    printf("\n--- property edit & persist ---\n");
    QString error;
    Document doc;
    check(doc.open(tdm, false, &error), QStringLiteral("以读写方式打开 %1").arg(error));

    check(doc.setProperty(ObjKind::File, 0, 0, QStringLiteral("title"), DDC_String,
                          QStringLiteral("示例数据文件"), &error),
          QStringLiteral("修改文件 title %1").arg(error));
    check(doc.setProperty(ObjKind::Channel, 0, 0, QStringLiteral("sensor"), DDC_String,
                          QStringLiteral("ACC-99"), &error),
          QStringLiteral("修改通道 sensor %1").arg(error));
    check(doc.createProperty(ObjKind::Group, 0, -1, QStringLiteral("operatorId"), DDC_Int16, 7,
                             &error),
          QStringLiteral("新增分组属性 operatorId %1").arg(error));
    check(doc.setProperty(ObjKind::Channel, 0, 0, QStringLiteral("name"), DDC_String,
                          QStringLiteral("SineRenamed"), &error),
          QStringLiteral("通道重命名 %1").arg(error));
    check(doc.save(&error), QStringLiteral("保存 %1").arg(error));
    doc.close();

    Document verify;
    check(verify.open(tdm, true, &error), QStringLiteral("重新打开 %1").arg(error));
    check(verify.groups().at(0).channels.at(0).name == QStringLiteral("SineRenamed"),
          QStringLiteral("重命名已持久化（%1）").arg(verify.groups().at(0).channels.at(0).name));
    QString sensor;
    for (const PropEntry& p : verify.properties(ObjKind::Channel, 0, 0)) {
        if (p.name == QLatin1String("sensor"))
            sensor = p.value.toString();
    }
    check(sensor == QStringLiteral("ACC-99"), QStringLiteral("通道属性已持久化（%1）").arg(sensor));
}

static void testExport(const QString& tdm)
{
    printf("\n--- export txt / csv ---\n");
    QString error;
    Document doc;
    check(doc.open(tdm, true, &error), QStringLiteral("打开用于导出的文件"));

    ExportSettings csv;
    csv.csv = true;
    csv.includeIndex = true;
    csv.includeMeta = true;
    check(doc.exportChannel(0, 0, QStringLiteral("samples/export_channel.csv"), csv, &error),
          QStringLiteral("导出单通道 CSV %1").arg(error));

    ExportSettings txt;
    txt.csv = false;
    check(doc.exportGroup(0, QStringLiteral("samples/export_group.txt"), txt, &error),
          QStringLiteral("导出分组 TXT %1").arg(error));

    QStringList files;
    check(doc.exportAllToDir(QStringLiteral("samples/export_all"), csv, &files, &error),
          QStringLiteral("导出全部 %1").arg(error));
    check(files.size() == 2, QStringLiteral("整文件导出生成 2 个文件（实际 %1）").arg(files.size()));

    QFile file(QStringLiteral("samples/export_channel.csv"));
    check(file.open(QIODevice::ReadOnly), QStringLiteral("CSV 文件可读"));
    const QByteArray data = file.readAll();
    check(data.startsWith("\xEF\xBB\xBF"), QStringLiteral("CSV 带 UTF-8 BOM（便于 Excel 打开）"));
    check(data.contains("Index"), QStringLiteral("CSV 含表头"));
    check(data.count('\n') > 2000, QStringLiteral("CSV 行数覆盖 2000 点数据"));
}

static void testMerge(const QString& tdm, const QString& tdms)
{
    printf("\n--- merge ---\n");
    QString error;
    QStringList log;

    const QString outAppend = QStringLiteral("samples/merged_append.tdms");
    check(mergeFiles(tdm, tdms, outAppend, 0, {}, {}, &error, &log),
          QStringLiteral("合并（追加分组模式）%1").arg(error));
    printf("%s", log.join(QStringLiteral("\n")).toUtf8().constData());
    printf("\n");

    Document merged;
    check(merged.open(outAppend, true, &error), QStringLiteral("打开合并结果 %1").arg(error));
    check(merged.groups().size() == 4,
          QStringLiteral("合并后分组数 = 4（实际 %1）").arg(merged.groups().size()));

    int totalChannels = 0;
    for (const GroupInfo& g : merged.groups())
        totalChannels += g.channels.size();
    check(totalChannels == 8, QStringLiteral("合并后通道数 = 8（实际 %1）").arg(totalChannels));

    QVector<double> values;
    QStringList labels;
    check(merged.readChannel(0, 0, &values, &labels, &error), QStringLiteral("读取合并后通道"));
    check(values.size() == 2000 && std::fabs(values.at(100)) < 10.0,
          QStringLiteral("合并后数据完整（%1 点）").arg(values.size()));

    const QString outSame = QStringLiteral("samples/merged_same_group.tdms");
    log.clear();
    check(mergeFiles(tdm, tdms, outSame, 1, {}, {}, &error, &log),
          QStringLiteral("合并（同名分组合并）%1").arg(error));

    Document merged2;
    check(merged2.open(outSame, true, &error), QStringLiteral("打开同名合并结果"));
    check(merged2.groups().size() == 3,
          QStringLiteral("同名合并后分组数 = 3（实际 %1）").arg(merged2.groups().size()));
    int simChannels = 0;
    for (const GroupInfo& g : merged2.groups()) {
        if (g.name == QStringLiteral("Simulated Data"))
            simChannels = g.channels.size();
    }
    check(simChannels == 5, QStringLiteral("同名分组内通道数 = 5（实际 %1）").arg(simChannels));
}

static void testDelete(const QString& tdms)
{
    printf("\n--- delete channel (TDMS requires rebuild) ---\n");
    QString error;

    const QString copy = QStringLiteral("samples/delete_test.tdms");
    QFile::remove(copy);
    check(QFile::copy(tdms, copy), QStringLiteral("复制测试文件"));

    Document doc;
    check(doc.open(copy, false, &error), QStringLiteral("打开测试文件 %1").arg(error));
    const int before = doc.groups().at(0).channels.size();

    check(doc.removeChannel(0, 0, &error), QStringLiteral("删除通道 %1").arg(error));
    check(doc.groups().at(0).channels.size() == before - 1,
          QStringLiteral("内存结构通道数 %1 -> %2").arg(before).arg(doc.groups().at(0).channels.size()));


    check(doc.save(&error), QStringLiteral("保存重建后的文件"));
    doc.close();


    Document verify;
    check(verify.open(copy, true, &error), QStringLiteral("重新打开验证 %1").arg(error));
    check(verify.groups().at(0).channels.size() == before - 1,
          QStringLiteral("磁盘文件通道数已减少（%1）").arg(verify.groups().at(0).channels.size()));
    check(verify.groups().size() >= 2, QStringLiteral("其他分组未被破坏"));
}

static void testSaveAs(const QString& tdms)
{
    printf("\n--- save as ---\n");
    QString error;
    const QString target = QStringLiteral("samples/saveas_copy.tdms");
    QFile::remove(target);

    Document doc;
    check(doc.open(tdms, false, &error), QStringLiteral("打开源文件 %1").arg(error));
    const int groups = doc.groups().size();
    check(doc.saveAs(target, &error), QStringLiteral("另存为 %1").arg(error));
    check(doc.path() == QFileInfo(target).absoluteFilePath(),
          QStringLiteral("文档路径已切换到新文件（%1）").arg(doc.path()));
    doc.close();

    Document verify;
    check(verify.open(target, true, &error), QStringLiteral("打开另存文件 %1").arg(error));
    check(verify.groups().size() == groups,
          QStringLiteral("另存文件分组数一致（%1）").arg(verify.groups().size()));
    check(QFile::exists(target + QStringLiteral("_index")),
          QStringLiteral("TDMS 索引伴随文件已复制"));
}

static void testChannelTiming(const QString& tdms)
{
    printf("\n--- channel timing ---\n");
    QString error;
    Document doc;
    check(doc.open(tdms, false, &error), QStringLiteral("打开文件 %1").arg(error));

    const QDateTime start(QDate(2026, 12, 24), QTime(18, 0, 0, 250));
    check(doc.setChannelTiming(0, 0, start, 0.0025, 1.5, &error),
          QStringLiteral("写入 wf_start_time / wf_increment %1").arg(error));
    check(doc.save(&error), QStringLiteral("保存时间信息 %1").arg(error));
    doc.close();

    Document verify;
    check(verify.open(tdms, true, &error), QStringLiteral("重新打开 %1").arg(error));
    const ChannelInfo& ch = verify.groups().at(0).channels.at(0);
    check(std::fabs(ch.increment - 0.0025) < 1e-9,
          QStringLiteral("采样间隔已持久化（%1）").arg(ch.increment));
    check(std::fabs(ch.startOffset - 1.5) < 1e-9,
          QStringLiteral("起始偏移已持久化（%1）").arg(ch.startOffset));
    check(ch.startTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
              == QStringLiteral("2026-12-24 18:00:00.250"),
          QStringLiteral("起始时刻已持久化（%1）")
              .arg(ch.startTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))));
    check(verify.fileDateTime().isValid(),
          QStringLiteral("文件 datetime 属性可读取（%1）")
              .arg(verify.fileDateTime().toString(Qt::ISODate)));
}

static void testImportParsing()
{
    printf("\n--- external data parsing ---\n");
    const QString csv = QStringLiteral("samples/import_demo.csv");
    {
        QFile file(csv);
        file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out.setGenerateByteOrderMark(true);
        out << "Time [s],Voltage [V],Note\n";
        for (int i = 0; i < 100; ++i) {
            out << QString::number(i * 0.01, 'f', 3) << ","
                << QString::number(std::sin(i * 0.1) * 12.0, 'f', 4) << ",";
            if (i % 25 == 0)
                out << "标记" << i;
            out << "\n";
        }
    }

    ImportOptions options;
    DataTable table;
    QString error;
    check(parseDataFile(csv, options, &table, &error),
          QStringLiteral("解析 CSV %1").arg(error));
    check(table.columns.size() == 3, QStringLiteral("列数 = 3（实际 %1）").arg(table.columns.size()));
    check(table.rowCount == 100, QStringLiteral("行数 = 100（实际 %1）").arg(table.rowCount));
    check(table.columns.at(0).name == QStringLiteral("Time") && table.columns.at(0).unit == QStringLiteral("s"),
          QStringLiteral("表头单位解析：%1 [%2]").arg(table.columns.at(0).name, table.columns.at(0).unit));
    check(table.columns.at(0).numeric && table.columns.at(1).numeric,
          QStringLiteral("前两列识别为数值列"));
    check(!table.columns.at(2).numeric, QStringLiteral("第三列识别为文本列"));

    const QString txt = QStringLiteral("samples/import_demo.txt");
    {
        QFile file(txt);
        file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        QTextStream out(&file);
        out << "# comment line\n";
        out << "idx\tsignal\n";
        for (int i = 0; i < 50; ++i)
            out << i << "\t" << (i * 0.5) << "\n";
    }
    DataTable table2;
    check(parseDataFile(txt, options, &table2, &error),
          QStringLiteral("解析制表符 TXT %1").arg(error));
    check(table2.columns.size() == 2 && table2.rowCount == 50,
          QStringLiteral("TXT 解析结果 %1 列 / %2 行").arg(table2.columns.size()).arg(table2.rowCount));
}


// DDC 17.0 的 DDC_OpenFileEx / DDC_CreateFile 只认相对路径：传绝对路径时它会把
// 绝对路径再次拼到当前工作目录后面，返回 -6211。TdmsCore 内部用“切目录 + 传文件名”
// 规避，这里把三种入口（打开、创建、保存）都用绝对路径跑一遍，锁住这个回归点。
static void testAbsolutePaths(const QString& tdm, const QString& tdms)
{
    printf("\n--- absolute paths ---\n");
    const QString cwdBefore = QDir::currentPath();

    const QString absTdm = QFileInfo(tdm).absoluteFilePath();
    check(QDir::isAbsolutePath(absTdm), QStringLiteral("测试路径为绝对路径（%1）").arg(absTdm));

    Document opened;
    QString error;
    check(opened.open(absTdm, false, &error),
          QStringLiteral("以绝对路径打开 TDM（%1）").arg(error));
    check(opened.groups().size() == 2,
          QStringLiteral("绝对路径打开后读到 %1 个分组").arg(opened.groups().size()));
    check(QDir::isAbsolutePath(opened.path()),
          QStringLiteral("文档路径以绝对形式保存"));

    check(opened.save(&error), QStringLiteral("绝对路径打开的文档可保存（%1）").arg(error));
    check(QDir::currentPath() == cwdBefore, QStringLiteral("打开/保存后工作目录未被改动"));

    Document openedTdms;
    check(openedTdms.open(QFileInfo(tdms).absoluteFilePath(), true, &error),
          QStringLiteral("以绝对路径打开 TDMS（%1）").arg(error));

    // Create + save through an absolute path (this is what File > New produces).
    const QString absNew = QFileInfo(QStringLiteral("samples/abs_create.tdms")).absoluteFilePath();
    QFile::remove(absNew);
    QFile::remove(absNew + QStringLiteral("_index"));
    Document created;
    check(created.create(absNew, QStringLiteral("TDMS"), &error),
          QStringLiteral("以绝对路径创建 TDMS（%1）").arg(error));
    int group = -1;
    check(created.addGroup(QStringLiteral("AbsGroup"), QString(), &group, &error),
          QStringLiteral("绝对路径文档添加分组（%1）").arg(error));
    check(created.addChannel(group, QStringLiteral("AbsCh"), QString(), QStringLiteral("V"),
                             DDC_Double, ramp(100, 0.0, 0.1), &error),
          QStringLiteral("绝对路径文档添加通道（%1）").arg(error));
    check(created.save(&error), QStringLiteral("绝对路径文档保存（%1）").arg(error));
    check(QFile::exists(absNew), QStringLiteral("绝对路径文件确实写到了目标位置"));
    check(QDir::currentPath() == cwdBefore, QStringLiteral("创建/保存后工作目录未被改动"));

    Document verify;
    check(verify.open(absNew, true, &error),
          QStringLiteral("回读绝对路径创建的文件（%1）").arg(error));
    check(verify.groups().size() == 1 && verify.groups().at(0).channels.size() == 1,
          QStringLiteral("回读结果 1 分组 / 1 通道"));

    // 中文路径与中文文件名：DDC 的路径参数按本地 ANSI 编码解析，传 UTF-8 会
    // 返回 -6209（文件不存在），因此 TdmsCore 里用的是 toLocal8Bit。
    const QString cnDir = QFileInfo(QStringLiteral("samples")).absoluteFilePath()
                          + QStringLiteral("/中文 目录");
    check(QDir().mkpath(cnDir), QStringLiteral("创建中文目录"));
    const QString cnPath = cnDir + QStringLiteral("/测试 数据.tdms");
    QFile::remove(cnPath);
    QFile::remove(cnPath + QStringLiteral("_index"));

    Document cnDoc;
    check(cnDoc.create(cnPath, QStringLiteral("TDMS"), &error),
          QStringLiteral("以中文路径创建 TDMS（%1）").arg(error));
    int cnGroup = -1;
    check(cnDoc.addGroup(QStringLiteral("中文分组"), QStringLiteral("测试"), &cnGroup, &error),
          QStringLiteral("中文路径文档添加分组（%1）").arg(error));
    check(cnDoc.addChannel(cnGroup, QStringLiteral("通道一"), QStringLiteral("说明"),
                           QStringLiteral("mV"), DDC_Double, ramp(64, 0.0, 0.5), &error),
          QStringLiteral("中文路径文档添加通道（%1）").arg(error));
    check(cnDoc.save(&error), QStringLiteral("中文路径文档保存（%1）").arg(error));
    check(QFile::exists(cnPath), QStringLiteral("中文路径文件确实存在"));

    Document cnVerify;
    check(cnVerify.open(cnPath, true, &error),
          QStringLiteral("以中文绝对路径重新打开（%1）").arg(error));
    check(cnVerify.groups().size() == 1
              && cnVerify.groups().at(0).channels.at(0).name == QStringLiteral("通道一"),
          QStringLiteral("中文路径回读通道名正确"));
    check(QDir::currentPath() == cwdBefore, QStringLiteral("中文路径操作后工作目录未被改动"));
}

static void testSettingsIni()
{
    printf("\n--- settings ini ---\n");
    const QString ini = QStringLiteral("samples/settings_test.ini");
    QFile::remove(ini);
    qputenv("TDMSVIEWER_CONFIG", ini.toLocal8Bit());

    Settings& settings = Settings::instance();
    check(Settings::configFilePath() == QFileInfo(ini).absoluteFilePath(),
          QStringLiteral("配置文件路径取自 TDMSVIEWER_CONFIG（%1）").arg(Settings::configFilePath()));
    check(!Settings::isPortableConfig(), QStringLiteral("环境变量模式下不报告为便携模式"));

    AppSettings values = settings.values();
    values.theme = ThemeMode::Dark;
    values.showAxisTop = false;
    values.showAxisRight = false;
    values.topAxisMode = TopAxisMode::Absolute;
    values.show3DView = false;
    values.trace3DStyle = Trace3DStyle::Dots;
    values.lineWidth = 12.0;   // out of range on purpose -> normalised to 6.0
    settings.setValues(values);

    check(QFile::exists(ini), QStringLiteral("首选项写入 INI 文件"));
    QSettings store(ini, QSettings::IniFormat);
    check(store.value(QStringLiteral("appearance/theme")).toInt() == 2,
          QStringLiteral("appearance/theme = 2（深色）"));
    check(store.value(QStringLiteral("axes/top")).toBool() == false,
          QStringLiteral("axes/top = false"));
    check(store.value(QStringLiteral("axes/right")).toBool() == false,
          QStringLiteral("axes/right = false"));
    check(store.value(QStringLiteral("axes/topMode")).toInt() == 2,
          QStringLiteral("axes/topMode = 2（绝对时间）"));
    check(store.value(QStringLiteral("plot3d/enabled")).toBool() == false,
          QStringLiteral("plot3d/enabled = false"));
    check(store.value(QStringLiteral("plot3d/style")).toInt() == 2,
          QStringLiteral("plot3d/style = 2（散点）"));
    check(qFuzzyCompare(settings.values().lineWidth, 6.0),
          QStringLiteral("线宽被归一化到 6.0（实际 %1）").arg(settings.values().lineWidth));

    settings.reload();
    check(settings.values().showAxisTop == false && settings.values().theme == ThemeMode::Dark,
          QStringLiteral("重新加载后设置保持"));

    settings.resetToDefaults();
    check(settings.values().showAxisTop && settings.values().showAxisRight
              && settings.values().show3DView,
          QStringLiteral("恢复默认后四条轴与三维页重新启用"));

    qunsetenv("TDMSVIEWER_CONFIG");
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QDir().mkpath(QStringLiteral("samples"));
    QDir().mkpath(QStringLiteral("samples/export_all"));

    const QString tdm = QStringLiteral("samples/demo.tdm");
    const QString tdms = QStringLiteral("samples/demo.tdms");
    QFile::remove(tdm);
    QFile::remove(tdms);

    testCreateAndRead(tdm, tdms);
    testProperties(tdm);
    testExport(tdm);
    testMerge(tdm, tdms);
    testDelete(tdms);
    testChannelTiming(tdms);
    testSaveAs(tdms);
    testImportParsing();
    testAbsolutePaths(tdm, tdms);
    testSettingsIni();

    printf("\n==== %d checks, %d failures ====\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
