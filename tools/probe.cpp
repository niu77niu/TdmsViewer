// ---------------------------------------------------------------------------
// probe.cpp - verifies the real behaviour of the NI TDM C DLL.
// Console tool, ASCII output only (cmd codepage safety).
// ---------------------------------------------------------------------------
#include "nilibddc.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

static void show(const char* what, int err)
{
    if (err < 0)
        printf("  %-58s ERR  %d (%s)\n", what, err, DDC_GetLibraryErrorDescription(err));
    else
        printf("  %-58s OK\n", what);
}

static std::string strProp(int kind, void* handle, const char* prop, int* err)
{
    unsigned int len = 0;
    DDCFileHandle f = kind == 0 ? (DDCFileHandle)handle : nullptr;
    DDCChannelGroupHandle g = kind == 1 ? (DDCChannelGroupHandle)handle : nullptr;
    DDCChannelHandle c = kind == 2 ? (DDCChannelHandle)handle : nullptr;

    if (kind == 0) *err = DDC_GetFileStringPropertyLength(f, prop, &len);
    else if (kind == 1) *err = DDC_GetChannelGroupStringPropertyLength(g, prop, &len);
    else *err = DDC_GetChannelStringPropertyLength(c, prop, &len);
    if (*err < 0) return std::string();

    std::vector<char> buf(len + 1, 0);
    if (kind == 0) *err = DDC_GetFileProperty(f, prop, buf.data(), len + 1);
    else if (kind == 1) *err = DDC_GetChannelGroupProperty(g, prop, buf.data(), len + 1);
    else *err = DDC_GetChannelProperty(c, prop, buf.data(), len + 1);
    return std::string(buf.data());
}

static int writeSample(const char* path, const char* type)
{
    printf("\n=== create+write %s (%s) ===\n", path, type);
    remove(path);

    DDCFileHandle file = nullptr;
    int e = DDC_CreateFile(path, type, "Probe File", "probe description", "probe title", "probe author", &file);
    show("DDC_CreateFile", e);
    if (e < 0) return e;

    DDCChannelGroupHandle group = nullptr;
    e = DDC_AddChannelGroup(file, "Group A", "first group", &group);
    show("DDC_AddChannelGroup", e);

    DDCChannelHandle chDouble = nullptr, chInt16 = nullptr, chStr = nullptr, chTs = nullptr;
    e = DDC_AddChannel(group, DDC_Double, "Sine", "sine wave", "V", &chDouble);
    show("DDC_AddChannel (double)", e);
    e = DDC_AddChannel(group, DDC_Int16, "Counter", "counter", "count", &chInt16);
    show("DDC_AddChannel (int16)", e);
    e = DDC_AddChannel(group, DDC_String, "Labels", "string channel", "", &chStr);
    show("DDC_AddChannel (string)", e);
    e = DDC_AddChannel(group, DDC_Timestamp, "Time", "timestamp channel", "", &chTs);
    show("DDC_AddChannel (timestamp)", e);

    const int N = 200;
    std::vector<double> d(N);
    std::vector<short> s(N);
    for (int i = 0; i < N; ++i) { d[i] = sin(i * 0.05) * 10.0; s[i] = (short)i; }

    e = DDC_SetDataValuesDouble(chDouble, d.data(), N);
    show("DDC_SetDataValuesDouble", e);
    e = DDC_SetDataValuesInt16(chInt16, s.data(), N);
    show("DDC_SetDataValuesInt16", e);

    std::vector<const char*> labels(N);
    std::vector<std::string> storage(N);
    for (int i = 0; i < N; ++i) { storage[i] = "L" + std::to_string(i); labels[i] = storage[i].c_str(); }
    e = DDC_SetDataValuesString(chStr, labels.data(), N);
    show("DDC_SetDataValuesString", e);

    std::vector<unsigned int> y(N, 2026), mo(N, 11), da(N, 11), h(N), mi(N), se(N), wd(N);
    std::vector<double> ms(N, 0.0);
    for (int i = 0; i < N; ++i) { h[i] = 8; mi[i] = i / 60; se[i] = i % 60; ms[i] = (i % 1000) * 0.5; }
    e = DDC_SetDataValuesTimestampComponents(chTs, y.data(), mo.data(), da.data(), h.data(),
                                             mi.data(), se.data(), ms.data(), N);
    show("DDC_SetDataValuesTimestampComponents", e);

    // custom properties on three levels, several types
    show("DDC_CreateFilePropertyString", DDC_CreateFilePropertyString(file, "operator", "Zhang San"));
    show("DDC_CreateFilePropertyDouble", DDC_CreateFilePropertyDouble(file, "temperature", 21.5));
    show("DDC_CreateFilePropertyInt32", DDC_CreateFilePropertyInt32(file, "runId", 42));
    show("DDC_CreateChannelGroupPropertyString", DDC_CreateChannelGroupPropertyString(group, "site", "Lab-1"));
    show("DDC_CreateChannelGroupPropertyDouble", DDC_CreateChannelGroupPropertyDouble(group, "rate", 100.0));
    show("DDC_CreateChannelPropertyString", DDC_CreateChannelPropertyString(chDouble, "sensor", "ACC-01"));
    show("DDC_CreateChannelPropertyInt32", DDC_CreateChannelPropertyInt32(chDouble, "zeroOffset", 3));
    show("DDC_CreateChannelPropertyDouble", DDC_CreateChannelPropertyDouble(chDouble, "gain", 1.25));

    e = DDC_SaveFile(file);
    show("DDC_SaveFile", e);
    e = DDC_CloseFile(file);
    show("DDC_CloseFile", e);
    return 0;
}

