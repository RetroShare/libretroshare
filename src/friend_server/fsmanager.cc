#include <cmath>
#include "fsmanager.h"
#include "fsclient.h"

// #define DEBUG_FS_MANAGER 1

RsFriendServer *rsFriendServer = nullptr;

static const rstime_t MIN_DELAY_BETWEEN_FS_REQUESTS =   30;
static const rstime_t MAX_DELAY_BETWEEN_FS_REQUESTS = 3600;
static const uint32_t DEFAULT_FRIENDS_TO_REQUEST    =   10;

static const std::string DEFAULT_PROXY_ADDRESS         = "127.0.0.1";
static const uint16_t    DEFAULT_FRIEND_SERVER_PORT    = 2017;
static const uint16_t    DEFAULT_PROXY_PORT            = 9050;

FriendServerManager::FriendServerManager()
{
    mLastFriendReqestCampain = 0;
    mFriendsToRequest = DEFAULT_FRIENDS_TO_REQUEST;
    mServerPort = DEFAULT_FRIEND_SERVER_PORT;
}
void FriendServerManager::startServer()
{
    if(!isRunning())
    {
        std::cerr << "Starting Friend Server Manager." << std::endl;
        RsTickingThread::start() ;
    }
}
void FriendServerManager::stopServer()
{
    if(isRunning() && !shouldStop())
    {
        std::cerr << "Stopping Friend Server Manager." << std::endl;
        RsTickingThread::askForStop() ;
    }
}

bool FriendServerManager::checkServerAddress(const std::string& addr,uint16_t port, uint32_t timeout_ms)
{
    uint16_t rs_tor_port;
    std::string rs_tor_addr;
    uint32_t flags;

    rsPeers->getProxyServer(RS_HIDDEN_TYPE_TOR,rs_tor_addr,rs_tor_port,flags);

    return FsClient::checkProxyConnection(addr,port,rs_tor_addr,rs_tor_port,timeout_ms);
}

void FriendServerManager::setServerAddress(const std::string& addr,uint16_t port)
{
    mServerAddress = addr;
    mServerPort = port;
}
void FriendServerManager::setFriendsToRequest(uint32_t n)
{
    mFriendsToRequest = n;
}

void FriendServerManager::threadTick()
{
#ifdef DEBUG_FS_MANAGER
    std::cerr << "Ticking FriendServerManager..." << std::endl;
#endif
    std::this_thread::sleep_for(std::chrono::seconds(2));

    if(mServerAddress.empty())
    {
        RsErr() << "No friend server address has been setup. This is probably a bug.";
        return;
    }
    // Check for requests. Compute how much to wait based on how many friends we have already

    std::vector<RsPgpId> friends;
    rsPeers->getPgpFriendList(friends);

    // log-scale interpolation of the delay between two requests.

    if(mFriendsToRequest == 0 || mFriendsToRequest < friends.size())
    {
        RsErr() << "No friends to request! This is unexpected. Returning." << std::endl;
        return;
    }

    // This formula makes RS wait much longuer between two requests to the server when the number of friends is close the
    // wanted number
    //   Delay for 0 friends:   30 secs.
    //   Delay for 1 friends:   30 secs.
    //   Delay for 2 friends:   32 secs.
    //   Delay for 3 friends:   35 secs.
    //   Delay for 4 friends:   44 secs.
    //   Delay for 5 friends:   66 secs.
    //   Delay for 6 friends:  121 secs.
    //   Delay for 7 friends:  258 secs.
    //   Delay for 8 friends:  603 secs.
    //   Delay for 9 friends: 1466 secs.

    RsDbg() << friends.size() << " friends already, " << std::max((int)mFriendsToRequest - (int)friends.size(),0) << " friends to request";

    double s = (friends.size() < mFriendsToRequest)? ( (mFriendsToRequest - friends.size())/(double)mFriendsToRequest) : 1.0;
    rstime_t delay_for_request = MIN_DELAY_BETWEEN_FS_REQUESTS + (int)floor(exp(-1*s + log(MAX_DELAY_BETWEEN_FS_REQUESTS)*(1.0-s)));

    std::cerr << "Delay for " << friends.size() << " friends: " << delay_for_request << " secs." << std::endl;

    rstime_t now = time(nullptr);

    if(mLastFriendReqestCampain + delay_for_request < now)
    {
        mLastFriendReqestCampain = now;

        std::cerr << "Requesting new friends to friend server..." << std::endl;

        // get current proxy port and address from RS Tor proxy

        uint16_t rs_tor_port;
        std::string rs_tor_addr;
        uint32_t flags;

        rsPeers->getProxyServer(RS_HIDDEN_TYPE_TOR,rs_tor_addr,rs_tor_port,flags);

        std::cerr << "Got Tor proxy address/port: " << rs_tor_addr << ":" << rs_tor_port << std::endl;

        std::map<RsPeerId,PeerFriendshipLevel> already_received_peers;

        for(auto& it:mAlreadyReceivedPeers)
        {
            RsPeerDetails det;
            PeerFriendshipLevel lev;

            if(!rsPeers->getPeerDetails(it.first,det) || !det.accept_connection)
                lev = PeerFriendshipLevel::HAS_KEY;
            else
                lev = PeerFriendshipLevel::HAS_ACCEPTED_KEY;

            already_received_peers[it.first] = lev;
        }

        std::map<RsPeerId,std::pair<std::string,PeerFriendshipLevel> > friend_certificates;

        FsClient().requestFriends(mServerAddress,
                                  mServerPort,
                                  rs_tor_addr,
                                  rs_tor_port,
                                  mFriendsToRequest,
                                  mCachedPGPPassphrase,
                                  already_received_peers,
                                  friend_certificates);	// blocking call

        std::cerr << "Got the following list of friend certificates:" << std::endl;

        // Let's put them in a vector to easy searching.

        std::list<RsPeerId> lst;
        rsPeers->getFriendList(lst);
        std::set<RsPeerId> friend_locations_set(lst.begin(),lst.end());

        for(const auto& invite:friend_certificates)
        {
            RsPeerDetails det;
            uint32_t err_code;

            if(!rsPeers->parseShortInvite(invite.second.first,det,err_code))
            {
                RsErr() << "Parsing error " << err_code << " in invite \"" << invite.first << "\"";
                continue;
            }

            mAlreadyReceivedPeers[det.id] = invite.second;

            if(friend_locations_set.find(det.id) != friend_locations_set.end())
            {
                RsDbg() << "    Kwn -- Distant status: " << static_cast<int>(invite.second.second) << " " << det.gpg_id << " " << det.id << " " << det.dyndns;
                continue;
            }

            RsDbg() << "    New -- Distant status: " << static_cast<int>(invite.second.second) << " " << det.gpg_id << " " << det.id << " " << det.dyndns;

            if(mAutoAddFriends)
                rsPeers->addSslOnlyFriend(det.id,det.gpg_id,det);
        }
    }
}

std::map<RsPeerId,RsFriendServer::RsFsPeerInfo> FriendServerManager::getPeersInfo()
{
    std::map<RsPeerId,RsFsPeerInfo> res;

    for(auto it:mAlreadyReceivedPeers)
    {
        RsFsPeerInfo info;

        info.mInvite = it.second.first;
        info.mPeerLevel = it.second.second;

        RsPeerDetails det;

        if(!rsPeers->getPeerDetails(it.first,det) || !det.accept_connection)
            info.mOwnLevel = PeerFriendshipLevel::HAS_KEY;
        else
            info.mOwnLevel = PeerFriendshipLevel::HAS_ACCEPTED_KEY;

        res[it.first] = info;
    }
    return res;
}

