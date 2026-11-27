#include "retroshare/rsidentity.h"
#include "util/stacktrace.h"
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
        RS_ERR("Trying to create a GxsDispatch with an identity (id=",own_id," that is not your own).") ;
        return false;
    }

    RS_STACK_MUTEX(mDispatchMtx);

    // 1 - generate a non existing random dispatch id.

    do { dispatch_id = RsGxsDispatchId::random(); } while(_dispatches.find(dispatch_id) != _dispatches.end());

    // 2 - store the dispatch info

    DispatchEntry& de(_dispatches[dispatch_id]);		// creates the entry

    de.dispatch_id = dispatch_id;		// unique id of the dispatch
    de.own_gxs_id  = own_id;			// ID used to participate in the dispatch
    de.last_activity = time(nullptr);
    de.client_services = { service_id };

    for(const auto& id:participants)
        de.gxs_ids[id] = 0;            	// No activity yet.

    return true;
}

bool p3GxsDispatch::sendData(RsGxsDispatchId dispatch_id,uint32_t service_id,const uint8_t *data,uint32_t size)
{
    RsGxsId own_id;
    {
        RS_STACK_MUTEX(mDispatchMtx);

        // 1 - look for the dispatch. Do some checks.

        auto dit = _dispatches.find(dispatch_id);

        if(dit == _dispatches.end())
        {
            RS_ERR("Cannot find dispatch id ",dispatch_id," to send the locally supplied data. See callstack.");
            print_stacktrace();
            return false;
        }
        DispatchEntry& de(dit->second);

        if(de.client_services.find(service_id) == de.client_services.end())
        {
            RS_ERR("trying to send data to dispatch id ",dispatch_id," for service id ",service_id," but this service is not registered in this dispatch.");
            print_stacktrace();
            return false;
        }

        own_id = de.own_gxs_id;
    }

    // 2 - create the data item

    RsGxsDispatchDataItem *item = new RsGxsDispatchDataItem();

    item->data_size   = size;
    item->data        = const_cast<uint8_t*>(data); // the data member is not const, but will not be modified by bounceDispatchItem()
    item->orig_id     = own_id;
    item->dispatch_id = dispatch_id;
    item->service_id  = service_id;

    item->unique_item_counter = RSRandom::random_u64();	// this of course may cause a collision, but the probability is small enough so that we can neglect this possibility

    // 3 - bounce it (which also registers this item as already seen). The source tunnel is zero so that all destination tunnels will be used.

    return bounceDispatchItem(RsGxsTunnelId(),item);
}

void p3GxsDispatch::handleIncomingItem(const RsGxsDispatchItem *item)
{
    switch(item->PacketSubType())
    {
    case RS_PKT_SUBTYPE_GXS_DISPATCH_DATA:
    {
        auto itm = dynamic_cast<const RsGxsDispatchDataItem*>(item);
        handleIncomingDispatchData(itm->dispatch_id,itm->service_id,itm->data,itm->data_size);
    }
        break;

    default:
    case RS_PKT_SUBTYPE_GXS_DISPATCH_STATUS:
    case RS_PKT_SUBTYPE_GXS_DISPATCH_DATA_ACK:
                                                    NOT_IMPLEMENTED;
                                                    break;
    }
}

void p3GxsDispatch::handleIncomingDispatchData(const RsGxsDispatchId& dispatch_id,uint32_t service_id,uint8_t *data,uint32_t data_size)
{
    RsGxsDispatchClient *client = nullptr;

    {
        RS_STACK_MUTEX(mDispatchMtx); /********** STACK LOCKED MTX ******/

        // 1 - find the client services

        auto it = _dispatches.find(dispatch_id);

        if(it == _dispatches.end())
        {
            RsErr() << "Received an incoming data for a dispatch that doesn't exist." << std::endl;
            return;
        }

        // Check that the client service id is registered

        auto clit = _clients.find(service_id);

        if(clit == _clients.end())
        {
            RsErr() << "Received an incoming data for service id " << service_id << " but this service id is not registered." ;
            return;
        }
        client = clit->second;
    }

    client->receiveData(dispatch_id,data,data_size);
}

//=====================================================================================================================//
//                                                Routing algorithms                                                   //
//=====================================================================================================================//

// returns:
// 	true: the object is not a duplicate and should be used
// 	false: the object is a duplicate or there is an error, and it should be destroyed.
//
bool p3GxsDispatch::bounceDispatchItem(const RsGxsTunnelId& incoming_tunnel_id,const RsGxsDispatchItem *item)
{
    time_t now = time(nullptr);
    RS_STACK_MUTEX(mDispatchMtx); /********** STACK LOCKED MTX ******/

    // 1 - Look in existing dispatches. If found, bounce it.

    auto it = _dispatches.find(item->dispatch_id);

    if(it == _dispatches.end())
    {
        RS_ERR("asked to bounce a dispatch item in dispatch " , item->dispatch_id , " that does not exist.");
        return false ;
    }

    DispatchEntry& de(it->second);

    // 2 - check if the item was received before

    if(de.msg_cache.find(item->unique_item_counter) != de.msg_cache.end())
        return false;

    de.msg_cache[item->unique_item_counter] = now; // received now.

    // Adds the peer to the list of participants

    de.gxs_ids[item->orig_id] = now; // update last activity time for this id.
    de.last_activity = now;

#ifdef TODO
    // Check that if we have a lobby bouncing object, it's not flooding the lobby
    if(!locked_bouncingObjectCheck(item,peer_id,lobby.participating_friends.size()))
        return false;
#endif

    // Forward to allparticipating friends, except this peer.

    RsGxsDispatchSerialiser ser;
    uint32_t size = ser.size(const_cast<RsGxsDispatchItem*>(item));
    RsTemporaryMemory mem(size);

    ser.serialise(const_cast<RsGxsDispatchItem*>(item),mem,&size);

    for(const auto& it:de.tunnel_ids)
        if(it.first != incoming_tunnel_id)
            rsGxsTunnel->sendData(it.first,GXS_DISPATCH_SERVICE_ID,mem,size);

    return true ;
}

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

    // 1 - deserialize the data and check which dispatch the data goes to

    auto *item = dynamic_cast<RsGxsDispatchItem*>(RsGxsDispatchSerialiser().deserialise(data,&data_size)) ;
    free(data) ;	// the tunnel service handed the buffer over to us

    if(item != NULL)
    {
        if(bounceDispatchItem(tunnel_id,item)) 	// 2 - bounce the data to other peers in the dispatch
            handleIncomingItem(item); 			// 3 - handle it locally

        delete item;							// 4 - delete the item.
    }
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


