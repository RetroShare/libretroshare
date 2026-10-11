#include "p3gxsdispatch.h"

#define NOT_IMPLEMENTED RS_ERR() << "NOT implemented yet!" ;

// returns:
// 	true: the object is not a duplicate and should be used
// 	false: the object is a duplicate or there is an error, and it should be destroyed.
//
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

//=====================================================================================================================//
//                              Implementation of RsGxsTunnelClient methods                                            //
//=====================================================================================================================//

void p3GxsDispatch::notifyTunnelStatus(const RsGxsTunnelId& tunnel_id,uint32_t tunnel_status)
{
    NOT_IMPLEMENTED;
}
void p3GxsDispatch::receiveData(const RsGxsTunnelId& id,unsigned char *data,uint32_t data_size)
{
    NOT_IMPLEMENTED;
}
void p3GxsDispatch::connectToGxsTunnelService(RsGxsTunnelService *tunnel_service)
{
    NOT_IMPLEMENTED;
}
bool p3GxsDispatch::acceptDataFromPeer(const RsGxsId& gxs_id,const RsGxsTunnelId& tunnel_id,bool am_I_client_side)
{
    NOT_IMPLEMENTED;
}


