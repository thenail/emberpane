#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Just enough JSON for a settings file and one web response.
///
/// Written here rather than taken as a dependency: the whole format is a few
/// hundred lines, and a settings file is the one thing in a tray utility that
/// must never fail to load because a package version moved.
namespace EmberPane::Core::Json
{
    class Value
    {
    public:
        enum class Kind { Null, Bool, Number, String, Array, Object };

        Value() = default;
        Value(bool value) : m_kind(Kind::Bool), m_bool(value) {}
        Value(double value) : m_kind(Kind::Number), m_number(value) {}
        Value(int value) : m_kind(Kind::Number), m_number(value) {}
        Value(std::wstring value) : m_kind(Kind::String), m_string(std::move(value)) {}
        Value(wchar_t const* value) : m_kind(Kind::String), m_string(value) {}

        static Value MakeArray() { Value v; v.m_kind = Kind::Array; return v; }
        static Value MakeObject() { Value v; v.m_kind = Kind::Object; return v; }

        Kind GetKind() const { return m_kind; }
        bool IsNull() const { return m_kind == Kind::Null; }

        std::optional<bool> AsBool() const;
        std::optional<double> AsNumber() const;
        std::optional<int> AsInt() const;
        std::optional<std::wstring> AsString() const;

        /// Object member, or nullptr when absent or this is not an object.
        Value const* Find(std::wstring_view name) const;

        std::vector<Value> const& Items() const { return m_array; }
        std::vector<std::pair<std::wstring, Value>> const& Members() const { return m_object; }

        void Append(Value value) { m_array.push_back(std::move(value)); }

        /// Adds or replaces a member, keeping insertion order for writing.
        void Set(std::wstring name, Value value);

    private:
        Kind m_kind{ Kind::Null };
        bool m_bool{ false };
        double m_number{ 0 };
        std::wstring m_string;
        std::vector<Value> m_array;
        std::vector<std::pair<std::wstring, Value>> m_object;
    };

    /// Nothing when the text is not valid JSON.
    std::optional<Value> Parse(std::wstring_view text);

    /// Indented, as a settings file a person might open should be.
    std::wstring Write(Value const& value);

    std::wstring FromUtf8(std::string_view utf8);
    std::string ToUtf8(std::wstring_view text);

    /// A file's JSON. Nothing when it is missing, unreadable or not JSON.
    std::optional<Value> ReadFile(std::wstring const& path);

    /// Through a temporary file moved over the old one, so that a crash
    /// halfway leaves the old file whole. Makes the directory if need be.
    bool WriteFile(std::wstring const& path, std::wstring const& text);
}
