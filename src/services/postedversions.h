// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "retroshare/rsposted.h"
#include <map>

namespace PostedVersions
{
/** Resolve already authenticated revisions without moving comments or votes
 * to a new thread. An orphan revision is hidden until its original arrives. */
inline void resolve(std::vector<RsPostedPost>& posts)
{
    using Key = RsGxsGrpMsgIdPair;
    std::map<Key, const RsPostedPost*> originals;
    std::map<Key, const RsPostedPost*> latest;
    for(const auto& post : posts)
    {
        const auto& meta = post.mMeta;
        if(!meta.mParentId.isNull() || meta.mMsgId.isNull()) continue;
        if(meta.mOrigMsgId.isNull() || meta.mOrigMsgId == meta.mMsgId)
            originals[{meta.mGroupId, meta.mMsgId}] = &post;
        else
        {
            auto& revision = latest[{meta.mGroupId, meta.mOrigMsgId}];
            if(!revision || revision->mMeta.mPublishTs < meta.mPublishTs
                    || (revision->mMeta.mPublishTs == meta.mPublishTs
                        && revision->mMeta.mMsgId < meta.mMsgId))
                revision = &post;
        }
    }

    std::vector<RsPostedPost> resolved;
    resolved.reserve(originals.size());
    for(const auto& entry : originals)
    {
        auto post = *entry.second;
        post.mRevisionId = post.mMeta.mMsgId;
        const auto revision = latest.find(entry.first);
        if(revision != latest.end()
                && revision->second->mMeta.mPublishTs >= post.mMeta.mPublishTs)
        {
            const auto& edit = *revision->second;
            post.mMeta.mMsgName = edit.mMeta.mMsgName;
            post.mLink = edit.mLink;
            post.mNotes = edit.mNotes;
            post.mImage = edit.mImage;
            post.mRevisionId = edit.mMeta.mMsgId;
        }
        resolved.push_back(std::move(post));
    }
    posts.swap(resolved);
}
}
