cmake_minimum_required(VERSION 3.25)

set(OXO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(OXO_SDK "${OXO_ROOT}/third_party/vst3sdk")
set(OXO_ZLIB "${CMAKE_CURRENT_LIST_DIR}/Dependencies/zlib-1.3.1")
if(NOT EXISTS "${OXO_SDK}/public.sdk/source/vst/vstaudioeffect.cpp")
    message(FATAL_ERROR "Pinned Steinberg SDK missing at third_party/vst3sdk")
endif()
if(NOT EXISTS "${OXO_ZLIB}/zlib.h")
    message(FATAL_ERROR "zlib 1.3.1 missing at Windows_Build/Dependencies/zlib-1.3.1")
endif()

# Parse exactly the same identity as the original macOS CMake project.
foreach(OXO_IDENTITY_FILE Identity.xcconfig Identity.local.xcconfig)
    set(OXO_IDENTITY_PATH "${OXO_ROOT}/${OXO_IDENTITY_FILE}")
    if(EXISTS "${OXO_IDENTITY_PATH}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${OXO_IDENTITY_PATH}")
        file(STRINGS "${OXO_IDENTITY_PATH}" OXO_IDENTITY_LINES REGEX "^[A-Z_0-9]+[ \t]*=")
        foreach(OXO_LINE IN LISTS OXO_IDENTITY_LINES)
            string(REGEX MATCH "^([A-Z_0-9]+)[ \t]*=[ \t]*(.*)$" OXO_UNUSED "${OXO_LINE}")
            string(STRIP "${CMAKE_MATCH_2}" "${CMAKE_MATCH_1}")
        endforeach()
    endif()
endforeach()
foreach(OXO_KEY OXO_PRODUCT_NAME OXO_VENDOR OXO_VENDOR_WEBSITE OXO_VENDOR_EMAIL OXO_BUNDLE_ID
        OXO_VST3_PROCESSOR_UID OXO_VST3_CONTROLLER_UID OXO_DATA_FOLDER OXO_LIBRARY_FOLDER
        MARKETING_VERSION CURRENT_PROJECT_VERSION)
    if("${${OXO_KEY}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${OXO_KEY} in Identity.xcconfig")
    endif()
endforeach()
# Keep the Steinberg SDK project version tied to the original Synth2 identity.
project(Synth2WindowsVST3 VERSION "${MARKETING_VERSION}" LANGUAGES C CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Windows x64 MSVC is required. Run build_windows.bat on Windows.")
endif()
set(OXO_MARKETING_VERSION "${MARKETING_VERSION}")
set(OXO_BUILD_NUMBER "${CURRENT_PROJECT_VERSION}")
string(REPLACE " " "" OXO_VST3_PACKAGE_NAME "${OXO_PRODUCT_NAME}")
foreach(OXO_UID PROCESSOR CONTROLLER)
    string(REGEX REPLACE "([0-9A-Fa-f]+)" "0x\\1," OXO_TEXT "${OXO_VST3_${OXO_UID}_UID}")
    string(REGEX REPLACE ",$" "" OXO_VST3_${OXO_UID}_UID_WORDS "${OXO_TEXT}")
endforeach()
set(OXO_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${OXO_GENERATED_DIR}")
configure_file("${OXO_ROOT}/OxoSynth2VST3/source/OxoSynth2Identity.h.in"
    "${OXO_GENERATED_DIR}/OxoSynth2Identity.h" @ONLY)

# Avoid all SDK link installation/symlink attempts (no Admin/Developer Mode).
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_AUV2_BUILDS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory("${OXO_SDK}" "${CMAKE_CURRENT_BINARY_DIR}/vst3sdk")
smtg_enable_vst3_sdk()

set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${OXO_ZLIB}" "${CMAKE_CURRENT_BINARY_DIR}/zlib")

set(OXO_SRC "${OXO_ROOT}/OxoSynth2VST3/source")
smtg_add_vst3plugin(OxoSynth2VST3
    PACKAGE_NAME "${OXO_VST3_PACKAGE_NAME}"
    SOURCES_LIST
        "${OXO_SRC}/OxoSynth2CIDs.h"
        "${OXO_SRC}/OxoSynth2Processor.cpp"
        "${OXO_SRC}/OxoSynth2Processor.h"
        "${OXO_SRC}/OxoSynth2Controller.cpp"
        "${OXO_SRC}/OxoSynth2Controller.h"
        "${OXO_SRC}/OxoSynth2Entry.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/WindowsEditor.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/WindowsEditor.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/WindowsTrash.cpp"
        "${OXO_ROOT}/EditorCore/State.hpp"
        "${OXO_ROOT}/EditorCore/Storage.hpp"
        "${OXO_ROOT}/EditorCore/SynthEditorCore.hpp"
)
target_compile_features(OxoSynth2VST3 PRIVATE cxx_std_20)
target_compile_definitions(OxoSynth2VST3 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _USE_MATH_DEFINES)
target_compile_options(OxoSynth2VST3 PRIVATE /EHsc /fp:precise /utf-8)
target_include_directories(OxoSynth2VST3 PRIVATE
    "${OXO_ROOT}" "${OXO_GENERATED_DIR}" "${OXO_ZLIB}" "${CMAKE_CURRENT_BINARY_DIR}/zlib"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source")
# Native GDI+ renders the same SwiftUI design coordinates without introducing
# a separate plugin framework or changing Steinberg's controller architecture.
target_link_libraries(OxoSynth2VST3 PRIVATE sdk zlibstatic shell32 ole32 gdiplus user32 gdi32 comdlg32 winhttp)

# Match the original plugin's bundled factory and starter banks (unaltered).
file(GLOB OXO_FACTORY_PATCHES CONFIGURE_DEPENDS "${OXO_ROOT}/Shared/Resources/Synth2 Factory/*.sy1")
smtg_target_add_plugin_resources(OxoSynth2VST3 RESOURCES ${OXO_FACTORY_PATCHES}
    OUTPUT_SUBDIRECTORY "Synth2 Factory")
file(GLOB OXO_STARTER_BANKS LIST_DIRECTORIES true CONFIGURE_DEPENDS
    "${OXO_ROOT}/Shared/Resources/Synth2 Starter Banks/*")
foreach(OXO_BANK IN LISTS OXO_STARTER_BANKS)
    if(IS_DIRECTORY "${OXO_BANK}")
        get_filename_component(OXO_BANK_NAME "${OXO_BANK}" NAME)
        file(GLOB OXO_BANK_PATCHES CONFIGURE_DEPENDS "${OXO_BANK}/*.sy1")
        smtg_target_add_plugin_resources(OxoSynth2VST3 RESOURCES ${OXO_BANK_PATCHES}
            OUTPUT_SUBDIRECTORY "Synth2 Starter Banks/${OXO_BANK_NAME}")
    endif()
endforeach()

# Engine CPU bench (tools/bench, the mac's program) with the plugin's
# compiler and options: build_windows.bat --bench.
add_executable(synth2_bench EXCLUDE_FROM_ALL "${OXO_ROOT}/tools/bench/main.cpp")
target_compile_features(synth2_bench PRIVATE cxx_std_20)
target_compile_definitions(synth2_bench PRIVATE NOMINMAX _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
target_compile_options(synth2_bench PRIVATE /EHsc /fp:precise /utf-8)

add_executable(synth2_storage_tests Tests/StorageSelectionTests.cpp)
target_compile_features(synth2_storage_tests PRIVATE cxx_std_20)
target_include_directories(synth2_storage_tests PRIVATE "${OXO_ROOT}")
