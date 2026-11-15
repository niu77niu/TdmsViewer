cmake_minimum_required(VERSION 3.20)
project(TdmsViewer VERSION 1.0.0 LANGUAGES CXX)

# CMake >= 3.27: find_package() should honour upper-case <PKG>_ROOT hints.
# QWT_ROOT is our own cache variable; with the OLD behaviour CMake emits a
# dev warning on every configure.  Keep this optional for older CMake.
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

# Windows resource compiler: needed for the executable icon / version info.
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ---------------------------------------------------------------------------
# Qt
# ---------------------------------------------------------------------------
# The Qt prefix is supplied by the caller (build.bat does it via
# -DCMAKE_PREFIX_PATH).  QT_DIR is a convenience override for CI machines.
# No machine-specific path is baked into the released sources.
if(NOT CMAKE_PREFIX_PATH AND DEFINED ENV{QT_DIR})
    set(CMAKE_PREFIX_PATH "$ENV{QT_DIR}" CACHE PATH "Qt installation prefix")
endif()
if(NOT CMAKE_PREFIX_PATH AND NOT Qt6_DIR)
    message(FATAL_ERROR
        "Qt 6 prefix not set. Pass -DCMAKE_PREFIX_PATH=<Qt msvc2022_64> "
        "or set the QT_DIR environment variable.")
endif()
find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Widgets Svg Concurrent PrintSupport OpenGL OpenGLWidgets)
find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# NI TDM C DLL
# ---------------------------------------------------------------------------
# The NI TDM C DLL SDK is deliberately NOT part of the source release archive
# (NI license section 4).  Developers point TDM_ROOT at their own SDK copy,
# either with -DTDM_ROOT=<path> or through the TDM_ROOT environment variable.
if(DEFINED ENV{TDM_ROOT})
    set(TDM_DEFAULT_ROOT "$ENV{TDM_ROOT}")
else()
    set(TDM_DEFAULT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/TDM C DLL")
endif()
set(TDM_ROOT "${TDM_DEFAULT_ROOT}" CACHE PATH
    "NI TDM C DLL 17.0 SDK root (contains dev/include, dev/lib, dev/bin)")
set(TDM_INCLUDE "${TDM_ROOT}/dev/include")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TDM_LIB "${TDM_ROOT}/dev/lib/64-bit/msvc64/nilibddc.lib")
    set(TDM_BIN "${TDM_ROOT}/dev/bin/64-bit")
else()
    set(TDM_LIB "${TDM_ROOT}/dev/lib/32-bit/msvc/nilibddc.lib")
    set(TDM_BIN "${TDM_ROOT}/dev/bin/32-bit")
endif()

if(NOT EXISTS "${TDM_LIB}")
    message(FATAL_ERROR
        "NI TDM import library not found: ${TDM_LIB}\n"
        "Install the NI TDM C DLL 17.0 SDK and pass -DTDM_ROOT=<sdk path> "
        "(see THIRD_PARTY_NOTICES.md for the download and license terms).")
endif()

