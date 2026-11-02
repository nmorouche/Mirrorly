// Mirrorly - Apple binary property list (bplist00) reader / writer
#include "plist.h"

#include <cstring>
#include <functional>
#include <sstream>

namespace mn {

std::string Plist::getString(const std::string& key, const std::string& def) const {
    auto v = get(key);
    return (v && v->type == Type::String) ? v->s : def;
}

uint64_t Plist::getUInt(const std::string& key, uint64_t def) const {
    auto v = get(key);
    if (!v) return def;
    if (v->type == Type::Int) return v->i;
    if (v->type == Type::Real) return (uint64_t)v->r;
    if (v->type == Type::Bool) return v->b ? 1 : 0;
    return def;
}

bool Plist::getBool(const std::string& key, bool def) const {
    auto v = get(key);
    if (!v) return def;
    if (v->type == Type::Bool) return v->b;
    if (v->type == Type::Int) return v->i != 0;
    return def;
}

const std::vector<uint8_t>* Plist::getData(const std::string& key) const {
    auto v = get(key);
    return (v && v->type == Type::Data) ? &v->data : nullptr;
}

std::string Plist::toDebugString(int indent) const {
    std::ostringstream o;
    std::string pad(indent * 2, ' ');
    switch (type) {
    case Type::Null: o << "null"; break;
    case Type::Bool: o << (b ? "true" : "false"); break;
    case Type::Int: o << i; break;
    case Type::Real: o << r; break;
    case Type::String: o << '"' << s << '"'; break;
    case Type::Date: o << "date(" << r << ")"; break;
    case Type::Uid: o << "uid(" << i << ")"; break;
    case Type::Data: o << "<" << data.size() << " bytes>"; break;
    case Type::Array:
        o << "[\n";
        for (auto& e : array) o << pad << "  " << (e ? e->toDebugString(indent + 1) : "nil") << "\n";
        o << pad << "]";
        break;
    case Type::Dict:
        o << "{\n";
        for (auto& kv : dict) o << pad << "  " << kv.first << ": " << (kv.second ? kv.second->toDebugString(indent + 1) : "nil") << "\n";
        o << pad << "}";
        break;
    }
    return o.str();
}

// ---------------------------------------------------------------- parsing

namespace {

struct Reader {
    const uint8_t* d;
    size_t n;
    int offsetSize, refSize;
    uint64_t numObjects, topObject, offsetTableOffset;
    int depth = 0;

    uint64_t readBE(size_t pos, int size) const {
        uint64_t v = 0;
        for (int k = 0; k < size; k++) v = (v << 8) | d[pos + k];
        return v;
    }

    bool objectOffset(uint64_t idx, size_t& out) const {
        if (idx >= numObjects) return false;
        size_t p = (size_t)(offsetTableOffset + idx * offsetSize);
        if (p + offsetSize > n) return false;
        out = (size_t)readBE(p, offsetSize);
        return out < n;
    }

    // reads the length for a marker with low nibble; advances pos
    bool readLength(size_t& pos, uint8_t low, uint64_t& len) const {
        if (low != 0xF) { len = low; return true; }
        if (pos >= n) return false;
        uint8_t m = d[pos++];
        if ((m & 0xF0) != 0x10) return false;
        int sz = 1 << (m & 0x0F);
        if (pos + sz > n) return false;
        len = readBE(pos, sz > 8 ? 8 : sz);
        if (sz > 8) len = readBE(pos + sz - 8, 8);
        pos += sz;
        return true;
    }

    PlistPtr parse(uint64_t idx) {
        if (++depth > 64) return nullptr;
        PlistPtr r = parseInner(idx);
        depth--;
        return r;
    }

