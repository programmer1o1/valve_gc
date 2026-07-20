#include "keyvalue.h"

#include <cstdio>

class KeyValueParser
{
public:
    explicit KeyValueParser(std::string_view str)
        : m_ptr{ str.begin() }
        , m_end{ str.end() }
    {
    }

    [[nodiscard]] bool NextToken()
    {
    start:
        while (true)
        {
            if (IsEndOfFile())
            {
                return false;
            }

            if (*m_ptr > ' ')
            {
                break;
            }

            m_ptr++;
        }

        if (m_ptr[0] != '/' || m_ptr[1] != '/')
        {
            return true;
        }

        m_ptr += 2;

        while (!IsEndOfFile() && *m_ptr != '\n')
        {
            m_ptr++;
        }

        goto start;
    }

    std::string_view ParseString()
    {
        m_ptr++; // skip the start quote

        auto start = m_ptr;

        while (!IsEndOfFile() && *m_ptr != '"')
        {
            m_ptr++;
        }

        size_t length = m_ptr - start;

        m_ptr++; // skip the end quote

        return { &start[0], length };
    }

    char PeekCharacter() const { return m_ptr[0]; }
    void SkipCharacter() { m_ptr++; }

private:
    bool IsEndOfFile() const { return m_ptr >= m_end; }

    std::string_view::const_iterator m_ptr;
    std::string_view::const_iterator m_end;
};

std::string LoadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        return {};
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string buffer;
    buffer.resize(size);
    long bytesRead = static_cast<long>(fread(buffer.data(), 1, size, f));

    fclose(f);

    if (bytesRead != size)
    {
        return {};
    }

    return buffer;
}

KeyValue::KeyValue(std::string_view name)
    : m_name{ name }
{
}

bool KeyValue::ParseFromFile(const char *path)
{
    std::string data = LoadFile(path);
    if (data.empty())
    {
        return false;
    }

    KeyValueParser parser{ data };
    return Parse(parser);
}

bool KeyValue::Parse(KeyValueParser &parser)
{
    m_subkeys.reserve(8);

    while (true)
    {
        if (!parser.NextToken())
        {
            return true;
        }

        KeyValue *current;

        switch (parser.PeekCharacter())
        {
        case '"':
            current = FindOrCreateSubkey(parser.ParseString());
            break;

        case '}':
            parser.SkipCharacter();
            return true;

        default:
            return false;
        }

        if (!parser.NextToken())
        {
            return false;
        }

        switch (parser.PeekCharacter())
        {
        case '"':
            current->m_string = parser.ParseString();
            break;

        case '{':
            parser.SkipCharacter();
            if (!current->Parse(parser))
            {
                return false;
            }
            break;

        default:
            return false;
        }
    }
}

KeyValue *KeyValue::FindOrCreateSubkey(std::string_view name)
{
    for (KeyValue &subkey : m_subkeys)
    {
        if (subkey.m_name == name)
        {
            return &subkey;
        }
    }

    return &m_subkeys.emplace_back(name);
}

const KeyValue *KeyValue::GetSubkey(std::string_view name) const
{
    for (const KeyValue &subkey : m_subkeys)
    {
        if (subkey.m_name == name)
        {
            return &subkey;
        }
    }

    return nullptr;
}

std::string_view KeyValue::GetString(std::string_view name, std::string_view fallback) const
{
    const KeyValue *subkey = GetSubkey(name);
    if (!subkey)
    {
        return fallback;
    }

    return subkey->m_string;
}
