// Mirrorly - minimal multicast DNS / DNS-SD responder
#include "mdns.h"

#include <algorithm>
#include <cstring>

#include "log.h"

namespace mn {

namespace {

const uint16_t T_A = 1, T_PTR = 12, T_TXT = 16, T_SRV = 33, T_ANY = 255;
const uint32_t MDNS_GROUP = 0xE00000FB;  // 224.0.0.251 (host order)

using Name = std::vector<std::string>;

std::string lower(const std::string& s) {
    std::string r = s;
    for (auto& c : r)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return r;
}

bool nameEq(const Name& a, const Name& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

Name splitType(const std::string& type) {  // "_airplay._tcp" -> {"_airplay","_tcp"}
    Name n;
    size_t start = 0;
    while (start <= type.size()) {
        size_t dot = type.find('.', start);
        if (dot == std::string::npos) dot = type.size();
        if (dot > start) n.push_back(type.substr(start, dot - start));
        start = dot + 1;
    }
    return n;
}

struct Record {
    Name name;
    uint16_t type;
    uint32_t ttl;
    bool unique;  // cache-flush
    std::vector<uint8_t> rdata;   // raw, except names are encoded uncompressed
};

void putU16(std::vector<uint8_t>& o, uint16_t v) { o.push_back((uint8_t)(v >> 8)); o.push_back((uint8_t)v); }
void putU32(std::vector<uint8_t>& o, uint32_t v) { putU16(o, (uint16_t)(v >> 16)); putU16(o, (uint16_t)v); }
void putName(std::vector<uint8_t>& o, const Name& n) {
    for (auto& l : n) {
        size_t len = std::min<size_t>(l.size(), 63);
        o.push_back((uint8_t)len);
        o.insert(o.end(), l.begin(), l.begin() + len);
    }
    o.push_back(0);
}

// Reads a (possibly compressed) DNS name. Returns false on malformed input.
bool readName(const uint8_t* d, size_t n, size_t& pos, Name& out) {
    size_t p = pos;
    bool jumped = false;
    int hops = 0;
    out.clear();
    while (true) {
        if (p >= n) return false;
        uint8_t len = d[p];
        if (len == 0) {
            p++;
            break;
        }
        if ((len & 0xC0) == 0xC0) {
            if (p + 1 >= n || ++hops > 16) return false;
            size_t target = ((len & 0x3F) << 8) | d[p + 1];
            if (!jumped) pos = p + 2;
            jumped = true;
            p = target;
            continue;
        }
        if (p + 1 + len > n) return false;
        out.emplace_back((const char*)d + p + 1, len);
        p += 1 + len;
    }
    if (!jumped) pos = p;
    return true;
}

}  // namespace

MdnsResponder::MdnsResponder() {}
MdnsResponder::~MdnsResponder() { stop(); }

bool MdnsResponder::start(const std::string& hostname, const std::vector<MdnsService>& services) {
    stop();
    net_init();
    hostname_ = hostname;
    services_ = services;
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == MN_INVALID_SOCKET) {
        error_ = "could not create mDNS socket";
        return false;
    }
    sock_set_reuse(sock_);
    udp_disable_connreset(sock_);
#ifdef SO_REUSEPORT
    int one = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEPORT, (const char*)&one, sizeof(one));
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(5353);
    if (bind(sock_, (sockaddr*)&a, sizeof(a)) != 0) {
        error_ = "could not bind UDP port 5353 (another Bonjour service may hold it exclusively)";
        LOGE("mDNS: bind 5353 failed (%d)", sock_last_error());
        sock_close(sock_);
        sock_ = MN_INVALID_SOCKET;
        return false;
    }
    int ttl = 255;
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
    int loop = 1;
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, sizeof(loop));
    refreshInterfaces();
    running_ = true;
    pendingAnnounces_ = 3;
    nextAnnounce_ = 0;
    thread_ = std::thread(&MdnsResponder::threadMain, this);
    return true;
}

void MdnsResponder::stop() {
    if (running_) {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            announce(true);
        }
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }
    if (sock_ != MN_INVALID_SOCKET) {
        sock_close(sock_);
        sock_ = MN_INVALID_SOCKET;
    }
    joined_.clear();
}

void MdnsResponder::update(const std::vector<MdnsService>& services) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (running_) announce(true);
    services_ = services;
    pendingAnnounces_ = 3;
    nextAnnounce_ = 0;
}