    PlistPtr parseInner(uint64_t idx) {
        size_t pos;
        if (!objectOffset(idx, pos)) return nullptr;
        uint8_t marker = d[pos++];
        uint8_t hi = marker >> 4, lo = marker & 0x0F;
        auto p = std::make_shared<Plist>();
        switch (hi) {
        case 0x0:
            if (marker == 0x08) { p->type = Plist::Type::Bool; p->b = false; }
            else if (marker == 0x09) { p->type = Plist::Type::Bool; p->b = true; }
            else p->type = Plist::Type::Null;
            return p;
        case 0x1: {
            int sz = 1 << lo;
            if (pos + sz > n) return nullptr;
            p->type = Plist::Type::Int;
            if (sz <= 8) p->i = readBE(pos, sz);
            else p->i = readBE(pos + sz - 8, 8);  // 16-byte ints: take low 64 bits
            return p;
        }
        case 0x2: {
            int sz = 1 << lo;
            if (pos + sz > n) return nullptr;
            p->type = Plist::Type::Real;
            if (sz == 4) {
                uint32_t v = (uint32_t)readBE(pos, 4);
                float f;
                memcpy(&f, &v, 4);
                p->r = f;
            } else if (sz == 8) {
                uint64_t v = readBE(pos, 8);
                memcpy(&p->r, &v, 8);
            }
            return p;
        }
        case 0x3: {
            if (pos + 8 > n) return nullptr;
            uint64_t v = readBE(pos, 8);
            p->type = Plist::Type::Date;
            memcpy(&p->r, &v, 8);
            return p;
        }
        case 0x4: {
            uint64_t len;
            if (!readLength(pos, lo, len) || pos + len > n) return nullptr;
            p->type = Plist::Type::Data;
            p->data.assign(d + pos, d + pos + len);
            return p;
        }
        case 0x5: {
            uint64_t len;
            if (!readLength(pos, lo, len) || pos + len > n) return nullptr;
            p->type = Plist::Type::String;
            p->s.assign((const char*)d + pos, (size_t)len);
            return p;
        }
        case 0x6: {  // UTF-16BE -> UTF-8
            uint64_t len;
            if (!readLength(pos, lo, len) || pos + len * 2 > n) return nullptr;
            p->type = Plist::Type::String;
            for (uint64_t k = 0; k < len; k++) {
                uint32_t c = (uint32_t)readBE(pos + k * 2, 2);
                if (c >= 0xD800 && c <= 0xDBFF && k + 1 < len) {
                    uint32_t c2 = (uint32_t)readBE(pos + (k + 1) * 2, 2);
                    if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
                        c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
                        k++;
                    }
                }
                std::string& s = p->s;
                if (c < 0x80) s.push_back((char)c);
                else if (c < 0x800) { s.push_back((char)(0xC0 | (c >> 6))); s.push_back((char)(0x80 | (c & 0x3F))); }
                else if (c < 0x10000) { s.push_back((char)(0xE0 | (c >> 12))); s.push_back((char)(0x80 | ((c >> 6) & 0x3F))); s.push_back((char)(0x80 | (c & 0x3F))); }
                else { s.push_back((char)(0xF0 | (c >> 18))); s.push_back((char)(0x80 | ((c >> 12) & 0x3F))); s.push_back((char)(0x80 | ((c >> 6) & 0x3F))); s.push_back((char)(0x80 | (c & 0x3F))); }
            }
            return p;
        }
        case 0x8: {
            int sz = lo + 1;
            if (pos + sz > n) return nullptr;
            p->type = Plist::Type::Uid;
            p->i = readBE(pos, sz > 8 ? 8 : sz);
            return p;
        }
        case 0xA: {
            uint64_t len;
            if (!readLength(pos, lo, len) || pos + len * refSize > n) return nullptr;
            p->type = Plist::Type::Array;
            for (uint64_t k = 0; k < len; k++) {
                auto e = parse(readBE(pos + k * refSize, refSize));
                if (!e) return nullptr;
                p->array.push_back(e);
            }
            return p;
        }
        case 0xD: {
            uint64_t len;
            if (!readLength(pos, lo, len) || pos + len * 2 * refSize > n) return nullptr;
            p->type = Plist::Type::Dict;
            for (uint64_t k = 0; k < len; k++) {
                auto key = parse(readBE(pos + k * refSize, refSize));
                auto val = parse(readBE(pos + (len + k) * refSize, refSize));
                if (!key || !val || key->type != Plist::Type::String) return nullptr;
                p->dict.emplace_back(key->s, val);
            }
            return p;
        }
        default:
            return nullptr;
        }
    }
};

}  // namespace

