// SPDX-License-Identifier: MIT
#include "json_fields.hpp"

namespace pace {
using namespace json_detail;

namespace {
bool clean_text(std::string_view text, bool provider_text) {
    if (!provider_text)
        return valid_utf8(text);
    // Provider metadata may contain multiline descriptions. Display fields are validated
    // separately; control characters other than ordinary line breaks/tabs remain forbidden.
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' || text[i] == '\r' || text[i] == '\t') {
            if (!valid_utf8(text.substr(start, i - start)))
                return false;
            start = i + 1;
        }
    }
    return valid_utf8(text.substr(start));
}

bool clean_tree(const cJSON *j, unsigned depth, unsigned &count, unsigned max_depth,
                bool provider_text) {
    if (!j || depth > max_depth || ++count > 4096)
        return false;
    if (j->string && !valid_utf8(j->string))
        return false;
    if (cJSON_IsString(j) && !clean_text(j->valuestring, provider_text))
        return false;
    if (cJSON_IsNumber(j) && !std::isfinite(j->valuedouble))
        return false;
    for (const cJSON *a = j->child; a; a = a->next) {
        if (cJSON_IsObject(j)) {
            for (const cJSON *b = a->next; b; b = b->next)
                if (!std::strcmp(a->string, b->string))
                    return false;
        }
        if (!clean_tree(a, depth + 1, count, max_depth, provider_text))
            return false;
    }
    return true;
}

// Reject deep/huge token trees BEFORE cJSON allocation/recursive descent, and NUL
// escapes before C strings can hide a suffix. cJSON validates remaining grammar.
bool bounded_json(std::string_view s, unsigned max_depth) {
    unsigned depth = 0, tokens = 0;
    bool quoted = false, escaped = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = s[i];
        if (c == 0)
            return false;
        if (!quoted && c < 0x20 && c != '\t' && c != '\n' && c != '\r')
            return false;
        if (quoted) {
            if (escaped) {
                if (c == 'u' && s.substr(i, 5) == "u0000")
                    return false;
                escaped = false;
            } else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
            else if (c < 0x20)
                return false;
        } else if (c == '"') {
            quoted = true;
            ++tokens;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            // cJSON intentionally accepts some non-JSON number spellings. Keep
            // this boundary strict (no leading zero, missing fraction/exponent).
            std::size_t end = i;
            if (s[end] == '-')
                ++end;
            if (end == s.size() || s[end] < '0' || s[end] > '9')
                return false;
            if (s[end] == '0')
                ++end;
            else
                while (end < s.size() && s[end] >= '0' && s[end] <= '9')
                    ++end;
            if (end < s.size() && s[end] == '.') {
                const auto start = ++end;
                while (end < s.size() && s[end] >= '0' && s[end] <= '9')
                    ++end;
                if (start == end)
                    return false;
            }
            if (end < s.size() && (s[end] == 'e' || s[end] == 'E')) {
                ++end;
                if (end < s.size() && (s[end] == '+' || s[end] == '-'))
                    ++end;
                const auto start = end;
                while (end < s.size() && s[end] >= '0' && s[end] <= '9')
                    ++end;
                if (start == end)
                    return false;
            }
            if (end < s.size() && s[end] != ',' && s[end] != ']' && s[end] != '}' &&
                s[end] != ' ' && s[end] != '\t' && s[end] != '\n' && s[end] != '\r')
                return false;
            i = end - 1;
            ++tokens;
        } else if (c == '{' || c == '[') {
            if (++depth > max_depth)
                return false;
            ++tokens;
        } else if (c == '}' || c == ']') {
            if (!depth)
                return false;
            --depth;
        } else if (c == ',' || c == ':')
            ++tokens;
        if (tokens > 8192)
            return false;
    }
    return !quoted && !escaped && depth == 0;
}
} // namespace

Json::Json(const char *bytes, std::size_t size, std::size_t limit, unsigned max_depth,
           bool provider_text) {
    if (!bytes || !size || size > limit || max_depth > 12 ||
        !bounded_json({bytes, size}, max_depth))
        return;
    const char *end = nullptr;
    root_ = cJSON_ParseWithLengthOpts(bytes, size, &end, false);
    if (root_) {
        while (end < bytes + size && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
            ++end;
        unsigned count = 0;
        if (end != bytes + size || !clean_tree(root_, 0, count, max_depth, provider_text)) {
            cJSON_Delete(root_);
            root_ = nullptr;
        }
    }
}

Json::~Json() { cJSON_Delete(root_); }
} // namespace pace
