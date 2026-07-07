// SPDX-FileCopyrightText: 2024 RetroShare Team
// SPDX-License-Identifier: AGPL-3.0-only
//
// p3friendrequest.h
// Concrete implementation of RsFriendRequest.
// Lives in libretroshare/src/rsserver/

#pragma once

#include <map>
#include <mutex>
#include <string>

#include "retroshare/rsfriendrequest.h"
#include "rsitems/rsitem.h"
#include "pqi/p3cfgmgr.h"
#include "util/rstime.h"

// Forward declarations
class p3PeerMgr;

// ── Serialisable config item ───────────────────────────────────────────────

/**
 * RsFriendRequestItem
 *
 * Stores the entire pending-request map in a single RsItem and implements
 * its own binary serialisation, following the pattern used by other
 * p3Config subclasses in RS (e.g. p3BanList) that avoid a separate
 * RsSerialiser class.
 *
 * Wire format:
 *   uint8  SERIAL_VERSION  (= 1)
 *   uint32 entry_count
 *   for each entry:
 *     uint8[32]  sslId  (raw bytes)
 *     uint8[20]  pgpId  (raw bytes)
 *     uint32     pgpName length, then raw UTF-8 bytes
 *     uint32     pgpFingerprint length, then raw UTF-8 bytes
 *     uint64     firstSeen  (seconds since epoch)
 *     uint64     lastSeen
 *     uint32     attemptCount
 *     uint8      rejected  (0 or 1)
 */
class RsFriendRequestItem : public RsItem
{
public:
    static constexpr uint8_t  PKT_VERSION = 0x02;
    static constexpr uint8_t  PKT_CLASS   = 0x05;
    static constexpr uint8_t  PKT_TYPE    = 0x20;
    static constexpr uint16_t PKT_SUBTYPE = 0x0001;

    RsFriendRequestItem();

    void clear() override;
    std::ostream& print(std::ostream& out, uint16_t indent = 0) override;
    void serial_process(RsGenericSerializer::SerializeJob j,
                        RsGenericSerializer::SerializeContext& ctx) override;

    // Called by p3Config's internal I/O. RS expects these three methods on
    // items that manage their own serialisation.
    uint32_t serial_size() const;
    bool     serialise(void* data, uint32_t& pktsize) const;
    bool     deserialise(void* data, uint32_t pktsize);

    std::map<RsPeerId, RsFriendRequestEntry> entries;

private:
    static constexpr uint8_t SERIAL_VERSION = 1;

    // Helpers — pointer is advanced past the written/read bytes on success.
    static bool writeU8  (uint8_t*& p, uint8_t* end, uint8_t v);
    static bool writeU32 (uint8_t*& p, uint8_t* end, uint32_t v);
    static bool writeU64 (uint8_t*& p, uint8_t* end, uint64_t v);
    static bool writeBytes(uint8_t*& p, uint8_t* end, const void* src, uint32_t len);
    static bool writeStr (uint8_t*& p, uint8_t* end, const std::string& s);

    static bool readU8   (const uint8_t*& p, const uint8_t* end, uint8_t& v);
    static bool readU32  (const uint8_t*& p, const uint8_t* end, uint32_t& v);
    static bool readU64  (const uint8_t*& p, const uint8_t* end, uint64_t& v);
    static bool readBytes(const uint8_t*& p, const uint8_t* end, void* dst, uint32_t len);
    static bool readStr  (const uint8_t*& p, const uint8_t* end, std::string& s);
};

// ── p3FriendRequest ────────────────────────────────────────────────────────

class p3FriendRequest
        : public RsFriendRequest
        , public p3Config
{
public:
    explicit p3FriendRequest(p3PeerMgr* peerMgr);
    ~p3FriendRequest() override = default;

    // ── RsFriendRequest interface ──────────────────────────────────────────

    bool getPendingRequests(
            std::list<RsFriendRequestEntry>& requests) override;

    bool getAllRequests(
            std::list<RsFriendRequestEntry>& requests) override;

    uint32_t pendingCount() override;

    bool acceptRequest (const RsPeerId& sslId) override;
    bool rejectRequest (const RsPeerId& sslId) override;
    bool deleteRequest (const RsPeerId& sslId) override;
    bool clearRejected () override;

    // Promoted to the RsFriendRequest public interface so AuthSSL can call it
    // via rsFriendRequest without a static_cast to the concrete type.
    // Thread-safe: may be called from the OpenSSL verify callback thread.
    void onUnknownPeerConnectionAttempt(
            const RsPeerId&    sslId,
            const RsPgpId&     pgpId,
            const std::string& pgpName,
            const std::string& pgpFingerprint) override;

    // ── p3Config persistence ───────────────────────────────────────────────

protected:
    RsSerialiser* setupSerialiser() override;
    bool saveList(bool& cleanup, std::list<RsItem*>& lst) override;
    bool loadList(std::list<RsItem*>& load) override;

private:
    p3PeerMgr* mPeerMgr;

    mutable std::mutex mMtx;
    std::map<RsPeerId, RsFriendRequestEntry> mEntries; // guarded by mMtx

    // Calls the no-argument p3Config::IndicateConfigChanged().
    // Avoids any dependency on the non-existent CheckPriority enum.
    void markDirty();
};
