#!/usr/bin/env python3
"""Build clean, reproducible release archives for TdmsViewer.

Usage (from the project root or anywhere):

    python tools/package_release.py
    python tools/package_release.py --version 1.0.1
    python tools/package_release.py --include-tdm      # NOT recommended, see below

Two artefacts are written to dist/:

    TdmsViewer-<version>-win64/   portable binary bundle (+ .zip + .sha256)
    TdmsViewer-<version>-src/     source bundle        (+ .zip + .sha256)

The source bundle deliberately excludes the NI TDM C DLL SDK: section 4 of the
NI license forbids providing the DLL to third parties except as part of your
own product.  Developers install their own SDK and pass -DTDM_ROOT=<path>.
Use --include-tdm only if you have verified that your distribution complies
with that license.

Only whitelisted files are copied, so build intermediates, development-only
test / probe sources, debug screenshots, test executables, the internal
RELEASE.md checklist and personal INI files never leak into a release.
Both bundles carry LICENSE (Apache-2.0) and NOTICE as required by section 4
of the Apache License.
"""

import argparse
import hashlib
import os
import re
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
BUILD = ROOT / "build"
TDM = ROOT / "TDM C DLL"
QWT = ROOT / "third_party" / "QWT"

# --- binary bundle -----------------------------------------------------------
QT_DLLS = [
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Svg.dll",
    "Qt6Network.dll", "Qt6OpenGL.dll", "Qt6OpenGLWidgets.dll",
    "dxcompiler.dll", "dxil.dll",
]
QWT_DLLS = ["qwtcore.dll", "qwtplot.dll", "qwtplot3d.dll"]
# Files deployed by windeployqt / by the NI TDM runtime.  qwt.dll is an old
# leftover and is intentionally NOT shipped.
NI_RUNTIME_FILES = [
    "nilibddc.dll", "Uds.dll", "dacasr.dll", "usiEx.dll", "uspTdms.dll",
    "tdms_ebd.dll", "usiPluginTDM.dll", "xerces-c_3_1_usi.dll",
    "USI_Xerces_License.txt", "USI_Xerces_Notice.txt",
    "EmbeddedUSICopyrights.chm",
]
PLUGIN_DIRS = [
    "platforms", "imageformats", "styles", "iconengines",
    "tls", "networkinformation", "generic",
]
DEMO_FILES = [
    "demo.tdm", "demo.tdms", "demo.tdx",
    "import_demo.csv", "import_demo.txt",
]
# Apache-2.0 section 4: LICENSE and NOTICE must ship with every distribution.
ROOT_DOCS = ["README.md", "LICENSE", "NOTICE", "THIRD_PARTY_NOTICES.md"]
# Only the screenshots referenced by README.md are shipped.  All other
# captures in docs/ are development-time artefacts.
DOC_IMAGES = [
    "screenshot-main.png", "screenshot-main-light.png", "screenshot-3d.png",
]

# --- source bundle -----------------------------------------------------------
# RELEASE.md is an internal publishing checklist (workspace cleanup, TODOs),
# so it stays in the repository and is NOT shipped in either bundle.
SOURCE_ROOT_FILES = [
    "CMakeLists.txt", "build.bat", "README.md", "LICENSE", "NOTICE",
    "THIRD_PARTY_NOTICES.md", ".gitignore",
]
SOURCE_DIRS = ["src", "resources", "patches"]
# Only build-time tools.  coretest.cpp / dialogs_smoke.cpp / probe.cpp /
# build_probe.bat are development-only debugging aids and are deliberately
# excluded from the released source tree.
SOURCE_TOOL_FILES = [
    "env.bat", "build_qwt.bat", "package_release.py",
]
QWT_VENDOR_DIRS = ["cmake", "src", "classincludes"]
QWT_VENDOR_FILES = [
    "CMakeLists.txt", "COPYING", "COPYING-qwtplot3D",
    "README.MD", "README-CN.MD", "CHANGES.md", "CHANGES-CN.md",
]

IGNORED_NAMES = {".git", "__pycache__", ".DS_Store"}


def log(msg):
    print(msg, flush=True)


