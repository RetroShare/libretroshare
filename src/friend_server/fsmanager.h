#include <map>

#include "util/rsthreads.h"
#include "retroshare/rsfriendserver.h"
#include "retroshare/rspeers.h"
#include "pqi/p3cfgmgr.h"

class FriendServerManager: public RsFriendServer, public RsTickingThread, public p3Config
{
public:
    FriendServerManager();

    virtual void startServer() override ;
    virtual void stopServer() override ;

    virtual bool checkServerAddress(const std::string& addr,uint16_t port, uint32_t timeout_ms) override;

    virtual void setProfilePassphrase(const std::string& passphrase) override { mCachedPGPPassphrase = passphrase; }
    virtual void setServerAddress(const std::string&,uint16_t) override ;
    virtual void setFriendsToRequest(uint32_t) override ;

    virtual uint32_t    friendsToRequest()     override { return mFriendsToRequest ; }
    virtual uint16_t    friendsServerPort()    override { return mServerPort ; }
    virtual std::string friendsServerAddress() override { return mServerAddress ; }

    virtual std::map<RsPeerId,RsFsPeerInfo> getPeersInfo() override;
    virtual bool autoAddFriends() const override { return mAutoAddFriends; }
    virtual void setAutoAddFriends(bool b) override { mAutoAddFriends = b; }
    virtual void allowPeer(const RsPeerId& pid) override;
protected:
    virtual void threadTick() override;

    //===================================================//
    //                  p3Config methods                 //
    //===================================================//

    // Load/save the routing info, the pending items in transit, and the config variables.
    //
    bool loadList(std::list<RsItem*>& items) override ;
    bool saveList(bool& cleanup,std::list<RsItem*>& items) override ;

    RsSerialiser *setupSerialiser() override ;

private:
    void updateStatus(RsFriendServerStatus new_status);

    uint32_t mFriendsToRequest;
    rstime_t mLastFriendReqestCampain;

    // encode the current list of friends obtained through the friendserver and their status

    RsFriendServerStatus mStatus;

    std::map<RsPeerId,std::pair<std::string,PeerFriendshipLevel> > mAlreadyReceivedPeers; // we keep track of these so as to not re-receive the ones we already have.
    std::string mServerAddress ;
    uint16_t mServerPort;
    uint16_t mProxyPort;
    std::string mCachedPGPPassphrase;
    bool mAutoAddFriends ; // should new friends be added automatically?

    RsMutex fsMgrMtx;
};
