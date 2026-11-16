cmake_minimum_required(VERSION 3.25)

set(OXO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(OXO_SDK "${OXO_ROOT}/third_party/vst3sdk")
if(NOT EXISTS "${OXO_SDK}/public.sdk/source/vst/vstaudioeffect.cpp")
    message(FATAL_ERROR "Pinned Steinberg SDK missing at third_party/vst3sdk")
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
project(Synth2LinuxVST3 VERSION "${MARKETING_VERSION}" LANGUAGES C CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Linux_Build is for Linux only.")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
find_package(ZLIB REQUIRED)
option(OXO_LINUX_EDITOR "Build the X11/cairo editor" ON)
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
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/LinuxTrash.cpp"
        "${OXO_ROOT}/EditorCore/State.hpp"
        "${OXO_ROOT}/EditorCore/Storage.hpp"
        "${OXO_ROOT}/EditorCore/SynthEditorCore.hpp"
)
target_compile_features(OxoSynth2VST3 PRIVATE cxx_std_20)
target_compile_options(OxoSynth2VST3 PRIVATE -O2 -fno-fast-math)
target_include_directories(OxoSynth2VST3 PRIVATE
    "${OXO_ROOT}" "${OXO_GENERATED_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_link_libraries(OxoSynth2VST3 PRIVATE sdk ZLIB::ZLIB)
if(OXO_LINUX_EDITOR)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OXO_GUI REQUIRED IMPORTED_TARGET x11 cairo fontconfig)
    # The editor Windows and Linux share (DesktopEditor/) + its X11 window.
    target_sources(OxoSynth2VST3 PRIVATE
        "${OXO_ROOT}/DesktopEditor/DesktopEditor.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/LinuxEditor.cpp")
    target_compile_definitions(OxoSynth2VST3 PRIVATE OXO_LINUX_EDITOR=1)
    target_link_libraries(OxoSynth2VST3 PRIVATE PkgConfig::OXO_GUI)
endif()

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
