cmake_minimum_required(VERSION 3.22)
project(KorgusPlayer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Statický runtime: .exe nepotrebuje Visual C++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(KP_VERSION "dev" CACHE STRING "Verzia programu (z CI)")

if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" juce)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

# REX2 / REX / RCY dekóder (public domain)
add_library(velociloops STATIC ThirdParty/VelociLoops/src/velociloops.cpp)
target_include_directories(velociloops PUBLIC ThirdParty/VelociLoops/include)
target_compile_features(velociloops PRIVATE cxx_std_17)

juce_add_gui_app(KorgusPlayer
    PRODUCT_NAME "Korgus Player"
    COMPANY_NAME "Korgus"
    VERSION 1.0.0)

target_sources(KorgusPlayer PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/FolderTree.cpp
    Source/FileGrid.cpp
    Source/PreviewPane.cpp
    Source/ThumbCache.cpp
    Source/ImageLoader.cpp
    Source/StatusBar.cpp
    Source/ImageCache.cpp
    Source/AudioInfoCache.cpp
    Source/FullscreenView.cpp
    Source/ExtraFormats.cpp
    Source/MidiPlayer.cpp
    Source/WinImaging.cpp
    Source/VideoView.cpp
    Source/DocViewers.cpp
    Source/FontView.cpp
    Source/PdfView.cpp
    Source/WinPdf.cpp
    Source/ModelView.cpp
    Source/CadView.cpp
    Source/FullscreenDoc.cpp
    Source/TextDoc.cpp
    Source/AppView.cpp
    Source/EmbeddedPreview.cpp
    Source/MfAudio.cpp
    Source/FormatInfo.cpp
    Source/ImageEdit.cpp
    Source/AudioPlayer.cpp
    Source/PlayerBar.cpp
    Source/AudioTags.cpp
    Source/PhotoInfo.cpp
    Source/MetaStore.cpp
    Source/ImageEditor.cpp
    Source/Translations.cpp
    Source/Updater.cpp
    Source/Transfer.cpp)

# verzia ako hlavičkový súbor (cez /D ju MSVC poškodil – z "1.0.23" ostalo "1")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/kp_version/Version.h" "#pragma once\n#define KP_VERSION \"${KP_VERSION}\"\n")
target_include_directories(KorgusPlayer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/kp_version")

target_compile_definitions(KorgusPlayer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
    JUCE_APPLICATION_NAME_STRING="Korgus Player"
    )

if(WIN32)
    target_link_libraries(KorgusPlayer PRIVATE windowscodecs ole32 shell32 windowsapp version gdi32 mfplat mfreadwrite mfuuid propsys uuid)
    target_compile_options(KorgusPlayer PRIVATE /bigobj)
    # C++/WinRT (PDF) potrebuje C++20 korutiny
    set_source_files_properties(Source/WinPdf.cpp PROPERTIES
        COMPILE_OPTIONS "/std:c++20"
        COMPILE_DEFINITIONS "_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS")
endif()

target_link_libraries(KorgusPlayer PRIVATE
    velociloops
    juce::juce_gui_extra
    juce::juce_cryptography
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_audio_devices
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