void MdnsResponder::refreshInterfaces() {
    auto list = list_ipv4_interfaces();
    for (auto& ni : list) {
        if (std::find(joined_.begin(), joined_.end(), ni.addr) != joined_.end()) continue;
        ip_mreq mreq{};
        mreq.imr_multiaddr.s_addr = htonl(MDNS_GROUP);
        mreq.imr_interface.s_addr = ni.addr;
        if (setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq)) == 0) {
            LOGI("mDNS: listening on %s (%s)", ip_to_string(ni.addr).c_str(), ni.name.c_str());
        } else {
            LOGW("mDNS: could not join multicast group on %s", ip_to_string(ni.addr).c_str());
        }
        joined_.push_back(ni.addr);
        pendingAnnounces_ = std::max(pendingAnnounces_, 2);
    }
    bool changed = list.size() != ifaces_.size();
    ifaces_ = list;
    if (changed) LOGD("mDNS: %u interfaces", (unsigned)ifaces_.size());
}

// Record kinds used by buildResponse
enum Kind { K_PTR = 0, K_SRV = 1, K_TXT = 2, K_ENUM = 3 };

std::vector<uint8_t> MdnsResponder::buildResponse(const std::vector<int>& answers, uint32_t iface_addr, bool goodbye,
                                                  uint16_t id, const std::vector<uint8_t>* question, bool legacy) {
    // answers: encoded as (serviceIndex << 4) | kind, or -1 for host A record
    std::vector<Record> an, ad;
    Name host = {hostname_, "local"};
    auto makeRecord = [&](int code) -> Record {
        Record r;
        if (code < 0) {
            r.name = host;
            r.type = T_A;
            r.ttl = 120;
            r.unique = true;
            putU32(r.rdata, ntohl(iface_addr));
            return r;
        }
        const MdnsService& s = services_[code >> 4];
        Name type = splitType(s.type);
        type.push_back("local");
        Name inst = type;
        inst.insert(inst.begin(), s.instance);
        switch (code & 15) {
        case K_PTR:
            r.name = type; r.type = T_PTR; r.ttl = 4500; r.unique = false;
            putName(r.rdata, inst);
            break;
        case K_ENUM:
            r.name = {"_services", "_dns-sd", "_udp", "local"}; r.type = T_PTR; r.ttl = 4500; r.unique = false;
            putName(r.rdata, type);
            break;
        case K_SRV:
            r.name = inst; r.type = T_SRV; r.ttl = 120; r.unique = true;
            putU16(r.rdata, 0); putU16(r.rdata, 0); putU16(r.rdata, s.port);
            putName(r.rdata, host);
            break;
        case K_TXT:
            r.name = inst; r.type = T_TXT; r.ttl = 4500; r.unique = true;
            for (auto& t : s.txt) {
                size_t l = std::min<size_t>(t.size(), 255);
                r.rdata.push_back((uint8_t)l);
                r.rdata.insert(r.rdata.end(), t.begin(), t.begin() + l);
            }
            if (s.txt.empty()) r.rdata.push_back(0);
            break;
        }
        return r;
    };
    std::vector<int> adCodes;
    for (int code : answers) {
        an.push_back(makeRecord(code));
        if (code >= 0 && (code & 15) == K_PTR) {
            int base = code & ~15;
            adCodes.push_back(base | K_SRV);
            adCodes.push_back(base | K_TXT);
            adCodes.push_back(-1);
        } else if (code >= 0 && (code & 15) == K_SRV) {
            adCodes.push_back(-1);
        }
    }
    for (int code : adCodes) {
        if (std::find(answers.begin(), answers.end(), code) != answers.end()) continue;
        bool dup = false;
        for (auto& r : ad) {
            Record t = makeRecord(code);
            if (nameEq(r.name, t.name) && r.type == t.type) { dup = true; break; }
        }
        if (!dup) ad.push_back(makeRecord(code));
    }
    std::vector<uint8_t> o;
    putU16(o, id);
    putU16(o, 0x8400);  // response, authoritative
    putU16(o, question ? 1 : 0);
    putU16(o, (uint16_t)an.size());
    putU16(o, 0);
    putU16(o, (uint16_t)ad.size());
    if (question) o.insert(o.end(), question->begin(), question->end());
    auto writeRec = [&](const Record& r) {
        putName(o, r.name);
        putU16(o, r.type);
        putU16(o, (uint16_t)(0x0001 | ((r.unique && !legacy) ? 0x8000 : 0)));
        uint32_t ttl = goodbye ? 0 : (legacy ? std::min<uint32_t>(r.ttl, 10) : r.ttl);
        putU32(o, ttl);
        putU16(o, (uint16_t)r.rdata.size());
        o.insert(o.end(), r.rdata.begin(), r.rdata.end());
    };
    for (auto& r : an) writeRec(r);
    for (auto& r : ad) writeRec(r);
    return o;
}

