#pragma once

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace nativejson {

inline std::string trim(const std::string& input) {
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        ++start;
    }
    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    return input.substr(start, end - start);
}

inline bool looksLikeJsonObject(const std::string& input) {
    std::string t = trim(input);
    return t.size() >= 2 && t.front() == '{' && t.back() == '}';
}

inline bool looksLikeJsonArray(const std::string& input) {
    std::string t = trim(input);
    return t.size() >= 2 && t.front() == '[' && t.back() == ']';
}

inline std::string escapeString(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char ch : input) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += ch; break;
        }
    }
    return out;
}

inline bool extractQuotedString(const std::string& input,
                                size_t startQuote,
                                std::string& out,
                                size_t* endPos = nullptr) {
    if (startQuote >= input.size() || input[startQuote] != '"') {
        return false;
    }
    std::string result;
    bool escaped = false;
    for (size_t i = startQuote + 1; i < input.size(); ++i) {
        char ch = input[i];
        if (escaped) {
            switch (ch) {
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case '\\': result += '\\'; break;
                case '"': result += '"'; break;
                default: result += ch; break;
            }
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            out = result;
            if (endPos) {
                *endPos = i;
            }
            return true;
        }
        result += ch;
    }
    return false;
}

inline size_t skipWhitespace(const std::string& input, size_t pos) {
    while (pos < input.size() && std::isspace(static_cast<unsigned char>(input[pos]))) {
        ++pos;
    }
    return pos;
}

inline bool findKeyPosition(const std::string& json, const std::string& key, size_t& valuePos) {
    const std::string token = "\"" + key + "\"";
    size_t keyPos = json.find(token);
    if (keyPos == std::string::npos) {
        return false;
    }
    size_t colonPos = json.find(':', keyPos + token.size());
    if (colonPos == std::string::npos) {
        return false;
    }
    valuePos = skipWhitespace(json, colonPos + 1);
    return valuePos < json.size();
}

inline bool extractStringField(const std::string& json,
                               const std::string& key,
                               std::string& out) {
    size_t valuePos = 0;
    if (!findKeyPosition(json, key, valuePos)) {
        return false;
    }
    return extractQuotedString(json, valuePos, out);
}

inline bool extractUIntField(const std::string& json,
                             const std::string& key,
                             uint32_t& out) {
    size_t valuePos = 0;
    if (!findKeyPosition(json, key, valuePos)) {
        return false;
    }
    size_t endPos = valuePos;
    while (endPos < json.size() && std::isdigit(static_cast<unsigned char>(json[endPos]))) {
        ++endPos;
    }
    if (endPos == valuePos) {
        return false;
    }
    out = static_cast<uint32_t>(std::strtoul(json.substr(valuePos, endPos - valuePos).c_str(), nullptr, 10));
    return true;
}

inline bool extractBoolField(const std::string& json,
                             const std::string& key,
                             bool& out) {
    size_t valuePos = 0;
    if (!findKeyPosition(json, key, valuePos)) {
        return false;
    }
    if (json.compare(valuePos, 4, "true") == 0) {
        out = true;
        return true;
    }
    if (json.compare(valuePos, 5, "false") == 0) {
        out = false;
        return true;
    }
    return false;
}

inline bool extractBracketedValue(const std::string& json,
                                  const std::string& key,
                                  char open,
                                  char close,
                                  std::string& out) {
    size_t valuePos = 0;
    if (!findKeyPosition(json, key, valuePos) || json[valuePos] != open) {
        return false;
    }
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = valuePos; i < json.size(); ++i) {
        char ch = json[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }
        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == open) {
            ++depth;
        } else if (ch == close) {
            --depth;
            if (depth == 0) {
                out = json.substr(valuePos, i - valuePos + 1);
                return true;
            }
        }
    }
    return false;
}

inline bool extractArrayField(const std::string& json,
                              const std::string& key,
                              std::string& out) {
    return extractBracketedValue(json, key, '[', ']', out);
}

inline bool extractObjectField(const std::string& json,
                               const std::string& key,
                               std::string& out) {
    return extractBracketedValue(json, key, '{', '}', out);
}

inline std::vector<std::string> extractStringArrayValues(const std::string& arrayJson) {
    std::vector<std::string> values;
    if (!looksLikeJsonArray(arrayJson)) {
        return values;
    }
    size_t pos = 1;
    while (pos < arrayJson.size() - 1) {
        pos = skipWhitespace(arrayJson, pos);
        if (pos >= arrayJson.size() - 1) {
            break;
        }
        if (arrayJson[pos] == '"') {
            std::string value;
            size_t endPos = pos;
            if (!extractQuotedString(arrayJson, pos, value, &endPos)) {
                break;
            }
            values.push_back(value);
            pos = endPos + 1;
        } else {
            ++pos;
        }
    }
    return values;
}

inline std::vector<std::string> splitObjectArray(const std::string& arrayJson) {
    std::vector<std::string> objects;
    if (!looksLikeJsonArray(arrayJson)) {
        return objects;
    }
    bool inString = false;
    bool escaped = false;
    int depth = 0;
    size_t objectStart = std::string::npos;
    for (size_t i = 0; i < arrayJson.size(); ++i) {
        char ch = arrayJson[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }
        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == '{') {
            if (depth == 0) {
                objectStart = i;
            }
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && objectStart != std::string::npos) {
                objects.push_back(arrayJson.substr(objectStart, i - objectStart + 1));
                objectStart = std::string::npos;
            }
        }
    }
    return objects;
}

} // namespace nativejson
