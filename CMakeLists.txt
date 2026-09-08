
# ============================================================================
#  C:\workspace\Domina\CMakeLists.txt
#  Domina - seeded MIDI arpeggiator (Fanan)
#  JUCE 9 MIDI effect: VST3, CLAP via clap-juce-extensions
# ============================================================================
cmake_minimum_required(VERSION 3.22)

# JUCE 9 builds zlib/png/jpeg/flac in C mode - LANGUAGES must include C
project(Domina VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- JUCE 9 location: local default, overridable for CI ---------------------
set(JUCE_DIR "C:/workspace/JUCE" CACHE PATH "Path to the JUCE 9 checkout")
if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    if(DEFINED ENV{JUCE_DIR} AND EXISTS "$ENV{JUCE_DIR}/CMakeLists.txt")
        set(JUCE_DIR "$ENV{JUCE_DIR}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt")
        set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE")
    else()
        message(FATAL_ERROR "JUCE not found - configure with -DJUCE_DIR=<path to JUCE 9>")
    endif()
endif()
add_subdirectory(${JUCE_DIR} JUCE)

# ---- the plugin --------------------------------------------------------------
# Domina is an INSTRUMENT that happens to emit MIDI rather than audio.
#
# It was briefly built as a VST3 MIDI effect, which is the honest description
# but the wrong shape in practice: several hosts will not load MIDI-effect VST3s
# at all, and those that do put them somewhere awkward to route out of. Every
# host knows how to load an instrument and take MIDI from it, so that is what
# this is - a synth with a silent stereo output and NEEDS_MIDI_OUTPUT.
#
# Plugin formats only. A standalone build of an arpeggiator would have nothing
# to play through, so it was only ever a debugging convenience - add Standalone
# back to FORMATS if that is ever wanted again.
# AU needs no if(APPLE) around it: JUCE only offers the AU kind when the system
# is Darwin, so this silently produces nothing extra on Windows. AU_MAIN_TYPE
# derives itself from IS_SYNTH TRUE.
#
# The VST3 manifest helper is a host-architecture tool, so it is switched off for
# the universal macOS build and left on for Windows.
if(APPLE)
    set(DOMINA_VST3_MANIFEST FALSE)
else()
    set(DOMINA_VST3_MANIFEST TRUE)
endif()

juce_add_plugin(Domina
    COMPANY_NAME "Fanan"
    BUNDLE_ID com.fananteam.domina
    PLUGIN_MANUFACTURER_CODE Fnan
    PLUGIN_CODE Domn
    FORMATS VST3 AU
    PRODUCT_NAME "Domina"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    HARDENED_RUNTIME_ENABLED TRUE          # required for notarisation
    VST3_AUTO_MANIFEST ${DOMINA_VST3_MANIFEST}
    COPY_PLUGIN_AFTER_BUILD FALSE)  # TRUE = auto-install the VST3 after each build

target_sources(Domina PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_include_directories(Domina PRIVATE src)

# ---- embedded assets ---------------------------------------------------------
# The logo is compiled INTO the binary, not read from disk: a VST3 loaded from a
# host has no idea where C:\workspace\Domina is, and a plugin that needs files
# next to it is a plugin that ships broken.
#
# The path must be ABSOLUTE. juce_add_binary_data passes SOURCES straight to
# juceaide as custom-command arguments, and those resolve against the BUILD
# directory - a relative "assets/..." is looked for under build/ and the step
# dies with MSB8066, which says nothing about a missing file.
#
# The filename also has no space: juce_add_binary_data turns it into a C++
# identifier, and a space in a build path is a class of problem worth not
# having at all.
set(DOMINA_LOGO "${CMAKE_CURRENT_SOURCE_DIR}/assets/fanan_logo.png")

if(NOT EXISTS "${DOMINA_LOGO}")
    message(FATAL_ERROR
        "Logo not found:\n"
        "    ${DOMINA_LOGO}\n"
        "  It must be named fanan_logo.png with NO SPACE. If the file is still\n"
        "  called \"fanan logo.png\", rename it and re-run cmake.")
endif()

file(SIZE "${DOMINA_LOGO}" DOMINA_LOGO_SIZE)
message(STATUS "Domina logo: ${DOMINA_LOGO} (${DOMINA_LOGO_SIZE} bytes)")

if(DOMINA_LOGO_SIZE EQUAL 0)
    message(FATAL_ERROR "Logo file is empty - re-export it: ${DOMINA_LOGO}")
endif()

# HEADER_NAME and NAMESPACE are left at their defaults (BinaryData.h /
# BinaryData) rather than restated, so this matches the shape JUCE's own
# examples use and there is one less thing to be wrong.
juce_add_binary_data(DominaAssets SOURCES "${DOMINA_LOGO}")

# The plugin formats are shared libraries, so anything linked into them has to
# be position-independent. JUCE usually sets this itself; stating it costs
# nothing and turns a confusing link error into a non-event.
set_target_properties(DominaAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# ON WHILE THE STUCK-NOTE BUG IS OPEN. Set this back to OFF before shipping.
#
# It was OFF and defaulting a diagnostic to off costs a round trip every time -
# CMake caches options, so re-running the build alone does not pick up a -D
# flag, and the trace file just stays empty with nothing saying why. The macOS
# workflow passes -DDOMINA_TRACE=OFF explicitly, so release builds are clean
# regardless of what this default says.
option(DOMINA_TRACE "Write a lifecycle trace, note trace and crash log at runtime" ON)

if(DOMINA_TRACE)
    message(STATUS "Domina: RUNTIME TRACE IS ON -> %APPDATA%/Fanan/Domina_trace.txt")
else()
    message(STATUS "Domina: runtime trace off")
endif()

target_compile_definitions(Domina PUBLIC
    DOMINA_TRACE=$<BOOL:${DOMINA_TRACE}>
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0)

# JUCE_DISPLAY_SPLASH_SCREEN is deliberately absent: JUCE 9 has no splash screen
# and warns on every build when the flag is set.

# JUCE_ASIO was here for the Standalone's device manager. A hosted plugin never
# opens an audio device, so it went with the Standalone.

target_link_libraries(Domina
    PRIVATE
        DominaAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---- CLAP via clap-juce-extensions (JUCE 9.0.1 has no native CLAP) ----------
# The GitHub ZIP unpacks as "clap-juce-extensions-main"; a git clone is usually
# named "clap-juce-extensions". Accept either, plus a sibling checkout so CI on
# macOS does not need the Windows path baked in.
set(CLAP_EXT_DIR "C:/workspace/clap-juce-extensions-main"
    CACHE PATH "Path to clap-juce-extensions")

if(NOT EXISTS "${CLAP_EXT_DIR}/CMakeLists.txt")
    foreach(candidate
            "$ENV{CLAP_EXT_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../clap-juce-extensions-main"
            "${CMAKE_CURRENT_SOURCE_DIR}/../clap-juce-extensions"
            "C:/workspace/clap-juce-extensions")
        if(candidate AND EXISTS "${candidate}/CMakeLists.txt")
            set(CLAP_EXT_DIR "${candidate}")
            break()
        endif()
    endforeach()
endif()

# clap-libs/clap and clap-libs/clap-helpers are GIT SUBMODULES, and a GitHub ZIP
# download leaves them empty. Check for a HEADER in each, not just a
# CMakeLists.txt: clap's own CMakeLists opens include/clap/version.h at configure
# time, so a folder holding nothing but CMakeLists.txt still kills the configure
# - and with a message about a missing file rather than a missing library.
# Checking here keeps the reason obvious and keeps a broken CLAP from taking the
# VST3 build down with it.
set(DOMINA_CLAP_OK FALSE)
if(NOT EXISTS "${CLAP_EXT_DIR}/CMakeLists.txt")
    message(WARNING "clap-juce-extensions not found at ${CLAP_EXT_DIR} - building without CLAP")
elseif(EXISTS "${CLAP_EXT_DIR}/clap-libs/clap/include/clap/version.h"
       AND EXISTS "${CLAP_EXT_DIR}/clap-libs/clap-helpers/include/clap/helpers/plugin.hh")
    set(DOMINA_CLAP_OK TRUE)
else()
    message(WARNING
        "clap-juce-extensions found at ${CLAP_EXT_DIR}, but clap-libs is incomplete "
        "- building without CLAP.\n"
        "  Each folder needs the WHOLE library, not just its CMakeLists.txt. These "
        "two files must exist:\n"
        "    ${CLAP_EXT_DIR}/clap-libs/clap/include/clap/version.h\n"
        "    ${CLAP_EXT_DIR}/clap-libs/clap-helpers/include/clap/helpers/plugin.hh\n"
        "  Easiest fix, from ${CLAP_EXT_DIR}:\n"
        "    git clone https://github.com/free-audio/clap.git clap-libs/clap\n"
        "    git clone https://github.com/free-audio/clap-helpers.git clap-libs/clap-helpers")
endif()

if(DOMINA_CLAP_OK)
    add_subdirectory(${CLAP_EXT_DIR} clap-juce-extensions EXCLUDE_FROM_ALL)

    # INSTRUMENT FIRST, and this is deliberate rather than sloppy.
    #
    # By the CLAP spec Domina is a note-effect: it takes notes and produces
    # notes, never audio. But Studio One - and therefore Fender Studio - has no
    # browser category for a third-party note effect; its Note FX are built-in
    # only. A CLAP declaring itself purely note-effect scans cleanly and then
    # appears NOWHERE, which is exactly the shape of an afternoon we already
    # spent. So it says instrument first, the same compromise IS_SYNTH makes for
    # the VST3, and keeps note-effect after it for hosts that know better.
    #
    # None of this touches the MIDI output. The CLAP note ports come from
    # acceptsMidi()/producesMidi(), and the events themselves come from our
    # addOutboundEventsToQueue override - neither reads the feature list.
    clap_juce_extensions_plugin(TARGET Domina
        CLAP_ID "com.fananteam.domina"
        CLAP_FEATURES instrument note-effect)

    # NOTHING IS COPIED ANYWHERE. The build writes to build/ and stops there -
    # installing is a separate act, done by hand, on purpose. A post-build copy
    # here once left a second Domina.clap in a scanned folder while another sat
    # in Program Files, and the host cached one and loaded the other.
endif()
