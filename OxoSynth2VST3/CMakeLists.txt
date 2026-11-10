cmake_minimum_required(VERSION 3.25)

# Product identity + version: ../Identity.xcconfig is the ONE source (shared
# with OxoSynth2.xcodeproj). Plain `KEY = value` lines become CMake
# variables; an optional gitignored Identity.local.xcconfig overrides them.
foreach(OXO_IDENTITY_FILE Identity.xcconfig Identity.local.xcconfig)
    set(OXO_IDENTITY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../${OXO_IDENTITY_FILE}")
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
        OXO_AU_MANUFACTURER OXO_AU_SUBTYPE OXO_AU_NAME OXO_VST3_PROCESSOR_UID OXO_VST3_CONTROLLER_UID
        OXO_DATA_FOLDER OXO_LIBRARY_FOLDER MARKETING_VERSION CURRENT_PROJECT_VERSION)
    if("${${OXO_KEY}}" STREQUAL "")
        message(FATAL_ERROR "[Oxo] ${OXO_KEY} missing in Identity.xcconfig")
    endif()
endforeach()
set(OXO_MARKETING_VERSION ${MARKETING_VERSION})
set(OXO_BUILD_NUMBER ${CURRENT_PROJECT_VERSION})
string(REPLACE " " "" OXO_VST3_PACKAGE_NAME "${OXO_PRODUCT_NAME}")
foreach(OXO_UID PROCESSOR CONTROLLER)
    string(REGEX REPLACE "([0-9A-Fa-f]+)" "0x\\1," OXO_TEXT "${OXO_VST3_${OXO_UID}_UID}")
    string(REGEX REPLACE ",$" "" OXO_VST3_${OXO_UID}_UID_WORDS "${OXO_TEXT}")
endforeach()

set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0 CACHE STRING "macOS deployment target" FORCE)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "build architectures")

project(OxoSynth2VST3
    VERSION ${OXO_MARKETING_VERSION}
    DESCRIPTION "Synth2 VST3 - a Synth1 recreation"
    LANGUAGES C CXX OBJC OBJCXX Swift
)

set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/vst3sdk")
if(NOT EXISTS "${vst3sdk_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "VST3 SDK not found (git submodule, v3.8.1_build_84). Fetch it with:\n"
        "git submodule update --init --recursive")
endif()

set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

# The SDK adds bare compile options (-Werror=return-type etc.) at directory
# scope; scope them to C-family languages so they don't reach swiftc.
get_directory_property(OXO_SDK_COMPILE_OPTIONS COMPILE_OPTIONS)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")
foreach(OXO_OPT IN LISTS OXO_SDK_COMPILE_OPTIONS)
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:${OXO_OPT}>")
endforeach()

# Identity values for the C++ / Swift code (OXO_* macros; Swift imports
# them through the editor's bridging header).
set(OXO_GENERATED_DIR "${PROJECT_BINARY_DIR}/generated")
configure_file(source/OxoSynth2Identity.h.in "${OXO_GENERATED_DIR}/OxoSynth2Identity.h" @ONLY)
configure_file(resource/au-info.plist "${OXO_GENERATED_DIR}/au-info.plist" @ONLY)

smtg_add_vst3plugin(OxoSynth2VST3
    PACKAGE_NAME ${OXO_VST3_PACKAGE_NAME}
    SOURCES_LIST
    source/OxoSynth2CIDs.h
    ../EditorCore/State.hpp
    ../EditorCore/Storage.hpp
    source/OxoSynth2Processor.h
    source/OxoSynth2Processor.cpp
    source/OxoSynth2Controller.h
    source/OxoSynth2Controller.cpp
    source/OxoSynth2Entry.cpp
    source/OxoSynth2EditorView.h
    source/OxoSynth2EditorView.mm
    source/OxoSynth2Trash.mm
    ../EditorCore/OxoSynth2Bridge.h
    ../EditorCore/Trash.hpp
    ../EditorCore/SynthEditorCore.hpp
)

