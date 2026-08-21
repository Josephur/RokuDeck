// SPDX-License-Identifier: GPL-3.0-or-later

#include "rokudeckapplet.h"

#include <QAbstractItemModel>
#include <QUuid>

#include <KPluginFactory>

namespace RokuDeck {

RokuDeckApplet::RokuDeckApplet(QObject* parent, const KPluginMetaData& data,
                               const QVariantList& args)
    : Plasma::Applet(parent, data, args), m_devices(this),
      m_daemon(&m_devices,
               QStringLiteral("plasmoid-") + QUuid::createUuid().toString(QUuid::WithoutBraces),
               this) {
    connect(&m_devices, &QAbstractItemModel::rowsInserted, this,
            &RokuDeckApplet::ensureSelectedSerial);
    connect(&m_devices, &QAbstractItemModel::rowsRemoved, this,
            &RokuDeckApplet::ensureSelectedSerial);
    connect(&m_devices, &QAbstractItemModel::modelReset, this,
            &RokuDeckApplet::ensureSelectedSerial);
    connect(&m_daemon, &DaemonProxy::availableChanged, this,
            [this](bool) { Q_EMIT availableChanged(); });
    connect(&m_daemon, &DaemonProxy::errorOccurred, this, &RokuDeckApplet::setLastError);
    // Plasma::Applet::busy is FINAL and means exactly this, so feed it rather
    // than shadowing it: a redeclared property here is silently ignored, which
    // would leave every QML binding on "busy" reading the base class's value.
    connect(&m_daemon, &DaemonProxy::busyChanged, this,
            [this] { setBusy(m_daemon.isBusy()); });
    connect(&m_daemon, &DaemonProxy::deviceOperationFailed, this,
            [this](const QString& operation, const QString& message) {
                setLastError(message);
                Q_EMIT deviceOperationFailed(operation, message);
            });
    connect(&m_daemon, &DaemonProxy::deviceOperationSucceeded, this,
            &RokuDeckApplet::deviceOperationSucceeded);
    connect(&m_daemon, &DaemonProxy::deviceTestFinished, this,
            &RokuDeckApplet::deviceTestFinished);
    connect(&m_daemon, &DaemonProxy::discoveryFinished, this, &RokuDeckApplet::discoveryFinished);
    connect(this, &Plasma::Applet::destroyedChanged, this, [this](bool destroyed) {
        if (destroyed) {
            m_daemon.stop();
        } else {
            m_daemon.start();
        }
    });
    m_daemon.start();
}

RokuDeckApplet::~RokuDeckApplet() {
    m_daemon.stop();
}

DevicesModel* RokuDeckApplet::devices() {
    return &m_devices;
}

QString RokuDeckApplet::selectedSerial() const {
    return m_selectedSerial;
}

void RokuDeckApplet::setSelectedSerial(const QString& serial) {
    if (m_selectedSerial == serial || (!serial.isEmpty() && !m_devices.containsSerial(serial))) {
        return;
    }
    m_selectedSerial = serial;
    Q_EMIT selectedSerialChanged();
}

bool RokuDeckApplet::isAvailable() const {
    return m_daemon.isAvailable();
}

QString RokuDeckApplet::lastError() const {
    return m_lastError;
}

void RokuDeckApplet::refresh() {
    setLastError({});
    m_daemon.refresh();
}

void RokuDeckApplet::sendCommand(const QString& serial, const QString& action,
                                 const QVariant& value) {
    setLastError({});
    m_daemon.sendCommand(serial, action, value);
}

void RokuDeckApplet::addDevice(const QVariantMap& device) {
    setLastError({});
    m_daemon.addDevice(device);
}

void RokuDeckApplet::updateDevice(const QString& serial, const QVariantMap& changes) {
    setLastError({});
    m_daemon.updateDevice(serial, changes);
}

void RokuDeckApplet::removeDevice(const QString& serial) {
    setLastError({});
    m_daemon.removeDevice(serial);
}

void RokuDeckApplet::setDeviceOrder(const QStringList& serials) {
    setLastError({});
    m_daemon.setDeviceOrder(serials);
}

void RokuDeckApplet::testDevice(const QString& address) {
    setLastError({});
    m_daemon.testDevice(address);
}

void RokuDeckApplet::discoverDevices() {
    setLastError({});
    m_daemon.discoverDevices();
}

void RokuDeckApplet::ensureSelectedSerial() {
    if (m_devices.containsSerial(m_selectedSerial)) {
        return;
    }
    setSelectedSerial(m_devices.serials().value(0));
}

void RokuDeckApplet::setLastError(const QString& message) {
    if (m_lastError == message) {
        return;
    }
    m_lastError = message;
    Q_EMIT lastErrorChanged();
}

K_PLUGIN_CLASS_WITH_JSON(RokuDeckApplet, "metadata.json")

} // namespace RokuDeck

#include "moc_rokudeckapplet.cpp"
#include "rokudeckapplet.moc"