PlistPtr plist_parse_binary(const uint8_t* data, size_t len) {
    if (!data || len < 8 + 32 || memcmp(data, "bplist00", 8) != 0) return nullptr;
    Reader rd;
    rd.d = data;
    rd.n = len;
    const uint8_t* t = data + len - 32;
    rd.offsetSize = t[6];
    rd.refSize = t[7];
    rd.numObjects = rd.readBE(len - 24, 8);
    rd.topObject = rd.readBE(len - 16, 8);
    rd.offsetTableOffset = rd.readBE(len - 8, 8);
    if (rd.offsetSize < 1 || rd.offsetSize > 8 || rd.refSize < 1 || rd.refSize > 8) return nullptr;
    if (rd.offsetTableOffset + rd.numObjects * rd.offsetSize > len - 32) return nullptr;
    return rd.parse(rd.topObject);
}

// ---------------------------------------------------------------- writing

namespace {

struct Writer {
    std::vector<const Plist*> objects;  // flattened
    std::vector<std::string> keyStrings;  // storage for dict keys
    std::vector<Plist> keyNodes;

    std::vector<uint8_t> out;
    std::vector<uint64_t> offsets;
    int refSize = 1;

    static void writeBE(std::vector<uint8_t>& o, uint64_t v, int size) {
        for (int k = size - 1; k >= 0; k--) o.push_back((uint8_t)(v >> (8 * k)));
    }

    void writeMarkerLen(uint8_t hi, uint64_t len) {
        if (len < 15) {
            out.push_back((uint8_t)((hi << 4) | len));
        } else {
            out.push_back((uint8_t)((hi << 4) | 0xF));
            writeInt(len);
        }
    }

    void writeInt(uint64_t v) {
        if (v <= 0xFF) { out.push_back(0x10); writeBE(out, v, 1); }
        else if (v <= 0xFFFF) { out.push_back(0x11); writeBE(out, v, 2); }
        else if (v <= 0xFFFFFFFFULL) { out.push_back(0x12); writeBE(out, v, 4); }
        else if (v <= 0x7FFFFFFFFFFFFFFFULL) { out.push_back(0x13); writeBE(out, v, 8); }
        else { out.push_back(0x14); writeBE(out, 0, 8); writeBE(out, v, 8); }  // unsigned 128-bit
    }
};

}  // namespace

