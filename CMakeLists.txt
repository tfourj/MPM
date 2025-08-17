cmake_minimum_required(VERSION 3.16)

project(MPM VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Mqtt Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Mqtt Network)

set(PROJECT_SOURCES
        src/main.cpp
        src/mainwindow.cpp
        src/mainwindow_actions.cpp
        src/mainwindow_mqtt.cpp
        src/mainwindow_tray.cpp
        src/mainwindow_windows.cpp
        src/mainwindow.h
        src/ui/mainwindow.ui
        src/actions/actions.cpp
        src/actions/actions.h
        src/actions/actiondialog.cpp
        src/actions/actiondialog.h
        src/assets/assets.qrc
        src/assets/ico.rc
        src/common/settings.cpp
        src/common/settings.h
        src/common/crypto_win.cpp
        src/common/crypto_win.h
        src/common/service_ipc_client.cpp
        src/common/service_ipc_client.h
        src/common/ipc_auth.cpp
        src/common/ipc_auth.h
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(MPM
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET MPM APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(MPM SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(MPM
            ${PROJECT_SOURCES}
        )
    endif()
endif()

set_target_properties(MPM PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build/$<CONFIG>"
)

target_link_libraries(MPM PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Mqtt Qt${QT_VERSION_MAJOR}::Network)

# Expose version to the application
target_compile_definitions(MPM PRIVATE APP_VERSION="${PROJECT_VERSION}")

# Let AUTOUIC find .ui files that live outside the source file's directory
set_property(TARGET MPM PROPERTY AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")

# Link Windows system libraries
if (WIN32)
    target_link_libraries(MPM PRIVATE Crypt32 Ole32 Shell32 PowrProf Wtsapi32 Userenv Advapi32)
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.MPM)
endif()
set_target_properties(MPM PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS MPM
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(MPM)
endif()

if(WIN32 AND NOT STATIC_BUILD)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        get_target_property(_qt_bin_dir Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin_dir "${_qt_bin_dir}" DIRECTORY)

        set(_deploy_dir $<TARGET_FILE_DIR:MPM>)

        add_custom_command(TARGET MPM POST_BUILD
            COMMAND "${_qt_bin_dir}/windeployqt.exe"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --no-quick-import
                --no-virtualkeyboard
                --no-3dcore
                "$<TARGET_FILE:MPM>"
            COMMENT "Deploying only core Qt DLLs with windeployqt..."
        )

        add_custom_command(TARGET MPM POST_BUILD
            COMMAND cmd /C "cd /D \"${CMAKE_SOURCE_DIR}\" && echo Checking for user_post_deploy.bat in: %CD% && if exist \"user_post_deploy.bat\" call \"user_post_deploy.bat\" \"${_deploy_dir}\""
            COMMENT "Running user_post_deploy.bat from project root if it exists..."
        )

    endif()
endif()

# Windows Service target
if (WIN32)
    add_executable(MPMService
        src/service/main_service.cpp
        src/service/win_service.cpp
        src/service/win_service.h
        src/service/mqtt_daemon.cpp
        src/service/mqtt_daemon.h
        src/service/ipc_server.cpp
        src/service/ipc_server.h
        src/common/settings.cpp
        src/common/settings.h
        src/common/logging.cpp
        src/common/logging.h
        src/common/crypto_win.cpp
        src/common/crypto_win.h
        src/common/ipc_auth.cpp
        src/common/ipc_auth.h
        src/actions/actions.cpp
        src/actions/actions.h
    )
    # Build as console subsystem so main() is used
    target_link_options(MPMService PRIVATE -Wl,-subsystem,console)
    target_include_directories(MPMService PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(MPMService PRIVATE Qt${QT_VERSION_MAJOR}::Mqtt Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Crypt32 PowrProf Wtsapi32 Userenv Advapi32)
    set_target_properties(MPMService PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build/$<CONFIG>"
        WIN32_EXECUTABLE FALSE
    )
endif()
