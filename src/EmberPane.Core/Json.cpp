#include "pch.h"
#include "Json.h"

namespace EmberPane::Core::Json
{
    std::optional<bool> Value::AsBool() const
    {
        if (m_kind == Kind::Bool) return m_bool;
        return std::nullopt;
    }

    std::optional<double> Value::AsNumber() const
    {
        if (m_kind == Kind::Number) return m_number;
        return std::nullopt;
    }

    std::optional<int> Value::AsInt() const
    {
        if (m_kind != Kind::Number || !std::isfinite(m_number)) return std::nullopt;
        return static_cast<int>(std::llround(std::clamp(m_number, -2147483648.0, 2147483647.0)));
    }

    std::optional<std::wstring> Value::AsString() const
    {
        if (m_kind == Kind::String) return m_string;
        return std::nullopt;
    }

    Value const* Value::Find(std::wstring_view name) const
    {
        if (m_kind != Kind::Object) return nullptr;
        for (auto const& [key, value] : m_object)
            if (key == name)
                return &value;
        return nullptr;
    }

    void Value::Set(std::wstring name, Value value)
    {
        m_kind = Kind::Object;
        for (auto& [key, existing] : m_object)
        {
            if (key == name)
            {
                existing = std::move(value);
                return;
            }
        }
        m_object.emplace_back(std::move(name), std::move(value));
    }

    // ------------------------------------------------------------ parsing

    namespace
    {
        class Parser
        {
        public:
            explicit Parser(std::wstring_view text) : m_text(text) {}

            std::optional<Value> ParseDocument()
            {
                auto value = ParseValue(0);
                SkipSpace();
                if (!value || m_pos != m_text.size()) return std::nullopt;
                return value;
            }

        private:
            /// Deep enough for any real document, shallow enough that a
            /// hostile one cannot overflow the stack.
            static constexpr int MaxDepth = 64;

            std::wstring_view m_text;
            size_t m_pos{ 0 };

            void SkipSpace()
            {
                while (m_pos < m_text.size() && (m_text[m_pos] == L' ' || m_text[m_pos] == L'\t' ||
                                                 m_text[m_pos] == L'\r' || m_text[m_pos] == L'\n'))
                    ++m_pos;
            }

            bool Consume(std::wstring_view token)
            {
                if (m_text.substr(m_pos, token.size()) != token) return false;
                m_pos += token.size();
                return true;
            }

            std::optional<Value> ParseValue(int depth)
            {
                if (depth > MaxDepth) return std::nullopt;

                SkipSpace();
                if (m_pos >= m_text.size()) return std::nullopt;

                wchar_t c = m_text[m_pos];
                if (c == L'{') return ParseObject(depth);
                if (c == L'[') return ParseArray(depth);
                if (c == L'"')
                {
                    auto s = ParseString();
                    if (!s) return std::nullopt;
                    return Value(std::move(*s));
                }
                if (Consume(L"true")) return Value(true);
                if (Consume(L"false")) return Value(false);
                if (Consume(L"null")) return Value();
                return ParseNumber();
            }

            std::optional<Value> ParseObject(int depth)
            {
                ++m_pos; // {
                auto object = Value::MakeObject();

                SkipSpace();
                if (Consume(L"}")) return object;

                for (;;)
                {
                    SkipSpace();
                    if (m_pos >= m_text.size() || m_text[m_pos] != L'"') return std::nullopt;

                    auto name = ParseString();
                    if (!name) return std::nullopt;

                    SkipSpace();
                    if (!Consume(L":")) return std::nullopt;

                    auto value = ParseValue(depth + 1);
                    if (!value) return std::nullopt;

                    object.Set(std::move(*name), std::move(*value));

                    SkipSpace();
                    if (Consume(L"}")) return object;
                    if (!Consume(L",")) return std::nullopt;
                }
            }

            std::optional<Value> ParseArray(int depth)
            {
                ++m_pos; // [
                auto array = Value::MakeArray();

                SkipSpace();
                if (Consume(L"]")) return array;

                for (;;)
                {
                    auto value = ParseValue(depth + 1);
                    if (!value) return std::nullopt;
                    array.Append(std::move(*value));

                    SkipSpace();
                    if (Consume(L"]")) return array;
                    if (!Consume(L",")) return std::nullopt;
                }
            }

            std::optional<unsigned> ParseHex4()
            {
                if (m_pos + 4 > m_text.size()) return std::nullopt;

                unsigned value = 0;
                for (int i = 0; i < 4; ++i)
                {
                    wchar_t c = m_text[m_pos++];
                    value <<= 4;
                    if (c >= L'0' && c <= L'9') value |= c - L'0';
                    else if (c >= L'a' && c <= L'f') value |= c - L'a' + 10;
                    else if (c >= L'A' && c <= L'F') value |= c - L'A' + 10;
                    else return std::nullopt;
                }
                return value;
            }

