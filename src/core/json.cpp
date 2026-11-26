#include "json.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace apgd {
namespace {

void writeString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);          // UTF-8 passes through unchanged
                }
        }
    }
    out += '"';
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

struct Parser {
    const std::string& s;
    size_t i = 0;
    int depth = 0;

    [[noreturn]] void fail(const char* what) const {
        throw std::runtime_error(std::string("JSON: ") + what + " at " + std::to_string(i));
    }
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    bool eat(char c) { ws(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
    void expect(char c) { if (!eat(c)) fail("unexpected character"); }
    bool lit(const char* word) {
        size_t n = std::char_traits<char>::length(word);
        if (s.compare(i, n, word) == 0) { i += n; return true; }
        return false;
    }

    uint32_t hex4() {
        if (i + 4 > s.size()) fail("short \\u escape");
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else fail("bad \\u escape");
        }
        return v;
    }

    std::string str() {
        expect('"');
        std::string out;
        while (true) {
            if (i >= s.size()) fail("unterminated string");
            char c = s[i++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) fail("bad escape");
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        uint32_t lo = hex4();
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else { appendUtf8(out, 0xFFFD); cp = lo; }
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
    }

    Json number() {
        size_t start = i;
        if (s[i] == '-') ++i;
        bool isFloat = false;
        while (i < s.size()) {
            char c = s[i];
            if (c >= '0' && c <= '9') { ++i; continue; }
            if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') { isFloat = true; ++i; continue; }
            break;
        }
        std::string tok = s.substr(start, i - start);
        if (tok.empty() || tok == "-") fail("bad number");
        if (!isFloat) {
            errno = 0;
            char* end = nullptr;
            long long v = std::strtoll(tok.c_str(), &end, 10);
            if (errno == 0 && end && *end == 0) return Json(static_cast<int64_t>(v));
        }
        char* end = nullptr;
        double d = std::strtod(tok.c_str(), &end);
        if (!end || *end != 0) fail("bad number");
        return Json(d);
    }

    Json value() {
        if (++depth > 200) fail("nested too deeply");
        ws();
        if (i >= s.size()) fail("unexpected end");
        Json out;
        char c = s[i];
        if (c == '{') {
            ++i;
            out = Json::object();
            if (!eat('}')) {
                do {
                    ws();
                    std::string key = str();
                    expect(':');
                    out.set(key, value());
                } while (eat(','));
                expect('}');
            }
        } else if (c == '[') {
            ++i;
            out = Json::array();
            if (!eat(']')) {
                do { out.push(value()); } while (eat(','));
                expect(']');
            }
        } else if (c == '"') {
            out = Json(str());
        } else if (lit("true")) {
            out = Json(true);
        } else if (lit("false")) {
            out = Json(false);
        } else if (lit("null")) {
            out = Json();
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            out = number();
        } else {
            fail("unexpected character");
        }
        --depth;
        return out;
    }
};

}  // namespace

Json Json::parse(const std::string& text) {
    Parser p{text};
    Json v = p.value();
    p.ws();
    if (p.i != text.size()) p.fail("trailing characters");
    return v;
}

void Json::write(std::string& out) const {
    switch (m_type) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += m_bool ? "true" : "false"; break;
        case Type::Int: out += std::to_string(m_int); break;
        case Type::Double: {
            if (!std::isfinite(m_double)) { out += "null"; break; }
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.17g", m_double);
            out += buf;
            break;
        }
        case Type::String: writeString(out, m_str); break;
        case Type::Array: {
            out += '[';
            bool first = true;
            for (auto& v : *m_arr) { if (!first) out += ','; first = false; v.write(out); }
            out += ']';
            break;
        }
        case Type::Object: {
            out += '{';
            bool first = true;
            for (auto& kv : *m_obj) {
                if (!first) out += ',';
                first = false;
                writeString(out, kv.first);
                out += ':';
                kv.second.write(out);
            }
            out += '}';
            break;
        }
    }
}

}  // namespace apgd
