#include "retroshare/rsidentity.h"
#include "p3gxsdispatch.h"
#include "rsgxsdispatchitems.h"

#define NOT_IMPLEMENTED RS_ERR("NOT implemented yet!")

//=====================================================================================================================//
//                                                p3Service functions                                                  //
//=====================================================================================================================//

int p3GxsDispatch::tick()
{
    NOT_IMPLEMENTED;

    // 1 - make sure the number of directly connected ids is sufficient

    // 2 - make sure that managed ids currently have or request tunnels.

    // 3 - clean dispatches that are unused/empty

    return 0;
}


const std::string GXS_DISPATCH_APP_NAME = "gxs_dispatch";

const uint16_t GXS_DISPATCH_APP_MAJOR_VERSION = 1;
const uint16_t GXS_DISPATCH_APP_MINOR_VERSION = 0;
const uint16_t GXS_DISPATCH_MIN_MAJOR_VERSION = 1;
const uint16_t GXS_DISPATCH_MIN_MINOR_VERSION = 0;

RsServiceInfo p3GxsDispatch::getServiceInfo()
{
    return RsServiceInfo((int)RsServiceType::GXS_DISPATCH,
        GXS_DISPATCH_APP_NAME,
        GXS_DISPATCH_APP_MAJOR_VERSION,
        GXS_DISPATCH_APP_MINOR_VERSION,
        GXS_DISPATCH_MIN_MAJOR_VERSION,
        GXS_DISPATCH_MIN_MINOR_VERSION);
}

//=====================================================================================================================//
//                                              GxsDispatch functionality                                              //
//=====================================================================================================================//

bool p3GxsDispatch::createGxsDispatch(const RsGxsId& own_id,const std::list<RsGxsId>& participants,uint32_t service_id,RsGxsDispatchId& dispatch_id)
{
    // perform a few checks
    if(!rsIdentity->isOwnId(own_id))
    {
        RS_ERR("Trying to create a GxsDispatch with an identity (id=",own_id,"(that is not your own.") ;
        return false;
    }

    RS_STACK_MUTEX(mDispatchMtx);

    // 1 - generate a non existing random dispatch id.

    do { dispatch_id.random(); } while(_dispatches.find(dispatch_id) != _dispatches.end());

    // 2 - store the dispatch info

    DispatchEntry& de(_dispatches[dispatch_id]);		// creates the entry

    de.dispatch_id = dispatch_id;		// unique id of the lobby
    de.own_gxs_id  = own_id;			// ID used to participate in the dispatch
    de.last_activity = time(nullptr);
    de.client_service = service_id;

    for(const auto& id:participants)
        de.gxs_ids[id] = 0;            	// No activity yet.

    return true;
}

void p3GxsDispatch::handleIncomingItem(RsGxsDispatchItem *item)
{
    NOT_IMPLEMENTED;
}

//=====================================================================================================================//
//                                                Routing algorithms                                                   //
//=====================================================================================================================//

