cmake_minimum_required(VERSION 3.16)
project(QtController VERSION 0.2.2 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/src/views")
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Test)
set(COMMUNICATION_SOURCE_DIR "" CACHE PATH "Directory containing the external peak_communication CMakeLists.txt")
if(NOT EXISTS "${COMMUNICATION_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set COMMUNICATION_SOURCE_DIR to your authorized communication module directory")
endif()
add_subdirectory("${COMMUNICATION_SOURCE_DIR}" peak_communication)
add_library(bootloader_protocol STATIC
    src/protocol/DiagnosticTransport.h
    src/protocol/CanTransport.h src/protocol/CanTransport.cpp
    src/protocol/SimulatedUdsEcu.h src/protocol/SimulatedUdsEcu.cpp
    src/protocol/SimulatedCanEcu.h src/protocol/SimulatedCanEcu.cpp
    src/protocol/FirmwareImage.h src/protocol/FirmwareImage.cpp
    src/protocol/LinTransport.h src/protocol/LinTransport.cpp
    src/protocol/UdsSession.h src/protocol/UdsSession.cpp
    src/protocol/FlashJob.h src/protocol/FlashJob.cpp
    src/protocol/SimulatedLinEcu.h src/protocol/SimulatedLinEcu.cpp)
target_include_directories(bootloader_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(bootloader_protocol PUBLIC Qt${QT_VERSION_MAJOR}::Core)
add_library(bootloader_ui STATIC
    src/domain/HostTypes.h src/domain/HostTypes.cpp
    src/infrastructure/SettingsStore.h src/infrastructure/SettingsStore.cpp
    src/infrastructure/ChannelWorker.h src/infrastructure/ChannelWorker.cpp
    src/model/ChannelModel.h src/model/ChannelModel.cpp
    src/model/FrameTableModel.h src/model/FrameTableModel.cpp
    src/viewmodels/ChannelViewModel.h src/viewmodels/ChannelViewModel.cpp
    src/views/ChannelPage.h src/views/ChannelPage.cpp
    src/views/MainWindow.h src/views/MainWindow.cpp src/views/MainWindow.ui
    resources/resources.qrc)
target_include_directories(bootloader_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(bootloader_ui PUBLIC bootloader_protocol peak_communication Qt${QT_VERSION_MAJOR}::Widgets)
if(MINGW)
    target_compile_options(bootloader_ui PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()
get_target_property(qmake_executable Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
get_filename_component(qt_bin "${qmake_executable}" DIRECTORY)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${qt_bin}" NO_DEFAULT_PATH)
option(BUILD_SEEDKEY_BRIDGE "Build the optional external DLL bridge (no algorithm included)" OFF)
if(BUILD_SEEDKEY_BRIDGE)
    set(SEEDKEY_CXX32 "" CACHE FILEPATH "Path to a 32-bit MinGW C++ compiler")
    if(NOT EXISTS "${SEEDKEY_CXX32}")
        message(FATAL_ERROR "Set SEEDKEY_CXX32 to a 32-bit MinGW C++ compiler")
    endif()
    set(SEEDKEY_BRIDGE "${CMAKE_BINARY_DIR}/seedkey/SeedkeyBridge32.exe")
    add_custom_command(OUTPUT "${SEEDKEY_BRIDGE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/seedkey"
        COMMAND "${SEEDKEY_CXX32}" -O2 -static -municode "${CMAKE_SOURCE_DIR}/src/seedkey/SeedkeyBridge32.cpp" -o "${SEEDKEY_BRIDGE}"
        DEPENDS src/seedkey/SeedkeyBridge32.cpp VERBATIM)
    add_custom_target(seedkey_bridge ALL DEPENDS "${SEEDKEY_BRIDGE}")
endif()
function(host_deploy target)
    if(BUILD_SEEDKEY_BRIDGE)
        add_dependencies(${target} seedkey_bridge)
    endif()
    peak_deploy(${target})
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:${target}>")
    endif()
endfunction()
if(WIN32)
    enable_language(RC)
endif()
add_executable(QtBootloader WIN32 src/main.cpp)
if(WIN32)
    target_sources(QtBootloader PRIVATE resources/app.rc)
endif()
target_link_libraries(QtBootloader PRIVATE bootloader_ui)
host_deploy(QtBootloader)
enable_testing()
add_subdirectory(tests)
