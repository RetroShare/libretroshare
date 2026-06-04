// SPDX-FileCopyrightText: 2024 RetroShare Team
// SPDX-License-Identifier: AGPL-3.0-only
//
// p3friendrequest.cc

#include "p3friendrequest.h"

#include <cstring>   // memcpy
#include <ctime>     // time()

#include "retroshare/rspeers.h"   // rsPeers->addSslOnlyFriend
#include "rsserver/p3peermgr.h"
#include "serialiser/rsbaseserial.h"
#include "util/rsdebug.h"

RS_SET_CONTEXT_DEBUG_LEVEL(1)

// Global interface pointer (mirrors rsPeers in p3peers.cc). Declared extern in
// retroshare/rsfriendrequest.h; assigned during startup in rsserver/rsinit.cc.
RsFriendRequest* rsFriendRequest = nullptr;

// ══════════════════════════════════════════════════════════════════════════
// RsFriendRequestItem — serialisation helpers
// ══════════════════════════════════════════════════════════════════════════
//
// All multi-byte integers are stored big-endian (network byte order),
// consistent with the rest of RS serialisation.

static inline void putU8 (uint8_t*& p, uint8_t v)
{
    *p++ = v;
}

static inline void putU32(uint8_t*& p, uint32_t v)
{
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >>  8) & 0xFF;
    p[3] =  v        & 0xFF;
    p += 4;
}

static inline void putU64(uint8_t*& p, uint64_t v)
{
    putU32(p, (uint32_t)(v >> 32));
    putU32(p, (uint32_t)(v & 0xFFFFFFFF));
}

static inline void putBytes(uint8_t*& p, const void* src, uint32_t len)
{
    std::memcpy(p, src, len);
    p += len;
}

static inline void putStr(uint8_t*& p, const std::string& s)
{
    uint32_t len = static_cast<uint32_t>(s.size());
    putU32(p, len);
    putBytes(p, s.data(), len);
}

static inline bool getU8(const uint8_t*& p, const uint8_t* end, uint8_t& v)
{
    if (p + 1 > end) return false;
    v = *p++;
    return true;
}

static inline bool getU32(const uint8_t*& p, const uint8_t* end, uint32_t& v)
{
    if (p + 4 > end) return false;
    v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
      | ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
    p += 4;
    return true;
}

static inline bool getU64(const uint8_t*& p, const uint8_t* end, uint64_t& v)
{
    uint32_t hi, lo;
    if (!getU32(p, end, hi)) return false;
    if (!getU32(p, end, lo)) return false;
    v = ((uint64_t)hi << 32) | lo;
    return true;
}

static inline bool getBytes(const uint8_t*& p, const uint8_t* end,
                             void* dst, uint32_t len)
{
    if (p + len > end) return false;
    std::memcpy(dst, p, len);
    p += len;
    return true;
}

static inline bool getStr(const uint8_t*& p, const uint8_t* end, std::string& s)
{
    uint32_t len = 0;
    if (!getU32(p, end, len))       return false;
    if (p + len > end)              return false;
    s.assign(reinterpret_cast<const char*>(p), len);
    p += len;
    return true;
}

// ── RsFriendRequestItem ────────────────────────────────────────────────────

RsFriendRequestItem::RsFriendRequestItem()
    : RsItem(RS_PKT_VERSION2, PKT_CLASS, PKT_TYPE, PKT_SUBTYPE)
{}

void RsFriendRequestItem::clear()
{
    entries.clear();
}

std::ostream& RsFriendRequestItem::print(std::ostream& out, uint16_t indent)
{
    printRsItemBase(out, "RsFriendRequestItem", indent);
    out << std::string(indent + 4, ' ')
        << "entries: " << entries.size() << std::endl;
    return out;
}

uint32_t RsFriendRequestItem::serial_size() const
{
    // header (8) + version (1) + count (4)
    uint32_t sz = 8 + 1 + 4;
    for (const auto& [id, e] : entries)
    {
        sz += 32;   // sslId
        sz += 20;   // pgpId
        sz += 4 + static_cast<uint32_t>(e.pgpName.size());
        sz += 4 + static_cast<uint32_t>(e.pgpFingerprint.size());
        sz += 8;    // firstSeen
        sz += 8;    // lastSeen
        sz += 4;    // attemptCount
        sz += 1;    // rejected
    }
    return sz;
}

