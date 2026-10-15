#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// A small JSON value, parser and writer.
//
// Qt has QJsonDocument, but the cloud import (CSV rows and API replies turned
// into documents, then into nights and days) is worth testing with plain g++,
// and a host without Qt headers cannot run Qt code. So the data model is Qt-free
// and this is its JSON. It is deliberately forgiving about what it accepts and
// strict about what it writes.
namespace Garmin {

class Json
{
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    static Json boolean(bool v);
    static Json number(double v);
    static Json string(std::string v);
    static Json array();
    static Json object();

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }
    bool isObject() const { return m_type == Type::Object; }
    bool isArray() const { return m_type == Type::Array; }
    bool isString() const { return m_type == Type::String; }

    // Member of an object, or nullptr. Insertion order is kept.
    const Json *get(const std::string &key) const;
    void set(const std::string &key, Json value);
    void push(Json value);

    const std::vector<Json> &items() const { return m_array; }
    const std::vector<std::pair<std::string, Json>> &members() const { return m_object; }

    // Numeric value; accepts a number or a string that holds one, because the
    // CSV export carries every cell as text. `fallback` otherwise.
    double asNumber(double fallback = 0) const;
    bool hasNumber() const;
    std::string asString(const std::string &fallback = std::string()) const;

    // Convenience on objects: member as number / string / presence of a number.
    double num(const std::string &key, double fallback = 0) const;
    std::optional<double> optNum(const std::string &key) const;
    std::string str(const std::string &key, const std::string &fallback = std::string()) const;

    std::string dump() const;

    // Parses `text`. On failure returns nullopt and, if given, the byte offset.
    static std::optional<Json> parse(const std::string &text, size_t *errorAt = nullptr);

private:
    void dumpTo(std::string &out) const;

    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0;
    std::string m_string;
    std::vector<Json> m_array;
    std::vector<std::pair<std::string, Json>> m_object;
};

} // namespace Garmin
