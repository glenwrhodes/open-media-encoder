cmake_minimum_required(VERSION 3.21)
project(OpenMediaEncoder
    VERSION 1.4.0
    DESCRIPTION "Open-source batch media encoder built on Qt and FFmpeg"
    HOMEPAGE_URL "https://github.com/glenwrhodes/open-media-encoder"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(OME_BUILD_TESTS "Build the integration test suite" ON)
# Each platform keeps its own FFmpeg so one checkout can be built on several systems.
if(WIN32)
    set(OME_FFMPEG_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg")
elseif(APPLE)
    set(OME_FFMPEG_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg-macos")
else()
    set(OME_FFMPEG_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg-linux")
endif()
set(OME_FFMPEG_DIR "${OME_FFMPEG_DEFAULT}" CACHE PATH
    "Folder containing the ffmpeg/ffprobe binaries to bundle (see scripts/fetch-ffmpeg.*)")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/W3 /utf-8 /permissive-)
endif()
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------------------------------------------------------------- core library
# Formats, presets, probing, command building, queue model and encoding engine. No widgets.
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/core/*.h)
add_library(encoder_core STATIC ${CORE_SOURCES})
target_include_directories(encoder_core PUBLIC src)
target_link_libraries(encoder_core PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Network)
target_compile_definitions(encoder_core PUBLIC OME_VERSION="${PROJECT_VERSION}")
if(WIN32)
    target_link_libraries(encoder_core PRIVATE powrprof)
endif()

# ---------------------------------------------------------------- application
file(GLOB UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp src/ui/*.h)
set(APP_SOURCES src/main.cpp ${UI_SOURCES})
if(WIN32)
    configure_file(resources/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    list(APPEND APP_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()
if(APPLE)
    # Application icon, generated from the PNG at build time.
    set(OME_ICNS "${CMAKE_CURRENT_BINARY_DIR}/OpenMediaEncoder.icns")
    add_custom_command(OUTPUT "${OME_ICNS}"
        COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/resources/icon-512.png -DOUT=${OME_ICNS}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MakeIcns.cmake
        DEPENDS resources/icon-512.png cmake/MakeIcns.cmake)
    set_source_files_properties("${OME_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND APP_SOURCES "${OME_ICNS}")
endif()
qt_add_executable(OpenMediaEncoder WIN32 MACOSX_BUNDLE ${APP_SOURCES})
target_link_libraries(OpenMediaEncoder PRIVATE encoder_core Qt6::Widgets)
if(WIN32)
    target_link_libraries(OpenMediaEncoder PRIVATE dwmapi ole32)
endif()
if(APPLE)
    set_target_properties(OpenMediaEncoder PROPERTIES
        OUTPUT_NAME "Open Media Encoder"
        MACOSX_BUNDLE_BUNDLE_NAME "Open Media Encoder"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.glenwrhodes.openmediaencoder"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "OpenMediaEncoder.icns"
        MACOSX_BUNDLE_COPYRIGHT "Copyright (c) Open Media Encoder contributors. MIT License.")
endif()

# ---------------------------------------------------------------- bundled FFmpeg
# Copy ffmpeg/ffprobe next to the executables so development builds and tests use the
# same pinned FFmpeg that ships in release packages.
set(OME_FFMPEG_FILES "")
foreach(f ffmpeg${CMAKE_EXECUTABLE_SUFFIX} ffprobe${CMAKE_EXECUTABLE_SUFFIX})
    if(EXISTS "${OME_FFMPEG_DIR}/${f}")
        list(APPEND OME_FFMPEG_FILES "${OME_FFMPEG_DIR}/${f}")
    endif()
endforeach()
if(OME_FFMPEG_FILES)
    # Next to the app (inside the .app bundle on macOS) and next to the tests in the build folder.
    add_custom_target(bundle_ffmpeg ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:OpenMediaEncoder>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OME_FFMPEG_FILES} "$<TARGET_FILE_DIR:OpenMediaEncoder>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OME_FFMPEG_FILES} "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Copying bundled FFmpeg")
    add_dependencies(OpenMediaEncoder bundle_ffmpeg)
else()
    message(WARNING "Bundled FFmpeg not found in ${OME_FFMPEG_DIR}. Run scripts/fetch-ffmpeg.ps1 (Windows) "
                    "or scripts/fetch-ffmpeg.sh (Linux/macOS); "
                    "until then the app falls back to an ffmpeg on PATH.")
endif()

# ---------------------------------------------------------------- install / package layout
if(WIN32)
    # Flat, portable layout: everything lives next to OpenMediaEncoder.exe.
    install(TARGETS OpenMediaEncoder RUNTIME DESTINATION .)
    set(OME_BIN_DIR .)
    set(OME_DOC_DIR .)
elseif(APPLE)
    # Self-contained bundle; FFmpeg is already copied into Contents/MacOS by bundle_ffmpeg.
    install(TARGETS OpenMediaEncoder BUNDLE DESTINATION .)
    set(OME_DOC_DIR "Open Media Encoder.app/Contents/Resources")
else()
    # Standard Unix layout (also used inside the AppImage): bin/, share/applications, share/icons.
    include(GNUInstallDirs)
    install(TARGETS OpenMediaEncoder RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(OME_BIN_DIR ${CMAKE_INSTALL_BINDIR})
    set(OME_DOC_DIR ${CMAKE_INSTALL_DATADIR}/doc/open-media-encoder)
    install(FILES resources/linux/open-media-encoder.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/icon-512.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
            RENAME open-media-encoder.png)
endif()
if(OME_FFMPEG_FILES)
    if(OME_BIN_DIR)
        install(PROGRAMS ${OME_FFMPEG_FILES} DESTINATION ${OME_BIN_DIR})
    endif()
    install(FILES "${OME_FFMPEG_DIR}/LICENSE.txt" DESTINATION ${OME_DOC_DIR}/licenses RENAME FFmpeg-LICENSE.txt
            OPTIONAL)
    install(FILES "${OME_FFMPEG_DIR}/README.txt" DESTINATION ${OME_DOC_DIR}/licenses RENAME FFmpeg-BUILD-README.txt
            OPTIONAL)
endif()
install(FILES LICENSE THIRD_PARTY_NOTICES.md README.md DESTINATION ${OME_DOC_DIR})

# ---------------------------------------------------------------- tests
if(OME_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    qt_add_executable(core_tests tests/tst_core.cpp)
    target_link_libraries(core_tests PRIVATE encoder_core Qt6::Test)
    if(TARGET bundle_ffmpeg)
        add_dependencies(core_tests bundle_ffmpeg)
    endif()
    add_test(NAME core_tests COMMAND core_tests)
    set_tests_properties(core_tests PROPERTIES TIMEOUT 1800)
endif()