bool RsFriendRequestItem::serialise(void* data, uint32_t& pktsize) const
{
    uint32_t needed = serial_size();
    if (pktsize < needed) { pktsize = needed; return false; }
    pktsize = needed;

    uint8_t* p = static_cast<uint8_t*>(data);

    // RS item header (8 bytes)
    putU8 (p, RS_PKT_VERSION2);
    putU8 (p, PKT_CLASS);
    putU8 (p, PKT_TYPE);
    putU8 (p, (uint8_t)(PKT_SUBTYPE >> 8));
    putU8 (p, (uint8_t)(PKT_SUBTYPE & 0xFF));
    putU32(p, needed);   // total size field (overwrites last 4 of the 8-byte hdr)
    // Note: RS item header layout: [ver][class][type][sub_hi][sub_lo][size32]
    // The size field starts at offset 4 (4 bytes). Rewind and write correctly:
    //
    // Actually RS uses getRsItemId() layout. Use the base-class helper instead.
    // Reset and use getRsItemHeader() pattern:
    p = static_cast<uint8_t*>(data);
    uint32_t hdr = 0;
    hdr = (uint32_t)RS_PKT_VERSION2 << 24
        | (uint32_t)PKT_CLASS        << 16
        | (uint32_t)PKT_TYPE         <<  8
        | (uint32_t)(PKT_SUBTYPE & 0xFF);
    putU32(p, hdr);
    putU32(p, needed);  // packet size

    putU8 (p, SERIAL_VERSION);
    putU32(p, static_cast<uint32_t>(entries.size()));

    for (const auto& [id, e] : entries)
    {
        // sslId: RsPeerId is a fixed 32-byte identifier
        const std::string sslStr = e.sslId.toStdString();
        // toStdString() on RsPeerId returns the hex string; use the raw bytes
        // via getId() which returns a const uint8_t* of SSL_ID_SIZE bytes.
        putBytes(p, e.sslId.getId(), RsPeerId::SIZE_IN_BYTES);

        // pgpId: 20 bytes
        putBytes(p, e.pgpId.getId(), RsPgpId::SIZE_IN_BYTES);

        putStr(p, e.pgpName);
        putStr(p, e.pgpFingerprint);
        putU64(p, static_cast<uint64_t>(e.firstSeen));
        putU64(p, static_cast<uint64_t>(e.lastSeen));
        putU32(p, e.attemptCount);
        putU8 (p, e.rejected ? 1 : 0);
    }

    return true;
}

bool RsFriendRequestItem::deserialise(void* data, uint32_t pktsize)
{
    const uint8_t* p   = static_cast<const uint8_t*>(data);
    const uint8_t* end = p + pktsize;

    // Skip the 8-byte RS item header (already parsed by the framework).
    p += 8;

    uint8_t  ver   = 0;
    uint32_t count = 0;

    if (!getU8 (p, end, ver))   return false;
    if (ver != SERIAL_VERSION)
    {
        RS_ERR("RsFriendRequestItem: unknown serial version ", (int)ver);
        return false;
    }
    if (!getU32(p, end, count)) return false;

    entries.clear();
    for (uint32_t i = 0; i < count; ++i)
    {
        RsFriendRequestEntry e;

        uint8_t sslBuf[RsPeerId::SIZE_IN_BYTES] = {};
        uint8_t pgpBuf[RsPgpId::SIZE_IN_BYTES]  = {};

        if (!getBytes(p, end, sslBuf, RsPeerId::SIZE_IN_BYTES)) return false;
        if (!getBytes(p, end, pgpBuf, RsPgpId::SIZE_IN_BYTES))  return false;

        e.sslId.init(sslBuf);
        e.pgpId.init(pgpBuf);

        if (!getStr(p, end, e.pgpName))        return false;
        if (!getStr(p, end, e.pgpFingerprint)) return false;

        uint64_t fs = 0, ls = 0;
        if (!getU64(p, end, fs))                return false;
        if (!getU64(p, end, ls))                return false;
        e.firstSeen    = static_cast<rstime_t>(fs);
        e.lastSeen     = static_cast<rstime_t>(ls);

        if (!getU32(p, end, e.attemptCount))    return false;

        uint8_t rej = 0;
        if (!getU8(p, end, rej))                return false;
        e.rejected = (rej != 0);

        entries[e.sslId] = e;
    }

    return true;
}

// ══════════════════════════════════════════════════════════════════════════
// p3FriendRequest
// ══════════════════════════════════════════════════════════════════════════

p3FriendRequest::p3FriendRequest(p3PeerMgr* peerMgr)
    : p3Config(CONFIG_TYPE_FRIEND_REQUESTS)
    , mPeerMgr(peerMgr)
{}

// ── AuthSSL hook ───────────────────────────────────────────────────────────

void p3FriendRequest::onUnknownPeerConnectionAttempt(
        const RsPeerId&    sslId,
        const RsPgpId&     pgpId,
        const std::string& pgpName,
        const std::string& pgpFingerprint)
{
    {
        std::lock_guard<std::mutex> lk(mMtx);

        rstime_t now = static_cast<rstime_t>(time(nullptr));
        auto it = mEntries.find(sslId);

        if (it == mEntries.end())
        {
            RsFriendRequestEntry entry;
            entry.sslId          = sslId;
            entry.pgpId          = pgpId;
            entry.pgpName        = pgpName;
            entry.pgpFingerprint = pgpFingerprint;
            entry.firstSeen      = now;
            entry.lastSeen       = now;
            entry.attemptCount   = 1;
            entry.rejected       = false;
            mEntries[sslId]      = entry;
            RS_DBG1("New friend request from '", pgpName, "' (", sslId, ")");
        }
        else
        {
            // Preserve the rejected flag — the user's decision stands.
            it->second.lastSeen = now;
            it->second.attemptCount++;
            RS_DBG2("Repeated attempt from '", it->second.pgpName,
                    "' (#", it->second.attemptCount, ")");
        }
    } // lock released before markDirty

    markDirty();
}

