/*
json.hpp — a small JSON value, parser and writer.

Used for the one door into the engine (JSON in, JSON out) and for the resources
and state files. Hand-written so the core has no dependencies and builds
unchanged for macOS and WebAssembly, where exceptions are turned off. Parse
failures are reported through the `error` out-parameter.
*/
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std;

namespace interviewprep {

class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };
  using Member = pair<string, Json>;

  Json() = default;
  Json(bool b) : type_(Type::Bool), bool_(b) {}
  Json(double n) : type_(Type::Number), num_(n) {}
  Json(int n) : type_(Type::Number), num_(n) {}
  Json(const char* s) : type_(Type::String), str_(s) {}
  Json(string s) : type_(Type::String), str_(move(s)) {}

  static Json array() { Json j; j.type_ = Type::Array; return j; }
  static Json object() { Json j; j.type_ = Type::Object; return j; }

  // Returns Null and fills `error` when `text` is not valid JSON.
  static Json parse(string_view text, string* error = nullptr);
  string dump() const;

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }
  bool isString() const { return type_ == Type::String; }
  bool isNumber() const { return type_ == Type::Number; }

  /*
  Readers. Each returns `fallback` on a type mismatch, so a missing or
  malformed field in a file degrades to a default instead of crashing.
  */
  bool asBool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
  double asNum(double fallback = 0) const { return type_ == Type::Number ? num_ : fallback; }
  int asInt(int fallback = 0) const { return type_ == Type::Number ? static_cast<int>(num_) : fallback; }
  const string& asStr() const { static const string empty; return type_ == Type::String ? str_ : empty; }
  string asStr(const char* fallback) const { return type_ == Type::String ? str_ : fallback; }

  // Arrays.
  const vector<Json>& items() const { static const vector<Json> empty; return type_ == Type::Array ? arr_ : empty; }
  void push(Json v) { arr_.push_back(move(v)); }
  size_t size() const { return type_ == Type::Array ? arr_.size() : type_ == Type::Object ? obj_.size() : 0; }

  /*
  Objects. Insertion order is kept. Lookup is linear, which is fine for the
  small objects used here.
  */
  const vector<Member>& members() const { static const vector<Member> empty; return type_ == Type::Object ? obj_ : empty; }
  const Json& operator[](const char* key) const;
  bool has(const char* key) const;
  Json& set(const string& key, Json v);  // returns *this so calls chain

 private:
  Type type_ = Type::Null;
  bool bool_ = false;
  double num_ = 0;
  string str_;
  vector<Json> arr_;
  vector<Member> obj_;

  void dumpTo(string& out) const;
  friend class JsonParser;
};

}  // namespace interviewprep
