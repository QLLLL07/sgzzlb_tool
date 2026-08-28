// json.hpp - 极简 JSON DOM：解析 + 序列化，无第三方依赖。
// 支持 null/bool/number/string/array/object，UTF-8 直通，\uXXXX 与代理对。
#pragma once
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace jq {

class Json {
public:
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ };
    Json() : type_(NUL), b_(false), n_(0.0) {}
    Json(Type t) : type_(t), b_(false), n_(0.0) {}
    Json(bool b) : type_(BOOL), b_(b), n_(0.0) {}
    Json(double n) : type_(NUM), b_(false), n_(n) {}
    Json(const std::string& s) : type_(STR), b_(false), n_(0.0), s_(s) {}
    Json(const char* s) : type_(STR), b_(false), n_(0.0), s_(s ? s : "") {}

    static Json array() { return Json(ARR); }
    static Json object() { return Json(OBJ); }

    Type type() const { return type_; }
    bool isNull() const { return type_ == NUL; }
    bool isBool() const { return type_ == BOOL; }
    bool isNumber() const { return type_ == NUM; }
    bool isString() const { return type_ == STR; }
    bool isArray() const { return type_ == ARR; }
    bool isObject() const { return type_ == OBJ; }

    bool asBool(bool def = false) const { return isBool() ? b_ : def; }
    double asNumber(double def = 0.0) const { return isNumber() ? n_ : def; }
    int asInt(int def = 0) const { return isNumber() ? (int)n_ : def; }
    const std::string& asString() const { static const std::string empty; return isString() ? s_ : empty; }
    const std::string& asString(const std::string& def) const { return isString() ? s_ : def; }

    void setBool(bool v) { type_ = BOOL; b_ = v; }
    void setNumber(double v) { type_ = NUM; n_ = v; }
    void setString(const std::string& v) { type_ = STR; s_ = v; }

    // array 操作
    size_t size() const { return arr_.size(); }
    const Json& at(size_t i) const { return arr_.at(i); }
    const Json& operator[](size_t i) const { return arr_.at(i); }
    void push_back(const Json& v) { arr_.push_back(v); }

    // object 操作（内部用 vector 保证顺序，小对象够用）
    const Json& get(const std::string& key) const {
        static const Json null_;
        for (auto& kv : obj_)
            if (kv.first == key) return kv.second;
        return null_;
    }
    bool has(const std::string& key) const {
        for (auto& kv : obj_) if (kv.first == key) return true;
        return false;
    }
    void set(const std::string& key, const Json& v) {
        for (auto& kv : obj_)
            if (kv.first == key) { kv.second = v; return; }
        obj_.emplace_back(key, v);
    }
    // 便捷：若无 key 则插入默认
    void ensure(const std::string& key, const Json& v) { if (!has(key)) set(key, v); }

    // ---- 解析 ----
    static Json parse(const std::string& text) {
        Parser p(text);
        Json v = p.parseValue();
        p.skipWs();
        if (!p.eof()) throw std::runtime_error("json: trailing data");
        return v;
    }

    // ---- 序列化 ----
    std::string dump() const {
        std::string out;
        dumpTo(out);
        return out;
    }

private:
    Type type_;
    bool b_;
    double n_;
    std::string s_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;

    void dumpTo(std::string& out) const {
        switch (type_) {
            case NUL: out += "null"; break;
            case BOOL: out += b_ ? "true" : "false"; break;
            case NUM: {
                if (std::isnan(n_) || std::isinf(n_)) { out += "0"; break; }
                if (n_ == (double)(long long)n_ && std::fabs(n_) < 9e15) {
                    out += std::to_string((long long)n_);
                } else {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.6f", n_);
                    std::string s(buf);
                    while (s.size() && s.back() == '0') s.pop_back();
                    if (s.size() && s.back() == '.') s.pop_back();
                    out += s;
                }
                break;
            }
            case STR: out += '"'; escape(s_, out); out += '"'; break;
            case ARR: {
                out += '[';
                for (size_t i = 0; i < arr_.size(); i++) {
                    if (i) out += ',';
                    arr_[i].dumpTo(out);
                }
                out += ']';
                break;
            }
            case OBJ: {
                out += '{';
                for (size_t i = 0; i < obj_.size(); i++) {
                    if (i) out += ',';
                    out += '"'; escape(obj_[i].first, out); out += '"';
                    out += ':';
                    obj_[i].second.dumpTo(out);
                }
                out += '}';
                break;
            }
        }
    }

    static void escape(const std::string& s, std::string& out) {
        for (unsigned char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out += (char)c;
                    }
            }
        }
    }

    struct Parser {
        const std::string& t;
        size_t i = 0;
        Parser(const std::string& text) : t(text) {}
        bool eof() const { return i >= t.size(); }
        char cur() const { return i < t.size() ? t[i] : '\0'; }
        void skipWs() { while (i < t.size() && (cur()==' '||cur()=='\t'||cur()=='\n'||cur()=='\r')) i++; }
        [[noreturn]] void fail(const char* msg) {
            throw std::runtime_error(std::string(msg) + " at offset " + std::to_string(i));
        }
        bool eat(char c) {
            skipWs();
            if (cur() == c) { i++; return true; }
            return false;
        }
        void expect(char c) {
            if (!eat(c)) { std::string m = "json: expected '"; m += c; m += "'"; fail(m.c_str()); }
        }

        Json parseValue() {
            skipWs();
            char c = cur();
            if (c == '{') return parseObject();
            if (c == '[') return parseArray();
            if (c == '"') return Json(parseString());
            if (c == 't' || c == 'f') return Json(parseBool());
            if (c == 'n') { expectWord("null"); return Json(); }
            return Json(parseNumber());
        }
        Json parseObject() {
            expect('{');
            Json o(Json::OBJ);
            skipWs();
            if (cur() == '}') { i++; return o; }
            while (true) {
                skipWs();
                if (cur() != '"') fail("json: expected key");
                std::string k = parseString();
                expect(':');
                o.set(k, parseValue());
                skipWs();
                char c = cur();
                if (c == ',') { i++; continue; }
                if (c == '}') { i++; break; }
                fail("json: expected , or }");
            }
            return o;
        }
        Json parseArray() {
            expect('[');
            Json a(Json::ARR);
            skipWs();
            if (cur() == ']') { i++; return a; }
            while (true) {
                a.push_back(parseValue());
                skipWs();
                char c = cur();
                if (c == ',') { i++; continue; }
                if (c == ']') { i++; break; }
                fail("json: expected , or ]");
            }
            return a;
        }
        bool parseBool() {
            if (cur() == 't') { expectWord("true"); return true; }
            expectWord("false"); return false;
        }
        void expectWord(const char* w) {
            size_t n = 0;
            while (w[n]) {
                if (i + n >= t.size() || t[i + n] != w[n]) fail("json: bad literal");
                n++;
            }
            i += n;
        }
        double parseNumber() {
            size_t start = i;
            if (cur() == '-') i++;
            while (i < t.size() && (isdigit((unsigned char)cur()) || cur()=='.' || cur()=='e' || cur()=='E' || cur()=='+' || cur()=='-')) i++;
            std::string num = t.substr(start, i - start);
            if (num.empty()) fail("json: bad number");
            try {
                return std::stod(num);
            } catch (...) {
                fail("json: bad number");
            }
        }
        std::string parseString() {
            if (cur() != '"') fail("json: expected string");
            i++; // skip "
            std::string out;
            while (true) {
                if (i >= t.size()) fail("json: unterminated string");
                unsigned char c = (unsigned char)t[i];
                if (c == '"') { i++; break; }
                if (c == '\\') {
                    i++;
                    if (i >= t.size()) fail("json: bad escape");
                    char e = t[i++];
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
                            uint32_t cp = parseHex4();
                            if (cp >= 0xD800 && cp <= 0xDBFF) { // 高代理，取低代理
                                if (i + 1 < t.size() && t[i] == '\\' && t[i+1] == 'u') {
                                    i += 2;
                                    uint32_t lo = parseHex4();
                                    if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                }
                            }
                            appendUtf8(out, cp);
                            break;
                        }
                        default: fail("json: bad escape");
                    }
                } else {
                    out += (char)c;
                    i++;
                }
            }
            return out;
        }
        uint32_t parseHex4() {
            if (i + 4 > t.size()) fail("json: bad \\u");
            uint32_t v = 0;
            for (int k = 0; k < 4; k++) {
                char c = t[i++];
                v <<= 4;
                if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
                else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
                else fail("json: bad hex");
            }
            return v;
        }
        static void appendUtf8(std::string& out, uint32_t cp) {
            if (cp < 0x80) {
                out += (char)cp;
            } else if (cp < 0x800) {
                out += (char)(0xC0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            } else {
                out += (char)(0xF0 | (cp >> 18));
                out += (char)(0x80 | ((cp >> 12) & 0x3F));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
        }
    };
};

} // namespace jq