static void readBack(const char* path, const char* type)
{
    printf("\n=== read back %s ===\n", path);
    DDCFileHandle file = nullptr;
    int e = DDC_OpenFileEx(path, type, 1, &file);
    show("DDC_OpenFileEx(readOnly=1)", e);
    if (e < 0) return;

    printf("  file title = \"%s\"\n", strProp(0, file, "title", &e).c_str());

    unsigned int nFileProps = 0;
    e = DDC_GetNumFileProperties(file, &nFileProps);
    printf("  file properties (%u):\n", nFileProps);
    for (unsigned int i = 0; i < nFileProps; ++i) {
        size_t len = 0;
        DDC_GetFilePropertyNameLengthFromIndex(file, i, &len);
        std::vector<char> nb(len + 2, 0);
        DDC_GetFilePropertyNameFromIndex(file, i, nb.data(), nb.size());
        DDCDataType dt = DDC_String;
        DDC_GetFilePropertyType(file, nb.data(), &dt);
        printf("    - %-20s type=%d\n", nb.data(), (int)dt);
    }

    unsigned int nGroups = 0;
    DDC_GetNumChannelGroups(file, &nGroups);
    printf("  groups: %u\n", nGroups);
    for (unsigned int g = 0; g < nGroups; ++g) {
        std::vector<DDCChannelGroupHandle> groups(nGroups);
        DDC_GetChannelGroups(file, groups.data(), nGroups);
        e = 0;
        printf("  group \"%s\"\n", strProp(1, groups[g], "name", &e).c_str());

        unsigned int nProps = 0;
        DDC_GetNumChannelGroupProperties(groups[g], &nProps);
        for (unsigned int i = 0; i < nProps; ++i) {
            size_t len = 0;
            DDC_GetChannelGroupPropertyNameLengthFromIndex(groups[g], i, &len);
            std::vector<char> nb(len + 2, 0);
            DDC_GetChannelGroupPropertyNameFromIndex(groups[g], i, nb.data(), nb.size());
            DDCDataType dt = DDC_String;
            DDC_GetChannelGroupPropertyType(groups[g], nb.data(), &dt);
            printf("    prop %-16s type=%d\n", nb.data(), (int)dt);
        }

        unsigned int nCh = 0;
        DDC_GetNumChannels(groups[g], &nCh);
        std::vector<DDCChannelHandle> chans(nCh);
        DDC_GetChannels(groups[g], chans.data(), nCh);
        for (unsigned int c = 0; c < nCh; ++c) {
            std::string name = strProp(2, chans[c], "name", &e);
            std::string unit = strProp(2, chans[c], "unit_string", &e);
            unsigned __int64 n = 0;
            DDC_GetNumDataValues(chans[c], &n);
            DDCDataType dt = DDC_String;
            DDC_GetDataType(chans[c], &dt);
            printf("    channel \"%s\" unit=\"%s\" type=%d values=%llu\n",
                   name.c_str(), unit.c_str(), (int)dt, (unsigned long long)n);

            unsigned int cProps = 0;
            DDC_GetNumChannelProperties(chans[c], &cProps);
            for (unsigned int i = 0; i < cProps; ++i) {
                size_t len = 0;
                DDC_GetChannelPropertyNameLengthFromIndex(chans[c], i, &len);
                std::vector<char> nb(len + 2, 0);
                DDC_GetChannelPropertyNameFromIndex(chans[c], i, nb.data(), nb.size());
                DDCDataType pt = DDC_String;
                DDC_GetChannelPropertyType(chans[c], nb.data(), &pt);
                printf("      prop %-14s type=%d\n", nb.data(), (int)pt);
            }
        }
    }
    DDC_CloseFile(file);
}