# ---------------------------------------------------------------------------
# QWT 7 (third_party/QWT, built by tools/build_qwt.bat into third_party/qwt-install)
# Provides the CMake targets qwt::core, qwt::plot and qwt::plot3d.
# ---------------------------------------------------------------------------
set(QWT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/qwt-install" CACHE PATH "Qwt 7 install prefix")
if(NOT EXISTS "${QWT_ROOT}/lib/cmake/qwt/qwtConfig.cmake")
    message(FATAL_ERROR
        "Qwt 7 not found in ${QWT_ROOT}. Run tools\\build_qwt.bat first.")
endif()
list(APPEND CMAKE_PREFIX_PATH "${QWT_ROOT}")
find_package(qwt 7 REQUIRED)
message(STATUS "QWT 7 headers: ${QWT_ROOT}/include/qwt")

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(TdmsViewer WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/core/TdmsCore.cpp
    src/core/TdmsCore.h
    src/core/TableData.cpp
    src/core/TableData.h
    src/core/Settings.cpp
    src/core/Settings.h
    src/core/TimeUtil.cpp
    src/core/TimeUtil.h
    src/ui/Theme.cpp
    src/ui/Theme.h
    src/ui/Icons.cpp
    src/ui/Icons.h
    src/ui/SettingsDialog.cpp
    src/ui/SettingsDialog.h
    src/ui/PlotPanel.cpp
    src/ui/PlotPanel.h
    src/ui/Plot3DPanel.cpp
    src/ui/Plot3DPanel.h
    src/ui/PropertyPanel.cpp
    src/ui/PropertyPanel.h
    src/ui/ImportDialog.cpp
    src/ui/ImportDialog.h
    src/ui/ExportDialog.cpp
    src/ui/ExportDialog.h
    src/ui/MergeDialog.cpp
    src/ui/MergeDialog.h
    resources/app.qrc
    resources/win/app.rc
)

target_include_directories(TdmsViewer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${TDM_INCLUDE}"
)

target_link_libraries(TdmsViewer PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::OpenGL Qt6::OpenGLWidgets
    qwt::plot
    qwt::plot3d
    "${TDM_LIB}"
)

if(MSVC)
    target_compile_options(TdmsViewer PRIVATE /utf-8 /W3 /MP /bigobj)
    target_link_options(TdmsViewer PRIVATE /SUBSYSTEM:WINDOWS)
    set_property(TARGET TdmsViewer PROPERTY VS_DEBUGGER_WORKING_DIRECTORY
        "$<TARGET_FILE_DIR:TdmsViewer>")
endif()

# Copy the TDM runtime next to the executable (required by nilibddc.dll).
add_custom_command(TARGET TdmsViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${TDM_BIN}" "$<TARGET_FILE_DIR:TdmsViewer>"
    COMMENT "Copying NI TDM runtime files"
)

# Copy the Qwt 7 runtime next to the executable.
set(QWT_DLL_POSTFIX "")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(QWT_DLL_POSTFIX "d")
endif()
foreach(qwt_dll qwtcore qwtplot qwtplot3d)
    if(EXISTS "${QWT_ROOT}/bin/${qwt_dll}${QWT_DLL_POSTFIX}.dll")
        add_custom_command(TARGET TdmsViewer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QWT_ROOT}/bin/${qwt_dll}${QWT_DLL_POSTFIX}.dll" "$<TARGET_FILE_DIR:TdmsViewer>"
        )
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Development-only test / probe tools.  The released source archive omits
# these sources, so guard the targets: a plain configure must still succeed.
# ---------------------------------------------------------------------------
option(BUILD_TESTS "Build the core console regression test" ON)
if(BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/coretest.cpp"
               AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/dialogs_smoke.cpp")
    add_executable(tdmscore_test
        tools/coretest.cpp
        src/core/TdmsCore.cpp
        src/core/TableData.cpp
        src/core/TimeUtil.cpp
        src/core/Settings.cpp src/core/Settings.h
    )
    target_include_directories(tdmscore_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${TDM_INCLUDE}"
    )
    target_link_libraries(tdmscore_test PRIVATE Qt6::Core "${TDM_LIB}")
    if(MSVC)
        target_compile_options(tdmscore_test PRIVATE /utf-8 /MP)
    endif()
    add_custom_command(TARGET tdmscore_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TDM_BIN}" "$<TARGET_FILE_DIR:tdmscore_test>"
    )

    # Offscreen rendering of the dialogs (layout smoke test, not shipped).
    add_executable(tdmsdialogs_smoke
        tools/dialogs_smoke.cpp
        src/ui/ImportDialog.cpp src/ui/ImportDialog.h
        src/ui/ExportDialog.cpp src/ui/ExportDialog.h
        src/ui/MergeDialog.cpp src/ui/MergeDialog.h
        src/ui/SettingsDialog.cpp src/ui/SettingsDialog.h
        src/core/TdmsCore.cpp src/core/TdmsCore.h
        src/core/TableData.cpp src/core/TableData.h
        src/core/Settings.cpp src/core/Settings.h
        src/core/TimeUtil.cpp src/core/TimeUtil.h
        src/ui/Theme.cpp src/ui/Theme.h
        src/ui/Icons.cpp src/ui/Icons.h
        resources/app.qrc
    )
    target_include_directories(tdmsdialogs_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${TDM_INCLUDE}"
    )
    target_link_libraries(tdmsdialogs_smoke PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg "${TDM_LIB}")
    if(MSVC)
        target_compile_options(tdmsdialogs_smoke PRIVATE /utf-8 /MP)
        target_link_options(tdmsdialogs_smoke PRIVATE /SUBSYSTEM:CONSOLE)
    endif()
    add_custom_command(TARGET tdmsdialogs_smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TDM_BIN}" "$<TARGET_FILE_DIR:tdmsdialogs_smoke>"
    )
elseif(BUILD_TESTS)
    message(STATUS "Development test sources not present; skipping BUILD_TESTS targets")
endif()

# ---------------------------------------------------------------------------
# Optional: library probe (console tool used to verify TDM SDK behaviour)
# ---------------------------------------------------------------------------
option(BUILD_PROBE "Build the TDM library probe tool" OFF)
if(BUILD_PROBE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/probe.cpp")
    add_executable(tdmsprobe tools/probe.cpp)
    target_include_directories(tdmsprobe PRIVATE "${TDM_INCLUDE}")
    target_link_libraries(tdmsprobe PRIVATE "${TDM_LIB}")
    if(MSVC)
        target_compile_options(tdmsprobe PRIVATE /utf-8)
    endif()
    add_custom_command(TARGET tdmsprobe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TDM_BIN}" "$<TARGET_FILE_DIR:tdmsprobe>"
    )
elseif(BUILD_PROBE)
    message(STATUS "Development probe source not present; skipping tdmsprobe")
endif()
