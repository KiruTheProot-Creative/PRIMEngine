#pragma once
// json.h : a small JSON reader (for Blockbench and glTF model files).
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mc {

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false; double n = 0; std::string s;
    std::vector<Json> a; std::map<std::string, Json> o;
    const Json& operator[](const std::string& k) const;      // missing key: a null value
    const Json& operator[](size_t i) const;                  // out of range: a null value
    bool has(const std::string& k) const { return type == Object && o.count(k); }
    size_t size() const { return type == Array ? a.size() : type == Object ? o.size() : 0; }
    double num(double def = 0) const { return type == Number ? n : type == Bool ? (b ? 1 : 0) : def; }
    const std::string& str() const { return s; }
    bool isNull() const { return type == Null; }
};
// false (and a message) when the text is not valid JSON
bool parseJson(const std::string& text, Json& out, std::string* error = nullptr);

std::vector<uint8_t> base64Decode(const std::string& s);   // skips anything that is not base64 (line breaks ...)
std::string base64Encode(const std::vector<uint8_t>& d);

} // namespace mc
