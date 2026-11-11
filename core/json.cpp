#include "json.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace std;

namespace interviewprep {

const Json& Json::operator[](const char* key) const {
  static const Json null;
  if (type_ != Type::Object) return null;
  for (const auto& m : obj_) {
    if (m.first == key) return m.second;
  }
  return null;
}

bool Json::has(const char* key) const {
  if (type_ != Type::Object) return false;
  for (const auto& m : obj_) {
    if (m.first == key) return true;
  }
  return false;
}

Json& Json::set(const string& key, Json v) {
  type_ = Type::Object;
  for (auto& m : obj_) {
    if (m.first == key) {
      m.second = move(v);
      return *this;
    }
  }
  obj_.emplace_back(key, move(v));
  return *this;
}

// ── Parser ────────────────────────────────────────────────────────────────

class JsonParser {
 public:
  explicit JsonParser(string_view s) : s_(s) {}

  Json run(string* error) {
    Json v = value(0);
    space();
    if (ok_ && i_ != s_.size()) fail("unexpected text after the JSON value");
    if (!ok_) {
      if (error) *error = err_ + " at byte " + to_string(i_);
      return Json();
    }
    return v;
  }

 private:
  static constexpr int kMaxDepth = 64;

  string_view s_;
  size_t i_ = 0;
  bool ok_ = true;
  string err_;

  void fail(const char* msg) {
    if (ok_) { ok_ = false; err_ = msg; }
  }

  void space() {
    while (i_ < s_.size()) {
      char c = s_[i_];
      if (c == ' ' || c == '\n' || c == '\t' || c == '\r') ++i_;
      else break;
    }
  }

  bool eat(char c) {
    if (i_ < s_.size() && s_[i_] == c) { ++i_; return true; }
    return false;
  }

  bool word(const char* w) {
    size_t n = strlen(w);
    if (s_.substr(i_, n) == w) { i_ += n; return true; }
    return false;
  }

  Json value(int depth) {
    if (depth > kMaxDepth) { fail("JSON is nested too deeply"); return Json(); }
    space();
    if (i_ >= s_.size()) { fail("unexpected end of input"); return Json(); }
    switch (s_[i_]) {
      case '{': return object(depth);
      case '[': return array(depth);
      case '"': { Json j; j.type_ = Json::Type::String; j.str_ = quotedString(); return j; }
      case 't': if (word("true")) return Json(true); break;
      case 'f': if (word("false")) return Json(false); break;
      case 'n': if (word("null")) return Json(); break;
      default: return number();
    }
    fail("invalid literal");
    return Json();
  }

  Json object(int depth) {
    Json j = Json::object();
    ++i_;  // {
    space();
    if (eat('}')) return j;
    while (ok_) {
      space();
      if (i_ >= s_.size() || s_[i_] != '"') { fail("expected a string key"); break; }
      string key = quotedString();
      space();
      if (!eat(':')) { fail("expected ':'"); break; }
      Json v = value(depth + 1);
      j.obj_.emplace_back(move(key), move(v));
      space();
      if (eat('}')) break;
      if (!eat(',')) { fail("expected ',' or '}'"); break; }
    }
    return j;
  }

  Json array(int depth) {
    Json j = Json::array();
    ++i_;  // [
    space();
    if (eat(']')) return j;
    while (ok_) {
      j.arr_.push_back(value(depth + 1));
      space();
      if (eat(']')) break;
      if (!eat(',')) { fail("expected ',' or ']'"); break; }
    }
    return j;
  }

  static void appendUtf8(string& out, unsigned cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
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

  bool hex4(unsigned& out) {
    if (i_ + 4 > s_.size()) return false;
    out = 0;
    for (int k = 0; k < 4; ++k) {
      char c = s_[i_ + k];
      out <<= 4;
      if (c >= '0' && c <= '9') out |= c - '0';
      else if (c >= 'a' && c <= 'f') out |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') out |= c - 'A' + 10;
      else return false;
    }
    i_ += 4;
    return true;
  }

  string quotedString() {
    string out;
    ++i_;  // opening quote
    while (i_ < s_.size()) {
      char c = s_[i_++];
      if (c == '"') return out;
      if (c != '\\') { out += c; continue; }
      if (i_ >= s_.size()) break;
      char e = s_[i_++];
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
          unsigned cp = 0;
          if (!hex4(cp)) { fail("bad \\u escape"); return out; }
          if (cp >= 0xD800 && cp <= 0xDBFF && word("\\u")) {  // surrogate pair
            unsigned lo = 0;
            if (!hex4(lo)) { fail("bad \\u escape"); return out; }
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          appendUtf8(out, cp);
          break;
        }
        default: fail("bad escape in string"); return out;
      }
    }
    fail("unterminated string");
    return out;
  }

  Json number() {
    size_t start = i_;
    if (i_ < s_.size() && s_[i_] == '-') ++i_;
    while (i_ < s_.size()) {
      char c = s_[i_];
      if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') ++i_;
      else break;
    }
    if (i_ == start) { fail("invalid value"); return Json(); }
    string token(s_.substr(start, i_ - start));
    char* end = nullptr;
    double d = strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size()) { fail("invalid number"); return Json(); }
    return Json(d);
  }
};

Json Json::parse(string_view text, string* error) {
  return JsonParser(text).run(error);
}

// ── Writer ────────────────────────────────────────────────────────────────

static void writeString(string& out, const string& s) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

void Json::dumpTo(string& out) const {
  switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: {
      if (!isfinite(num_)) { out += "null"; break; }
      char buf[32];
      if (num_ == floor(num_) && fabs(num_) < 1e15) {
        snprintf(buf, sizeof buf, "%.0f", num_);
      } else {
        snprintf(buf, sizeof buf, "%.6g", num_);
      }
      out += buf;
      break;
    }
    case Type::String: writeString(out, str_); break;
    case Type::Array: {
      out += '[';
      bool first = true;
      for (const auto& v : arr_) {
        if (!first) out += ',';
        first = false;
        v.dumpTo(out);
      }
      out += ']';
      break;
    }
    case Type::Object: {
      out += '{';
      bool first = true;
      for (const auto& m : obj_) {
        if (!first) out += ',';
        first = false;
        writeString(out, m.first);
        out += ':';
        m.second.dumpTo(out);
      }
      out += '}';
      break;
    }
  }
}

string Json::dump() const {
  string out;
  dumpTo(out);
  return out;
}

}  // namespace interviewprep
