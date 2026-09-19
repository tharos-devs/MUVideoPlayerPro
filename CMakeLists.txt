cmake_minimum_required(VERSION 3.22)

project(MUVideoPlayerPro VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.12
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# FFmpeg (dev : Homebrew sur macOS, vcpkg sur Windows ; le build minimal
# maison embarqué reste un projet à part, cf. PROJECT_CONTEXT.md). Décodage
# uniquement, pas d'encodage. Exposé sous un seul nom de cible (PkgConfig::
# FFMPEG) sur les deux plateformes pour que le reste du fichier n'ait pas à
# distinguer -- pkg-config n'existe pas nativement sur Windows, et le
# détecter fiablement via les .pc générés par vcpkg est plus fragile que de
# s'appuyer sur le mécanisme officiel de vcpkg (CMAKE_TOOLCHAIN_FILE qui
# augmente find_path()/find_library()).
if(WIN32)
    set(FFMPEG_COMPONENTS avformat avcodec avutil swscale swresample)

    find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h REQUIRED)

    set(FFMPEG_LIBRARIES)
    foreach(component ${FFMPEG_COMPONENTS})
        find_library(FFMPEG_${component}_LIBRARY ${component} REQUIRED)
        list(APPEND FFMPEG_LIBRARIES "${FFMPEG_${component}_LIBRARY}")
    endforeach()

    # FFmpeg est lié statiquement (triplet vcpkg x64-windows-static-md) --
    # pas de DLL avcodec-*.dll etc. à déployer à côté du plugin, ce que
    # LoadLibraryW (utilisé par les hôtes VST3, dont MuseScore) ne cherchait
    # pas de toute façon dans le dossier du plugin lui-même. En statique, il
    # faut lier explicitement les bibliothèques Windows que FFmpeg utilise en
    # interne (Media Foundation, DXVA2/D3D11 pour le hardware decode,
    # Schannel pour TLS, threads Win32...), normalement invisibles derrière
    # les DLL FFmpeg.
    list(APPEND FFMPEG_LIBRARIES
        Bcrypt.lib
        Secur32.lib
        ncrypt.lib
        Crypt32.lib
        Mfplat.lib
        Mfuuid.lib
        Strmiids.lib
        dxva2.lib
        d3d11.lib
        dxgi.lib
        ole32.lib
        oleaut32.lib
        user32.lib
        gdi32.lib
        advapi32.lib
        ws2_32.lib
        shlwapi.lib
        Vfw32.lib
    )

    add_library(PkgConfig::FFMPEG INTERFACE IMPORTED)
    set_target_properties(PkgConfig::FFMPEG PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${FFMPEG_LIBRARIES}"
    )
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat
        libavcodec
        libavutil
        libswscale
        libswresample
    )
endif()

juce_add_plugin(MUVideoPlayerPro
    COMPANY_NAME "MUVideoPlayerPro"
    PLUGIN_MANUFACTURER_CODE Thrs
    PLUGIN_CODE Mvpp
    FORMATS VST3
    PRODUCT_NAME "MUVideoPlayerPro"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    # "Generator" force MuseScore à continuer d'appeler processBlock() même
    # transport à l'arrêt (cf. AudioFxCategory::FxGenerator /
    # shouldProcessDuringSilence() côté MuseScore) : sans ça, le plugin est
    # ignoré tant que rien ne joue et ne peut jamais émettre de MMC "Play".
    VST3_CATEGORIES Fx Generator
)

target_sources(MUVideoPlayerPro
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Transport/MMCEncoder.cpp
        Source/Transport/RecentFilesStore.cpp
        Source/UI/HitPointsSidebar.cpp
        Source/UI/TimelineComponent.cpp
        Source/UI/VideoInformationPanel.cpp
        Source/UI/VideoSettingsPanel.cpp
        Source/Video/VideoDecoder.cpp
        Source/Video/VideoGLComponent.cpp
)

target_compile_definitions(MUVideoPlayerPro
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MUVideoPlayerPro
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_opengl
        PkgConfig::FFMPEG
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

enable_testing()
add_subdirectory(Tests)