void MdnsResponder::sendMulticast(const std::vector<uint8_t>& pkt, uint32_t iface_addr) {
    in_addr ia;
    ia.s_addr = iface_addr;
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&ia, sizeof(ia));
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(MDNS_GROUP);
    to.sin_port = htons(5353);
    sendto(sock_, (const char*)pkt.data(), (int)pkt.size(), 0, (sockaddr*)&to, sizeof(to));
}

void MdnsResponder::announce(bool goodbye) {
    if (sock_ == MN_INVALID_SOCKET) return;
    std::vector<int> all;
    for (size_t i = 0; i < services_.size(); i++) {
        int b = (int)i << 4;
        all.push_back(b | K_PTR);
        all.push_back(b | K_SRV);
        all.push_back(b | K_TXT);
        if (!goodbye) all.push_back(b | K_ENUM);
    }
    if (!goodbye) all.push_back(-1);
    for (auto& ni : ifaces_) {
        auto pkt = buildResponse(all, ni.addr, goodbye, 0, nullptr, false);
        sendMulticast(pkt, ni.addr);
    }
}

void MdnsResponder::handlePacket(const uint8_t* d, size_t n, const sockaddr_in& from) {
    if (n < 12) return;
    uint16_t id = (uint16_t)((d[0] << 8) | d[1]);
    uint16_t flags = (uint16_t)((d[2] << 8) | d[3]);
    if (flags & 0x8000) return;  // a response, not a query
    int qd = (d[4] << 8) | d[5];
    size_t pos = 12;
    bool legacy = ntohs(from.sin_port) != 5353;
    bool wantUnicast = legacy;
    std::vector<int> answers;
    std::vector<uint8_t> firstQuestion;
    Name host = {hostname_, "local"};
    Name enumName = {"_services", "_dns-sd", "_udp", "local"};
    for (int q = 0; q < qd; q++) {
        size_t qstart = pos;
        Name qn;
        if (!readName(d, n, pos, qn) || pos + 4 > n) return;
        uint16_t qtype = (uint16_t)((d[pos] << 8) | d[pos + 1]);
        uint16_t qclass = (uint16_t)((d[pos + 2] << 8) | d[pos + 3]);
        pos += 4;
        if (qclass & 0x8000) wantUnicast = true;
        size_t before = answers.size();
        for (size_t i = 0; i < services_.size(); i++) {
            const auto& s = services_[i];
            Name type = splitType(s.type);
            type.push_back("local");
            Name inst = type;
            inst.insert(inst.begin(), s.instance);
            int b = (int)i << 4;
            if (nameEq(qn, type) && (qtype == T_PTR || qtype == T_ANY)) answers.push_back(b | K_PTR);
            if (nameEq(qn, inst)) {
                if (qtype == T_SRV || qtype == T_ANY) answers.push_back(b | K_SRV);
                if (qtype == T_TXT || qtype == T_ANY) answers.push_back(b | K_TXT);
            }
            if (nameEq(qn, enumName) && (qtype == T_PTR || qtype == T_ANY)) answers.push_back(b | K_ENUM);
        }
        if (nameEq(qn, host) && (qtype == T_A || qtype == T_ANY)) answers.push_back(-1);
        if (answers.size() > before && firstQuestion.empty() && legacy) {
            Name tmp;
            size_t p2 = qstart;
            readName(d, n, p2, tmp);
            putName(firstQuestion, qn);
            putU16(firstQuestion, qtype);
            putU16(firstQuestion, (uint16_t)(qclass & 0x7FFF));
        }
    }
    if (answers.empty()) return;
    std::sort(answers.begin(), answers.end());
    answers.erase(std::unique(answers.begin(), answers.end()), answers.end());

    // choose interface on the same subnet as the querier
    uint32_t iface = 0;
    for (auto& ni : ifaces_)
        if ((ni.addr & ni.netmask) == (from.sin_addr.s_addr & ni.netmask)) { iface = ni.addr; break; }
    if (!iface) {
        if (ifaces_.empty()) return;
        iface = ifaces_[0].addr;
    }
    auto pkt = buildResponse(answers, iface, false, legacy ? id : 0, legacy ? &firstQuestion : nullptr, legacy);
    if (legacy || wantUnicast) {
        sendto(sock_, (const char*)pkt.data(), (int)pkt.size(), 0, (const sockaddr*)&from, sizeof(from));
        if (legacy) return;
    }
    sendMulticast(pkt, iface);
}

