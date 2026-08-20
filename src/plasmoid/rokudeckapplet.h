// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "daemonproxy.h"
#include "devicesmodel.h"

#include <QObject>
#include <QString>
#include <QVariant>
#include <qqmlregistration.h>

namespace RokuDeck {

class RokuDeckApplet final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(RokuDeckBridge)
    QML_SINGLETON

    Q_PROPERTY(DevicesModel* devices READ devices CONSTANT)
    Q_PROPERTY(QString selectedSerial READ selectedSerial WRITE setSelectedSerial NOTIFY
                   selectedSerialChanged)
    Q_PROPERTY(bool available READ isAvailable NOTIFY availableChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

  public:
    explicit RokuDeckApplet(QObject* parent = nullptr);
    ~RokuDeckApplet() override;

    [[nodiscard]] DevicesModel* devices();
    [[nodiscard]] QString selectedSerial() const;
    void setSelectedSerial(const QString& serial);
    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] QString lastError() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void sendCommand(const QString& serial, const QString& action,
                                 const QVariant& value = {});

  Q_SIGNALS:
    void selectedSerialChanged();
    void availableChanged();
    void lastErrorChanged();

  private:
    void ensureSelectedSerial();
    void setLastError(const QString& message);

    DevicesModel m_devices;
    DaemonProxy m_daemon;
    QString m_selectedSerial;
    QString m_lastError;
};

} // namespace RokuDeck
