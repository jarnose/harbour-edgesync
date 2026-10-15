cmake_minimum_required(VERSION 3.19)
project(harbour-edgesync CXX)

# std::optional-free, but the Qt-free core uses C++17 conveniences. It is
# compiled and tested with plain g++ as well; see tests/README.md.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# No "Bluetooth" Qt component: Sailfish OS 5.1's SDK target has no
# pkgconfig(Qt5Bluetooth) at all (QtConnectivity is not part of its Qt5), so the
# Edge is reached through BlueZ's D-Bus API (org.bluez.*) with Qt5::DBus, the way
# harbour-suuntosync and harbour-ourasync do it.
find_package(Qt5 COMPONENTS Core Sql DBus Network Qml Gui Quick LinguistTools REQUIRED)

include(FindPkgConfig)
pkg_search_module(SAILFISH sailfishapp REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# The Qt-free core: Garmin's GFDI protocol, the sync session, the FIT reader and
# the GPX writer. Everything in here is built and tested with plain g++ in CI,
# so a bug in it never needs a phone.
set(CORE_SOURCES
    src/gfdi/crc.cpp
    src/gfdi/cobs.cpp
    src/gfdi/message.cpp
    src/gfdi/messages.cpp
    src/gfdi/multilink.cpp
    src/gfdi/session.cpp
    src/fit/fitdecoder.cpp
    src/fit/activity.cpp
    src/export/gpx.cpp
    src/map/mercator.cpp
    src/map/vectortile.cpp
)

add_executable(harbour-edgesync
    src/harbour-edgesync.cpp
    src/controller/appcontroller.cpp
    src/controller/models.cpp
    src/ble/bluezmanagedobjects.cpp
    src/ble/edgescanner.cpp
    src/ble/edgelink.cpp
    src/store/activitystore.cpp
    src/map/maptilesource.cpp
    src/map/mapcanvas.cpp
    ${CORE_SOURCES}
)

target_compile_definitions(harbour-edgesync PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>
)

target_include_directories(harbour-edgesync PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    $<BUILD_INTERFACE:
    ${SAILFISH_INCLUDE_DIRS}
>)

target_link_libraries(harbour-edgesync
    Qt5::Quick
    Qt5::Network
    Qt5::Sql
    Qt5::DBus
    ${SAILFISH_LDFLAGS}
)

install(TARGETS harbour-edgesync RUNTIME DESTINATION bin)
install(DIRECTORY qml DESTINATION share/harbour-edgesync)
install(FILES harbour-edgesync.desktop DESTINATION share/applications)
foreach(size 86x86 108x108 128x128 172x172)
    install(FILES icons/${size}/harbour-edgesync.png
        DESTINATION share/icons/hicolor/${size}/apps)
endforeach()

# Source strings (qsTr()/tr()) are English. translations/harbour-edgesync.ts
# tracks them as the extraction base and each harbour-edgesync-<lang>.ts is a
# translated copy. qt5_add_translation only compiles .ts to .qm; a normal build
# never touches the committed .ts files (run lupdate by hand, see README).
file(GLOB TS_FILES "${CMAKE_SOURCE_DIR}/translations/*.ts")
file(GLOB LANG_TS_FILES "${CMAKE_SOURCE_DIR}/translations/harbour-edgesync-*.ts")
if(LANG_TS_FILES)
    qt5_add_translation(QM_FILES ${LANG_TS_FILES})
    add_custom_target(translations ALL DEPENDS ${QM_FILES})
    add_dependencies(harbour-edgesync translations)
    install(FILES ${QM_FILES} DESTINATION share/harbour-edgesync/translations)
endif()

# Get the other files reachable from the project tree in Qt Creator.
file(GLOB_RECURSE QML_FILES "${CMAKE_SOURCE_DIR}/qml/*.qml" "${CMAKE_SOURCE_DIR}/qml/*.js")
add_custom_target(distfiles
    SOURCES
        harbour-edgesync.desktop
        rpm/harbour-edgesync.spec
        ${QML_FILES}
        ${TS_FILES})

# Tell Qt Creator where the application executable would be located on the device.
file(WRITE "${CMAKE_BINARY_DIR}/QtCreatorDeployment.txt"
    "${CMAKE_INSTALL_PREFIX}
${CMAKE_BINARY_DIR}/harbour-edgesync:bin
")
