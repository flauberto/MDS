cmake_minimum_required(VERSION 3.22)
project(Batuta VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/utf-8 /bigobj)
endif()

add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)
include(third_party/link/AbletonLinkConfig.cmake)

# SDK ASIO (Steinberg). Se a pasta existir, o suporte a ASIO e compilado no Windows.
set(BATUTA_ASIO_SDK "${CMAKE_SOURCE_DIR}/third_party/asiosdk" CACHE PATH "Pasta do ASIO SDK")
set(BATUTA_HAS_ASIO OFF)
if(WIN32 AND EXISTS "${BATUTA_ASIO_SDK}/common/iasiodrv.h")
  set(BATUTA_HAS_ASIO ON)
  message(STATUS "Batuta: ASIO SDK encontrado, ASIO habilitado")
elseif(WIN32)
  message(WARNING "Batuta: ASIO SDK nao encontrado em ${BATUTA_ASIO_SDK}; compilando sem ASIO")
endif()

file(GLOB BATUTA_VOICES CONFIGURE_DEPENDS Assets/voices/*.wav)
juce_add_binary_data(BatutaData SOURCES ${BATUTA_VOICES})

set(ENGINE_SOURCES Source/Engine/Model.cpp Source/Engine/Engine.cpp Source/Engine/Package.cpp)

function(batuta_common target)
  juce_generate_juce_header(${target})
  target_sources(${target} PRIVATE ${ENGINE_SOURCES})
  target_include_directories(${target} PRIVATE Source)
  target_compile_definitions(${target} PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_OGGVORBIS=1 JUCE_USE_FLAC=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:${target},JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
  if(BATUTA_HAS_ASIO)
    target_compile_definitions(${target} PRIVATE JUCE_ASIO=1)
    target_include_directories(${target} PRIVATE "${BATUTA_ASIO_SDK}/common")
  endif()
  target_link_libraries(${target} PRIVATE BatutaData Ableton::Link
    juce::juce_audio_utils juce::juce_audio_formats juce::juce_audio_devices juce::juce_gui_extra
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
endfunction()

juce_add_gui_app(Batuta PRODUCT_NAME "Batuta" COMPANY_NAME "Batuta" BUNDLE_ID "com.batuta.desktop")
target_sources(Batuta PRIVATE Source/Desktop/Main.cpp)
batuta_common(Batuta)

# A interface do celular tambem compila no computador (usada para testes)
juce_add_gui_app(BatutaMobile PRODUCT_NAME "BatutaMobile" COMPANY_NAME "Batuta" BUNDLE_ID "com.batuta.mobile")
target_sources(BatutaMobile PRIVATE Source/Mobile/Main.cpp)
batuta_common(BatutaMobile)

juce_add_console_app(BatutaTest PRODUCT_NAME "BatutaTest")
target_sources(BatutaTest PRIVATE Tests/EngineTest.cpp)
batuta_common(BatutaTest)