void MdnsResponder::threadMain() {
    std::vector<uint8_t> buf(9000);
    while (running_) {
        uint64_t now = now_ms();
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (now - lastIfaceCheck_ > 5000) {
                lastIfaceCheck_ = now;
                refreshInterfaces();
            }
            if (pendingAnnounces_ > 0 && now >= nextAnnounce_) {
                announce(false);
                pendingAnnounces_--;
                nextAnnounce_ = now + 1000;
            }
        }
        int w = sock_wait_readable(sock_, 250);
        if (w <= 0) continue;
        sockaddr_in from{};
        socklen_t fl = sizeof(from);
        int r = recvfrom(sock_, (char*)buf.data(), (int)buf.size(), 0, (sockaddr*)&from, &fl);
        if (r <= 0) continue;
        std::lock_guard<std::mutex> lk(mutex_);
        handlePacket(buf.data(), (size_t)r, from);
    }
}

// ============================================================== browser

static std::string joinName(const Name& n, size_t from = 0) {
    std::string r;
    for (size_t i = from; i < n.size(); i++) {
        if (!r.empty()) r += '.';
        r += n[i];
    }
    return r;
}

bool MdnsBrowser::start(const std::vector<std::string>& types) {
    if (running_) return true;
    net_init();
    types_ = types;
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == MN_INVALID_SOCKET) return false;
    sock_set_reuse(sock_);
    udp_disable_connreset(sock_);
#ifdef SO_REUSEPORT
    int one = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEPORT, (const char*)&one, sizeof(one));
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(5353);
    if (bind(sock_, (sockaddr*)&a, sizeof(a)) != 0) {
        LOGW("mDNS browser: bind 5353 failed (%d)", sock_last_error());
        sock_close(sock_);
        sock_ = MN_INVALID_SOCKET;
        return false;
    }
    int ttl = 255, loop = 1;
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, sizeof(loop));
    running_ = true;
    nextQuery_ = 0;
    lastIfaceCheck_ = 0;
    thread_ = std::thread(&MdnsBrowser::loop, this);
    return true;
}

void MdnsBrowser::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (sock_ != MN_INVALID_SOCKET) sock_close(sock_);
    sock_ = MN_INVALID_SOCKET;
    joined_.clear();
}

std::vector<MdnsFound> MdnsBrowser::find(const std::string& type, uint64_t maxAgeMs) {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<MdnsFound> out;
    uint64_t now = now_ms();
    for (auto& i : insts_) {
        if (!i.srv || lower(i.type) != lower(type) || now - i.seen > maxAgeMs) continue;
        for (auto& h : hosts_)
            if (h.first == lower(i.host)) {
                MdnsFound f;
                f.instance = i.label;
                f.type = i.type;
                f.host = i.host;
                f.ip = h.second;
                f.port = i.port;
                f.seen = i.seen;
                out.push_back(f);
                break;
            }
    }
    return out;
}

void MdnsBrowser::sendQueries() {
    std::vector<uint8_t> q = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    q[5] = (uint8_t)types_.size();
    for (auto& t : types_) {
        Name n = splitType(t);
        n.push_back("local");
        putName(q, n);
        putU16(q, T_PTR);
        putU16(q, 1);
    }
    auto ifs = list_ipv4_interfaces();
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(MDNS_GROUP);
    to.sin_port = htons(5353);
    for (auto& ni : ifs) {
        in_addr ia;
        ia.s_addr = ni.addr;
        setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&ia, sizeof(ia));
        sendto(sock_, (const char*)q.data(), (int)q.size(), 0, (sockaddr*)&to, sizeof(to));
    }
}

