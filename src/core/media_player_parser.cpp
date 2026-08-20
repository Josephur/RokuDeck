// SPDX-License-Identifier: GPL-3.0-or-later
#include "media_player_parser.h"

#include <QXmlStreamReader>

namespace RokuDeck {
namespace {

[[nodiscard]] MediaPlaybackState mediaStateFor(const QString &value)
{
    if (value.compare(u"play", Qt::CaseInsensitive) == 0 || value.compare(u"playing", Qt::CaseInsensitive) == 0) {
        return MediaPlaybackState::Playing;
    }
    if (value.compare(u"pause", Qt::CaseInsensitive) == 0 || value.compare(u"paused", Qt::CaseInsensitive) == 0) {
        return MediaPlaybackState::Paused;
    }
    if (value.compare(u"stop", Qt::CaseInsensitive) == 0 || value.compare(u"stopped", Qt::CaseInsensitive) == 0) {
        return MediaPlaybackState::Stopped;
    }
    if (value.compare(u"idle", Qt::CaseInsensitive) == 0) {
        return MediaPlaybackState::Idle;
    }
    return MediaPlaybackState::Unknown;
}

} // namespace

ParseResult<MediaPlaybackState> parseMediaPlayer(QByteArrayView xml)
{
    QXmlStreamReader reader(QByteArray(xml.data(), xml.size()));
    bool sawRoot = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) {
            continue;
        }
        if (!sawRoot) {
            sawRoot = true;
            if (reader.name() != u"player") {
                return {ParseError{ParseError::Code::UnexpectedRootElement, QStringLiteral("Expected player root element")}};
            }
            continue;
        }
        if (reader.name() == u"state") {
            const QString value = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            if (reader.hasError()) {
                return {ParseError{ParseError::Code::MalformedXml, reader.errorString()}};
            }
            return {mediaStateFor(value)};
        }
        reader.skipCurrentElement();
    }

    if (reader.hasError()) {
        return {ParseError{ParseError::Code::MalformedXml, reader.errorString()}};
    }
    return {ParseError{ParseError::Code::MissingRequiredElement, QStringLiteral("Missing player state element")}};
}

} // namespace RokuDeck
