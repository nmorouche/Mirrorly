// Mirrorly - Apple binary property list (bplist00) reader / writer
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mn {

class Plist;
using PlistPtr = std::shared_ptr<Plist>;

class Plist {
public:
    enum class Type { Null, Bool, Int, Real, String, Data, Array, Dict, Date, Uid };

    Type type = Type::Null;
    bool b = false;
    uint64_t i = 0;  // raw 64-bit value (signed values are stored two's complement)
    double r = 0;
    std::string s;
    std::vector<uint8_t> data;
    std::vector<PlistPtr> array;
    std::vector<std::pair<std::string, PlistPtr>> dict;  // insertion ordered

    // constructors
    static PlistPtr makeDict() { auto p = std::make_shared<Plist>(); p->type = Type::Dict; return p; }
    static PlistPtr makeArray() { auto p = std::make_shared<Plist>(); p->type = Type::Array; return p; }
    static PlistPtr makeString(const std::string& v) { auto p = std::make_shared<Plist>(); p->type = Type::String; p->s = v; return p; }
    static PlistPtr makeInt(uint64_t v) { auto p = std::make_shared<Plist>(); p->type = Type::Int; p->i = v; return p; }
    static PlistPtr makeReal(double v) { auto p = std::make_shared<Plist>(); p->type = Type::Real; p->r = v; return p; }
    static PlistPtr makeBool(bool v) { auto p = std::make_shared<Plist>(); p->type = Type::Bool; p->b = v; return p; }
    static PlistPtr makeData(const std::vector<uint8_t>& v) { auto p = std::make_shared<Plist>(); p->type = Type::Data; p->data = v; return p; }
    static PlistPtr makeData(const uint8_t* d, size_t n) { return makeData(std::vector<uint8_t>(d, d + n)); }

    // dict helpers
    Plist& set(const std::string& key, PlistPtr v) {
        for (auto& kv : dict)
            if (kv.first == key) { kv.second = v; return *this; }
        dict.emplace_back(key, v);
        return *this;
    }
    PlistPtr get(const std::string& key) const {
        for (auto& kv : dict)
            if (kv.first == key) return kv.second;
        return nullptr;
    }
    Plist& append(PlistPtr v) { array.push_back(v); return *this; }

    bool isDict() const { return type == Type::Dict; }
    bool isArray() const { return type == Type::Array; }
    bool isData() const { return type == Type::Data; }
    bool isString() const { return type == Type::String; }

    // convenience typed getters on dict entries
    std::string getString(const std::string& key, const std::string& def = "") const;
    uint64_t getUInt(const std::string& key, uint64_t def = 0) const;
    bool getBool(const std::string& key, bool def = false) const;
    const std::vector<uint8_t>* getData(const std::string& key) const;

    std::string toDebugString(int indent = 0) const;
};

// Parses a bplist00 buffer. Returns nullptr on failure.
PlistPtr plist_parse_binary(const uint8_t* data, size_t len);
// Serializes to bplist00.
std::vector<uint8_t> plist_write_binary(const PlistPtr& root);

}  // namespace mn
