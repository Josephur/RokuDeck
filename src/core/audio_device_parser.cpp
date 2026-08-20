// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_device_parser.h"

#include <QXmlStreamReader>

namespace RokuDeck {

ParseResult<DeviceSnapshot> parseAudioDevice(QByteArrayView xml)
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
            if (reader.name() != u"audio-device") {
                return {ParseError{ParseError::Code::UnexpectedRootElement, QStringLiteral("Expected audio-device root element")}};
            }
            continue;
        }

        const QString name = reader.name().toString();
        const QString value = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
        if (name == u"volume") {
            bool valid = false;
            const int volume = value.toInt(&valid);
            if (!valid) {
                return {ParseError{ParseError::Code::InvalidValue, QStringLiteral("Audio volume is not an integer")}};
            }
            snapshot.volume = volume;
        } else if (name == u"mute-status") {
            if (value.compare(u"true", Qt::CaseInsensitive) == 0) {
                snapshot.muted = true;
            } else if (value.compare(u"false", Qt::CaseInsensitive) == 0) {
                snapshot.muted = false;
            } else {
                return {ParseError{ParseError::Code::InvalidValue, QStringLiteral("Mute status is not a boolean")}};
            }
        }
    }

    if (reader.hasError()) {
        return {ParseError{ParseError::Code::MalformedXml, reader.errorString()}};
    }
    if (!sawRoot) {
        return {ParseError{ParseError::Code::MissingRequiredElement, QStringLiteral("Missing audio-device root element")}};
    }
    return {std::move(snapshot)};
}

} // namespace RokuDeck
