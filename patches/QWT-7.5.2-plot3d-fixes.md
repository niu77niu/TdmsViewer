# Qwt 7.5.2 (`czyt1988/QWT`) 本地修改清单

## 基线信息

| 项 | 值 |
| --- | --- |
| 上游 | <https://github.com/czyt1988/QWT> |
| 版本 | `CMakeLists.txt` 中 `QWT_VERSION 7.5.2`，`CHANGES.md` 最新条目为 v7.3.6 (2026-07-31) |
| 引入方式 | 2026-11-11 以源码拷贝方式放入 `third_party/QWT/`，**未保留 `.git`**，因此无法给出精确的上游 commit，也无法自动生成标准 `.patch` |
| 构建方式 | `tools\build_qwt.bat` 编译安装到 `third_party/qwt-install` |

> 若后续需要正式的 patch 文件：从上游 clone 对应 tag，把
> `third_party/QWT/src/plot3d/` 与本仓库同名目录做 `diff -u`，即可得到可提交的 PR 补丁。

## 一、需要保留的功能性改动

**文件：`third_party/QWT/src/plot3d/qwt3d_line3d.cpp`**

这三处是 `Qwt3DLine::Tube`（管线样式）在本软件的多通道三维曲线场景下能正常显示的前提，
缺一仍会复现「整条曲线看不见 / 深色主题纯黑」。

1. **`buildTubeGeometry()` 增加 `axisScale` 参数**
   - 切线、平行移动坐标系（parallel-transport frame）、环上顶点偏移全部先在
     **归一化空间**里计算，再按 `radius / axisScale` 映射回世界坐标；
     法线按 `radial * axisScale` 归一化。
   - `axisScale = (1/dt.x, 1/dt.y, 1/dt.z)`，`dt` 取自 `plot()->hull()`
     （即坐标系的整体范围，不能用单条曲线的 hull——通道索引在 Y 上是常数，会退化）。
   - 原因：`Qwt3DPlot` 默认 `AUTOFILL` 会逐轴归一化 `normX/normY/normZ = maxRange/dv`。
     本软件 X = 采样点序号 0~1500、Y/Z = 0~1，短轴的 `norm` 约为 1500；
     原实现把世界坐标里算出的管半径再乘上这个缩放，三角形全部落到裁剪体之外。

2. **自动半径改为归一化单位**
   - `buildVBO()` 中 `float radius = 0.005f;`（原本是 `0.005 × 世界空间 hull 对角线`）。
   - 显式 `setTubeRadius(r)` 时按 `r / maxRange` 换算成归一化单位，量纲与上面一致。

3. **`Qwt3DLine::draw()` 新增共享 `bindSurfaceShader()`**
   - Tube 与实体 marker 都复用 `surface.vert/frag`；该 fragment shader 需要
     `uUseLighting / uLightPos / uLightAmbient / uLightDiffuse / uLightSpecular /
     uMaterialAmbient / uMaterialDiffuse / uMaterialSpecular / uShininess /
     uUseOverrideColor` 等 uniform。
   - 补丁前只上传了矩阵和 `uUseLighting`，其余保持 0；开启光照（Dark 主题）时
     Blinn-Phong 结果恒为 0，管线呈纯黑。现在按 `Qwt3DSurface::draw()` 的口径补齐。

**复现条件（未打补丁的上游）**：`Qwt3DLine` + `Tube` + `tubeRadius = 0`（auto）+
三轴量级差异大的数据（例如 X 0~1500、Y 0~1、Z 0~1）+ 开启光照。
上游自带的 `examples/3D/line3D` 用螺旋线（三轴量级接近）且默认显式
`setTubeRadius(0.05)`，因此不会触发。

## 二、调试后已回退、不要重新引入的改动

| 文件 | 曾做的实验 | 现状 |
| --- | --- | --- |
| `qwt3d_plot.cpp` | 正交投影按控件宽高比补偿、`dbgLog`、`invalidateGLResources` 循环 | 已恢复上游 `ortho(-radius, +radius, ...)` 原版 |
| `qwt3d_line3d.h`、`qwt3d_plotitem.h/.cpp` | 调试期被改写 | 已恢复上游逻辑 |
| `src/ui/Plot3DPanel.cpp`（本应用层） | 鼠标手势钳制、tooltip、`rotationChanged`、`currentChanged→refresh()`、坐标盒居中 | 已按需求全部回退 |

这几个文件虽然在调试中被重写过（mtime 较新），但**没有需要保留的功能差异**；
发布前如需绝对干净，可从上游重新取同名文件覆盖后再重新构建验证。

## 三、不在 QWT 侧的两项配套修正

- **散点样式看起来像实线**：不是库 bug，是 1500 个 3px `GL_POINTS` 在约 500px
  轴长上重叠。修正位于 `src/ui/Plot3DPanel.cpp`：`kMaxDotsPerChannel = 450`、
  `kDotPixelSize = 1.8`，散点样式单独使用更低的点预算。
- **NI TDM C DLL 的 `-6211`**：见 `src/core/TdmsCore.cpp` 的 `callWithFileName()`
  （临时切换工作目录 + 只传 ANSI 文件名），与 Qwt 无关。

## 四、升级 Qwt 时的检查清单

1. 重新编译 Qwt：`tools\build_qwt.bat Release rebuild`。
2. 把 `third_party/qwt-install/bin/qwtplot3d.dll` 复制到 `build/`（应用构建脚本会
   自动复制，但手工改库源码后需确认 DLL 已更新）。
3. 用浅色 + 深色主题分别验证：细线、散点、管线三种样式都要出图。
4. 运行 `build\tdmscore_test.exe`（核心回归，110 项断言）。
