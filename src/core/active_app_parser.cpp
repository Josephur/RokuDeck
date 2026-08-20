// SPDX-License-Identifier: GPL-3.0-or-later
#include "active_app_parser.h"

#include <QXmlStreamReader>

namespace RokuDeck {

ParseResult<ActiveAppState> parseActiveApp(QByteArrayView xml)
{
    QXmlStreamReader reader(QByteArray(xml.data(), xml.size()));
    bool sawRoot = false;
    std::optional<ActiveAppState> activeApp;
    std::optional<ActiveAppState> screensaver;

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
        const bool isScreensaver = reader.name() == u"screensaver";
        if (!isScreensaver && reader.name() != u"app") {
            reader.skipCurrentElement();
            continue;
        }

        const QXmlStreamAttributes attributes = reader.attributes();
        ActiveAppState app;
        app.id = attributes.value(u"id").toString();
        app.type = attributes.value(u"type").toString();
        app.input = attributes.value(u"input").toString();
        app.name = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
        app.screensaver = isScreensaver || app.type.compare(u"screensaver", Qt::CaseInsensitive) == 0
                || app.id.compare(u"screensaver", Qt::CaseInsensitive) == 0
            ? TriState::Active
            : TriState::Inactive;
        if (isScreensaver && app.type.isEmpty()) {
            app.type = QStringLiteral("screensaver");
        }

        if (reader.hasError()) {
            return {ParseError{ParseError::Code::MalformedXml, reader.errorString()}};
        }
        if (app.screensaver == TriState::Active) {
            screensaver = std::move(app);
        } else {
            activeApp = std::move(app);
        }
    }

    if (reader.hasError()) {
        return {ParseError{ParseError::Code::MalformedXml, reader.errorString()}};
    }
    if (screensaver.has_value()) {
        return {std::move(screensaver.value())};
    }
    if (activeApp.has_value()) {
        return {std::move(activeApp.value())};
    }
    return {ParseError{ParseError::Code::MissingRequiredElement, QStringLiteral("Missing active app element")}};
}

} // namespace RokuDeck
