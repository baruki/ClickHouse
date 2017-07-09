#include <Parsers/Lexer.h>
#include <Common/StringUtils.h>
#include <common/find_first_symbols.h>


namespace DB
{

namespace
{

/// This must be consistent with functions in ReadHelpers.h
template <char quote, TokenType success_token, TokenType error_token>
Token quotedString(const char *& pos, const char * const token_begin, const char * const end)
{
    ++pos;
    while (true)
    {
        pos = find_first_symbols<quote, '\\'>(pos, end);
        if (pos >= end)
            return Token(error_token, token_begin, end);

        if (*pos == quote)
        {
            ++pos;
            if (pos < end && *pos == quote)
            {
                ++pos;
                continue;
            }
            return Token(success_token, token_begin, pos);
        }

        if (*pos == '\\')
        {
            ++pos;
            if (pos >= end)
                return Token(error_token, token_begin, end);
            ++pos;
            continue;
        }

        __builtin_unreachable();
    }
}

}


Token Lexer::nextToken()
{
    if (pos >= end)
        return Token(TokenType::EndOfStream, end, 0);

    const char * const token_begin = pos;

    auto commentUntilEndOfLine = [&]() mutable
    {
        pos = find_first_symbols('\n', end);    /// This means that newline in single-line comment cannot be escaped.
        return Token(TokenType::Comment, token_begin, pos);
    };

    switch (*pos)
    {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
        case '\f':
        case '\v':
        {
            ++pos;
            while (pos < end && isWhitespaceASCII(*pos))
                ++pos;
            return Token(TokenType::Whitespace, token_begin, pos);
        }

        case 'a'...'z':
        case 'A'...'Z':
        case '_':
        {
            if (pos > begin && isWordCharASCII(pos[-1]))
                return Token(TokenType::ErrorWordWithoutWhitespace, pos, 1);
            ++pos;
            while (pos < end && isWordCharASCII(*pos))
                ++pos;
            return Token(TokenType::BareWord, token_begin, pos);
        }

        case '0'...'9':
        {
            /// 0x, 0b
            if (pos < end - 2 && *pos == '0' && (pos[1] == 'x' || pos[1] == 'b'))
            {
                pos += 2;
            }

            while (pos < end && isNumericASCII(*pos))
                ++pos;

            /// decimal point
            if (pos < end && *pos == '.')
            {
                ++pos;
                while (pos < end && isNumericASCII(*pos))
                    ++pos;
            }

            /// exponentation
            if (pos + 1 < end && (*pos == 'e' || *pos == 'p'))
            {
                ++pos;

                /// sign of exponent
                if (pos < end - 1 && (*pos == '-' || *pos == '+'))
                    ++pos;

                while (pos < end && isNumericASCII(*pos))
                    ++pos;
            }

            return Token(TokenType::Number, token_begin, pos);
        }

        case '\'':
            return quotedString<'\'', TokenType::StringLiteral, TokenType::ErrorSingleQuoteIsNotClosed>(pos, token_begin, end);
        case '"':
            return quotedString<'"', TokenType::QuotedIdentifier, TokenType::ErrorDoubleQuoteIsNotClosed>(pos, token_begin, end);
        case '`':
            return quotedString<'`', TokenType::QuotedIdentifier, TokenType::ErrorBackQuoteIsNotClosed>(pos, token_begin, end);

        case '(':
            return Token(TokenType::OpeningRoundBracket, token_begin, ++pos);
        case ')':
            return Token(TokenType::ClosingRoundBracket, token_begin, ++pos);
        case '[':
            return Token(TokenType::OpeningSquareBracket, token_begin, ++pos);
        case ']':
            return Token(TokenType::ClosingSquareBracket, token_begin, ++pos);

        case ',':
            return Token(TokenType::Comma, token_begin, ++pos);
        case ';':
            return Token(TokenType::Semicolon, token_begin, ++pos);

        case '.':   /// qualifier, tuple access operator or start of floating point number
        {
            /// Just after identifier or complex expression.
            if (pos > begin && (pos[-1] == ')' || pos[-1] == ']' || isAlphaNumericASCII(pos[-1])))
                return Token(TokenType::Dot, token_begin, ++pos);

            ++pos;
            while (pos < end && isNumericASCII(*pos))
                ++pos;

            /// exponentation
            if (pos < end - 1 && (*pos == 'e' || *pos == 'p'))
            {
                ++pos;

                /// sign of exponent
                if (pos < end - 1 && (*pos == '-' || *pos == '+'))
                    ++pos;

                while (pos < end && isNumericASCII(*pos))
                    ++pos;
            }

            return Token(TokenType::Number, token_begin, pos);
        }

        case '+':
            return Token(TokenType::Plus, token_begin, ++pos);
        case '-':   /// minus (-), arrow (->) or start of comment (--)
        {
            ++pos;
            if (pos < end && *pos == '>')
                return Token(TokenType::Arrow, token_begin, ++pos);

            if (pos < end && *pos == '-')
            {
                ++pos;
                return commentUntilEndOfLine();
            }

            return Token(TokenType::Minus, token_begin, pos);
        }
        case '*':
            ++pos;
            return Token(TokenType::Asterisk, token_begin, pos);
        case '/':   /// division (/) or start of comment (//, /*)
        {
            ++pos;
            if (pos < end && (*pos == '/' || *pos == '*'))
            {
                if (*pos == '/')
                {
                    ++pos;
                    return commentUntilEndOfLine();
                }
                else
                {
                    ++pos;
                    while (pos <= end - 2)
                    {
                        /// This means that nested multiline comments are not supported.
                        if (pos[0] == '*' && pos[1] == '/')
                        {
                            pos += 2;
                            return Token(TokenType::Comment, token_begin, pos);
                        }
                        ++pos;
                    }
                    return Token(TokenType::ErrorMultilineCommentIsNotClosed, token_begin, end);
                }
            }
            return Token(TokenType::Division, token_begin, pos);
        }
        case '%':
            return Token(TokenType::Modulo, token_begin, ++pos);
        case '=':   /// =, ==
        {
            ++pos;
            if (pos < end && *pos == '=')
                ++pos;
            return Token(TokenType::Equals, token_begin, pos);
        }
        case '!':   /// !=
        {
            ++pos;
            if (pos < end && *pos == '=')
                return Token(TokenType::NotEquals, token_begin, ++pos);
            return Token(TokenType::ErrorSingleExclamationMark, token_begin, pos);
        }
        case '<':   /// <, <=, <>
        {
            ++pos;
            if (pos < end && *pos == '=')
                return Token(TokenType::LessOrEquals, token_begin, ++pos);
            if (pos < end && *pos == '>')
                return Token(TokenType::NotEquals, token_begin, ++pos);
            return Token(TokenType::Less, token_begin, pos);
        }
        case '>':   /// >, >=
        {
            ++pos;
            if (pos < end && *pos == '=')
                return Token(TokenType::GreaterOrEquals, token_begin, ++pos);
            return Token(TokenType::Greater, token_begin, pos);
        }
        case '?':
            return Token(TokenType::QuestionMark, token_begin, ++pos);
        case ':':
            return Token(TokenType::Colon, token_begin, ++pos);
        case '|':
        {
            ++pos;
            if (pos < end && *pos == '|')
                return Token(TokenType::Concatenation, token_begin, ++pos);
            return Token(TokenType::ErrorSinglePipeMark, token_begin, pos);
        }

        default:
            return Token(TokenType::Error, token_begin, ++pos);
    }
}

}