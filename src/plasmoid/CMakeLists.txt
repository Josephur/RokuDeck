# SPDX-License-Identifier: GPL-3.0-or-later

find_package(Qt6 6.7 REQUIRED COMPONENTS Qml Quick)
find_package(KF6 6.0 REQUIRED COMPONENTS I18n)
find_package(Plasma 6.0 REQUIRED)

plasma_add_applet(io.github.josephur.rokudeck
  QML_SOURCES
    qml/main.qml
    qml/CompactRepresentation.qml
    qml/FullRepresentation.qml
    qml/DeviceListItem.qml
    qml/DeviceDetail.qml
  CPP_SOURCES
    rokudeckapplet.cpp
    daemonproxy.cpp
    devicesmodel.cpp
  GENERATE_APPLET_CLASS
)

target_link_libraries(io.github.josephur.rokudeck
  PRIVATE
    RokuDeck::DBusInterface
    Qt6::DBus
    Qt6::Qml
    Qt6::Quick
    Plasma::Plasma
    KF6::I18n
    KF6::ConfigCore
)