# SwiftUI editor: the shared UI (Shared/, reused by the future AUv3) + the
# VST3 bridge, as a static Swift library exposing @_cdecl entry points
# (EditorCore/OxoSynth2Bridge.h). Same pattern as oxo_simple.
file(GLOB OXO_SHARED_SWIFT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../Shared/*.swift")
add_library(OxoSynth2Editor STATIC
    ${OXO_SHARED_SWIFT_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/source/editor/OxoSynth2EditorBridge.swift"
)
set_target_properties(OxoSynth2Editor PROPERTIES
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/source/editor/OxoSynth2Editor-Bridging-Header.h"
    XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
)
target_include_directories(OxoSynth2Editor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source/editor" "${CMAKE_CURRENT_SOURCE_DIR}/../EditorCore" "${OXO_GENERATED_DIR}")
set_source_files_properties(source/OxoSynth2EditorView.mm source/OxoSynth2Trash.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")

# Repo root on the include path: EngineCore/ headers resolve as
# "EngineCore/...", like every Oxo target.
target_include_directories(OxoSynth2VST3 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/.." "${OXO_GENERATED_DIR}")
target_compile_options(OxoSynth2VST3 PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-std=c++20>)
target_link_libraries(OxoSynth2VST3 PRIVATE sdk OxoSynth2Editor z
    "-framework AppKit"
    "-framework SwiftUI"
    "-framework Combine"
    "-framework UniformTypeIdentifiers"
)
# Swift runtime search paths for linking the Swift static lib into the C++ module.
set_target_properties(OxoSynth2VST3 PROPERTIES
    XCODE_ATTRIBUTE_LIBRARY_SEARCH_PATHS "$(TOOLCHAIN_DIR)/usr/lib/swift/macosx /usr/lib/swift $(inherited)"
    XCODE_ATTRIBUTE_ALWAYS_EMBED_SWIFT_STANDARD_LIBRARIES "NO"
)

if(SMTG_MAC AND XCODE AND SMTG_ENABLE_AUV2_BUILDS)
    # Wraps the VST3 binary itself as an AUv2 instrument component (SDK's
    # public.sdk/source/vst/auwrapper) - no plugin code needed, buses/MIDI
    # come straight from the processor's own bus config. Requires
    # Apple's AudioUnitSDK in third_party/AudioUnitSDK (git submodule,
    # auto-detected by SMTG_CoreAudioSupport.cmake as a sibling of
    # third_party/vst3sdk).
    # add_subdirectory(vst3sdk) above only extends CMAKE_MODULE_PATH in its
    # own scope, so re-add it here (same as the SDK's own tutorials do).
    list(APPEND CMAKE_MODULE_PATH "${vst3sdk_SOURCE_DIR}/cmake/modules")
    # XCODE 27: the module's PRE_BUILD `xcodebuild` of Apple's
    # AudioUnitSDK.xcodeproj inherits that project's macOS 11.0 deployment
    # target, which Xcode 27 rejects (12.0 minimum) — the AUv2 build fails in
    # "CMake PreBuild Rules". Include a GENERATED copy of the module whose
    # nested build is pinned to our own deployment target (same no-touch rule
    # as the auwrapper.mm patch below: third_party holds pristine submodules).
    set(OXO_AUV2_MODULE_SRC "${vst3sdk_SOURCE_DIR}/cmake/modules/SMTG_AddVST3AuV2.cmake")
    set(OXO_AUV2_MODULE_PATCHED "${PROJECT_BINARY_DIR}/cmake_patched/SMTG_AddVST3AuV2.cmake")
    set(OXO_AUV2_SDKBUILD_ANCHOR "-target AudioUnitSDK build -configuration Release")
    file(READ "${OXO_AUV2_MODULE_SRC}" OXO_AUV2_MODULE_TEXT)
    string(FIND "${OXO_AUV2_MODULE_TEXT}" "${OXO_AUV2_SDKBUILD_ANCHOR}" OXO_AUV2_SDKBUILD_POS)
    if(OXO_AUV2_SDKBUILD_POS EQUAL -1)
        message(FATAL_ERROR
            "[Oxo] AudioUnitSDK xcodebuild call not found - re-check the "
            "deployment-target patch in OxoSynth2VST3/CMakeLists.txt against "
            "${OXO_AUV2_MODULE_SRC}")
    endif()
    string(REPLACE "${OXO_AUV2_SDKBUILD_ANCHOR}"
        "${OXO_AUV2_SDKBUILD_ANCHOR} MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
        OXO_AUV2_MODULE_TEXT "${OXO_AUV2_MODULE_TEXT}")
    file(WRITE "${OXO_AUV2_MODULE_PATCHED}" "${OXO_AUV2_MODULE_TEXT}")
    message(STATUS "[Oxo] patched SMTG_AddVST3AuV2.cmake: AudioUnitSDK built for macOS ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    include("${OXO_AUV2_MODULE_PATCHED}")
    smtg_target_add_auv2(OxoSynth2AUv2
        BUNDLE_NAME "${OXO_PRODUCT_NAME}"
        BUNDLE_IDENTIFIER ${OXO_BUNDLE_ID}.audiounit
        INFO_PLIST_TEMPLATE "${OXO_GENERATED_DIR}/au-info.plist"
        VST3_PLUGIN_TARGET OxoSynth2VST3)
    # Apple's AudioUnitSDK headers use C++23 (std::expected, concepts,
    # std::span); the SDK's own auwrapper targets request only cxx_std_17.
    # Bump both the unused static-lib copy
    # (public.sdk/source/vst/auwrapper/CMakeLists.txt, added unconditionally
    # once SMTG_ENABLE_AUV2_BUILDS is on) and our module.
    if(TARGET au_wrapper)
        set_target_properties(au_wrapper PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
    endif()
    set_target_properties(OxoSynth2AUv2 PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

    # SDK PATCH (auwrapper.mm). third_party/vst3sdk is a pristine submodule,
    # so instead of patching it in place compile a GENERATED copy of the file
    # and drop the SDK's from the target's sources. The copy keeps the SDK's
    # relative includes ("auwrapper.h", "NSDataIBStream.h", "aucocoaview.h")
    # working via the extra include dir below. (The v3.7.12 AUv2 CC /
    # pitch-bend bug, `pid != kNoParamId` in HandleNonNoteEvent, is fixed
    # upstream since 3.8: no patch needed any more.)
    set(OXO_AUWRAPPER_DIR "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/auwrapper")
    set(OXO_AUWRAPPER_SRC "${OXO_AUWRAPPER_DIR}/auwrapper.mm")
    set(OXO_AUWRAPPER_PATCHED "${PROJECT_BINARY_DIR}/auwrapper_patched/auwrapper.mm")
    # The wrapper ignores VST3 parameter FLAGS when it
    # builds the AU parameter list, so our hidden MIDI proxy parameters
    # (kIsHidden, see OxoSynth2Controller) showed up in Logic's automation list
    # as "MIDI ch1 #0"... next to the real ones. Skip hidden parameters.
    # NOTE: plain semicolons, NOT `\;` — inside a quoted CMake string the escape
    # keeps its backslash in the stored value, and the patch would never match.
    # The `AudioUnitParameterInfo auInfo` line is what makes this the parameter
    # LIST loop (cacheParameterValues) and not `syncParameterValues`, which uses
    # the same two lines to copy values into the AU's scope — the proxies still
    # need those, they are how a CC reaches the processor.
    set(OXO_AUWRAPPER_HIDE_ANCHOR "			AudioUnitParameterInfo auInfo;
			ParameterInfo pi;
			editController->getParameterInfo (i, pi);
")
    set(OXO_AUWRAPPER_HIDE_PATCH "${OXO_AUWRAPPER_HIDE_ANCHOR}
			// OXO SAMPLE: hidden parameters stay out of the AU list.
			if ((pi.flags & ParameterInfo::kIsHidden) != 0)
				continue;
")
    file(READ "${OXO_AUWRAPPER_SRC}" OXO_AUWRAPPER_TEXT)
    string(FIND "${OXO_AUWRAPPER_TEXT}" "${OXO_AUWRAPPER_HIDE_ANCHOR}" OXO_AUWRAPPER_HIDE_POS)
    if(OXO_AUWRAPPER_HIDE_POS EQUAL -1)
        message(FATAL_ERROR
            "[Oxo] auwrapper.mm parameter-cache loop not found - re-check the "
            "kIsHidden patch in OxoSynth2VST3/CMakeLists.txt against "
            "${OXO_AUWRAPPER_SRC}")
    endif()
    string(REPLACE "${OXO_AUWRAPPER_HIDE_ANCHOR}" "${OXO_AUWRAPPER_HIDE_PATCH}"
        OXO_AUWRAPPER_TEXT "${OXO_AUWRAPPER_TEXT}")
    message(STATUS "[Oxo] patched auwrapper.mm: hidden parameters skipped in the AU list")
    # MIDI program change: updateProgramChangesCache re-reads the program
    # parameter's info into a zeroed shadowing copy, by INDEX `paramInfo.id`
    # = 0, so the program count came from parameter 0 (osc1 shape: 4
    # programs, program 1 played as 42 of 127, 4..127 dropped). Use the
    # info it already holds (programParameters, stepCount 127).
    set(OXO_AUWRAPPER_PRG_DECL "ParameterInfo paramInfo = {};")
    set(OXO_AUWRAPPER_PRG_CALL "editController->getParameterInfo (paramInfo.id, paramInfo) == kResultTrue")
    string(FIND "${OXO_AUWRAPPER_TEXT}" "${OXO_AUWRAPPER_PRG_DECL}" OXO_AUWRAPPER_PRG_DECL_POS)
    string(FIND "${OXO_AUWRAPPER_TEXT}" "${OXO_AUWRAPPER_PRG_CALL}" OXO_AUWRAPPER_PRG_CALL_POS)
    if(OXO_AUWRAPPER_PRG_DECL_POS EQUAL -1 OR OXO_AUWRAPPER_PRG_CALL_POS EQUAL -1)
        message(FATAL_ERROR
            "[Oxo] auwrapper.mm updateProgramChangesCache lookup not found - re-check the "
            "program-count patch in OxoSynth2VST3/CMakeLists.txt against ${OXO_AUWRAPPER_SRC}")
    endif()
    string(REPLACE "${OXO_AUWRAPPER_PRG_DECL}"
        "ParameterInfo paramInfo = programParameters.at (i); // OXO SYNTH2: the info held (stepCount)"
        OXO_AUWRAPPER_TEXT "${OXO_AUWRAPPER_TEXT}")
    string(REPLACE "${OXO_AUWRAPPER_PRG_CALL}" "true" OXO_AUWRAPPER_TEXT "${OXO_AUWRAPPER_TEXT}")
    message(STATUS "[Oxo] patched auwrapper.mm: program count from the program parameter")
    file(WRITE "${OXO_AUWRAPPER_PATCHED}" "${OXO_AUWRAPPER_TEXT}")

    get_target_property(OXO_AUV2_SOURCES OxoSynth2AUv2 SOURCES)
    if(NOT "${OXO_AUWRAPPER_SRC}" IN_LIST OXO_AUV2_SOURCES)
        message(FATAL_ERROR
            "[Oxo] ${OXO_AUWRAPPER_SRC} is not in OxoSynth2AUv2's sources - "
            "smtg_target_add_auv2 changed, re-check the auwrapper.mm patch")
    endif()
    list(REMOVE_ITEM OXO_AUV2_SOURCES "${OXO_AUWRAPPER_SRC}")
    list(APPEND OXO_AUV2_SOURCES "${OXO_AUWRAPPER_PATCHED}")
    set_target_properties(OxoSynth2AUv2 PROPERTIES SOURCES "${OXO_AUV2_SOURCES}")
    target_include_directories(OxoSynth2AUv2 PRIVATE "${OXO_AUWRAPPER_DIR}")
endif()


smtg_target_set_bundle(OxoSynth2VST3
    BUNDLE_IDENTIFIER ${OXO_BUNDLE_ID}.vst3
    COMPANY_NAME "${OXO_VENDOR}")
set_target_properties(OxoSynth2VST3 PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION "${OXO_BUILD_NUMBER}")

# The read-only factory bank (EngineCore/PatchLibrary.hpp kFactoryBankName,
# found at run time by EditorCore/Storage.hpp FactoryRoot). The AUv2 nests
# this bundle, so it carries the bank too. build_vst3.sh empties the built
# folder first (Xcode's copy phase never deletes a removed patch).
file(GLOB OXO_FACTORY_PATCHES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Resources/Synth2 Factory/*.sy1")
smtg_target_add_plugin_resources(OxoSynth2VST3 RESOURCES ${OXO_FACTORY_PATCHES} OUTPUT_SUBDIRECTORY "Synth2 Factory")
# The starter banks, copied ONCE into the user's library by the plugin
# (PatchLibrary.hpp SeedStarterBanks): one sub-folder per bank.
file(GLOB OXO_STARTER_BANKS LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Resources/Synth2 Starter Banks/*")
foreach(OXO_BANK IN LISTS OXO_STARTER_BANKS)
    if(IS_DIRECTORY "${OXO_BANK}")
        get_filename_component(OXO_BANK_NAME "${OXO_BANK}" NAME)
        file(GLOB OXO_BANK_PATCHES CONFIGURE_DEPENDS "${OXO_BANK}/*.sy1")
        smtg_target_add_plugin_resources(OxoSynth2VST3 RESOURCES ${OXO_BANK_PATCHES} OUTPUT_SUBDIRECTORY "Synth2 Starter Banks/${OXO_BANK_NAME}")
    endif()
endforeach()
