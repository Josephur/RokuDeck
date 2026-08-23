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

        // Real devices report the master values under <global> and per output
        // destination under <destinations>. Only the master values describe
        // what the remote's volume keys act on, so destinations are skipped.
        if (reader.name() == u"destinations") {
            reader.skipCurrentElement();
            continue;
        }
        if (reader.name() == u"global") {
            while (reader.readNextStartElement()) {
                if (reader.name() == u"volume") {
                    bool valid = false;
                    const int volume = reader.readElementText().trimmed().toInt(&valid);
                    if (!valid) {
                        return {ParseError{ParseError::Code::InvalidValue, QStringLiteral("Audio volume is not an integer")}};
                    }
                    snapshot.volume = volume;
                } else if (reader.name() == u"muted") {
                    const QString value = reader.readElementText().trimmed();
                    if (value.compare(u"true", Qt::CaseInsensitive) == 0) {
                        snapshot.muted = true;
                    } else if (value.compare(u"false", Qt::CaseInsensitive) == 0) {
                        snapshot.muted = false;
                    } else {
                        return {ParseError{ParseError::Code::InvalidValue, QStringLiteral("Mute status is not a boolean")}};
                    }
                } else {
                    reader.skipCurrentElement();
                }
            }
            continue;
        }

        // Older/flat payloads keep the values at the top level.
        const QString name = reader.name().toString();
        const QString value = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
        if (name == u"volume") {
            bool valid = false;
            const int volume = value.toInt(&valid);
            if (!valid) {
                return {ParseError{ParseError::Code::InvalidValue, QStringLiteral("Audio volume is not an integer")}};
            }
            snapshot.volume = volume;
        } else if (name == u"mute-status" || name == u"muted") {
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
