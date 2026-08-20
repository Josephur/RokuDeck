// SPDX-License-Identifier: GPL-3.0-or-later
#include "active_app_parser.h"

#include <QXmlStreamReader>

namespace RokuDeck {

ParseResult<ActiveAppState> parseActiveApp(QByteArrayView xml)
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
            if (reader.name() != u"active-app") {
                return {ParseError{ParseError::Code::UnexpectedRootElement, QStringLiteral("Expected active-app root element")}};
            }
            continue;
        }
        if (reader.name() != u"app") {
            reader.skipCurrentElement();
            continue;
        }

        const QXmlStreamAttributes attributes = reader.attributes();
        ActiveAppState app;
        app.id = attributes.value(u"id").toString();
        app.type = attributes.value(u"type").toString();
        app.input = attributes.value(u"input").toString();
        app.name = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
        app.screensaver = app.type.compare(u"screensaver", Qt::CaseInsensitive) == 0
                || app.id.compare(u"screensaver", Qt::CaseInsensitive) == 0
            ? TriState::Active
            : TriState::Inactive;

        if (reader.hasError()) {
            return {ParseError{ParseError::Code::MalformedXml, reader.errorString()}};
        }
        return {std::move(app)};
    }

    if (reader.hasError()) {
        return {ParseError{ParseError::Code::MalformedXml, reader.errorString()}};
    }
    return {ParseError{ParseError::Code::MissingRequiredElement, QStringLiteral("Missing active app element")}};
}

} // namespace RokuDeck