static void mutate(const char* path, const char* type)
{
    printf("\n=== mutate reopened %s (readOnly=0) ===\n", path);
    DDCFileHandle file = nullptr;
    int e = DDC_OpenFileEx(path, type, 0, &file);
    show("DDC_OpenFileEx(readOnly=0)", e);
    if (e < 0) return;

    unsigned int nGroups = 0;
    DDC_GetNumChannelGroups(file, &nGroups);
    std::vector<DDCChannelGroupHandle> groups(nGroups);
    DDC_GetChannelGroups(file, groups.data(), nGroups);
    unsigned int nCh = 0;
    DDC_GetNumChannels(groups[0], &nCh);
    std::vector<DDCChannelHandle> chans(nCh);
    DDC_GetChannels(groups[0], chans.data(), nCh);

    std::string first = strProp(2, chans[0], "name", &e);
    printf("  first channel: \"%s\"\n", first.c_str());

    // 1) existing custom property value change
    show("DDC_SetChannelPropertyDouble(gain=2.5)", DDC_SetChannelPropertyDouble(chans[0], "gain", 2.5));
    // 2) existing string property change on channel
    show("DDC_SetChannelPropertyString(sensor)", DDC_SetChannelPropertyString(chans[0], "sensor", "ACC-99"));
    // 3) new custom property on saved file
    show("DDC_CreateChannelPropertyString(newProp)",
         DDC_CreateChannelPropertyString(chans[0], "newProp", "abc"));
    // 4) rename channel through the reserved name property
    show("DDC_SetChannelPropertyString(name=Renamed)",
         DDC_SetChannelPropertyString(chans[0], "name", "Renamed"));
    // 5) append data to a saved channel
    double extra[3] = { 1.0, 2.0, 3.0 };
    show("DDC_AppendDataValuesDouble(+3)", DDC_AppendDataValuesDouble(chans[0], extra, 3));
    // 6) set description
    show("DDC_SetChannelPropertyString(description)",
         DDC_SetChannelPropertyString(chans[0], "description", "edited via viewer"));

    printf("  save after mutations:\n");
    show("DDC_SaveFile", DDC_SaveFile(file));
    DDC_CloseFile(file);

    // reopen and verify
    DDCFileHandle f2 = nullptr;
    e = DDC_OpenFileEx(path, type, 1, &f2);
    if (e >= 0) {
        DDC_GetNumChannelGroups(f2, &nGroups);
        std::vector<DDCChannelGroupHandle> g2(nGroups);
        DDC_GetChannelGroups(f2, g2.data(), nGroups);
        DDC_GetNumChannels(g2[0], &nCh);
        std::vector<DDCChannelHandle> c2(nCh);
        DDC_GetChannels(g2[0], c2.data(), nCh);
        unsigned __int64 n = 0;
        DDC_GetNumDataValues(c2[0], &n);
        printf("  after save: name=\"%s\" values=%llu\n",
               strProp(2, c2[0], "name", &e).c_str(), (unsigned long long)n);
        DDC_CloseFile(f2);
    }
}

static void removeTest(const char* path, const char* type)
{
    printf("\n=== remove channel / group on reopened %s ===\n", path);
    DDCFileHandle file = nullptr;
    int e = DDC_OpenFileEx(path, type, 0, &file);
    if (e < 0) { show("open", e); return; }

    unsigned int nGroups = 0;
    DDC_GetNumChannelGroups(file, &nGroups);
    std::vector<DDCChannelGroupHandle> groups(nGroups);
    DDC_GetChannelGroups(file, groups.data(), nGroups);
    unsigned int nCh = 0;
    DDC_GetNumChannels(groups[0], &nCh);
    std::vector<DDCChannelHandle> chans(nCh);
    DDC_GetChannels(groups[0], chans.data(), nCh);
    if (nCh >= 2) {
        show("DDC_RemoveChannel(last channel)", DDC_RemoveChannel(chans[nCh - 1]));
        show("DDC_SaveFile", DDC_SaveFile(file));
        unsigned int after = 0;
        DDC_GetNumChannels(groups[0], &after);
        printf("  channels before=%u after=%u\n", nCh, after);
    }
    DDC_CloseFile(file);
}

