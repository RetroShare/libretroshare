// SPDX-FileCopyrightText: 2024 RetroShare Team
// SPDX-License-Identifier: AGPL-3.0-only
//
// rsfriendrequest.h
// Public interface for incoming friend-request management.
// libretroshare — consumed by Qt GUI, JSON API, and AuthSSL.

#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "retroshare/rsids.h"
#include "retroshare/rspeers.h"   // RsPeerId, RsPgpId

// ── Forward declaration & singleton ───────────────────────────────────────

class RsFriendRequest;

/// Global singleton, set during RS initialisation in p3Server::init().
/// Always null-check before use: AuthSSL may call before init completes.
extern RsFriendRequest* rsFriendRequest;

// ── Data structure ─────────────────────────────────────────────────────────

/// One entry in the pending friend-request list, persisted across restarts.
struct RsFriendRequestEntry
{
    RsPeerId    sslId;           ///< SSL/node ID of the requesting peer
    RsPgpId     pgpId;           ///< PGP key ID of the requesting peer
    std::string pgpName;         ///< Human-readable name from their certificate CN
    std::string pgpFingerprint;  ///< Hex fingerprint string (for display)
    rstime_t    firstSeen;       ///< Unix timestamp of the first connection attempt
    rstime_t    lastSeen;        ///< Unix timestamp of the most recent attempt
    uint32_t    attemptCount;    ///< Total number of connection attempts recorded
    bool        rejected;        ///< true = user dismissed; hidden from pending list
};

// ── Interface ──────────────────────────────────────────────────────────────

/**
 * @brief RsFriendRequest
 *
 * Tracks peers that attempted to connect but are not yet friends.
 * Entries survive restarts (persisted via p3Config).
 *
 * Typical GUI usage:
 *   1. Poll getPendingRequests() on a timer, or react to RS events.
 *   2. Display badge = pendingCount().
 *   3. acceptRequest(sslId)  → adds the peer as a friend.
 *   4. rejectRequest(sslId)  → hides the entry; no network message sent.
 *   5. deleteRequest(sslId)  → permanently removes the entry.
 *
 * AuthSSL usage:
 *   Call onUnknownPeerConnectionAttempt() from VerifyX509Callback when a
 *   peer whose PGP key is not in the friend list attempts to connect.
 */
class RsFriendRequest
{
public:
    virtual ~RsFriendRequest() = default;

    // ── Query ──────────────────────────────────────────────────────────────

    /// Returns all non-rejected pending requests, newest-first.
    virtual bool getPendingRequests(
            std::list<RsFriendRequestEntry>& requests) = 0;

    /// Returns all requests including rejected ones (for an "All" view).
    virtual bool getAllRequests(
            std::list<RsFriendRequestEntry>& requests) = 0;

    /// Returns the count of non-rejected, non-accepted entries.
    /// Cheaper than loading the full list; use for badge display.
    virtual uint32_t pendingCount() = 0;

    // ── Actions ────────────────────────────────────────────────────────────

    /// Accept: calls rsPeers->addSslOnlyFriend() and removes the entry.
    virtual bool acceptRequest(const RsPeerId& sslId) = 0;

    /// Reject: marks the entry hidden. No network message is sent.
    /// Future connection attempts from the same peer update lastSeen/count
    /// but do not un-hide the entry (the user's choice is preserved).
    virtual bool rejectRequest(const RsPeerId& sslId) = 0;

    /// Permanently delete a single entry. A new entry will be created if
    /// the peer connects again.
    virtual bool deleteRequest(const RsPeerId& sslId) = 0;

    /// Remove all rejected entries from storage.
    virtual bool clearRejected() = 0;

    // ── AuthSSL hook ───────────────────────────────────────────────────────

    /**
     * @brief onUnknownPeerConnectionAttempt
     *
     * Must be called from AuthSSLimpl::VerifyX509Callback (authssl.cc)
     * when a peer whose PGP key is NOT in the friend list attempts to connect.
     *
     * Callers can use rsFriendRequest directly — no cast to the concrete
     * type is needed. Always null-check rsFriendRequest first.
     *
     * Thread-safe: may be invoked from the OpenSSL I/O thread.
     *
     * Example call site in authssl.cc:
     *   if (rsFriendRequest)
     *       rsFriendRequest->onUnknownPeerConnectionAttempt(
     *               sslId, pgpId, pgpName, pgpFingerprint);
     */
    virtual void onUnknownPeerConnectionAttempt(
            const RsPeerId&    sslId,
            const RsPgpId&     pgpId,
            const std::string& pgpName,
            const std::string& pgpFingerprint) = 0;
};
