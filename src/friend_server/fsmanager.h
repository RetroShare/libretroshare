#include <map>

#include "util/rsthreads.h"
#include "retroshare/rsfriendserver.h"
#include "retroshare/rspeers.h"

class FriendServerManager: public RsFriendServer, public RsTickingThread
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
protected:
    virtual void threadTick() override;

private:
    uint32_t mFriendsToRequest;
    rstime_t mLastFriendReqestCampain;

    // encode the current list of friends obtained through the friendserver and their status

    std::map<RsPeerId,std::pair<std::string,PeerFriendshipLevel> > mAlreadyReceivedPeers; // we keep track of these so as to not re-receive the ones we already have.
    std::string mServerAddress ;
    uint16_t mServerPort;
    uint16_t mProxyPort;
    std::string mCachedPGPPassphrase;
    bool mAutoAddFriends ; // should new friends be added automatically?
};
