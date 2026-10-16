cmake_minimum_required(VERSION 3.21)
project(OpenMediaEncoder
    VERSION 1.0.0
    DESCRIPTION "Open-source batch media encoder built on Qt and FFmpeg"
    HOMEPAGE_URL "https://github.com/glenwrhodes/open-media-encoder"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(OME_BUILD_TESTS "Build the integration test suite" ON)
set(OME_FFMPEG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg" CACHE PATH
    "Folder containing ffmpeg.exe/ffprobe.exe to bundle (see scripts/fetch-ffmpeg.ps1)")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
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
target_link_libraries(encoder_core PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent)
target_compile_definitions(encoder_core PUBLIC OME_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------- application
file(GLOB UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp src/ui/*.h)
set(APP_SOURCES src/main.cpp ${UI_SOURCES})
if(WIN32)
    configure_file(resources/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    list(APPEND APP_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()
qt_add_executable(OpenMediaEncoder WIN32 ${APP_SOURCES})
target_link_libraries(OpenMediaEncoder PRIVATE encoder_core Qt6::Widgets)
if(WIN32)
    target_link_libraries(OpenMediaEncoder PRIVATE dwmapi)
endif()

# ---------------------------------------------------------------- bundled FFmpeg
# Copy ffmpeg/ffprobe next to the executables so development builds and tests use the
# same pinned FFmpeg that ships in release packages.
set(OME_FFMPEG_FILES "")
foreach(f ffmpeg.exe ffprobe.exe)
    if(EXISTS "${OME_FFMPEG_DIR}/${f}")
        list(APPEND OME_FFMPEG_FILES "${OME_FFMPEG_DIR}/${f}")
    endif()
endforeach()
if(OME_FFMPEG_FILES)
    add_custom_target(bundle_ffmpeg ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OME_FFMPEG_FILES} "$<TARGET_FILE_DIR:OpenMediaEncoder>"
        COMMENT "Copying bundled FFmpeg")
    add_dependencies(OpenMediaEncoder bundle_ffmpeg)
else()
    message(WARNING "Bundled FFmpeg not found in ${OME_FFMPEG_DIR}. Run scripts/fetch-ffmpeg.ps1; "
                    "until then the app falls back to an ffmpeg on PATH.")
endif()

# ---------------------------------------------------------------- install / package layout
# Flat, portable layout: everything lives next to OpenMediaEncoder.exe.
install(TARGETS OpenMediaEncoder RUNTIME DESTINATION .)
if(OME_FFMPEG_FILES)
    install(FILES ${OME_FFMPEG_FILES} DESTINATION .)
    install(FILES "${OME_FFMPEG_DIR}/LICENSE.txt" DESTINATION licenses RENAME FFmpeg-LICENSE.txt OPTIONAL)
    install(FILES "${OME_FFMPEG_DIR}/README.txt" DESTINATION licenses RENAME FFmpeg-BUILD-README.txt OPTIONAL)
endif()
install(FILES LICENSE THIRD_PARTY_NOTICES.md README.md DESTINATION .)

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