static void readValues()
{
    printf("\n=== value reading (string / timestamp / big channel) ===\n");
    DDCFileHandle file = nullptr;
    int e = DDC_OpenFileEx("probe_files/probe.tdms", "TDMS", 1, &file);
    if (e < 0) { show("open", e); return; }

    unsigned int nGroups = 0;
    DDC_GetNumChannelGroups(file, &nGroups);
    std::vector<DDCChannelGroupHandle> groups(nGroups);
    DDC_GetChannelGroups(file, groups.data(), nGroups);
    unsigned int nCh = 0;
    DDC_GetNumChannels(groups[0], &nCh);
    std::vector<DDCChannelHandle> chans(nCh);
    DDC_GetChannels(groups[0], chans.data(), nCh);

    // --- string channel with pre-allocated buffers ---
    DDCChannelHandle strCh = nullptr;
    for (unsigned int i = 0; i < nCh; ++i) {
        DDCDataType t;
        DDC_GetDataType(chans[i], &t);
        if (t == DDC_String) strCh = chans[i];
    }
    if (strCh) {
        const int kBuf = 256, kN = 3;
        std::vector<std::vector<char>> holder(kN, std::vector<char>(kBuf, 0));
        std::vector<char*> ptrs(kN);
        for (int i = 0; i < kN; ++i) ptrs[i] = holder[i].data();
        e = DDC_GetDataValuesString(strCh, 0, kN, ptrs.data());
        show("DDC_GetDataValuesString(preallocated 256B)", e);
        if (e >= 0)
            printf("  values: [%s] [%s] [%s]\n", holder[0].data(), holder[1].data(), holder[2].data());

        // variant: null pointers (library allocates?)
        std::vector<char*> nulls(kN, nullptr);
        int e2 = DDC_GetDataValuesString(strCh, 0, kN, nulls.data());
        printf("  null-buffer variant -> %d %s", e2, e2 < 0 ? DDC_GetLibraryErrorDescription(e2) : "");
        if (e2 >= 0) printf(" first=%p \"%s\"", (void*)nulls[0], nulls[0] ? nulls[0] : "");
        printf("\n");
        if (e2 >= 0 && nulls[0]) DDC_FreeMemory(nulls[0]);
    }

    // --- timestamp channel ---
    DDCChannelHandle tsCh = nullptr;
    for (unsigned int i = 0; i < nCh; ++i) {
        DDCDataType t;
        DDC_GetDataType(chans[i], &t);
        if (t == DDC_Timestamp) tsCh = chans[i];
    }
    if (tsCh) {
        const size_t kN = 2;
        unsigned int y[2], mo[2], da[2], h[2], mi[2], se[2], wd[2];
        double ms[2];
        e = DDC_GetDataValuesTimestampComponents(tsCh, 0, kN, y, mo, da, h, mi, se, ms, wd);
        show("DDC_GetDataValuesTimestampComponents", e);
        if (e >= 0)
            printf("  ts: %04u-%02u-%02u %02u:%02u:%02u.%03.0f  weekday=%u\n",
                   y[0], mo[0], da[0], h[0], mi[0], se[0], ms[0], wd[0]);
    }

    // --- big double channel: create 1,000,000 values and time the read ---
    DDC_CloseFile(file);

    remove("probe_files/big.tdms");
    DDCFileHandle big = nullptr;
    e = DDC_CreateFile("probe_files/big.tdms", "TDMS", "Big", "", "", "", &big);
    DDCChannelGroupHandle g = nullptr;
    DDC_AddChannelGroup(big, "G", "", &g);
    DDCChannelHandle c = nullptr;
    DDC_AddChannel(g, DDC_Double, "D", "", "V", &c);
    const size_t N = 1000000;
    std::vector<double> data(N);
    for (size_t i = 0; i < N; ++i) data[i] = sin(i * 0.001);
    e = DDC_AppendDataValuesDouble(c, data.data(), N);
    show("append 1,000,000 doubles", e);
    e = DDC_SaveFile(big);
    show("save big file", e);
    DDC_CloseFile(big);

    DDCFileHandle r = nullptr;
    e = DDC_OpenFileEx("probe_files/big.tdms", "TDMS", 1, &r);
    DDC_GetNumChannelGroups(r, &nGroups);
    std::vector<DDCChannelGroupHandle> g2(nGroups);
    DDC_GetChannelGroups(r, g2.data(), nGroups);
    DDC_GetNumChannels(g2[0], &nCh);
    std::vector<DDCChannelHandle> c2(nCh);
    DDC_GetChannels(g2[0], c2.data(), nCh);
    unsigned __int64 count = 0;
    DDC_GetNumDataValues(c2[0], &count);
    printf("  big channel count = %llu\n", (unsigned long long)count);
    std::vector<double> out(count);
    clock_t t0 = clock();
    e = DDC_GetDataValuesDouble(c2[0], 0, (size_t)count, out.data());
    double secs = double(clock() - t0) / CLOCKS_PER_SEC;
    show("read 1M doubles", e);
    printf("  elapsed %.3f s, sample %.4f %.4f\n", secs, out[0], out[count > 1 ? 1 : 0]);
    DDC_CloseFile(r);
}

int main()
{
    const char* tdm = "probe_files/probe.tdm";
    const char* tdms = "probe_files/probe.tdms";
    system("if not exist probe_files mkdir probe_files");

    writeSample(tdm, "TDM");
    writeSample(tdms, "TDMS");
    readBack(tdm, "TDM");
    readBack(tdms, "TDMS");
    mutate(tdm, "TDM");
    mutate(tdms, "TDMS");
    removeTest(tdm, "TDM");
    removeTest(tdms, "TDMS");
    readValues();

    printf("\n=== done ===\n");
    return 0;
}
