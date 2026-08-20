// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_info_parser.h"

#include <QXmlStreamReader>

namespace RokuDeck {
namespace {

[[nodiscard]] ParseError malformedXml(const QXmlStreamReader &reader)
{
    return {ParseError::Code::MalformedXml, reader.errorString()};
}

[[nodiscard]] PowerState powerStateFor(const QString &value)
{
    if (value.compare(u"PowerOn", Qt::CaseInsensitive) == 0 || value.compare(u"On", Qt::CaseInsensitive) == 0) {
        return PowerState::On;
    }
    if (value.compare(u"Ready", Qt::CaseInsensitive) == 0 || value.compare(u"Standby", Qt::CaseInsensitive) == 0
        || value.compare(u"PowerOff", Qt::CaseInsensitive) == 0) {
        return PowerState::Standby;
    }
    return PowerState::Unknown;
}

} // namespace

ParseResult<DeviceSnapshot> parseDeviceInfo(QByteArrayView xml)
{
    QXmlStreamReader reader(QByteArray(xml.data(), xml.size()));
    DeviceSnapshot snapshot;
    bool sawRoot = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) {
            continue;
        }

        if (!sawRoot) {
            sawRoot = true;
            if (reader.name() != u"device-info") {
                return {ParseError{ParseError::Code::UnexpectedRootElement, QStringLiteral("Expected device-info root element")}};
            }
            continue;
        }

        const QString name = reader.name().toString();
        const QString value = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
        if (name == u"serial-number") {
            snapshot.serial = value;
        } else if (name == u"user-device-name") {
            snapshot.displayName = value;
        } else if (name == u"power-mode") {
            snapshot.power = powerStateFor(value);
        } else if (name.startsWith(u"supports-") && value.compare(u"true", Qt::CaseInsensitive) == 0) {
            snapshot.capabilities.insert(name);
        }
    }

    if (reader.hasError()) {
        return {malformedXml(reader)};
    }
    if (!sawRoot) {
        return {ParseError{ParseError::Code::MissingRequiredElement, QStringLiteral("Missing device-info root element")}};
    }

    if (snapshot.power == PowerState::On) {
        snapshot.health = DeviceHealth::Online;
    } else if (snapshot.power == PowerState::Standby) {
        snapshot.health = DeviceHealth::Standby;
    }
    snapshot.normalize();
    return {std::move(snapshot)};
}

} // namespace RokuDeck
