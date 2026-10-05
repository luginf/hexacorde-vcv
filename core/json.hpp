// Mini JSON (analyse et écriture), sans dépendance : suffit au format de setup d'Hexacorde.
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace hexa {

struct Json {
  enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<Json> arr;
  std::vector<std::pair<std::string, Json>> obj;   // l'ordre des clés est conservé

  static Json number(double v) { Json j; j.type = Num; j.num = v; return j; }
  static Json boolean(bool v) { Json j; j.type = Bool; j.b = v; return j; }
  static Json string(const std::string& s) { Json j; j.type = Str; j.str = s; return j; }
  static Json array() { Json j; j.type = Arr; return j; }
  static Json object() { Json j; j.type = Obj; return j; }

  bool isNull() const { return type == Null; }
  bool isNum() const { return type == Num; }
  bool isInt() const { return type == Num && std::floor(num) == num && std::isfinite(num); }
  bool isStr() const { return type == Str; }
  bool isArr() const { return type == Arr; }
  bool isObj() const { return type == Obj; }

  // objet : clé absente -> pointeur nul
  const Json* get(const std::string& k) const {
    if (type != Obj) return nullptr;
    for (const auto& kv : obj) if (kv.first == k) return &kv.second;
    return nullptr;
  }
  Json& set(const std::string& k, Json v) { obj.emplace_back(k, std::move(v)); return obj.back().second; }
  void push(Json v) { arr.push_back(std::move(v)); }

  // ---- écriture (compacte) ----
  static void quote(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
      if (c == '"') out += "\\\"";
      else if (c == '\\') out += "\\\\";
      else if (c == '\n') out += "\\n";
      else if (c == '\r') out += "\\r";
      else if (c == '\t') out += "\\t";
      else if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
      else out += static_cast<char>(c);
    }
    out += '"';
  }
  static void writeNum(double v, std::string& out) {
    char buf[40];
    if (std::floor(v) == v && std::fabs(v) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", v);
    else std::snprintf(buf, sizeof buf, "%.10g", v);
    out += buf;
  }
  void write(std::string& out) const {
    switch (type) {
      case Null: out += "null"; break;
      case Bool: out += b ? "true" : "false"; break;
      case Num: writeNum(num, out); break;
      case Str: quote(str, out); break;
      case Arr:
        out += '[';
        for (size_t i = 0; i < arr.size(); i++) { if (i) out += ','; arr[i].write(out); }
        out += ']'; break;
      case Obj:
        out += '{';
        for (size_t i = 0; i < obj.size(); i++) { if (i) out += ','; quote(obj[i].first, out); out += ':'; obj[i].second.write(out); }
        out += '}'; break;
    }
  }
  std::string dump() const { std::string s; write(s); return s; }

  // ---- analyse ----
  static bool parse(const std::string& text, Json& out, std::string* err = nullptr) {
    Parser p{text, 0, ""};
    p.ws();
    if (!p.value(out)) { if (err) *err = p.error; return false; }
    p.ws();
    if (p.i != text.size()) { if (err) *err = "trailing characters"; return false; }
    return true;
  }

 private:
  struct Parser {
    const std::string& s;
    size_t i;
    std::string error;
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++; }
    bool fail(const char* m) { error = m; return false; }
    bool lit(const char* w) {
      size_t n = std::char_traits<char>::length(w);
      if (s.compare(i, n, w) == 0) { i += n; return true; }
      return false;
    }
    static void utf8(unsigned cp, std::string& o) {
      if (cp < 0x80) o += static_cast<char>(cp);
      else if (cp < 0x800) { o += static_cast<char>(0xc0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3f)); }
      else if (cp < 0x10000) { o += static_cast<char>(0xe0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3f)); o += static_cast<char>(0x80 | (cp & 0x3f)); }
      else { o += static_cast<char>(0xf0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 0x3f)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3f)); o += static_cast<char>(0x80 | (cp & 0x3f)); }
    }
    bool hex4(unsigned& v) {
      if (i + 4 > s.size()) return false;
      v = 0;
      for (int k = 0; k < 4; k++) {
        char c = s[i++]; v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0'; else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10; else return false;
      }
      return true;
    }
    bool str(std::string& o) {
      if (s[i] != '"') return fail("string expected");
      i++;
      while (i < s.size() && s[i] != '"') {
        char c = s[i++];
        if (c != '\\') { o += c; continue; }
        if (i >= s.size()) return fail("bad escape");
        char e = s[i++];
        switch (e) {
          case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break;
          case 'b': o += '\b'; break; case 'f': o += '\f'; break;
          case 'u': { unsigned v; if (!hex4(v)) return fail("bad \\u"); utf8(v, o); break; }
          default: o += e;
        }
      }
      if (i >= s.size()) return fail("unterminated string");
      i++;
      return true;
    }
    bool value(Json& j) {
      if (i >= s.size()) return fail("unexpected end");
      char c = s[i];
      if (c == '{') {
        i++; j = Json::object(); ws();
        if (i < s.size() && s[i] == '}') { i++; return true; }
        for (;;) {
          ws(); std::string k; if (!str(k)) return false;
          ws(); if (i >= s.size() || s[i] != ':') return fail("':' expected"); i++; ws();
          Json v; if (!value(v)) return false;
          j.obj.emplace_back(std::move(k), std::move(v));
          ws();
          if (i < s.size() && s[i] == ',') { i++; continue; }
          if (i < s.size() && s[i] == '}') { i++; return true; }
          return fail("',' or '}' expected");
        }
      }
      if (c == '[') {
        i++; j = Json::array(); ws();
        if (i < s.size() && s[i] == ']') { i++; return true; }
        for (;;) {
          ws(); Json v; if (!value(v)) return false;
          j.arr.push_back(std::move(v)); ws();
          if (i < s.size() && s[i] == ',') { i++; continue; }
          if (i < s.size() && s[i] == ']') { i++; return true; }
          return fail("',' or ']' expected");
        }
      }
      if (c == '"') { j = Json::string(""); return str(j.str); }
      if (lit("true")) { j = Json::boolean(true); return true; }
      if (lit("false")) { j = Json::boolean(false); return true; }
      if (lit("null")) { j = Json(); return true; }
      if (c == '-' || (c >= '0' && c <= '9')) {
        char* end = nullptr;
        double v = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) return fail("bad number");
        i = static_cast<size_t>(end - s.c_str());
        j = Json::number(v); return true;
      }
      return fail("unexpected character");
    }
  };
};

}  // namespace hexa