            std::optional<std::wstring> ParseString()
            {
                ++m_pos; // "
                std::wstring result;

                while (m_pos < m_text.size())
                {
                    wchar_t c = m_text[m_pos++];
                    if (c == L'"') return result;

                    if (c != L'\\')
                    {
                        result.push_back(c);
                        continue;
                    }

                    if (m_pos >= m_text.size()) return std::nullopt;
                    wchar_t e = m_text[m_pos++];
                    switch (e)
                    {
                    case L'"': result.push_back(L'"'); break;
                    case L'\\': result.push_back(L'\\'); break;
                    case L'/': result.push_back(L'/'); break;
                    case L'b': result.push_back(L'\b'); break;
                    case L'f': result.push_back(L'\f'); break;
                    case L'n': result.push_back(L'\n'); break;
                    case L'r': result.push_back(L'\r'); break;
                    case L't': result.push_back(L'\t'); break;
                    case L'u':
                    {
                        // UTF-16 code units map straight onto wchar_t, so a
                        // surrogate pair is just two of them in a row.
                        auto unit = ParseHex4();
                        if (!unit) return std::nullopt;
                        result.push_back(static_cast<wchar_t>(*unit));
                        break;
                    }
                    default:
                        return std::nullopt;
                    }
                }

                return std::nullopt; // unterminated
            }

            std::optional<Value> ParseNumber()
            {
                size_t start = m_pos;
                if (m_pos < m_text.size() && (m_text[m_pos] == L'-' || m_text[m_pos] == L'+')) ++m_pos;
                while (m_pos < m_text.size() &&
                       (iswdigit(m_text[m_pos]) || m_text[m_pos] == L'.' || m_text[m_pos] == L'e' ||
                        m_text[m_pos] == L'E' || m_text[m_pos] == L'-' || m_text[m_pos] == L'+'))
                    ++m_pos;

                if (m_pos == start) return std::nullopt;

                std::wstring token{ m_text.substr(start, m_pos - start) };
                wchar_t* end = nullptr;

                // wcstod follows the C locale unless told otherwise, and a
                // decimal comma locale would read "59.33" as 59.
                static _locale_t c = _create_locale(LC_NUMERIC, "C");
                double value = _wcstod_l(token.c_str(), &end, c);

                if (end != token.c_str() + token.size()) return std::nullopt;
                return Value(value);
            }
        };

        void Escape(std::wstring& out, std::wstring const& text)
        {
            out.push_back(L'"');
            for (wchar_t c : text)
            {
                switch (c)
                {
                case L'"': out += L"\\\""; break;
                case L'\\': out += L"\\\\"; break;
                case L'\n': out += L"\\n"; break;
                case L'\r': out += L"\\r"; break;
                case L'\t': out += L"\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        wchar_t buffer[8];
                        swprintf_s(buffer, L"\\u%04x", static_cast<unsigned>(c));
                        out += buffer;
                    }
                    else
                    {
                        out.push_back(c);
                    }
                }
            }
            out.push_back(L'"');
        }

        std::wstring FormatNumber(double value)
        {
            if (!std::isfinite(value)) return L"0";

            // Whole numbers without a fraction, as System.Text.Json writes
            // them, so a file round-trips through either app unchanged.
            if (value == std::floor(value) && std::abs(value) < 1e15)
                return std::to_wstring(static_cast<long long>(value));

            static _locale_t c = _create_locale(LC_NUMERIC, "C");
            wchar_t buffer[64];
            _swprintf_s_l(buffer, std::size(buffer), L"%.17g", c, value);

            // Shortest form that reads back identically.
            for (int precision = 1; precision <= 17; ++precision)
            {
                wchar_t candidate[64];
                _swprintf_s_l(candidate, std::size(candidate), L"%.*g", c, precision, value);
                if (_wcstod_l(candidate, nullptr, c) == value)
                    return candidate;
            }
            return buffer;
        }

        void WriteValue(std::wstring& out, Value const& value, int indent)
        {
            auto newline = [&](int level)
            {
                out.push_back(L'\n');
                out.append(static_cast<size_t>(level) * 2, L' ');
            };

            switch (value.GetKind())
            {
            case Value::Kind::Null: out += L"null"; break;
            case Value::Kind::Bool: out += *value.AsBool() ? L"true" : L"false"; break;
            case Value::Kind::Number: out += FormatNumber(*value.AsNumber()); break;
            case Value::Kind::String: Escape(out, *value.AsString()); break;

            case Value::Kind::Array:
                if (value.Items().empty())
                {
                    out += L"[]";
                    break;
                }
                out.push_back(L'[');
                for (size_t i = 0; i < value.Items().size(); ++i)
                {
                    if (i > 0) out.push_back(L',');
                    newline(indent + 1);
                    WriteValue(out, value.Items()[i], indent + 1);
                }
                newline(indent);
                out.push_back(L']');
                break;

            case Value::Kind::Object:
                if (value.Members().empty())
                {
                    out += L"{}";
                    break;
                }
                out.push_back(L'{');
                for (size_t i = 0; i < value.Members().size(); ++i)
                {
                    if (i > 0) out.push_back(L',');
                    newline(indent + 1);
                    Escape(out, value.Members()[i].first);
                    out += L": ";
                    WriteValue(out, value.Members()[i].second, indent + 1);
                }
                newline(indent);
                out.push_back(L'}');
                break;
            }
        }
    }

    std::optional<Value> Parse(std::wstring_view text)
    {
        // A byte order mark is not JSON, but Notepad adds one.
        if (!text.empty() && text[0] == 0xFEFF)
            text.remove_prefix(1);

        return Parser(text).ParseDocument();
    }

    std::wstring Write(Value const& value)
    {
        std::wstring out;
        WriteValue(out, value, 0);
        return out;
    }

    std::wstring FromUtf8(std::string_view utf8)
    {
        if (utf8.empty()) return {};
        int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring text(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), text.data(), length);
        return text;
    }

    std::string ToUtf8(std::wstring_view text)
    {
        if (text.empty()) return {};
        int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length,
                            nullptr, nullptr);
        return utf8;
    }
}
