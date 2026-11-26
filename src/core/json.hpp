// Minimal JSON value + parser + writer (C++17, no dependencies).
// Kept in-house so the Archipelago logic compiles and is unit-tested without Geode, and does not depend on
// which JSON library a given Geode version ships.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace apgd {

class Json {
public:
    enum class Type { Null, Bool, Int, Double, String, Array, Object };
    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;   // keeps key order

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : m_type(Type::Bool), m_bool(b) {}
    Json(int i) : m_type(Type::Int), m_int(i) {}
    Json(int64_t i) : m_type(Type::Int), m_int(i) {}
    Json(double d) : m_type(Type::Double), m_double(d) {}
    Json(const char* s) : m_type(Type::String), m_str(s) {}
    Json(std::string s) : m_type(Type::String), m_str(std::move(s)) {}
    Json(Array a) : m_type(Type::Array), m_arr(std::make_shared<Array>(std::move(a))) {}
    Json(Object o) : m_type(Type::Object), m_obj(std::make_shared<Object>(std::move(o))) {}

    static Json array() { return Json(Array{}); }
    static Json object() { return Json(Object{}); }

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }
    bool isBool() const { return m_type == Type::Bool; }
    bool isNumber() const { return m_type == Type::Int || m_type == Type::Double; }
    bool isString() const { return m_type == Type::String; }
    bool isArray() const { return m_type == Type::Array; }
    bool isObject() const { return m_type == Type::Object; }

    bool asBool(bool def = false) const {
        if (m_type == Type::Bool) return m_bool;
        if (isNumber()) return asInt() != 0;
        return def;
    }
    int64_t asInt(int64_t def = 0) const {
        if (m_type == Type::Int) return m_int;
        if (m_type == Type::Double) return static_cast<int64_t>(m_double);
        if (m_type == Type::Bool) return m_bool ? 1 : 0;
        return def;
    }
    double asDouble(double def = 0) const {
        if (m_type == Type::Double) return m_double;
        if (m_type == Type::Int) return static_cast<double>(m_int);
        return def;
    }
    std::string asString(const std::string& def = "") const { return m_type == Type::String ? m_str : def; }

    const Array& items() const { static const Array empty; return isArray() ? *m_arr : empty; }
    const Object& members() const { static const Object empty; return isObject() ? *m_obj : empty; }
    size_t size() const { return isArray() ? m_arr->size() : isObject() ? m_obj->size() : 0; }

    bool contains(const std::string& key) const {
        for (auto& kv : members()) if (kv.first == key) return true;
        return false;
    }
    const Json& operator[](const std::string& key) const {
        static const Json null;
        for (auto& kv : members()) if (kv.first == key) return kv.second;
        return null;
    }
    const Json& operator[](size_t i) const {
        static const Json null;
        return isArray() && i < m_arr->size() ? (*m_arr)[i] : null;
    }
    Json& set(const std::string& key, Json value) {
        if (!isObject()) { *this = object(); }
        unshare();
        for (auto& kv : *m_obj) if (kv.first == key) { kv.second = std::move(value); return *this; }
        m_obj->emplace_back(key, std::move(value));
        return *this;
    }
    Json& push(Json value) {
        if (!isArray()) { *this = array(); }
        unshare();
        m_arr->push_back(std::move(value));
        return *this;
    }

    std::string dump() const { std::string out; write(out); return out; }
    static Json parse(const std::string& text);         // throws std::runtime_error on bad input

private:
    void unshare() {
        if (m_arr && m_arr.use_count() > 1) m_arr = std::make_shared<Array>(*m_arr);
        if (m_obj && m_obj.use_count() > 1) m_obj = std::make_shared<Object>(*m_obj);
    }
    void write(std::string& out) const;

    Type m_type = Type::Null;
    bool m_bool = false;
    int64_t m_int = 0;
    double m_double = 0;
    std::string m_str;
    std::shared_ptr<Array> m_arr;
    std::shared_ptr<Object> m_obj;
};

}  // namespace apgd
