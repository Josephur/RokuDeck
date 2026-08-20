// SPDX-License-Identifier: GPL-3.0-or-later

#include "rokudeckapplet.h"

#include <QAbstractItemModel>
#include <QUuid>

namespace RokuDeck {

RokuDeckApplet::RokuDeckApplet(QObject* parent)
    : QObject(parent),
      m_devices(this),
      m_daemon(&m_devices,
               QStringLiteral("plasmoid-") +
                   QUuid::createUuid().toString(QUuid::WithoutBraces),
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

} // namespace RokuDeck
