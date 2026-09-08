// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rsitems/rsposteditems.h"
#include "services/postedversions.h"
#include "gxs/gxssecurity.h"
#include "gxs/rsgxsdata.h"
#include "rsitems/rsnxsitems.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>

#define CHECK(condition) do { if(!(condition)) { \
    std::cerr << __LINE__ << ": " << #condition << std::endl; std::exit(1); \
} } while(false)

static RsGxsMessageId id(char c) { return RsGxsMessageId(std::string(40, c)); }

static std::vector<uint8_t> encode(RsGxsPostedGroupItem& item)
{
    RsGenericSerializer::SerializeContext size;
    item.serial_process(RsGenericSerializer::SIZE_ESTIMATE, size);
    std::vector<uint8_t> bytes(size.mOffset);
    RsGenericSerializer::SerializeContext out(bytes.data(), bytes.size());
    item.serial_process(RsGenericSerializer::SERIALIZE, out);
    CHECK(out.mOk && out.mOffset == bytes.size());
    return bytes;
}

static void serialization()
{
    for(bool image : {false, true})
        for(bool pins : {false, true})
        {
            RsPostedGroup group;
            group.mDescription = "Board description";
            uint8_t data[] = {1, 2, 3, 4};
            if(image) group.mGroupImage.copy(data, sizeof(data));
            if(pins) group.mPinnedPosts.ids = {id('1'), id('2')};
            RsGxsPostedGroupItem item;
            CHECK(item.fromPostedGroup(group, false));
            const auto bytes = encode(item);
            if(!pins)
                CHECK(bytes.size() == GetTlvStringSize(group.mDescription)
                      + (image ? item.mGroupImage.TlvSize() : 0));

            RsGxsPostedGroupItem decoded;
            RsGenericSerializer::SerializeContext in(
                        const_cast<uint8_t*>(bytes.data()), bytes.size());
            decoded.serial_process(RsGenericSerializer::DESERIALIZE, in);
            CHECK(in.mOk && in.mOffset == bytes.size());
            RsPostedGroup restored;
            CHECK(decoded.toPostedGroup(restored, true));
            CHECK(restored.mDescription == group.mDescription);
            CHECK(restored.mPinnedPosts.ids == group.mPinnedPosts.ids);
            CHECK(restored.mGroupImage.mSize == group.mGroupImage.mSize);
            if(image) CHECK(!memcmp(restored.mGroupImage.mData, data, sizeof(data)));
            item.clear();
            CHECK(item.mPinnedPosts.ids.empty());
        }

    RsGxsPostedGroupItem pinned;
    pinned.mPinnedPosts.ids.insert(id('1'));
    auto bytes = encode(pinned);
    bytes.pop_back();
    RsGxsPostedGroupItem truncated;
    RsGenericSerializer::SerializeContext in(bytes.data(), bytes.size());
    truncated.serial_process(RsGenericSerializer::DESERIALIZE, in);
    CHECK(!in.mOk);
}

static void revisions()
{
    RsPostedPost original;
    original.mMeta.mGroupId = RsGxsGroupId(std::string(32, '1'));
    original.mMeta.mMsgId = id('1');
    original.mMeta.mPublishTs = 100;
    original.mMeta.mAuthorId = RsGxsId(std::string(32, '1'));
    original.mMeta.mMsgName = "Original";
    original.mMeta.mMsgStatus = GXS_SERV::GXS_MSG_STATUS_VOTE_UP;
    original.mUpVotes = 9;
    original.mComments = 3;
    original.mHaveVoted = true;

    auto edit = original;
    edit.mMeta.mMsgId = id('2');
    edit.mMeta.mOrigMsgId = original.mMeta.mMsgId;
    edit.mMeta.mAuthorId = RsGxsId(std::string(32, '2'));
    edit.mMeta.mPublishTs = 101;
    edit.mMeta.mMsgName = "Edited";
    edit.mNotes = "New notes";
    edit.mLink = "https://example.org/edited";
    uint8_t data[] = {5, 6, 7};
    edit.mImage.copy(data, sizeof(data));
    edit.mUpVotes = edit.mComments = 0;

    auto newest = edit;
    newest.mMeta.mMsgId = id('3');
    newest.mMeta.mMsgName = "Newest";
    // A deterministic tie-break is required when edits share a timestamp.
    std::vector<RsPostedPost> input = {original, edit, newest};
    do
    {
        auto posts = input;
        PostedVersions::resolve(posts);
        CHECK(posts.size() == 1);
        const auto& post = posts.front();
        CHECK(post.mMeta.mMsgId == original.mMeta.mMsgId);
        CHECK(post.mMeta.mAuthorId == original.mMeta.mAuthorId);
        CHECK(post.mMeta.mPublishTs == original.mMeta.mPublishTs);
        CHECK(post.mMeta.mMsgStatus == original.mMeta.mMsgStatus);
        CHECK(post.mRevisionId == newest.mMeta.mMsgId);
        CHECK(post.mMeta.mMsgName == "Newest");
        CHECK(post.mNotes == edit.mNotes && post.mLink == edit.mLink);
        CHECK(post.mImage.mSize == sizeof(data));
        CHECK(!memcmp(post.mImage.mData, data, sizeof(data)));
        CHECK(post.mUpVotes == 9 && post.mComments == 3 && post.mHaveVoted);
    } while(std::next_permutation(input.begin(), input.end(),
                                 [](const RsPostedPost& a, const RsPostedPost& b)
                                 { return a.mMeta.mMsgId < b.mMeta.mMsgId; }));

    std::vector<RsPostedPost> orphan = {edit};
    PostedVersions::resolve(orphan);
    CHECK(orphan.empty());
    edit.mMeta.mParentId = id('4');
    orphan = {original, edit};
    PostedVersions::resolve(orphan);
    CHECK(orphan.size() == 1 && orphan.front().mMeta.mMsgName == "Original");

    edit.mMeta.mParentId.clear();
    edit.mMeta.mGroupId = RsGxsGroupId(std::string(32, '2'));
    orphan = {original, edit};
    PostedVersions::resolve(orphan);
    CHECK(orphan.size() == 1 && orphan.front().mMeta.mMsgName == "Original");

    original.mMeta.mOrigMsgId = original.mMeta.mMsgId;
    orphan = {original};
    PostedVersions::resolve(orphan);
    CHECK(orphan.size() == 1);
}