// returns:
// 	true: the object is not a duplicate and should be used
// 	false: the object is a duplicate or there is an error, and it should be destroyed.
//
#ifdef TODO
bool p3GxsDispatch::bounceDispatchObject(RsChatLobbyBouncingObject *item,const RsPeerId& peer_id)
{
    rstime_t now = time(NULL) ;
    RsStackMutex stack(mDistributedChatMtx); /********** STACK LOCKED MTX ******/
#ifdef DEBUG_CHAT_LOBBIES
    locked_printDebugInfo() ; // debug

    std::cerr << "Handling ChatLobbyMsg " << std::hex << item->msg_id << ", lobby id " << item->lobby_id << ", from peer id " << peer_id << std::endl;
#endif

    // send upward for display

    std::map<ChatLobbyId,ChatLobbyEntry>::iterator it(_chat_lobbys.find(item->lobby_id)) ;

    if(it == _chat_lobbys.end())
    {
#ifdef DEBUG_CHAT_LOBBIES
        std::cerr << "Chatlobby for id " << std::hex << item->lobby_id << " has no record. Dropping the msg." << std::dec << std::endl;
#endif
        return false ;
    }

    ChatLobbyEntry& lobby(it->second) ;

    // Adds the peer id to the list of friend participants, even if it's not original msg source

    if(peer_id != mServControl->getOwnId())
        lobby.participating_friends.insert(peer_id) ;

    lobby.gxs_ids[item->signature.keyId] = now ;

    // Checks wether the msg is already recorded or not

    std::map<ChatLobbyMsgId,rstime_t>::iterator it2(lobby.msg_cache.find(item->msg_id)) ;

    if(it2 != lobby.msg_cache.end()) // found!
    {
#ifdef DEBUG_CHAT_LOBBIES
        std::cerr << "  Msg already received at time " << it2->second << ". Dropping!" << std::endl ;
#endif
        it2->second = now ;	// update last msg seen time, to prevent echos.
        return false ;
    }
#ifdef DEBUG_CHAT_LOBBIES
    std::cerr << "  Msg not received already. Adding in cache, and forwarding!" << std::endl ;
#endif

    lobby.msg_cache[item->msg_id] = now ;
    lobby.last_activity = now ;

    // Check that if we have a lobby bouncing object, it's not flooding the lobby
    if(!locked_bouncingObjectCheck(item,peer_id,lobby.participating_friends.size()))
        return false;

    // Forward to allparticipating friends, except this peer.

    for(std::set<RsPeerId>::const_iterator it(lobby.participating_friends.begin());it!=lobby.participating_friends.end();++it)
        if((*it)!=peer_id && mServControl->isPeerConnected(mServType, *it))
        {
            RsChatLobbyBouncingObject *obj2 = item->duplicate() ; // makes a copy
            RsChatItem *item2 = dynamic_cast<RsChatItem*>(obj2) ;

            assert(item2 != NULL) ;

            item2->PeerId(*it) ;	// replaces the virtual peer id with the actual destination.

            checkSizeAndSendLobbyMessage(item2) ;
        }

    ++lobby.connexion_challenge_count ;

    return true ;
}
#endif

//=====================================================================================================================//
//                              Implementation of RsGxsTunnelClient methods                                            //
//=====================================================================================================================//

void p3GxsDispatch::notifyTunnelStatus(const RsGxsTunnelId& tunnel_id,uint32_t tunnel_status)
{
    NOT_IMPLEMENTED;

    // 1 - request the tunnel info

    // 2 - update the tunnel info: which distant peer it is, etc.
}

void p3GxsDispatch::receiveData(const RsGxsTunnelId& tunnel_id,unsigned char *data,uint32_t data_size)
{
    NOT_IMPLEMENTED;

    // 1 - deserialize the data and check which dispatch the data goes to

    // 2 - bounce the data to other peers in the dispatch

    // 3 - notify that data to the disptach client.

    // We cannot look in existing tunnel ids which dispatches this item id to, because a single tunnel may be used for multiple
    // dispatches. Only the content of the item will tell.

#ifdef TODO
    // receives the first item.
    {
        RS_STACK_MUTEX(mDispatchMtx) ;


        RsGxsTunnelService::GxsTunnelInfo tinfo;
        if(!mGxsTunnels->getTunnelInfo(tunnel_id,tinfo))
            return ;

        // Check if the data is accepted. We cannot prevent the creation of tunnels at the level of p3GxsTunnels, since tunnels are shared between services.
        // however,

        DistantChatContact& contact(mDistantChatContacts[DistantChatPeerId(tunnel_id)]) ;

        contact.to_id = tinfo.destination_gxs_id ;
        contact.from_id = tinfo.source_gxs_id ;
    }
#endif

    auto *item = dynamic_cast<RsGxsDispatchItem*>(RsGxsDispatchSerialiser().deserialise(data,&data_size)) ;
    free(data) ;	// the tunnel service handed the buffer over to us

    if(item != NULL)
        handleIncomingItem(item) ;
    else
        RS_ERR("  (EE) item could not be deserialised!");
}

void p3GxsDispatch::connectToGxsTunnelService(RsGxsTunnelService *tunnel_service)
{
    NOT_IMPLEMENTED;
}
bool p3GxsDispatch::acceptDataFromPeer(const RsGxsId& gxs_id,const RsGxsTunnelId& tunnel_id,bool am_I_client_side)
{
    // By default we always accept incoming data. This method is always called prior to receiveData().
    // Multiple cases:
    //	1 - gxs_id is already in a dispatch with the same tunnel_id, then data is naturally expected
    //  2 - else this may correspond to an invitation to joint a given dispatch, which can only be decided in receiveData() anyway.

    return true;
}


