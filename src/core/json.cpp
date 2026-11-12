#include "json.h"
#include <cstdlib>
#include <cstring>

namespace mc {

static const Json& nullJson() { static Json j; return j; }
const Json& Json::operator[](const std::string& k) const { if (type != Object) return nullJson(); auto it = o.find(k); return it == o.end() ? nullJson() : it->second; }
const Json& Json::operator[](size_t i) const { return type == Array && i < a.size() ? a[i] : nullJson(); }

namespace {
struct Parser {
    const char* p; const char* end; std::string err;
    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
    bool fail(const char* m) { if (err.empty()) err = m; return false; }
    static void utf8(std::string& o, unsigned c) {
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
        else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 63)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
    }
    bool str(std::string& o) {
        if (p >= end || *p != '"') return fail("expected a string"); p++;
        while (p < end && *p != '"') {
            if (*p == '\\') {
                if (++p >= end) return fail("bad escape");
                char c = *p++;
                switch (c) {
                case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break; case 'b': o += '\b'; break; case 'f': o += '\f'; break;
                case 'u': {
                    if (end - p < 4) return fail("bad \\u");
                    unsigned v = (unsigned)std::strtoul(std::string(p, 4).c_str(), nullptr, 16); p += 4;
                    if (v >= 0xD800 && v < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') { unsigned lo = (unsigned)std::strtoul(std::string(p + 2, 4).c_str(), nullptr, 16); p += 6; v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00); }
                    utf8(o, v); break; }
                default: o += c;
                }
            } else o += *p++;
        }
        if (p >= end) return fail("unfinished string"); p++; return true;
    }
    bool val(Json& j, int depth) {
        if (depth > 200) return fail("too deep");
        ws(); if (p >= end) return fail("unexpected end");
        char c = *p;
        if (c == '{') {
            j.type = Json::Object; p++; ws();
            if (p < end && *p == '}') { p++; return true; }
            for (;;) {
                ws(); std::string k; if (!str(k)) return false; ws();
                if (p >= end || *p != ':') return fail("expected :"); p++;
                if (!val(j.o[k], depth + 1)) return false; ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; return true; }
                return fail("expected , or }");
            }
        }
        if (c == '[') {
            j.type = Json::Array; p++; ws();
            if (p < end && *p == ']') { p++; return true; }
            for (;;) {
                j.a.emplace_back(); if (!val(j.a.back(), depth + 1)) return false; ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; return true; }
                return fail("expected , or ]");
            }
        }
        if (c == '"') { j.type = Json::String; return str(j.s); }
        if (!std::strncmp(p, "true", std::min<size_t>(4, end - p)) && end - p >= 4) { j.type = Json::Bool; j.b = true; p += 4; return true; }
        if (!std::strncmp(p, "false", std::min<size_t>(5, end - p)) && end - p >= 5) { j.type = Json::Bool; j.b = false; p += 5; return true; }
        if (!std::strncmp(p, "null", std::min<size_t>(4, end - p)) && end - p >= 4) { j.type = Json::Null; p += 4; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* q = p; while (q < end && (std::strchr("+-.eE", *q) || (*q >= '0' && *q <= '9'))) q++;
            j.type = Json::Number; j.n = std::strtod(std::string(p, q).c_str(), nullptr); p = q; return true;
        }
        return fail("unexpected character");
    }
};
}

bool parseJson(const std::string& text, Json& out, std::string* error) {
    Parser ps{ text.data(), text.data() + text.size(), "" };
    out = Json{};
    bool ok = ps.val(out, 0);
    if (!ok && error) *error = ps.err + " at character " + std::to_string(ps.p - text.data());
    return ok;
}

std::vector<uint8_t> base64Decode(const std::string& s) {
    std::vector<uint8_t> out; unsigned buf = 0; int bits = 0;
    for (char c : s) {
        int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' || c == '-' ? 62 : c == '/' || c == '_' ? 63 : -1;
        if (c == '=') break;
        if (v < 0) continue;
        buf = (buf << 6) | (unsigned)v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((uint8_t)(buf >> bits)); }
    }
    return out;
}
std::string base64Encode(const std::vector<uint8_t>& d) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o; size_t i = 0;
    for (; i + 2 < d.size(); i += 3) { unsigned v = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2]; o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63]; }
    if (i < d.size()) { unsigned v = d[i] << 16; if (i + 1 < d.size()) v |= d[i + 1] << 8; o += t[v >> 18]; o += t[(v >> 12) & 63]; o += i + 1 < d.size() ? t[(v >> 6) & 63] : '='; o += '='; }
    return o;
}

} // namespace mc