static void adminSignatures()
{
    RsTlvPublicRSAKey publicKey;
    RsTlvPrivateRSAKey privateKey;
    CHECK(GxsSecurity::generateKeyPair(publicKey, privateKey));
    publicKey.keyFlags |= RSTLV_KEY_DISTRIB_ADMIN;
    privateKey.keyFlags |= RSTLV_KEY_DISTRIB_ADMIN;
    RsTlvSecurityKeySet keys;
    keys.public_keys[publicKey.keyId] = publicKey;
    keys.private_keys[privateKey.keyId] = privateKey;

    RsNxsMsg msg(RS_SERVICE_GXS_TYPE_POSTED);
    msg.metaData = new RsGxsMsgMetaData;
    msg.metaData->mGroupId = RsGxsGroupId(std::string(32, '1'));
    msg.metaData->mOrigMsgId = id('1');
    msg.metaData->mAuthorId = RsGxsId(std::string(32, '2'));
    msg.metaData->mMsgName = "Admin edit";
    msg.metaData->mPublishTs = time(nullptr);
    const char payload[] = "Edited body";
    msg.msg.setBinData(payload, sizeof(payload));

    uint32_t metaSize = msg.metaData->serial_size();
    std::vector<char> data(sizeof(payload) + metaSize);
    memcpy(data.data(), payload, sizeof(payload));
    CHECK(msg.metaData->serialise(data.data() + sizeof(payload), &metaSize));
    RsTlvKeySignature signature;
    CHECK(GxsSecurity::getAdminSignature(data.data(), data.size(), keys, signature));
    CHECK(!GxsSecurity::validateAdminSignature(msg, keys));
    msg.metaData->signSet.keySignSet[GxsSecurity::ADMIN_SIGNATURE_INDEX] = signature;
    msg.metaData->mMsgId = id('3');
    CHECK(GxsSecurity::validateAdminSignature(msg, keys));
    // Validation must preserve metadata and be repeatable.
    CHECK(GxsSecurity::validateAdminSignature(msg, keys));
    CHECK(msg.metaData->mMsgId == id('3') && msg.metaData->mOrigMsgId == id('1'));

    msg.metaData->mMsgName = "Tampered";
    CHECK(!GxsSecurity::validateAdminSignature(msg, keys));
    msg.metaData->mMsgName = "Admin edit";
    msg.metaData->mOrigMsgId = id('4');
    CHECK(!GxsSecurity::validateAdminSignature(msg, keys));
    msg.metaData->mOrigMsgId = id('1');
    msg.metaData->mAuthorId = RsGxsId(std::string(32, '3'));
    CHECK(!GxsSecurity::validateAdminSignature(msg, keys));
    msg.metaData->mAuthorId = RsGxsId(std::string(32, '2'));
    static_cast<char*>(msg.msg.bin_data)[0] ^= 1;
    CHECK(!GxsSecurity::validateAdminSignature(msg, keys));
    static_cast<char*>(msg.msg.bin_data)[0] ^= 1;

    auto publisherKeys = keys;
    publisherKeys.private_keys.begin()->second.keyFlags &= ~RSTLV_KEY_DISTRIB_ADMIN;
    publisherKeys.private_keys.begin()->second.keyFlags |= RSTLV_KEY_DISTRIB_PUBLISH;
    publisherKeys.public_keys.begin()->second.keyFlags &= ~RSTLV_KEY_DISTRIB_ADMIN;
    publisherKeys.public_keys.begin()->second.keyFlags |= RSTLV_KEY_DISTRIB_PUBLISH;
    CHECK(!GxsSecurity::getAdminSignature(data.data(), data.size(), publisherKeys, signature));
    CHECK(!GxsSecurity::validateAdminSignature(msg, publisherKeys));
    keys.private_keys.clear();
    CHECK(!GxsSecurity::getAdminSignature(data.data(), data.size(), keys, signature));
    CHECK(GxsSecurity::validateAdminSignature(msg, keys));

    CHECK(GxsSecurity::generateKeyPair(publicKey, privateKey));
    publicKey.keyFlags |= RSTLV_KEY_DISTRIB_ADMIN;
    keys.public_keys.clear();
    keys.public_keys[publicKey.keyId] = publicKey;
    CHECK(!GxsSecurity::validateAdminSignature(msg, keys));
}

int main()
{
    serialization();
    revisions();
    adminSignatures();
    std::cout << "Posted serialization, revision and admin-signature regressions passed" << std::endl;
}
