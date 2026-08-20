// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <utility>
#include <variant>

namespace RokuDeck {

struct ParseError {
    enum class Code {
        MalformedXml,
        UnexpectedRootElement,
        MissingRequiredElement,
        InvalidValue,
    };

    Code code = Code::MalformedXml;
    QString message;
};

template<typename T>
class ParseResult {
public:
    ParseResult(T value)
        : m_result(std::move(value))
    {
    }

    ParseResult(ParseError error)
        : m_result(std::move(error))
    {
    }

    [[nodiscard]] bool has_value() const { return std::holds_alternative<T>(m_result); }
    [[nodiscard]] explicit operator bool() const { return has_value(); }
    [[nodiscard]] const T &value() const { return std::get<T>(m_result); }
    [[nodiscard]] T &value() { return std::get<T>(m_result); }
    [[nodiscard]] const ParseError &error() const { return std::get<ParseError>(m_result); }

private:
    std::variant<T, ParseError> m_result;
};

} // namespace RokuDeck
