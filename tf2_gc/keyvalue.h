#pragma once

#include <charconv>
#include <string>
#include <string_view>
#include <vector>

// Minimal standalone Valve KeyValues (KV1) reader, ported from csgo_gc/keyvalue.h
// but kept free of stdafx.h/protobuf so this module builds independently and fast.
class KeyValueParser;

template<typename T>
inline T FromString(std::string_view string)
{
    T value{};
    std::from_chars(string.data(), string.data() + string.size(), value);
    return value;
}

#if defined(__APPLE__)
// libc++ on macOS doesn't implement std::from_chars for floating point types
template<>
inline float FromString(std::string_view string)
{
    std::string temp{ string };
    return strtof(temp.c_str(), nullptr);
}
#endif

class KeyValue
{
public:
    explicit KeyValue(std::string_view name);

    bool ParseFromFile(const char *path);

    std::string_view Name() const { return m_name; }
    std::string_view String() const { return m_string; }

    const KeyValue *begin() const { return m_subkeys.data(); }
    const KeyValue *end() const { return m_subkeys.data() + m_subkeys.size(); }

    const KeyValue *GetSubkey(std::string_view name) const;
    std::string_view GetString(std::string_view name, std::string_view fallback = {}) const;

    template<typename T>
    T GetNumber(std::string_view name, T fallback = 0) const
    {
        const KeyValue *subkey = GetSubkey(name);
        if (!subkey)
        {
            return fallback;
        }

        return FromString<T>(subkey->m_string);
    }

private:
    bool Parse(KeyValueParser &parser);
    KeyValue *FindOrCreateSubkey(std::string_view name);

    std::string m_name;
    std::vector<KeyValue> m_subkeys;
    std::string m_string;
};

std::string LoadFile(const char *path);