def copy_file(src, dst):
    if not src.is_file():
        raise FileNotFoundError(f"missing: {src}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def copy_tree(src, dst, filter_fn=None):
    if not src.is_dir():
        raise FileNotFoundError(f"missing directory: {src}")
    for item in src.rglob("*"):
        if any(part in IGNORED_NAMES for part in item.relative_to(src).parts):
            continue
        rel = item.relative_to(src)
        if filter_fn and not filter_fn(rel):
            continue
        if item.is_file():
            copy_file(item, dst / rel)


def sha256_of(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def make_zip(folder):
    archive = DIST / (folder.name + ".zip")
    if archive.exists():
        archive.unlink()
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for item in sorted(folder.rglob("*")):
            if item.is_file():
                zf.write(item, item.relative_to(folder).as_posix())
    (DIST / (archive.name + ".sha256")).write_text(
        f"{sha256_of(archive)}  {archive.name}\n", encoding="utf-8")
    return archive


def summary(folder):
    files = [p for p in folder.rglob("*") if p.is_file()]
    size = sum(p.stat().st_size for p in files)
    log(f"  {folder.name}: {len(files)} files, {size / 1024 / 1024:.1f} MiB")
    return files


def build_binary(dst):
    log("[1/2] Binary bundle")
    copy_file(BUILD / "TdmsViewer.exe", dst / "TdmsViewer.exe")

    for name in QT_DLLS + QWT_DLLS + NI_RUNTIME_FILES:
        src = BUILD / name
        if src.is_file():
            copy_file(src, dst / name)
        else:
            log(f"  WARN optional runtime file not found: {name}")

    for name in PLUGIN_DIRS:
        src = BUILD / name
        if src.is_dir():
            copy_tree(src, dst / name)

    # NI data models are part of the TDM runtime, not a build artefact.
    if (BUILD / "DataModels").is_dir():
        copy_tree(BUILD / "DataModels", dst / "DataModels")

    for name in DEMO_FILES:
        copy_file(ROOT / "samples" / name, dst / "samples" / name)

    for name in ROOT_DOCS:
        copy_file(ROOT / name, dst / name)
    for name in DOC_IMAGES:
        copy_file(ROOT / "docs" / name, dst / "docs" / name)

    lic = dst / "licenses"
    copy_file(ROOT / "LICENSE", lic / "LICENSE.txt")
    copy_file(ROOT / "NOTICE", lic / "NOTICE.txt")
    copy_file(TDM / "license.rtf", lic / "NI-TDM-DLL-license.rtf")
    for name in ["USI_Xerces_License.txt", "USI_Xerces_Notice.txt",
                 "EmbeddedUSICopyrights.chm"]:
        if (BUILD / name).is_file():
            copy_file(BUILD / name, lic / name)
    copy_file(QWT / "COPYING", lic / "QWT-COPYING.txt")
    copy_file(QWT / "COPYING-qwtplot3D", lic / "QWT-COPYING-qwtplot3D.txt")
    copy_file(ROOT / "THIRD_PARTY_NOTICES.md", lic / "THIRD_PARTY_NOTICES.md")
    copy_file(ROOT / "patches" / "QWT-7.5.2-plot3d-fixes.md",
              lic / "QWT-plot3d-local-fixes.md")

    # Qt ships the LGPLv3/GPLv3 full text in <Qt base>/Licenses/LICENSE.
    # Find it relative to QT_DIR when set, otherwise probe the default layout.
    qt_license = None
    qt_dir = os.environ.get("QT_DIR")
    search_roots = [Path(qt_dir).resolve()] if qt_dir else []
    search_roots.append(Path("C:/Qt"))
    for base in search_roots:
        for parent in [base, *list(base.parents)[:4]]:
            candidate = parent / "Licenses" / "LICENSE"
            if candidate.is_file():
                qt_license = candidate
                break
        if qt_license:
            break
    if qt_license:
        copy_file(qt_license, lic / "Qt-LICENSE.txt")
        qt_copyright = qt_license.parent / "Copyright.txt"
        if qt_copyright.is_file():
            copy_file(qt_copyright, lic / "Qt-Copyright.txt")
    else:
        (lic / "Qt-LICENSE.txt").write_text(
            "Qt 6 runtime libraries are provided under the Qt LGPLv3 / GPLv3 / "
            "commercial licenses.\n"
            "Full license texts: https://doc.qt.io/qt-6/licensing.html\n"
            "Copy the <Qt installation>/Licenses/LICENSE file here before "
            "publishing.\n",
            encoding="utf-8")

    # Deliberately no config/: the application creates ./config/TdmsViewer.ini
    # on first launch, so no personal settings are shipped.
    required = ["TdmsViewer.exe", "qwtplot3d.dll", "nilibddc.dll",
                "platforms/qwindows.dll", "LICENSE", "NOTICE",
                "THIRD_PARTY_NOTICES.md"]
    missing = [r for r in required if not (dst / r).is_file()]
    if missing:
        raise RuntimeError("binary bundle incomplete: " + ", ".join(missing))
    return summary(dst)


def build_source(dst, include_tdm):
    log("[2/2] Source bundle")
    for name in SOURCE_ROOT_FILES:
        copy_file(ROOT / name, dst / name)
    for name in SOURCE_DIRS:
        copy_tree(ROOT / name, dst / name)
    for name in DOC_IMAGES:
        copy_file(ROOT / "docs" / name, dst / "docs" / name)
    for name in SOURCE_TOOL_FILES:
        copy_file(ROOT / "tools" / name, dst / "tools" / name)
    for name in DEMO_FILES:
        copy_file(ROOT / "samples" / name, dst / "samples" / name)

    qdst = dst / "third_party" / "QWT"
    for name in QWT_VENDOR_FILES:
        copy_file(QWT / name, qdst / name)
    for name in QWT_VENDOR_DIRS:
        copy_tree(QWT / name, qdst / name)

    if include_tdm:
        log("  WARNING: --include-tdm embeds the NI SDK in the SOURCE archive.")
        log("  Verify NI TDM DLL license section 4 before publishing it.")
        copy_tree(TDM, dst / "TDM C DLL")
    else:
        (dst / "GET-TDM-SDK.txt").write_text(
            "This source archive intentionally does not contain the NI TDM C DLL SDK.\n"
            "Download the TDM C DLL 17.0 from National Instruments, then configure with\n"
            "  cmake -S . -B build -G Ninja -DTDM_ROOT=<sdk path> ...\n"
            "or set the TDM_ROOT environment variable before running build.bat.\n"
            "See THIRD_PARTY_NOTICES.md for the license terms.\n",
            encoding="utf-8")

    required = ["CMakeLists.txt", "README.md", "LICENSE", "NOTICE",
                "THIRD_PARTY_NOTICES.md", "src/main.cpp", "tools/env.bat",
                "tools/build_qwt.bat",
                "third_party/QWT/src/plot3d/qwt3d_line3d.cpp",
                "patches/QWT-7.5.2-plot3d-fixes.md"]
    missing = [r for r in required if not (dst / r).is_file()]
    if missing:
        raise RuntimeError("source bundle incomplete: " + ", ".join(missing))

    # Guard against development-only files leaking back into the source tree.
    forbidden = ["coretest.cpp", "dialogs_smoke.cpp", "probe.cpp",
                 "build_probe.bat", "probe.obj", "RELEASE.md",
                 "TdmsViewer.ini", "CMakeCache.txt", "TDM C DLL/"]
    shipped = {p.relative_to(dst).as_posix() for p in dst.rglob("*") if p.is_file()}
    leaked = [f for f in forbidden if any(f in s for s in shipped)]
    if leaked:
        raise RuntimeError("source bundle contains development files: " + ", ".join(leaked))
    return summary(dst)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", help="release version (default: from CMakeLists.txt)")
    parser.add_argument("--include-tdm", action="store_true",
                        help="embed the NI SDK in the source archive (license risk)")
    parser.add_argument("--no-zip", action="store_true",
                        help="only create the folders, skip zip/checksum")
    args = parser.parse_args()

    version = args.version
    if not version:
        m = re.search(r"project\(TdmsViewer\s+VERSION\s+([0-9][0-9A-Za-z.\-]*)",
                      (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
        version = m.group(1) if m else "1.0.0"

    DIST.mkdir(parents=True, exist_ok=True)
    folders = [DIST / f"TdmsViewer-{version}-win64",
               DIST / f"TdmsViewer-{version}-src"]
    for folder in folders:
        if folder.is_dir():
            # Only ever remove our own previously generated release folders.
            if not folder.name.startswith("TdmsViewer-"):
                raise RuntimeError(f"refusing to remove unexpected folder: {folder}")
            shutil.rmtree(folder)
        folder.mkdir(parents=True)

    build_binary(folders[0])
    build_source(folders[1], args.include_tdm)

    if not args.no_zip:
        for folder in folders:
            archive = make_zip(folder)
            log(f"  archive: {archive.name}  sha256={sha256_of(archive)[:16]}...")

    log(f"[OK] Release {version} prepared in {DIST}")
    log("     Source bundle excludes the NI SDK, development-only test/probe sources")
    log("     and the internal RELEASE.md - read THIRD_PARTY_NOTICES.md.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (FileNotFoundError, RuntimeError) as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        sys.exit(1)
