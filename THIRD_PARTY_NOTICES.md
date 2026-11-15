# 第三方组件与许可声明 / Third-Party Notices

本文件随**每一个**发布包（源码包与二进制包）一同分发。

**本项目原创代码采用 Apache License 2.0**（全文见根目录 `LICENSE`，归属声明见
`NOTICE`）。下文列出的是**不在该授权范围内**的第三方组件及其条款：它们由各自的
权利人单独授权，本项目的 Apache-2.0 授权不延伸至这些文件。Apache-2.0 第 4 节要求
分发时保留归属声明；其中 NI TDM C DLL 的「禁止再分发」条款属于必须向下游传达的
许可义务，随本文件一并提供。

---

## 1. NI TDM C DLL 17.0（National Instruments）

- 用途：读写 `.tdm` / `.tdms` 文件（`dev/include`、`dev/lib`、`dev/bin`）。
- 许可：`TDM C DLL/license.rtf`（NATIONAL INSTRUMENTS SOFTWARE LICENSE AGREEMENT FOR TDM DLL）。
  二进制包的 `licenses/NI-TDM-DLL-license.rtf` 是该文件的副本。
- 依据许可第 2 条，允许「as part of your product」分发，前提是应用同时支持
  **读取与写入** TDM 和 TDMS（本软件支持）。
- 依据许可第 4 条，分发时必须包含以下版权声明（原文，不得改写）：

  > Portions used under license from National Instruments Corporation;
  > copyright (c) 2007 National Instruments Corporation.
  > All rights reserved.

- 依据许可第 4 条，本软件的使用者**不得再分发** NI TDM C DLL
  （含 `nilibddc.dll` 及随附的 USI / Xerces 运行时文件），无论是单独分发还是
  作为其它产品的一部分；本声明及 `NOTICE` 即作为该禁止条款的告知。
- **源码包不含** `TDM C DLL/`：许可第 4 条不允许把该 SDK 作为独立内容提供给
  第三方。开发者需自行从 NI 获取 SDK，用 `-DTDM_ROOT=<path>` 或环境变量
  `TDM_ROOT` 指向它。
- **二进制包含**运行时 DLL，因为它们是本产品不可分割的组成部分；这些文件
  必须和 `TdmsViewer.exe` 位于同一目录，不得安装到系统共享目录。
- 目标机器还需 **Microsoft Visual C++ 2015（VC140）或更高版本运行库**，
  该运行库不随本包分发。

## 2. USI / Xerces XML Parser（NI 运行时附带）

`dacasr.dll`、`usiEx.dll`、`uspTdms.dll`、`tdms_ebd.dll`、`usiPluginTDM.dll`、
`Uds.dll`、`xerces-c_3_1_usi.dll`、`DataModels/` 以及
`EmbeddedUSICopyrights.chm` 由 NI 运行时附带。二进制包的 `licenses/` 下保留了
`USI_Xerces_License.txt`、`USI_Xerces_Notice.txt`、`EmbeddedUSICopyrights.chm`，
不得删除（USI / Xerces 的归属与 NOTICE 义务）。

## 3. Qwt 7.5.2 — czyt1988/QWT

- 上游：<https://github.com/czyt1988/QWT>（在官方 Qwt 之上扩展，含 `qwt::plot3d`）。
- 许可：**Qwt License 1.0**（LGPL 兼容），见 `third_party/QWT/COPYING`；
  3D 模块另见 `third_party/QWT/COPYING-qwtplot3D`（QwtPlot3D / gl2ps）。
- 以**动态库**方式链接（`qwtcore.dll` / `qwtplot.dll` / `qwtplot3d.dll`）。
  LGPL 义务：随包提供许可文本，且允许使用者用自行编译的版本替换这些 DLL。
- 源码包随附精简后的 Qwt 源码树（`CMakeLists.txt`、`cmake/`、`src/`、
  `classincludes/` 与许可文件），以便使用者重新构建上述 DLL。
- **本仓库对 Qwt 源码做了本地修正**：需要保留的功能性改动集中在
  `third_party/QWT/src/plot3d/qwt3d_line3d.cpp`；调试过程中另有
  `qwt3d_plot.cpp`、`qwt3d_line3d.h`、`qwt3d_plotitem.h/.cpp` 被改写后又回退。
  完整清单与原因见 `patches/QWT-7.5.2-plot3d-fixes.md`。建议向上游提交 PR，
  升级 Qwt 时需重新应用这些修改。

## 4. Qt 6.11.2

- 许可：LGPLv3 / GPLv3 / 商业许可（Qt Company）。二进制包中的 `Qt6*.dll` 与
  `platforms/`、`imageformats/` 等插件由 `windeployqt` 部署，属于 LGPL 动态链接，
  使用者可以用自行编译的 Qt 库替换。
- 二进制包的 `licenses/Qt-LICENSE.txt` 是所用 Qt 安装目录下
  `Licenses/LICENSE`（LGPLv3 / GPLv3 全文）的副本；许可总览见
  <https://doc.qt.io/qt-6/licensing.html>。若该文件只是链接说明占位，
  发布前必须替换为许可全文。
- Qt 源码不包含在本仓库中，需自行安装 Qt 6（msvc2022_64）。

## 5. Microsoft Visual C++ 运行库

`MSVCP140.dll`、`VCRUNTIME140.dll`、`VCRUNTIME140_1.dll` 等由
Microsoft Visual C++ Redistributable 提供，不随本包分发；请通过微软官方
安装包在目标机器上安装。