// ── Query ──────────────────────────────────────────────────────────────────

bool p3FriendRequest::getPendingRequests(
        std::list<RsFriendRequestEntry>& requests)
{
    std::lock_guard<std::mutex> lk(mMtx);
    requests.clear();
    for (const auto& [id, e] : mEntries)
        if (!e.rejected)
            requests.push_back(e);
    requests.sort([](const RsFriendRequestEntry& a,
                     const RsFriendRequestEntry& b) {
        return a.lastSeen > b.lastSeen;
    });
    return true;
}

bool p3FriendRequest::getAllRequests(
        std::list<RsFriendRequestEntry>& requests)
{
    std::lock_guard<std::mutex> lk(mMtx);
    requests.clear();
    for (const auto& [id, e] : mEntries)
        requests.push_back(e);
    requests.sort([](const RsFriendRequestEntry& a,
                     const RsFriendRequestEntry& b) {
        return a.lastSeen > b.lastSeen;
    });
    return true;
}

uint32_t p3FriendRequest::pendingCount()
{
    std::lock_guard<std::mutex> lk(mMtx);
    uint32_t n = 0;
    for (const auto& [id, e] : mEntries)
        if (!e.rejected) ++n;
    return n;
}

// ── Actions ────────────────────────────────────────────────────────────────

bool p3FriendRequest::acceptRequest(const RsPeerId& sslId)
{
    RsPgpId pgpId;
    {
        std::lock_guard<std::mutex> lk(mMtx);
        auto it = mEntries.find(sslId);
        if (it == mEntries.end())
        {
            RS_ERR("acceptRequest: sslId not found: ", sslId);
            return false;
        }
        pgpId = it->second.pgpId;
        mEntries.erase(it);
    } // lock released — rsPeers call must not hold mMtx (avoids re-entry risk)

    bool ok = rsPeers->addSslOnlyFriend(sslId, pgpId);
    if (!ok)
        RS_ERR("addSslOnlyFriend failed for ", sslId);
    else
        RS_DBG1("Accepted friend request from ", sslId);

    markDirty();
    return ok;
}

bool p3FriendRequest::rejectRequest(const RsPeerId& sslId)
{
    {
        std::lock_guard<std::mutex> lk(mMtx);
        auto it = mEntries.find(sslId);
        if (it == mEntries.end()) return false;
        it->second.rejected = true;
        RS_DBG1("Rejected friend request from ", sslId);
    }
    markDirty();
    return true;
}

bool p3FriendRequest::deleteRequest(const RsPeerId& sslId)
{
    bool erased;
    {
        std::lock_guard<std::mutex> lk(mMtx);
        erased = mEntries.erase(sslId) > 0;
    }
    if (erased) markDirty();
    return erased;
}

bool p3FriendRequest::clearRejected()
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lk(mMtx);
        for (auto it = mEntries.begin(); it != mEntries.end(); )
        {
            if (it->second.rejected) { it = mEntries.erase(it); changed = true; }
            else ++it;
        }
    }
    if (changed) markDirty();
    return true;
}

// ── p3Config persistence ───────────────────────────────────────────────────

RsSerialiser* p3FriendRequest::setupSerialiser()
{
    // We use the direct serialise()/deserialise() methods on
    // RsFriendRequestItem rather than a separate RsSerialiser class.
    // Return a bare RsSerialiser with no added types; the item handles
    // its own binary I/O.
    return new RsSerialiser;
}

bool p3FriendRequest::saveList(bool& cleanup, std::list<RsItem*>& lst)
{
    cleanup = true;   // caller owns and deletes items after saving

    auto* item = new RsFriendRequestItem;
    {
        std::lock_guard<std::mutex> lk(mMtx);
        item->entries = mEntries;  // snapshot under lock
    }
    lst.push_back(item);
    return true;
}

bool p3FriendRequest::loadList(std::list<RsItem*>& load)
{
    {
        std::lock_guard<std::mutex> lk(mMtx);
        for (RsItem* raw : load)
        {
            auto* item = dynamic_cast<RsFriendRequestItem*>(raw);
            if (item)
                for (auto& [id, e] : item->entries)
                    mEntries.emplace(id, e);  // emplace: don't overwrite newer in-memory data
            delete raw;
        }
    }
    load.clear();
    RS_DBG1("Loaded ", mEntries.size(), " friend request entries");
    return true;
}

void p3FriendRequest::markDirty()
{
    // Use the no-argument overload available in all RS versions.
    // This avoids any dependency on the non-existent CheckPriority enum.
    p3Config::IndicateConfigChanged();
}
