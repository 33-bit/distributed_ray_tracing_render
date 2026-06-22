#pragma once
// Minimal recursive-descent JSON parser.  STL-only, no external deps.
// Produces a tree of JsonValue nodes (object/array/string/number/bool/null).
// Only handles the JSON subset needed by scene config files.

#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <cctype>
#include <cstdlib>

enum class JsonType { Null, Bool, Number, String, Array, Object };

struct JsonValue {
    JsonType type = JsonType::Null;
    double   num  = 0.0;
    bool     bval = false;
    std::string str;
    std::vector<JsonValue> arr;
    std::map<std::string, JsonValue> obj;

    // Convenience accessors ------------------------------------------------
    const JsonValue& operator[](const std::string& key) const {
        auto it = obj.find(key);
        if (it == obj.end())
            throw std::runtime_error("json: missing key '" + key + "'");
        return it->second;
    }
    const JsonValue& operator[](size_t i) const { return arr.at(i); }
    bool has(const std::string& key) const { return obj.count(key) > 0; }
    double as_num() const { return num; }
    const std::string& as_str() const { return str; }
    bool as_bool() const { return bval; }
    size_t size() const { return (type == JsonType::Array) ? arr.size() : obj.size(); }
};

// --------------------------------------------------------------------------
// Parser implementation
// --------------------------------------------------------------------------
class JsonParser {
public:
    static JsonValue parse(const std::string& text) {
        JsonParser p(text);
        p.skip_ws();
        JsonValue v = p.parse_value();
        p.skip_ws();
        if (p.pos_ < p.src_.size())
            throw std::runtime_error("json: trailing characters");
        return v;
    }

private:
    const std::string& src_;
    size_t pos_ = 0;

    explicit JsonParser(const std::string& s) : src_(s) {}

    char peek() const { return (pos_ < src_.size()) ? src_[pos_] : '\0'; }
    char next() {
        if (pos_ >= src_.size()) throw std::runtime_error("json: unexpected end");
        return src_[pos_++];
    }
    void skip_ws() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++pos_; continue; }
            if (c == '/' && pos_ + 1 < src_.size() && src_[pos_+1] == '/') {
                // line comment (non-standard but handy for config files)
                pos_ += 2;
                while (pos_ < src_.size() && src_[pos_] != '\n') ++pos_;
                continue;
            }
            break;
        }
    }
    void expect(char c) {
        skip_ws();
        char got = next();
        if (got != c)
            throw std::runtime_error(std::string("json: expected '") + c + "' got '" + got + "'");
    }

    JsonValue parse_value() {
        skip_ws();
        char c = peek();
        if (c == '{')  return parse_object();
        if (c == '[')  return parse_array();
        if (c == '"')  return parse_string_value();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
            return parse_number();
        throw std::runtime_error(std::string("json: unexpected char '") + c + "'");
    }

    JsonValue parse_object() {
        JsonValue v; v.type = JsonType::Object;
        next(); // '{'
        skip_ws();
        if (peek() == '}') { next(); return v; }
        for (;;) {
            skip_ws();
            std::string key = parse_string_raw();
            expect(':');
            skip_ws();
            v.obj[key] = parse_value();
            skip_ws();
            if (peek() == ',') { next(); continue; }
            break;
        }
        expect('}');
        return v;
    }

    JsonValue parse_array() {
        JsonValue v; v.type = JsonType::Array;
        next(); // '['
        skip_ws();
        if (peek() == ']') { next(); return v; }
        for (;;) {
            skip_ws();
            v.arr.push_back(parse_value());
            skip_ws();
            if (peek() == ',') { next(); continue; }
            break;
        }
        expect(']');
        return v;
    }

    std::string parse_string_raw() {
        expect('"');
        std::string s;
        for (;;) {
            char c = next();
            if (c == '"') break;
            if (c == '\\') {
                char e = next();
                switch (e) {
                    case '"': case '\\': case '/': s += e; break;
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    default: s += e; break;
                }
            } else {
                s += c;
            }
        }
        return s;
    }

    JsonValue parse_string_value() {
        JsonValue v; v.type = JsonType::String;
        v.str = parse_string_raw();
        return v;
    }

    JsonValue parse_number() {
        JsonValue v; v.type = JsonType::Number;
        size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        if (pos_ < src_.size() && src_[pos_] == '.') {
            ++pos_;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        }
        v.num = std::strtod(src_.c_str() + start, nullptr);
        return v;
    }

    JsonValue parse_bool() {
        JsonValue v; v.type = JsonType::Bool;
        if (src_.compare(pos_, 4, "true") == 0) { pos_ += 4; v.bval = true; }
        else if (src_.compare(pos_, 5, "false") == 0) { pos_ += 5; v.bval = false; }
        else throw std::runtime_error("json: invalid bool");
        return v;
    }

    JsonValue parse_null() {
        if (src_.compare(pos_, 4, "null") == 0) { pos_ += 4; return JsonValue{}; }
        throw std::runtime_error("json: invalid null");
    }
};