std::vector<uint8_t> plist_write_binary(const PlistPtr& root) {
    // Flatten object graph: each node gets an index; dict keys become string objects.
    struct Node {
        const Plist* p;            // non-null for real nodes
        std::string keyString;     // for synthesized key nodes
        bool isKey = false;
        std::vector<uint64_t> refs;  // child refs (arrays: items; dicts: keys then values)
    };
    std::vector<Node> nodes;
    std::function<uint64_t(const Plist*)> add = [&](const Plist* p) -> uint64_t {
        uint64_t idx = nodes.size();
        nodes.push_back(Node{p, "", false, {}});
        if (p->type == Plist::Type::Array) {
            std::vector<uint64_t> refs;
            for (auto& e : p->array) refs.push_back(add(e.get()));
            nodes[idx].refs = refs;
        } else if (p->type == Plist::Type::Dict) {
            std::vector<uint64_t> keys, vals;
            for (auto& kv : p->dict) {
                uint64_t k = nodes.size();
                nodes.push_back(Node{nullptr, kv.first, true, {}});
                keys.push_back(k);
            }
            for (auto& kv : p->dict) vals.push_back(add(kv.second.get()));
            keys.insert(keys.end(), vals.begin(), vals.end());
            nodes[idx].refs = keys;
        }
        return idx;
    };
    add(root.get());

    Writer w;
    w.refSize = nodes.size() < 256 ? 1 : (nodes.size() < 65536 ? 2 : 4);
    const char* magic = "bplist00";
    w.out.insert(w.out.end(), magic, magic + 8);
    std::vector<uint64_t> offsets;

    auto writeString = [&](const std::string& s) {
        bool ascii = true;
        for (unsigned char c : s)
            if (c >= 0x80) { ascii = false; break; }
        if (ascii) {
            w.writeMarkerLen(0x5, s.size());
            w.out.insert(w.out.end(), s.begin(), s.end());
        } else {
            // UTF-8 -> UTF-16BE
            std::vector<uint16_t> u16;
            for (size_t k = 0; k < s.size();) {
                unsigned char c = (unsigned char)s[k];
                uint32_t cp;
                int extra;
                if (c < 0x80) { cp = c; extra = 0; }
                else if ((c >> 5) == 6) { cp = c & 0x1F; extra = 1; }
                else if ((c >> 4) == 14) { cp = c & 0x0F; extra = 2; }
                else { cp = c & 0x07; extra = 3; }
                k++;
                for (int e = 0; e < extra && k < s.size(); e++, k++) cp = (cp << 6) | ((unsigned char)s[k] & 0x3F);
                if (cp >= 0x10000) {
                    cp -= 0x10000;
                    u16.push_back((uint16_t)(0xD800 + (cp >> 10)));
                    u16.push_back((uint16_t)(0xDC00 + (cp & 0x3FF)));
                } else {
                    u16.push_back((uint16_t)cp);
                }
            }
            w.writeMarkerLen(0x6, u16.size());
            for (auto c : u16) Writer::writeBE(w.out, c, 2);
        }
    };

    for (auto& nd : nodes) {
        offsets.push_back(w.out.size());
        if (nd.isKey) { writeString(nd.keyString); continue; }
        const Plist* p = nd.p;
        switch (p->type) {
        case Plist::Type::Null: w.out.push_back(0x00); break;
        case Plist::Type::Bool: w.out.push_back(p->b ? 0x09 : 0x08); break;
        case Plist::Type::Int: w.writeInt(p->i); break;
        case Plist::Type::Uid: w.out.push_back(0x87); Writer::writeBE(w.out, p->i, 8); break;
        case Plist::Type::Real: {
            w.out.push_back(0x23);
            uint64_t v;
            memcpy(&v, &p->r, 8);
            Writer::writeBE(w.out, v, 8);
            break;
        }
        case Plist::Type::Date: {
            w.out.push_back(0x33);
            uint64_t v;
            memcpy(&v, &p->r, 8);
            Writer::writeBE(w.out, v, 8);
            break;
        }
        case Plist::Type::String: writeString(p->s); break;
        case Plist::Type::Data:
            w.writeMarkerLen(0x4, p->data.size());
            w.out.insert(w.out.end(), p->data.begin(), p->data.end());
            break;
        case Plist::Type::Array:
            w.writeMarkerLen(0xA, nd.refs.size());
            for (auto r : nd.refs) Writer::writeBE(w.out, r, w.refSize);
            break;
        case Plist::Type::Dict:
            w.writeMarkerLen(0xD, nd.refs.size() / 2);
            for (auto r : nd.refs) Writer::writeBE(w.out, r, w.refSize);
            break;
        }
    }
    uint64_t offsetTableOffset = w.out.size();
    int offsetSize = offsetTableOffset < 256 ? 1 : (offsetTableOffset < 65536 ? 2 : (offsetTableOffset < 0xFFFFFFFFULL ? 4 : 8));
    for (auto o : offsets) Writer::writeBE(w.out, o, offsetSize);
    // trailer
    for (int k = 0; k < 6; k++) w.out.push_back(0);
    w.out.push_back((uint8_t)offsetSize);
    w.out.push_back((uint8_t)w.refSize);
    Writer::writeBE(w.out, nodes.size(), 8);
    Writer::writeBE(w.out, 0, 8);
    Writer::writeBE(w.out, offsetTableOffset, 8);
    return w.out;
}

}  // namespace mn
