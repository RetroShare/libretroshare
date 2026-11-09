# Posted Regression Tests

These tests build the Posted item serializer, revision resolver and GXS admin
signature implementation without Qt or a running RetroShare node. They also
compile the Posted service and shared GXS exchange implementation.

From the RetroShare checkout, initialize the header dependencies and run:

```sh
git submodule update --init supportlibs/rapidjson supportlibs/librnp
cmake -S libretroshare/tests/posted -B build-posted-tests
cmake --build build-posted-tests -j4
ctest --test-dir build-posted-tests --output-on-failure
```

OpenSSL development files and a C++17 compiler are required. Set
`OPENSSL_ROOT_DIR`, `RAPIDJSON_INCLUDE_DIR` or `RNP_INCLUDE_DIR` if necessary.

Coverage includes legacy groups with and without images, pinned groups with and
without images, truncated pin data, deterministic latest-revision selection,
missing originals, group isolation, original thread state and image retention,
valid admin signatures, absent signatures, tampered messages, publisher-only
keys and incorrect admin keys.

## Behavior and Compatibility

`createPostV2` accepts an optional final `origPostId` argument, preserving existing
call sites. Edits require both an owned signing identity and the board admin key.
Revisions receive an additional admin signature that updated peers verify.
Unsigned revisions in pre-existing local databases are excluded from post data.

The blocking content APIs return one post per original thread. Its title, body,
link and image come from the newest authenticated revision. `mRevisionId`
identifies that revision; `mMeta.mMsgId`, author, timestamp, read state, comments
and votes remain associated with the original. Revisions without a locally
available original are hidden until it arrives. Low-level token APIs still
return individual authenticated versions.

Pins live in the admin-signed board group and refer to original message IDs.
Old groups remain readable, and unpinned groups retain their old wire layout.
Older clients do not implement pinning or revision display; use updated clients
on both peers to verify these features.

## GUI Checks

After building the Qt GUI, use an admin node and a subscribed non-admin node:

1. Create posts containing text, links and static or animated images. Edit each
   field, remove an image, and edit a post a second time. Confirm only one post
   appears and existing comments, votes and links still work.
2. Right-click a post while another post is selected. Confirm Edit Post and
   Pin Post affect the right-clicked post. Non-admins must not see those actions.
3. Pin several posts, select New, Top and Hot, and use search and pagination.
   Matching pinned posts must precede matching unpinned posts in every sort.
4. Unpin a post, restart both nodes and edit the board description. Confirm the
   pin list survives reload, synchronization and unrelated board edits.
5. Cause a publish failure. Confirm the dialog preserves text and image data,
   displays an error, and allows retrying without duplicate submissions.