void MdnsBrowser::loop() {
    std::vector<uint8_t> buf(9000);
    while (running_) {
        uint64_t now = now_ms();
        if (now - lastIfaceCheck_ > 5000 || lastIfaceCheck_ == 0) {
            lastIfaceCheck_ = now;
            for (auto& ni : list_ipv4_interfaces()) {
                if (std::find(joined_.begin(), joined_.end(), ni.addr) != joined_.end()) continue;
                ip_mreq mreq{};
                mreq.imr_multiaddr.s_addr = htonl(MDNS_GROUP);
                mreq.imr_interface.s_addr = ni.addr;
                setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));
                joined_.push_back(ni.addr);
                nextQuery_ = 0;  // new network: ask right away
            }
        }
        if (now >= nextQuery_) {
            sendQueries();
            nextQuery_ = now + 4000;
        }
        if (sock_wait_readable(sock_, 200) <= 0) continue;
        sockaddr_in from{};
        socklen_t fl = sizeof(from);
        int r = recvfrom(sock_, (char*)buf.data(), (int)buf.size(), 0, (sockaddr*)&from, &fl);
        if (r > 12) handlePacket(buf.data(), (size_t)r);
    }
}

void MdnsBrowser::handlePacket(const uint8_t* d, size_t n) {
    if (!(d[2] & 0x80)) return;  // only responses
    int qd = (d[4] << 8) | d[5];
    int rr = ((d[6] << 8) | d[7]) + ((d[8] << 8) | d[9]) + ((d[10] << 8) | d[11]);
    size_t pos = 12;
    Name nm;
    for (int i = 0; i < qd; i++) {
        if (!readName(d, n, pos, nm) || pos + 4 > n) return;
        pos += 4;
    }
    uint64_t now = now_ms();
    std::lock_guard<std::mutex> lk(m_);
    auto inst = [&](const std::string& label, const std::string& type) -> Inst& {
        for (auto& i : insts_)
            if (lower(i.label) == lower(label) && lower(i.type) == lower(type)) return i;
        insts_.push_back(Inst{label, type, "", 0, 0, false});
        return insts_.back();
    };
    for (int i = 0; i < rr; i++) {
        if (!readName(d, n, pos, nm) || pos + 10 > n) return;
        uint16_t type = (uint16_t)((d[pos] << 8) | d[pos + 1]);
        uint32_t ttl = ((uint32_t)d[pos + 4] << 24) | ((uint32_t)d[pos + 5] << 16) | ((uint32_t)d[pos + 6] << 8) | d[pos + 7];
        uint16_t len = (uint16_t)((d[pos + 8] << 8) | d[pos + 9]);
        pos += 10;
        if (pos + len > n) return;
        size_t rd = pos;
        pos += len;
        if (type == T_PTR && nm.size() >= 3) {
            // <type>._tcp.local -> <instance>.<type>._tcp.local
            std::string svc = joinName(Name(nm.begin(), nm.end() - 1));
            bool wanted = false;
            for (auto& t : types_) wanted |= lower(t) == lower(svc);
            Name target;
            size_t p2 = rd;
            if (!wanted || !readName(d, n, p2, target) || target.empty()) continue;
            Inst& in = inst(target[0], svc);
            if (ttl == 0) in.seen = 0;  // goodbye
            else in.seen = now;
        } else if (type == T_SRV && nm.size() >= 4 && len >= 7) {
            std::string svc = joinName(Name(nm.begin() + 1, nm.end() - 1));
            bool wanted = false;
            for (auto& t : types_) wanted |= lower(t) == lower(svc);
            if (!wanted) continue;
            Name target;
            size_t p2 = rd + 6;
            if (!readName(d, n, p2, target)) continue;
            Inst& in = inst(nm[0], svc);
            in.port = (uint16_t)((d[rd + 4] << 8) | d[rd + 5]);
            in.host = joinName(target);
            in.srv = true;
            in.seen = ttl == 0 ? 0 : now;
        } else if (type == T_A && len == 4) {
            std::string host = lower(joinName(nm));
            uint32_t ip;
            memcpy(&ip, d + rd, 4);
            bool found = false;
            for (auto& h : hosts_)
                if (h.first == host) {
                    h.second = ip;
                    found = true;
                }
            if (!found) hosts_.push_back({host, ip});
        }
    }
}

}  // namespace mn
