/*******************************************************************************
 * libretroshare/src/chat: distantchat.cc                                      *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2015 by Cyril Soler <csoler@users.sourceforge.net>                *
 *                                                                             *
 * This program is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU Lesser General Public License as              *
 * published by the Free Software Foundation, either version 3 of the          *
 * License, or (at your option) any later version.                             *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                *
 * GNU Lesser General Public License for more details.                         *
 *                                                                             *
 * You should have received a copy of the GNU Lesser General Public License    *
 * along with this program. If not, see <https://www.gnu.org/licenses/>.       *
 *                                                                             *
 *******************************************************************************/
#include <unistd.h>

#include "openssl/rand.h"
#include "openssl/dh.h"
#include "openssl/err.h"

#include "crypto/rsaes.h"
#include "util/rsprint.h"
#include "util/rsmemory.h"

#include <retroshare/rsidentity.h>
#include <retroshare/rsiface.h>

#include <rsserver/p3face.h>
#include <services/p3idservice.h>
#include <gxs/gxssecurity.h>
#include <turtle/p3turtle.h>
#include <retroshare/rsids.h>

#include "p3gxstunnel.h"

#define DEBUG_GXS_TUNNEL_MANAGEMENT
#define DEBUG_GXS_TUNNEL_TRAFFIC
#define DEBUG_GXS_TUNNEL_VPID

static const uint32_t GXS_TUNNEL_KEEP_ALIVE_TIMEOUT = 6 ; // send keep alive packet so as to avoid tunnel breaks.

static const uint32_t RS_GXS_TUNNEL_DELAY_BETWEEN_RESEND     = 10 ; // re-send every 10 secs.
static const rstime_t RS_GXS_TUNNEL_DH_RESTART_MIN_DELAY     = 10 ; // min delay between two DH restarts caused by bad packets on one virtual peer
static const uint32_t RS_GXS_TUNNEL_DATA_PRINT_STORAGE_DELAY = 600 ; // store old message ids for 10 minutes.

static const uint32_t GXS_TUNNEL_ENCRYPTION_HMAC_SIZE    = SHA_DIGEST_LENGTH ;
static const uint32_t GXS_TUNNEL_ENCRYPTION_IV_SIZE      = 8 ;

#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
static const uint32_t INTERVAL_BETWEEN_DEBUG_DUMP        = 4 ;
#endif
static const uint32_t INTERVAL_BETWEEN_VPIDS_SANITY_CHECK  = 60 ; // check vpids anyway every 60 secs, to discard unused ones.
static const uint32_t INTERVAL_BEFORE_VPID_IS_ASSUMED_DEAD = 30 ; // a vpid is supposed to be kept alive. If nothing happens for 3 secs, we just don't used it.

static std::string GXS_TUNNEL_APP_NAME = "GxsTunnels" ;

static const uint8_t GXS_TUNNEL_APP_MAJOR_VERSION = 0x01 ;
static const uint8_t GXS_TUNNEL_APP_MINOR_VERSION = 0x00 ;
static const uint8_t GXS_TUNNEL_MIN_MAJOR_VERSION = 0x01 ;
static const uint8_t GXS_TUNNEL_MIN_MINOR_VERSION = 0x00 ;

RsGxsTunnelService *rsGxsTunnel = NULL ;

p3GxsTunnelService::p3GxsTunnelService(RsGixs *pids) 
            : mGixs(pids), mGxsTunnelMtx("GXS tunnel")
{
    mTurtle = nullptr ;
	mCurrentPacketCounter = 0 ;
    mVpidsChanged = false;
}

void p3GxsTunnelService::connectToTurtleRouter(p3turtle *tr)
{
	mTurtle = tr ;
	tr->registerTunnelService(this) ;
}

bool p3GxsTunnelService::registerClientService(uint32_t service_id,RsGxsTunnelService::RsGxsTunnelClientService *service)
{
    RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/

    if(mRegisteredServices.find(service_id) != mRegisteredServices.end())
    {
        std::cerr << "(EE) p3GxsTunnelService::registerClientService(): trying to register client " << std::hex << service_id << std::dec << ", which is already registered!" << std::endl;
        return false;
    }
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
    std::cerr << "p3GxsTunnelService::registerClientService(): registering client service " << std::hex << service_id << std::dec << std::endl;
#endif
    
    mRegisteredServices[service_id] = service ;
    return true ;
}

int p3GxsTunnelService::tick()
{
    
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
    rstime_t now = time(NULL);
	static rstime_t last_dump = 0;
    
    if(now > last_dump + INTERVAL_BETWEEN_DEBUG_DUMP )
    {
        last_dump = now ;
        debug_dump() ;
    }
#endif
    
    {
        RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/

        static rstime_t last_vpids_sanity_check = 0;

        if(mVpidsChanged || (now > last_vpids_sanity_check + INTERVAL_BETWEEN_VPIDS_SANITY_CHECK))
        {
            locked_updateTunnels();
            mVpidsChanged = false;
            last_vpids_sanity_check = now;
        }
    }

        // flush unsent elements

    flush() ;

    return 0 ;
}

RsServiceInfo p3GxsTunnelService::getServiceInfo()
{
    return RsServiceInfo(RS_SERVICE_TYPE_GXS_TUNNEL, 
		GXS_TUNNEL_APP_NAME,
		GXS_TUNNEL_APP_MAJOR_VERSION, 
		GXS_TUNNEL_APP_MINOR_VERSION, 
		GXS_TUNNEL_MIN_MAJOR_VERSION, 
		GXS_TUNNEL_MIN_MINOR_VERSION);
}

// Tunnel management: this function sweeps the virtual peer ids and updates the available tunnels accordingly,
// deletes inactive tunnels, etc.

void p3GxsTunnelService::locked_updateTunnels()
{
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
    std::cerr << "Updating tunnels..." << std::endl;
#endif

    auto vpid_accepted = [](const GxsTunnelDHInfo& info,rstime_t now) -> bool {

        return info.status == GxsTunnelVirtualPeerIdStatus::DH_DONE  			// the vpid doesn't exist anymore
               && now < info.last_data_TS + INTERVAL_BEFORE_VPID_IS_ASSUMED_DEAD ;// the vpid is non responsive. Could be a DH problem.
    };

    rstime_t now = time(nullptr);

    std::map<RsGxsTunnelId,GxsTunnelStatus> new_tunnel_states;

    // 1 - check tunnels health

    for(auto tunnel_it =_gxs_tunnel_contacts.begin();tunnel_it!= _gxs_tunnel_contacts.end();++tunnel_it)
    {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
        std::cerr << "  Tunnel ID " << tunnel_it->first << ": " ;
#endif
        auto& tunnel(tunnel_it->second);

        if(!tunnel.preferred_virtual_peer_id.isNull())
        {
            auto vpid_it2 = _gxs_tunnel_virtual_peer_ids.find(tunnel.preferred_virtual_peer_id);

            // 0 - check whether the preferred virtual peer id is alive. If not, set it to 0.

            if(vpid_it2 == _gxs_tunnel_virtual_peer_ids.end() || !vpid_accepted(vpid_it2->second,now))
            {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
                std::cerr << " vpid " << tunnel.preferred_virtual_peer_id << " is removed or inactive. Resetting." << std::endl;
#endif
                tunnel.preferred_virtual_peer_id.clear();
                new_tunnel_states[tunnel_it->first] = GxsTunnelStatus::TUNNEL_DN;
            }
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
            else
                std::cerr << " vpid " << tunnel.preferred_virtual_peer_id << " is active." << std::endl;
#endif
        }
        // 1 - Remove any tunnel that was remotely closed, since we cannot use it anymore.

        if(tunnel.status == GxsTunnelStatus::REMOTELY_CLOSED && tunnel.last_contact + 20 < now)
        {
            new_tunnel_states[tunnel_it->first] = GxsTunnelStatus::REMOTELY_CLOSED;
            locked_dropPendingData(tunnel_it->first) ;

            std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::iterator tmp = tunnel_it ;
            ++tmp ;
            _gxs_tunnel_contacts.erase(tunnel_it) ;
            tunnel_it=tmp ;
            continue ;
        }

        // 2 - re-digg tunnels that have died out of inaction

        if(tunnel.last_contact+20+GXS_TUNNEL_KEEP_ALIVE_TIMEOUT < now)
        {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
            std::cerr << "(II) GxsTunnelService:: connection interrupted with peer. Re-digging tunnels." << std::endl;
#endif
            tunnel.preferred_virtual_peer_id.clear() ;

            new_tunnel_states[tunnel_it->first] = GxsTunnelStatus::TUNNEL_DN;

            // Also reset turtle router monitoring so as to make the tunnel handling more responsive. If we don't do that,
            // the TR will wait 60 secs for the tunnel to die, which causes a significant waiting time in the chat window.

            if(tunnel.direction == RsTurtleGenericTunnelItem::DIRECTION_SERVER)
            {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
                std::cerr << "(II) GxsTunnelService:: forcing new tunnel campain." << std::endl;
#endif

                mTurtle->forceReDiggTunnels( randomHashFromDestinationGxsId(tunnel.to_gxs_id) );
            }
        }
    }

    // 2 - go through all existing virtual peer ids and select the last active one to talk to.

    for(auto vpid_it = _gxs_tunnel_virtual_peer_ids.begin();vpid_it != _gxs_tunnel_virtual_peer_ids.end();++vpid_it)
    {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
        std::cerr << "  Virtual Peer ID " << vpid_it->first << ": " ;
#endif
        const TurtleVirtualPeerId& vpid = vpid_it->first;
        const GxsTunnelDHInfo& dhinfo = vpid_it->second;

        if(!vpid_accepted(dhinfo,now))
        {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
            std::cerr << " not ready. Status is " << static_cast<int>(dhinfo.status) << std::endl;
#endif
            continue;
        }
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
        else
            std::cerr << " ready. Status is " << static_cast<int>(dhinfo.status) << " tunnel id = " << dhinfo.tunnel_id << std::endl;
#endif

        auto tunnel_it = _gxs_tunnel_contacts.find(dhinfo.tunnel_id);

        if(tunnel_it == _gxs_tunnel_contacts.end())
        {
            if(dhinfo.direction == RsTurtleGenericDataItem::DIRECTION_CLIENT)
            {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
                std::cerr << "   No tunnel for this VPID and we're acting as a server. Creating one." << std::endl;
#endif
                GxsTunnelPeerInfo& pinfo(_gxs_tunnel_contacts[dhinfo.tunnel_id]);

                pinfo.last_contact = time(nullptr);
                pinfo.last_keep_alive_sent = 0;
                pinfo.status = GxsTunnelStatus::TUNNEL_DN;	// so that when comparing with the new status it will be noticed
                pinfo.preferred_virtual_peer_id = vpid;
                pinfo.to_gxs_id = dhinfo.to_gxs_id;
                pinfo.own_gxs_id = dhinfo.own_gxs_id;
                pinfo.direction = dhinfo.direction;
                pinfo.hash = dhinfo.hash;

                new_tunnel_states[dhinfo.tunnel_id] = GxsTunnelStatus::CAN_TALK;	// at least one vpid with DH done.
            }
            else
                continue;	// we ignore this vpid because it's emitted locally, so if there's no tunnel, it means the tunnel was removed and the vpid can be let to die.
        }
        else
        {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
            std::cerr << "   Existing tunnel with preferred vpid " << tunnel_it->second.preferred_virtual_peer_id << std::endl;
#endif
            if(tunnel_it->second.preferred_virtual_peer_id < vpid)
            {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
                std::cerr << "   (Heuristic)-Replacing with active vpid " << vpid << std::endl;
#endif
                tunnel_it->second.preferred_virtual_peer_id = vpid;
                new_tunnel_states[tunnel_it->first] = GxsTunnelStatus::CAN_TALK;	// at least one vpid with DH done.
            }
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
            else
                std::cerr << "   Keeping it." << std::endl;
#endif
        }
    }

    // 3 - go through tunnels again and compare the new state with the previous state. Make a notification appropriately

#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
    std::cerr << "Going through tunnel changes:" << std::endl;
    for(auto cit:new_tunnel_states)
        std::cerr << "  tunnel " << cit.first << ": new status " << static_cast<int>(cit.second) << std::endl;
#endif
    for(auto tunnel_it =_gxs_tunnel_contacts.begin();tunnel_it!= _gxs_tunnel_contacts.end();++tunnel_it)
    {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
        std::cerr << "   " << tunnel_it->first << " : " ;
#endif
        auto nit = new_tunnel_states.find(tunnel_it->first);

        if(nit == new_tunnel_states.end())
        {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
            std::cerr << " No change recorded.  " << std::endl;
#endif
            continue;
        }
        if(nit->second == tunnel_it->second.status)	// nothing to do. That means the tunnel hasn't any change
        {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
            std::cerr << " state unchanged: " << static_cast<int>(nit->second) << std::endl;
#endif
            continue;
        }

#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
        std::cerr << " new state: " << static_cast<int>(nit->second)
                  << " old state: " << static_cast<int>(tunnel_it->second.status) << std::endl;
#endif
        tunnel_it->second.status = nit->second;

        for(auto cid:tunnel_it->second.client_services)
        {
            auto client_it = mRegisteredServices.find(cid);

            if(client_it == mRegisteredServices.end())
                std::cerr << "(EE) asked to notify a tunnel for a service that is not registered (service id=" << cid << std::endl;
            else
                client_it->second->notifyTunnelStatus(tunnel_it->first,nit->second);
        }
    }
}

// This function flushes items to be sent to tunnels, selecting the proper vpid to be used. It also cleans up old
// items.

void p3GxsTunnelService::flush()
{
    rstime_t now = time(nullptr);

    // Flush pending DH items. This is a higher priority, so we deal with them first.
    
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
    if(!pendingDHItems.empty() || !pendingGxsTunnelItems.empty() || !pendingGxsTunnelDataItems.empty())
        std::cerr << "p3GxsTunnelService::flush() flushing pending items." << std::endl;
#endif
    {
	    RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/

	    for(std::list<RsGxsTunnelDHPublicKeyItem*>::iterator it=pendingDHItems.begin();it!=pendingDHItems.end();)
		    if(locked_sendClearTunnelData(*it) )
			{
				delete *it ;
			    it = pendingDHItems.erase(it) ;
			}
		    else
			    ++it ;
    }
        
    // Flush items that could not be sent, probably because of a Mutex protected zone.
    //
    {
	    RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/

	    for(std::list<RsGxsTunnelItem*>::iterator it=pendingGxsTunnelItems.begin();it!=pendingGxsTunnelItems.end();)
		    if(locked_sendEncryptedTunnelData(*it) )
			{
				delete *it ;
			    it = pendingGxsTunnelItems.erase(it) ;
			}
		    else
		    {
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
			    std::cerr << "Cannot send encrypted data item to tunnel " << (*it)->PeerId() << std::endl;
#endif
                ++it ;
            }
    }
    
    // Look at pending data item, and re-send them if necessary.
    
    {
	    RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/

	    rstime_t now = time(NULL) ;

	    for(std::map<uint64_t, GxsTunnelData>::iterator it = pendingGxsTunnelDataItems.begin();it != pendingGxsTunnelDataItems.end();++it)
		    if(now > RS_GXS_TUNNEL_DELAY_BETWEEN_RESEND + it->second.last_sending_attempt)
		    {
			    if(locked_sendEncryptedTunnelData(it->second.data_item))
			    {                    		
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
				    std::cerr << "  sending data item #" << std::hex << it->first << std::dec << std::endl;
#endif
				    it->second.last_sending_attempt = now ;

                    // the item is kept until a ACK is received.
			    }
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
			    else
				    std::cerr << "  Cannot send item " << std::hex << it->first << std::dec << std::endl;
#endif
		    }
    }

    RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/

    for(std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::iterator it(_gxs_tunnel_contacts.begin());it!=_gxs_tunnel_contacts.end();++it)
    {
        // send keep alive packets to active tunnels.

        if(it->second.last_keep_alive_sent + GXS_TUNNEL_KEEP_ALIVE_TIMEOUT < now && it->second.status == GxsTunnelStatus::CAN_TALK)
        {
            RsGxsTunnelStatusItem *cs = new RsGxsTunnelStatusItem ;

            cs->status = RS_GXS_TUNNEL_FLAG_KEEP_ALIVE;
            cs->PeerId(RsPeerId(it->first)) ;

            // we send off-mutex to avoid deadlock.

            pendingGxsTunnelItems.push_back(cs) ;

            it->second.last_keep_alive_sent = now ;
        #ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
            std::cerr << "(II) GxsTunnelService:: Sending keep alive packet to gxs id " << it->first << std::endl;
        #endif
       }

        // clean old received data prints.

        for(std::map<uint64_t,rstime_t>::iterator it2=it->second.received_data_prints.begin();it2!=it->second.received_data_prints.end();)
            if(now > it2->second + RS_GXS_TUNNEL_DATA_PRINT_STORAGE_DELAY)
            {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
                std::cerr << "(II) erasing old data print for message #" << it2->first << " in tunnel " << it->first << std::endl;
#endif
                std::map<uint64_t,rstime_t>::iterator tmp(it2) ;
                ++tmp ;
                it->second.received_data_prints.erase(it2) ;
                it2 = tmp ;
           }
        else
            ++it2 ;
    }
}

// Handles items at the tunnel level. In this function the PeerId is the GXS tunnel ID.

void p3GxsTunnelService::handleIncomingItem(const RsGxsTunnelId& tunnel_id,RsGxsTunnelItem *item)
{
    if(item == NULL)
	    return ;

    // We have 3 things to do:
    //
    // 1 - if it's a data item, send an ACK
    // 2 - if it's an ack item, mark the item as properly received, and remove it from the queue
    // 3 - if it's a status item, act accordingly.

    switch(item->PacketSubType())
    {

    case RS_PKT_SUBTYPE_GXS_TUNNEL_DATA:		handleRecvTunnelDataItem(tunnel_id,dynamic_cast<RsGxsTunnelDataItem*>(item)) ;
	    break ;

    case RS_PKT_SUBTYPE_GXS_TUNNEL_DATA_ACK:	handleRecvTunnelDataAckItem(tunnel_id,dynamic_cast<RsGxsTunnelDataAckItem*>(item)) ;
	    break ;

    case RS_PKT_SUBTYPE_GXS_TUNNEL_STATUS:		handleRecvStatusItem(tunnel_id,dynamic_cast<RsGxsTunnelStatusItem*>(item)) ;
	    break ;

    default:
	    std::cerr << "(EE) impossible situation. DH items should be handled at the service level" << std::endl;
    }

    delete item ;
}

void p3GxsTunnelService::handleRecvTunnelDataAckItem(const RsGxsTunnelId &/*id*/,RsGxsTunnelDataAckItem *item)
{
    RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/
    
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
    std::cerr << "p3GxsTunnelService::handling RecvTunnelDataAckItem()" << std::endl;
    std::cerr << "  item counter = " << std::hex << item->unique_item_counter << std::dec << std::endl;
#endif
    
    // remove it from the queue.
    
    std::map<uint64_t,GxsTunnelData>::iterator it = pendingGxsTunnelDataItems.find(item->unique_item_counter) ;
    
    if(it == pendingGxsTunnelDataItems.end())
    {
        std::cerr << "  (EE) item number " << std::hex << item->unique_item_counter << std::dec << " is unknown. This is unexpected." << std::endl;
        return ;
    }
    
    delete it->second.data_item ;
    pendingGxsTunnelDataItems.erase(it) ;
}

void p3GxsTunnelService::handleRecvTunnelDataItem(const RsGxsTunnelId& tunnel_id,RsGxsTunnelDataItem *item) 
{
    // imediately send an ACK for this item
    
    RsGxsTunnelDataAckItem *ackitem = new RsGxsTunnelDataAckItem ;
    
    ackitem->unique_item_counter = item->unique_item_counter ;
    ackitem->PeerId(RsPeerId(tunnel_id)) ;
    
    {
	    RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/
	    pendingGxsTunnelItems.push_back(ackitem) ;	// we use the queue that does not need an ACK, in order to avoid an infinite loop ;-)
    }
    
    // notify the client for the received data
    
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
    std::cerr << "p3GxsTunnelService::handleRecvTunnelDataItem()" << std::endl;
    std::cerr << "    data size  = " << item->data_size << std::endl;
    std::cerr << "    service id = " << std::hex << item->service_id << std::dec << std::endl;
    std::cerr << "    counter id = " << std::hex << item->unique_item_counter << std::dec << std::endl;
#endif
    
    RsGxsTunnelClientService *service = NULL ;
    RsGxsId peer_from ;
    bool is_client_side = false ;
    
    {
	    RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/
	    std::map<uint32_t,RsGxsTunnelClientService *>::const_iterator it = mRegisteredServices.find(item->service_id) ;

	    if(it == mRegisteredServices.end())
	    {
		    std::cerr << "  (EE) no registered service with ID " << std::hex << item->service_id << std::dec << ". Rejecting item." << std::endl;
		    return ;
	    }
        service = it->second ;
    
        std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::iterator it2 = _gxs_tunnel_contacts.find(tunnel_id) ;
            
        if(it2 != _gxs_tunnel_contacts.end())
        {
            it2->second.client_services.insert(item->service_id) ;
            peer_from = it2->second.to_gxs_id ;
            is_client_side = (it2->second.direction == RsTurtleGenericDataItem::DIRECTION_SERVER);
        }
        else
        {
            std::cerr << "(EE) p3GxsTunnelService::handleRecvTunnelDataItem(): no tunnel found for tunnel id " << tunnel_id << std::endl;
            return;
        }

        // Check if the item has already been received. This is necessary because we actually re-send items until an ACK is received. If the ACK gets lost (connection interrupted) the
        // item may be received twice. This is conservative and ensure that no item is lost nor received twice.

        if(it2->second.received_data_prints.find(item->unique_item_counter) != it2->second.received_data_prints.end())
        {
            std::cerr << "(WW) received the same data item #" << std::hex << item->unique_item_counter << std::dec << " twice in last 20 mins. Tunnel id=" << tunnel_id << ". Probably a replay. Item will be dropped." << std::endl;
            return ;
        }
        it2->second.received_data_prints[item->unique_item_counter] = time(NULL) ;
    }
    
    if(service->acceptDataFromPeer(peer_from,tunnel_id,is_client_side))
	    service->receiveData(tunnel_id,item->data,item->data_size) ;
    
    item->data = NULL ;		// avoids deletion, since the client has the memory now
    item->data_size = 0 ;
}

void p3GxsTunnelService::handleRecvStatusItem(const RsGxsTunnelId& tunnel_id, RsGxsTunnelStatusItem *cs)
{
    std::vector<GxsTunnelStatus> notifications ;
    std::set<RsGxsTunnelClientService*> clients ;

#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
    std::cerr << "p3GxsTunnelService::handleRecvStatusItem(): tunnel_id=" << tunnel_id << " status=" << cs->status << std::endl;
#endif

    switch(cs->status)
    {
    case RS_GXS_TUNNEL_FLAG_CLOSING_DISTANT_CONNECTION:
    {
	    RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/

	    std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::iterator it = _gxs_tunnel_contacts.find(tunnel_id) ;

	    if(it == _gxs_tunnel_contacts.end())
	    {
		    std::cerr << "(EE) Cannot mark tunnel connection as closed. No connection openned for tunnel id " << tunnel_id << ". Unexpected situation." << std::endl;
		    return ;
	    }

#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
	    std::cerr << "  Marking distant chat as remotely closed for tunnel id " << tunnel_id << std::endl;
#endif
	    if(it->second.direction == RsTurtleGenericDataItem::DIRECTION_CLIENT)
            {
            it->second.status = GxsTunnelStatus::REMOTELY_CLOSED ;
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
            	std::cerr << "  This is server side. The tunnel cannot be re-openned, so we give it up." << std::endl;
#endif
            }
	    else
            {
            it->second.status = GxsTunnelStatus::TUNNEL_DN ;
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
            	std::cerr << "  This is client side. The tunnel will be re-openned automatically." << std::endl;
#endif
            }

        notifications.push_back(GxsTunnelStatus::REMOTELY_CLOSED) ;
    } // nothing more to do, because the decryption routing will update the last_contact time when decrypting.
	    break ;

    case RS_GXS_TUNNEL_FLAG_KEEP_ALIVE:
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
        std::cerr << "GxsTunnelService::handleRecvGxsTunnelStatusItem(): received keep alive packet for tunnel! peerId=" << cs->PeerId() << " tunnel=" << tunnel_id << std::endl;
#endif
	    break ;

    case RS_GXS_TUNNEL_FLAG_ACK_DISTANT_CONNECTION:
    {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
	    std::cerr << "Received ACK item from the distant peer!" << std::endl;
#endif

	    // in this case we notify the clients using this tunnel.

        notifications.push_back(GxsTunnelStatus::CAN_TALK) ;
    }
	    break ;

    default:
	    std::cerr << "(EE) unhandled tunnel status " << std::hex << cs->status << std::dec << std::endl;
	    break ;
    }

    // notify all clients

#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
    std::cerr << "  notifying clients. Prending notifications: " << notifications.size() << std::endl;
#endif

    if(notifications.size() > 0)
    {
	    RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/

	    std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::iterator it = _gxs_tunnel_contacts.find(tunnel_id) ;

        if(it == _gxs_tunnel_contacts.end())
        {
            std::cerr << "(EE) No information for tunnel id " << tunnel_id << ". Unexpected situation." << std::endl;
            return ;
        }
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
	    std::cerr << "    " << it->second.client_services.size() << " client services for tunnel id " << tunnel_id << std::endl;
#endif

	    for(std::set<uint32_t>::const_iterator it2(it->second.client_services.begin());it2!=it->second.client_services.end();++it2)
	    {
		    std::map<uint32_t,RsGxsTunnelClientService*>::const_iterator it3=mRegisteredServices.find(*it2) ;

		    if(it3 != mRegisteredServices.end())
			    clients.insert(it3->second) ;
	    }
    }

#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
    std::cerr << "  notifying " << clients.size() << " clients." << std::endl;
#endif

    for(std::set<RsGxsTunnelClientService*>::const_iterator it(clients.begin());it!=clients.end();++it)
	    for(uint32_t i=0;i<notifications.size();++i)
	    {
		    (*it)->notifyTunnelStatus(tunnel_id,notifications[i]) ;
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
            std::cerr << "  notifying client " << (void*)(*it) << " of status " << static_cast<int>(notifications[i]) << std::endl;
#endif
	    }
}

bool p3GxsTunnelService::handleTunnelRequest(const RsFileHash& hash,const RsPeerId& /*peer_id*/)
{
	RsStackMutex stack(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/

	// look into owned GXS ids, and see if the hash corresponds to the expected hash
	//
	std::list<RsGxsId> own_id_list ;
	rsIdentity->getOwnIds(own_id_list) ;

	// extract the GXS id from the hash

	RsGxsId destination_id = destinationGxsIdFromHash(hash) ;

	// linear search. Not costly because we have typically a low number of IDs. Otherwise, this really should be avoided!

	for(std::list<RsGxsId>::const_iterator it(own_id_list.begin());it!=own_id_list.end();++it)
		if(*it == destination_id)
		{
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
			std::cerr << "GxsTunnelService::handleTunnelRequest: received tunnel request for hash " << hash << std::endl;
			std::cerr << "  answering true!" << std::endl;
#endif
			return true ;
		}

	return false ;
}

void p3GxsTunnelService::addVirtualPeer(const TurtleFileHash& hash,const TurtleVirtualPeerId& virtual_peer_id,RsTurtleGenericTunnelItem::Direction dir)
{
#ifdef DEBUG_GXS_TUNNEL_VPID
	std::cerr << "GxsTunnelService:: received new virtual peer " << virtual_peer_id << " for hash " << hash << ", dir=" << dir << std::endl;
#endif
	RsGxsId own_gxs_id ;
    rstime_t now = time(nullptr);

	{
		RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/

		GxsTunnelDHInfo& dhinfo( _gxs_tunnel_virtual_peer_ids[virtual_peer_id] ) ;
        dhinfo.to_gxs_id.clear() ;

        if(dhinfo.dh != nullptr)
			DH_free(dhinfo.dh) ;

        dhinfo.dh = nullptr ;
        dhinfo.last_data_TS = now ;		// this avoids the tunnel to be removed before it receives a DH key.
        dhinfo.direction = dir ;
		dhinfo.hash = hash ;
        dhinfo.status = GxsTunnelVirtualPeerIdStatus::DH_TODO;
		dhinfo.tunnel_id.clear();

		if(dir == RsTurtleGenericTunnelItem::DIRECTION_CLIENT)	// server side
		{
			// check that a tunnel is not already working for this hash. If so, give up.

			own_gxs_id = destinationGxsIdFromHash(hash) ;
		}
		else	// client side
		{
            // look for the GxsTunnelContact that contains this hash, so we can know which id was used.
			std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::const_iterator it = _gxs_tunnel_contacts.begin();
            
            while(it != _gxs_tunnel_contacts.end() && it->second.hash != hash) ++it ;

			if(it == _gxs_tunnel_contacts.end())
			{
				std::cerr << "(EE) no pre-registered peer for hash " << hash << " on client side. This is a bug." << std::endl;
				return ;
			}

#ifdef TO_REMOVE
			if(it->second.status == RS_GXS_TUNNEL_STATUS_CAN_TALK)
			{
#ifdef DEBUG_GXS_TUNNEL    
				std::cerr << "  virtual peer is for a distant chat session that is already openned and alive. Giving it up." << std::endl;
#endif
				return ;
			}
#endif
			own_gxs_id = it->second.own_gxs_id ;
		}

#ifdef DEBUG_GXS_TUNNEL_VPID
		std::cerr << "  Creating new virtual peer ID entry and empty DH session key." << std::endl;
#endif

	}

#ifdef DEBUG_GXS_TUNNEL_VPID
	std::cerr << "  Adding virtual peer " << virtual_peer_id << " for chat hash " << hash << std::endl;
#endif

	// Start a new DH session for this tunnel
	RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/

	locked_restartDHSession(virtual_peer_id,own_gxs_id) ;
}

void p3GxsTunnelService::locked_restartDHSession(const RsPeerId& virtual_peer_id,const RsGxsId& own_gxs_id)
{
#ifdef DEBUG_GXS_TUNNEL_VPID
    std::cerr << "Starting new DH session." << std::endl;
#endif
    GxsTunnelDHInfo& dhinfo = _gxs_tunnel_virtual_peer_ids[virtual_peer_id] ;	// creates it, if necessary

    dhinfo.status = GxsTunnelVirtualPeerIdStatus::DH_TODO ;
    dhinfo.own_gxs_id = own_gxs_id ;
    dhinfo.last_dh_restart = time(NULL) ;
    dhinfo.dropped_packets = 0 ;

    if(!locked_initDHSessionKey(dhinfo.dh))
    {
        std::cerr << "  (EE) Cannot start DH session. Something went wrong." << std::endl;
        return ;
    }
    dhinfo.status = GxsTunnelVirtualPeerIdStatus::DH_HALF ;

    if(!locked_sendDHPublicKey(dhinfo.dh,own_gxs_id,virtual_peer_id))
        std::cerr << "  (EE) Cannot send DH public key. Something went wrong." << std::endl;
}

void p3GxsTunnelService::removeVirtualPeer(const TurtleFileHash& hash,const TurtleVirtualPeerId& virtual_peer_id)
{
#ifdef TO_REMOVE
    bool tunnel_dn = false ;
#endif
    std::set<RsGxsTunnelClientService*> client_services ;
    RsGxsTunnelId tunnel_id ;

#ifdef DEBUG_GXS_TUNNEL_VPID
    std::cerr << "GxsTunnelService: Removing virtual peer " << virtual_peer_id << " for hash " << hash << std::endl;
#else
    /* remove unused parameter warnings */
    (void) hash;
#endif
    {
        RsStackMutex stack(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/

        RsGxsId gxs_id ;
        std::map<TurtleVirtualPeerId,GxsTunnelDHInfo>::iterator it = _gxs_tunnel_virtual_peer_ids.find(virtual_peer_id) ;

        if(it == _gxs_tunnel_virtual_peer_ids.end())
        {
            std::cerr << "(EE) Cannot remove virtual peer " << virtual_peer_id << ": not found in tunnel list!!" << std::endl;
            return ;
        }
#ifdef TO_REMOVE
        tunnel_id = it->second.tunnel_id ;
#endif

        if(it->second.dh != nullptr)
            DH_free(it->second.dh) ;
        
        _gxs_tunnel_virtual_peer_ids.erase(it) ;
        mVpidsChanged = true;

#ifdef TO_REMOVE
        std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::iterator it2 = _gxs_tunnel_contacts.find(tunnel_id) ;

        if(it2 == _gxs_tunnel_contacts.end())
        {
            std::cerr << "(EE) Cannot find tunnel id " << tunnel_id << " in contact list. Weird." << std::endl;
            return ;
        }
        if(it2->second.virtual_peer_id == virtual_peer_id)
        {
            it2->second.status = RS_GXS_TUNNEL_STATUS_TUNNEL_DN ;
            it2->second.virtual_peer_id.clear() ;
            tunnel_dn = true ;
        }
        
        for(std::set<uint32_t>::const_iterator it(it2->second.client_services.begin());it!=it2->second.client_services.end();++it)
        {
            std::map<uint32_t,RsGxsTunnelClientService*>::const_iterator it2 = mRegisteredServices.find(*it) ;
            
            if(it2 != mRegisteredServices.end())
                client_services.insert(it2->second) ;
        }
#endif
    }

#ifdef TO_REMOVE
    if(tunnel_dn)
    {
        // notify all client services that this tunnel is down
        
        for(std::set<RsGxsTunnelClientService*>::const_iterator it(client_services.begin());it!=client_services.end();++it)
            (*it)->notifyTunnelStatus(tunnel_id,RS_GXS_TUNNEL_STATUS_TUNNEL_DN) ;
    }
#endif
}

void p3GxsTunnelService::receiveTurtleData(const RsTurtleGenericTunnelItem *gitem, const RsFileHash& hash, const RsPeerId& virtual_peer_id, RsTurtleGenericTunnelItem::Direction direction)
{
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
    std::cerr << "GxsTunnelService::receiveTurtleData(): Received turtle data. " << std::endl;
    std::cerr << "         hash = " << hash << std::endl;
    std::cerr << "         vpid = " << virtual_peer_id << std::endl;
    std::cerr << "    acting as = " << direction << std::endl;
#else
    /* remove unused parameter warnings */
    (void) direction;
#endif

    void *data_bytes;
    uint32_t data_size ;
    bool accept_fast_items = false;

    const RsTurtleGenericFastDataItem *fitem = dynamic_cast<const RsTurtleGenericFastDataItem*>(gitem) ;

    if(fitem != NULL)
    {
        data_bytes = fitem->data_bytes ;
        data_size = fitem->data_size ;
        accept_fast_items = true;
    }
    else
    {
		const RsTurtleGenericDataItem *item = dynamic_cast<const RsTurtleGenericDataItem*>(gitem) ;

		if(item == NULL)
		{
			std::cerr << "(EE) item is not a data item. That is an error." << std::endl;
			return ;
		}
        data_bytes = item->data_bytes ;
        data_size = item->data_size ;
    }

	// Call the AES crypto module
    // - the IV is the first 8 bytes of item->data_bytes

    if(data_size < 8)
    {
        std::cerr << "(EE) item encrypted data stream is too small: size = " << data_size << std::endl;
        return ;
    }
    unsigned char all_zero[8] = {0};

    if(memcmp(data_bytes,all_zero,8)) // checking possibly-unaligned data bytes.
    {
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
        std::cerr << "  Item is encrypted." << std::endl;
#endif

        // if cannot decrypt, it means the key is wrong. We need to re-negociate a new key.

        handleEncryptedData((uint8_t*)data_bytes,data_size,hash,virtual_peer_id,accept_fast_items) ;
    }
    else
    {
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
        std::cerr << "  Item is not encrypted." << std::endl;
#endif

        // Now try deserialise the decrypted data to make an RsItem out of it.
        //
        uint32_t pktsize = data_size-8;
        RsItem *citem = RsGxsTunnelSerialiser().deserialise(&((uint8_t*)data_bytes)[8],&pktsize) ;

        if(citem == NULL)
        {
            std::cerr << "(EE) item could not be de-serialized. That is an error." << std::endl;
            return ;
        }

        // DH key items are sent even before we know who we speak to, so the virtual peer id is used in this
        // case only.
        RsGxsTunnelDHPublicKeyItem *dhitem = dynamic_cast<RsGxsTunnelDHPublicKeyItem*>(citem) ;
        
        if(dhitem != NULL)
            handleRecvDHPublicKey(dhitem,virtual_peer_id) ;
        else
            std::cerr << "(EE) Deserialiased item has unexpected type." << std::endl;

		delete citem ;
    }
}

// This function encrypts the given data and adds a MAC and an IV into a serialised memory chunk that is then sent through the tunnel.

#ifndef V07_NON_BACKWARD_COMPATIBLE_CHANGE_004
bool p3GxsTunnelService::handleEncryptedData(const uint8_t *data_bytes,uint32_t data_size,const TurtleFileHash& hash,const RsPeerId& virtual_peer_id,bool accepts_fast_items)
#else
bool p3GxsTunnelService::handleEncryptedData(const uint8_t *data_bytes,uint32_t data_size,const TurtleFileHash& hash,const RsPeerId& virtual_peer_id)
#endif
{
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
    std::cerr << "p3GxsTunnelService::handleEncryptedDataItem()" << std::endl;
    std::cerr << "   size = " << data_size << std::endl;
    std::cerr << "  &data = " << (void*)data_bytes << std::endl;
    std::cerr << "     IV = " << RsUtil::BinToHex((unsigned char*)data_bytes,8,8) << std::endl;
    std::cerr << "   data = " << RsUtil::BinToHex((unsigned char*)data_bytes,data_size,100) ;
    std::cerr << std::endl;
#endif

    RsGxsTunnelItem *citem = NULL;
    RsGxsTunnelId tunnel_id;

    if(data_size <= GXS_TUNNEL_ENCRYPTION_IV_SIZE + GXS_TUNNEL_ENCRYPTION_HMAC_SIZE)
    {
        std::cerr << "(EE) p3GxsTunnelLLhandleEncryptedData(): data_size is " << data_size << " which is too small for an encrypted packet." << std::endl;
        return false;
    }

    {
        RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/

        uint32_t encrypted_size = data_size - GXS_TUNNEL_ENCRYPTION_IV_SIZE - GXS_TUNNEL_ENCRYPTION_HMAC_SIZE;
        uint32_t decrypted_size = RsAES::get_buffer_size(encrypted_size);
        uint8_t *encrypted_data = (uint8_t*)data_bytes+GXS_TUNNEL_ENCRYPTION_IV_SIZE+GXS_TUNNEL_ENCRYPTION_HMAC_SIZE;
        
        RsTemporaryMemory decrypted_data(decrypted_size);
        uint8_t aes_key[GXS_TUNNEL_AES_KEY_SIZE] ;
        
        if(!decrypted_data)
            return false ;

        std::map<TurtleVirtualPeerId,GxsTunnelDHInfo>::iterator vpid_it = _gxs_tunnel_virtual_peer_ids.find(virtual_peer_id) ;

        if(vpid_it == _gxs_tunnel_virtual_peer_ids.end())
        {
            std::cerr << "(EE) item is not coming out of a registered tunnel. Weird. hash=" << hash << ", peer id = " << virtual_peer_id << std::endl;
            return false ;
        }

        tunnel_id = vpid_it->second.tunnel_id ;
        std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::iterator tunnel_it2 = _gxs_tunnel_contacts.find(tunnel_id) ;

        if(tunnel_it2 == _gxs_tunnel_contacts.end())
        {
            std::cerr << "(EE) no tunnel data for tunnel ID=" << tunnel_id << ". This is a bug." << std::endl;
            return false ;
        }
#ifndef V07_NON_BACKWARD_COMPATIBLE_CHANGE_004
        if(accepts_fast_items)
        {
            if(!tunnel_it2->second.accepts_fast_turtle_items)
                std::cerr << "(II) received probe for Fast track turtle items for tunnel VPID " << tunnel_it2->second.preferred_virtual_peer_id << ": switching to Fast items mode." << std::endl;

            tunnel_it2->second.accepts_fast_turtle_items = true;
        }
#endif

#ifdef TO_REMOVE
        // Data from a duplicate tunnel for this pair of identities (see handleRecvDHPublicKey) was never keyed, so its
        // HMAC cannot match. Drop it without restarting the DH session of the tunnel that works.
        if(it2->second.status == RS_GXS_TUNNEL_STATUS_CAN_TALK && it2->second.virtual_peer_id != virtual_peer_id)
        {
            ++it->second.dropped_packets ;
            return false ;
        }

        // Every undecryptable packet used to restart the DH session. Packets encrypted with the previous key are still in
        // flight after a restart, so that kept both sides re-keying forever, at the cost of a 2048-bit DH per packet.
        auto rejectPacket = [&](const char *why)
        {
            rstime_t now = time(NULL) ;

            if(it->second.status == GxsTunnelVirtualPeerIdStatus::DH_HALF || now < it->second.last_dh_restart + RS_GXS_TUNNEL_DH_RESTART_MIN_DELAY)
            {
                ++it->second.dropped_packets ;
                return ;
            }
            std::cerr << "(EE) GxsTunnel: " << why << " on tunnel " << tunnel_id << " (virtual peer " << virtual_peer_id
                      << ", " << it->second.dropped_packets << " packets dropped since the last restart). Restarting DH session." << std::endl;

            locked_restartDHSession(virtual_peer_id,it2->second.own_gxs_id) ;
        };
#endif

        memcpy(aes_key,vpid_it->second.aes_key,GXS_TUNNEL_AES_KEY_SIZE) ;

#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
        std::cerr << "   Using IV: " << std::hex << *(uint64_t*)data_bytes << std::dec << std::endl;
        std::cerr << "   Decrypted buffer size: " << decrypted_size << std::endl;
        std::cerr << "   key  : " << RsUtil::BinToHex((unsigned char*)aes_key,GXS_TUNNEL_AES_KEY_SIZE) << std::endl;
        std::cerr << "   hmac : " << RsUtil::BinToHex((unsigned char*)data_bytes+GXS_TUNNEL_ENCRYPTION_IV_SIZE,GXS_TUNNEL_ENCRYPTION_HMAC_SIZE) << std::endl;
        std::cerr << "   data : " << RsUtil::BinToHex((unsigned char*)data_bytes,data_size,100) << std::endl;
#endif
        // first, check the HMAC
        
        unsigned char *hm = HMAC(EVP_sha1(),aes_key,GXS_TUNNEL_AES_KEY_SIZE,encrypted_data,encrypted_size,NULL,NULL) ;
        
        if(memcmp(hm,&data_bytes[GXS_TUNNEL_ENCRYPTION_IV_SIZE],GXS_TUNNEL_ENCRYPTION_HMAC_SIZE))
        {
#ifdef TO_REMOVE
            rejectPacket("packet HMAC does not match. Dropping that packet.") ;
#endif
            std::cerr << "Packet HMAC does not match for vpid " << virtual_peer_id << ". This is very unexpected. Droping that packet." << std::endl ;
            vpid_it->second.status = GxsTunnelVirtualPeerIdStatus::DH_TODO;
            return false ;
        }

        if(!RsAES::aes_decrypt_8_16(encrypted_data,encrypted_size, aes_key,(uint8_t*)data_bytes,decrypted_data,decrypted_size))
        {
#ifdef TO_REMOVE
            rejectPacket("packet decryption failed. This is very unexpected. Droping that packet.") ;
#endif
            std::cerr << "Packet decryption failed for vpid " << virtual_peer_id << ". This is very unexpected. Droping that packet." << std::endl ;
            vpid_it->second.status = GxsTunnelVirtualPeerIdStatus::DH_TODO;
            return false ;
        }
        tunnel_it2->second.last_contact = time(NULL) ;

#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
        std::cerr << "(II) Decrypted data: size=" << decrypted_size << std::endl;
#endif

        // Now try deserialise the decrypted data to make an RsItem out of it.
        //
        citem = dynamic_cast<RsGxsTunnelItem*>(RsGxsTunnelSerialiser().deserialise(decrypted_data,&decrypted_size)) ;
        
        if(citem == NULL)
        {
            std::cerr << "(EE) item could not be de-serialized. That is an error." << std::endl;
            return true;
        }

        tunnel_it2->second.total_received += decrypted_size ;
        
        // DH key items are sent even before we know who we speak to, so the virtual peer id is used in this
        // case only.

        citem->PeerId(virtual_peer_id) ;
        vpid_it->second.last_data_TS = time(nullptr);	// stamp the virtual peer id.
    }

#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
    std::cerr << "(II) Setting peer id to " << citem->PeerId() << std::endl;
#endif
    handleIncomingItem(tunnel_id,citem) ; // Treats the item, and deletes it

    return true ;
}

void p3GxsTunnelService::handleRecvDHPublicKey(RsGxsTunnelDHPublicKeyItem *item,const TurtleVirtualPeerId& vpid)
{
    if (!item)
    {
	    std::cerr << "p3GxsTunnelService:  Received null DH public key item. This should not happen." << std::endl;
        return;
    }

#ifdef DEBUG_GXS_TUNNEL_VPID
    std::cerr << "GxsTunnelService:  Received DH public key." << std::endl;
    item->print(std::cerr, 0) ;
#endif

    // Look for the current state of the key agreement.

    RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/

    std::map<RsPeerId,GxsTunnelDHInfo>::iterator vpid_it = _gxs_tunnel_virtual_peer_ids.find(vpid) ;

    if(vpid_it == _gxs_tunnel_virtual_peer_ids.end())
    {
        std::cerr << "  (EE) Cannot find virtual peer id in list!!" << std::endl;
        return ;
    }
    auto& vpid_info(vpid_it->second);

    // Now check the signature of the DH public key item.

#ifdef DEBUG_GXS_TUNNEL_VPID
    std::cerr << "  Checking signature. " << std::endl;
#endif

    uint32_t pubkey_size = BN_num_bytes(item->public_key) ;
    RsTemporaryMemory data(pubkey_size) ;

    if(data.size() != pubkey_size)
    {
        std::cerr << "  (EE) memory allocation error for pubkey size " << pubkey_size << ". Check the number for unpexpected values." << std::endl;
        return;
    }
    BN_bn2bin(item->public_key, data) ;

    RsTlvPublicRSAKey signature_key ;

    // We need to get the key of the sender, but if the key is not cached, we
    // need to get it first. So we let the system work for 2-3 seconds before
    // giving up. Normally this would only cause a delay for uncached keys,
    // which is rare. To force the system to cache the key, we first call for
    // getIdDetails().
    //
    RsIdentityDetails details  ;
    RsGxsId senders_id( item->signature.keyId ) ;

    for(int i=0;i<6;++i)
        if(!mGixs->getKey(senders_id,signature_key) || signature_key.keyData.bin_data == NULL)
        {
#ifdef DEBUG_GXS_TUNNEL_VPID
            std::cerr << "  Cannot get key. Waiting for caching. try " << i << "/6" << std::endl;
#endif
            usleep(500 * 1000) ;	// sleep for 500 msec.
        }
        else
            break ;

    if(signature_key.keyData.bin_data == NULL)
    {
	    std::cerr << "  (EE) Key unknown for checking signature from " << senders_id << ", can't verify signature. Using key provided in DH packet (without adding to the keyring)." << std::endl;

	    // check GXS key for defects.

	    if(!GxsSecurity::checkPublicKey(item->gxs_key))
	    {
		    std::cerr << "(SS) Security error in distant chat DH handshake: supplied key " << item->gxs_key.keyId << " is inconsistent. Refusing chat!" << std::endl;
		    return ;
	    }
	    if(item->gxs_key.keyId != item->signature.keyId)
	    {
		    std::cerr << "(SS) Security error in distant chat DH handshake: supplied key " << item->gxs_key.keyId << " is not the same than the item's signature key " << item->signature.keyId << ". Refusing chat!" << std::endl;
		    return ;
	    }

	    signature_key = item->gxs_key ;
    }

    if(!GxsSecurity::validateSignature((char*)(unsigned char*)data,pubkey_size,signature_key,item->signature))
    {
        std::cerr << "(SS) Signature was verified and it doesn't check! This is a security issue!" << std::endl;
        return ;
    }
    mGixs->timeStampKey(item->signature.keyId,RsIdentityUsage(RsServiceType::GXS_TUNNEL,RsIdentityUsage::GXS_TUNNEL_DH_SIGNATURE_CHECK));

#ifdef DEBUG_GXS_TUNNEL_VPID
    std::cerr << "  Signature checks! Sender's ID = " << senders_id << std::endl;
    std::cerr << "  Computing AES key" << std::endl;
#endif

    if(vpid_info.dh == nullptr)
    {
        std::cerr << "  (EE) no DH information for that peer. This is an error." << std::endl;
        return ;
    }
#ifdef TO_REMOVE
    if(it->second.status == RS_GXS_TUNNEL_DH_STATUS_KEY_AVAILABLE)
    {
#ifdef DEBUG_GXS_TUNNEL
        std::cerr << "  DH Session already set for this tunnel. Re-initing a new session!" << std::endl;
#endif

        locked_restartDHSession(vpid,it->second.own_gxs_id) ;
    }
#endif

    // gets current key params. By default, should contain all null pointers.
    //
    RsGxsId own_id = vpid_info.own_gxs_id ;
    
    RsGxsTunnelId tunnel_id = makeGxsTunnelId(own_id,senders_id) ;
    
    vpid_info.tunnel_id = tunnel_id ;
    vpid_info.to_gxs_id = senders_id ;

    mVpidsChanged = true;

#ifdef TO_REMOVE
    // When both ends request a tunnel to each other, turtle brings up one tunnel per direction for the same pair of
    // identities. The contact entry holds a single AES key, so the second handshake would overwrite the key of the
    // tunnel that is already talking, and both tunnels would then fail every HMAC check and restart DH sessions
    // forever. Keep the tunnel that talks, ignore the newcomer.
    {
        std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::const_iterator cit = _gxs_tunnel_contacts.find(tunnel_id) ;

        if(cit != _gxs_tunnel_contacts.end() && cit->second.status == RS_GXS_TUNNEL_STATUS_CAN_TALK
                && cit->second.virtual_peer_id != vpid
                && _gxs_tunnel_virtual_peer_ids.find(cit->second.virtual_peer_id) != _gxs_tunnel_virtual_peer_ids.end())
        {
            std::cerr << "(WW) GxsTunnel: tunnel " << tunnel_id << " is already active on virtual peer " << cit->second.virtual_peer_id
                      << ". Ignoring duplicate tunnel on virtual peer " << vpid << "." << std::endl;
            return ;
        }
    }
#endif

    assert(GXS_TUNNEL_AES_KEY_SIZE <= Sha1CheckSum::SIZE_IN_BYTES) ;

    // Looks for the DH params. If not there yet, create them.
    //
    int size = DH_size(vpid_info.dh) ;
    RsTemporaryMemory key_buff(size) ;

    if(size != DH_compute_key(key_buff,item->public_key,vpid_info.dh))
    {
        std::cerr << "  (EE) DH computation failed. Probably a bug. Error code=" << ERR_get_error() << std::endl;
        return ;
    }

    if(vpid_info.status == GxsTunnelVirtualPeerIdStatus::DH_DONE)
    {
        std::cerr << "(WW) received a new DH public key for a DH session that was already concluded. Using it and sending back the old public key. This is not supposed to happen."<< std::endl;
        vpid_info.status = GxsTunnelVirtualPeerIdStatus::DH_HALF ;

        if(!locked_sendDHPublicKey(vpid_info.dh,vpid_info.own_gxs_id,vpid))
            std::cerr << "  (EE) Cannot send DH public key. Something went wrong." << std::endl;
    }
    else
    {
        vpid_info.status = GxsTunnelVirtualPeerIdStatus::DH_DONE ;

        // Now hash the key buffer into a 16 bytes key.
        memcpy(vpid_info.aes_key, RsDirUtil::sha1sum(key_buff,size).toByteArray(),GXS_TUNNEL_AES_KEY_SIZE) ;

        // then we send an ACK packet to notify that the tunnel works. That's useful
        // because it makes the peer at the other end of the tunnel know that all
        // intermediate peer in the tunnel are able to transmit the data.
        // However, it is not possible here to call sendTurtleData(), without dead-locking
        // the turtle router, so we store the item is a list of items to be sent.

        RsGxsTunnelStatusItem *cs = new RsGxsTunnelStatusItem ;

        cs->status = RS_GXS_TUNNEL_FLAG_ACK_DISTANT_CONNECTION;
        cs->PeerId(RsPeerId(tunnel_id)) ;

        pendingGxsTunnelItems.push_back(cs) ;
    }

#ifdef DEBUG_GXS_TUNNEL_VPID
    std::cerr << "  DH key computation successed. New key in place." << std::endl;
#endif
    
#ifdef TO_REMOVE
    // make a hash of destination and source GXS ids in order to create the tunnel name

    GxsTunnelPeerInfo& pinfo(_gxs_tunnel_contacts[tunnel_id]) ;

    pinfo.last_contact = time(NULL) ;
    pinfo.last_keep_alive_sent = time(NULL) ;
    pinfo.status = RS_GXS_TUNNEL_STATUS_CAN_TALK ;
    pinfo.virtual_peer_id = vpid ;
    pinfo.direction = it->second.direction ;
    pinfo.own_gxs_id = own_id ;
    pinfo.to_gxs_id = item->signature.keyId;	// this is already set for client side but not for server side.
    
    // note: the hash might still be nn initialised on server side.
#endif

#ifdef DEBUG_GXS_TUNNEL_VPID
    std::cerr << "  DH key computed. Tunnel is now secured!" << std::endl;
    std::cerr << "  Key computed: " << RsUtil::BinToHex((char*)vpid_info.aes_key,16) << std::endl;
    std::cerr << "  Sending a ACK packet." << std::endl;
#endif
}

// Note: for some obscure reason, the typedef does not work here. Looks like a compiler error. So I use the primary type.

/*static*/ RsGxsTunnelId p3GxsTunnelService::makeGxsTunnelId(
        const RsGxsId &own_id, const RsGxsId &distant_id )
{
    unsigned char mem[RsGxsId::SIZE_IN_BYTES * 2] ;
    
    // Always sort the ids, as a matter to avoid confusion between the two. Also that generates the same tunnel ID on both sides
    // which helps debugging. If the code is right this is not needed anyway.
    
    if(own_id < distant_id)
    {
	    memcpy(mem, own_id.toByteArray(), RsGxsId::SIZE_IN_BYTES) ;
	    memcpy(mem+RsGxsId::SIZE_IN_BYTES, distant_id.toByteArray(), RsGxsId::SIZE_IN_BYTES) ;
    }
    else
    {
	    memcpy(mem, distant_id.toByteArray(), RsGxsId::SIZE_IN_BYTES) ;
	    memcpy(mem+RsGxsId::SIZE_IN_BYTES, own_id.toByteArray(), RsGxsId::SIZE_IN_BYTES) ;
    }
    
    assert( RsGxsTunnelId::SIZE_IN_BYTES <= Sha1CheckSum::SIZE_IN_BYTES ) ;
    
    return RsGxsTunnelId( RsDirUtil::sha1sum(mem, 2*RsGxsId::SIZE_IN_BYTES).toByteArray() ) ;
}

bool p3GxsTunnelService::locked_sendDHPublicKey(const DH *dh,const RsGxsId& own_gxs_id,const RsPeerId& virtual_peer_id)
{
	if(dh == NULL)
	{
		std::cerr << "  (EE) DH struct is not initialised! Error." << std::endl;
		return false ;
	}

	RsGxsTunnelDHPublicKeyItem *dhitem = new RsGxsTunnelDHPublicKeyItem ;
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	dhitem->public_key = BN_dup(dh->pub_key) ;
#else
    const BIGNUM *pub_key=NULL ;
    DH_get0_key(dh,&pub_key,NULL) ;
	dhitem->public_key = BN_dup(pub_key) ;
#endif

	// we should also sign the data and check the signature on the other end.
	//
	RsTlvKeySignature  signature ;
	RsTlvPrivateRSAKey signature_key ;
	RsTlvPublicRSAKey  signature_key_public ;

	uint32_t error_status ;

	uint32_t size = BN_num_bytes(dhitem->public_key) ;
    
    	RsTemporaryMemory data(size) ;
        
        if(data == NULL)
        {
            delete(dhitem);
            return false ;
        }
        
	BN_bn2bin(dhitem->public_key, data) ;

	if(!mGixs->signData((unsigned char*)data,size,own_gxs_id,signature,error_status))
	{
		switch(error_status)
		{
		case RsGixs::RS_GIXS_ERROR_KEY_NOT_AVAILABLE: std::cerr << "(EE) Key is not available. Cannot sign." << std::endl;
			break ;
		default: std::cerr << "(EE) Unknown error when signing" << std::endl;
			break ;
		}
		delete(dhitem);
		return false;
	}

	if(!mGixs->getKey(own_gxs_id,signature_key_public))
	{
		std::cerr << "  (EE) Could not retrieve own public key for ID = " << own_gxs_id << ". Giging up sending DH session params." << std::endl;
        delete(dhitem);
        return false ;
	}


	assert(!(signature_key_public.keyFlags & RSTLV_KEY_TYPE_FULL)) ;

	dhitem->signature = signature ;
	dhitem->gxs_key = signature_key_public ;
	dhitem->PeerId(virtual_peer_id) ;	

#ifdef DEBUG_GXS_TUNNEL_VPID
	std::cerr << "  Pushing DH session key item to pending distant messages..." << std::endl;
	dhitem->print(std::cerr, 2) ;
	std::cerr << std::endl;
#endif
	pendingDHItems.push_back(dhitem) ; // sent off-mutex to avoid deadlocking.

	return true ;
}

bool p3GxsTunnelService::locked_initDHSessionKey(DH *& dh)
{
    // We use our own DH group prime. This has been generated with command-line openssl and checked.
    
    static const std::string dh_prime_2048_hex = "B3B86A844550486C7EA459FA468D3A8EFD71139593FE1C658BBEFA9B2FC0AD2628242C2CDC2F91F5B220ED29AAC271192A7374DFA28CDDCA70252F342D0821273940344A7A6A3CB70C7897A39864309F6CAC5C7EA18020EF882693CA2C12BB211B7BA8367D5A7C7252A5B5E840C9E8F081469EBA0B98BCC3F593A4D9C4D5DF539362084F1B9581316C1F80FDAD452FD56DBC6B8ED0775F596F7BB22A3FE2B4753764221528D33DB4140DE58083DB660E3E105123FC963BFF108AC3A268B7380FFA72005A1515C371287C5706FFA6062C9AC73A9B1A6AC842C2764CDACFC85556607E86611FDF486C222E4896CDF6908F239E177ACC641FCBFF72A758D1C10CBB" ;

    if(dh != NULL)
    {
        DH_free(dh) ;
        dh = NULL ;
    }

    dh = DH_new() ;

    if(!dh)
    {
        std::cerr << "  (EE) DH_new() failed." << std::endl;
        return false ;
    }

#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
    BN_hex2bn(&dh->p,dh_prime_2048_hex.c_str()) ;
    BN_hex2bn(&dh->g,"5") ;
#else
    BIGNUM *pp=NULL ;
    BIGNUM *gg=NULL ;

    BN_hex2bn(&pp,dh_prime_2048_hex.c_str()) ;
    BN_hex2bn(&gg,"5") ;

    DH_set0_pqg(dh,pp,NULL,gg) ;
#endif

    // DH_check() runs primality tests on the 2048-bit prime and costs about 230 ms. The prime is a constant, so one check
    // per process is enough; checking at every DH restart turned each undecryptable packet into a CPU burst.
    static bool prime_checked = false ;

    if(!prime_checked)
    {
        int codes = 0 ;

        if(!DH_check(dh, &codes) || codes != 0)
        {
            std::cerr << "  (EE) DH check failed!" << std::endl;
            return false ;
        }
        prime_checked = true ;
    }

    if(!DH_generate_key(dh))
    {
        std::cerr << "  (EE) DH generate_key() failed! Error code = " << ERR_get_error() << std::endl;
        return false ;
    }
#ifdef DEBUG_GXS_TUNNEL_VPID
    std::cerr << "  (II) DH Session key inited." << std::endl;
#endif
    return true ;
}

// Sends the item in clear. This is only used for DH key exchange.
// in this case only, the item's PeerId is equal to the virtual peer Id for the tunnel, 
// since we ight not now the tunnel id yet.

bool p3GxsTunnelService::locked_sendClearTunnelData(RsGxsTunnelDHPublicKeyItem *item)
{
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
    std::cerr << "GxsTunnelService::sendClearTunnelData(): try sending item " << (void*)item << " to peer " << item->PeerId() << std::endl;
#endif

    // make a RsTurtleGenericData item out of it, and send it in clear.
    // this is compatible with nodes older than 0.6.6
    RsTurtleGenericDataItem *gitem = new RsTurtleGenericDataItem ;
    // force item priority to QOS_PRIORITY_RS_TURTLE_GENERIC_FAST_DATA to make the DH exchange faster on 0.6.6+ nodes
    // this will not affect old nodes
    gitem->setPriorityLevel(QOS_PRIORITY_RS_TURTLE_GENERIC_FAST_DATA);

    RsGxsTunnelSerialiser ser ;

    uint32_t rssize = ser.size(item);

    gitem->data_size  = rssize + 8 ;
    gitem->data_bytes = rs_malloc(rssize+8) ;

    if(gitem->data_bytes == NULL)
    {
        delete gitem ;
        return false ;
    }
    // by convention, we use a IV of 0 for unencrypted data.
    memset(gitem->data_bytes,0,8) ;

    if(!ser.serialise(item,&((uint8_t*)gitem->data_bytes)[8],&rssize))
    {
	    std::cerr << "(EE) Could not serialise item!!!" << std::endl;
	    delete gitem ;
	    return false;
    }

#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
    std::cerr << "   GxsTunnelService::sendClearTunnelData(): Sending clear data to virtual peer: " << item->PeerId() << std::endl;
    std::cerr << "     gitem->data_size = " << gitem->data_size << std::endl;
    std::cerr << "     data = " << RsUtil::BinToHex((unsigned char*)gitem->data_bytes,gitem->data_size,100) ;
    std::cerr << std::endl;
#endif
    mTurtle->sendTurtleData(item->PeerId(),gitem) ;
    
    return true ;
}

// Sends this item using secured/authenticated method, thx to the establshed cryptographic channel.

bool p3GxsTunnelService::locked_sendEncryptedTunnelData(RsGxsTunnelItem *item)
{
    RsGxsTunnelSerialiser ser;

    uint32_t rssize = ser.size(item);
    RsTemporaryMemory buff(rssize) ;

    if(!ser.serialise(item,buff,&rssize))
    {
	    std::cerr << "(EE) GxsTunnelService::sendEncryptedTunnelData(): Could not serialise item!" << std::endl;
	    return false;
    }

    uint8_t aes_key[GXS_TUNNEL_AES_KEY_SIZE] ;
    uint64_t IV = 0;

    RsGxsTunnelId tunnel_id ( item->PeerId() );

#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
    std::cerr << "Sending encrypted data to tunnel " << tunnel_id << std::endl;
#endif
    
    std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::iterator it = _gxs_tunnel_contacts.find(tunnel_id) ;

	if(it == _gxs_tunnel_contacts.end())
	{
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
		std::cerr << "  Cannot find contact key info for tunnel id "
		          << tunnel_id << ". Cannot send message!" << std::endl;
#endif
		return false;
	}
    if(it->second.status != GxsTunnelStatus::CAN_TALK)
	{
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
        std::cerr << "(EE) Cannot talk to tunnel id " << tunnel_id << ". Tunnel status is: " << static_cast<int>(it->second.status) << std::endl;
#endif
		return false;
	}

    it->second.total_sent += rssize ;	// counts the size of clear data that is sent
    
    RsPeerId virtual_peer_id = it->second.preferred_virtual_peer_id ;
    auto it2 = _gxs_tunnel_virtual_peer_ids.find(virtual_peer_id);

    if(it2 == _gxs_tunnel_virtual_peer_ids.end())
    {
        std::cerr << "p3GxsTunnel::locked_sendEncryptedTunnelData(): error. VPID " << virtual_peer_id << " for tunnel " << tunnel_id << " doesn't exist." << std::endl;
        return false;
    }

    memcpy(aes_key,it2->second.aes_key,GXS_TUNNEL_AES_KEY_SIZE) ;

    while(IV == 0) IV = RSRandom::random_u64() ; // make a random 8 bytes IV, that is not 0

#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
    std::cerr << "GxsTunnelService::sendEncryptedTunnelData(): tunnel found. Encrypting data." << std::endl;
#endif

    // Now encrypt this data using AES.
    //
    uint32_t encrypted_size = RsAES::get_buffer_size(rssize);
    RsTemporaryMemory encrypted_data(encrypted_size) ;

    if(!RsAES::aes_crypt_8_16(buff,rssize,aes_key,(uint8_t*)&IV,encrypted_data,encrypted_size))
    {
        std::cerr << "(EE) packet encryption failed." << std::endl;
        return false;
    }

    // make a TurtleGenericData item out of it:
    //

    uint32_t data_size  = encrypted_size + GXS_TUNNEL_ENCRYPTION_IV_SIZE + GXS_TUNNEL_ENCRYPTION_HMAC_SIZE ;
    void *data_bytes = rs_malloc(data_size) ;

    if(data_bytes == NULL)
        return false ;
    
    memcpy(& ((uint8_t*)data_bytes)[0]                                       ,&IV,8) ;

    unsigned int md_len = GXS_TUNNEL_ENCRYPTION_HMAC_SIZE ;
    HMAC(EVP_sha1(),aes_key,GXS_TUNNEL_AES_KEY_SIZE,encrypted_data,encrypted_size,&(((uint8_t*)data_bytes)[GXS_TUNNEL_ENCRYPTION_IV_SIZE]),&md_len) ;
    
    memcpy(& (((uint8_t*)data_bytes)[GXS_TUNNEL_ENCRYPTION_HMAC_SIZE+GXS_TUNNEL_ENCRYPTION_IV_SIZE]),encrypted_data,encrypted_size) ;
    
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
    std::cerr << "   Using  IV: " << std::hex << IV << std::dec << std::endl;
    std::cerr << "   Using Key: " << RsUtil::BinToHex((char*)aes_key,GXS_TUNNEL_AES_KEY_SIZE) ; std::cerr << std::endl;
    std::cerr << "        hmac: " << RsUtil::BinToHex((char*)data_bytes,GXS_TUNNEL_ENCRYPTION_HMAC_SIZE) << std::endl;
    std::cerr << "GxsTunnelService::sendEncryptedTunnelData(): Sending encrypted data to virtual peer: " << virtual_peer_id << std::endl;
    std::cerr << "   data_size = " << data_size << std::endl;
    std::cerr << "    serialised data = " << RsUtil::BinToHex((unsigned char*)data_bytes,data_size,100) ;
    std::cerr << std::endl;
#endif

    // We send the item through the turtle tunnel. We do that using the new 'fast' item type if the tunnel accepts it, and using the old slow one otherwise.
    // Still if the tunnel hasn't been probed, we duplicate the packet using the new fast item format. The packet being received twice on the other side will
    // be discarded with a warning.
    // Note that because the data is deleted by sendTurtleData(), we need to send all packets at the end, and properly duplicate the data when needed.

#ifdef V07_NON_BACKWARD_COMPATIBLE_CHANGE_004
	RsTurtleGenericFastDataItem *gitem = new RsTurtleGenericFastDataItem;
	gitem->data_bytes = data_bytes;
	gitem->data_size  = data_size ;
#else
	RsTurtleGenericDataItem *gitem = NULL;
	RsTurtleGenericFastDataItem *gitem2 = NULL;

	if(!it->second.accepts_fast_turtle_items)
	{
        std::cerr << "Sending old format (slow) item for packet IV=" << std::hex << IV << std::dec << " in tunnel VPID=" << it->second.preferred_virtual_peer_id << std::endl;
		gitem = new RsTurtleGenericDataItem ;

		gitem->data_bytes = data_bytes;
		gitem->data_size  = data_size ;
	}

	if(it->second.accepts_fast_turtle_items || !it->second.already_probed_for_fast_items)
	{
        std::cerr << "Sending new format (fast) item for packet IV=" << std::hex << IV << std::dec << " in tunnel VPID=" << it->second.preferred_virtual_peer_id << std::endl;
		gitem2 = new RsTurtleGenericFastDataItem ;

        if(gitem != NULL)	// duplicate the data because it was already sent in gitem.
		{
			gitem2->data_bytes = rs_malloc(data_size);
			gitem2->data_size  = data_size ;
            memcpy(gitem2->data_bytes,data_bytes,data_size);
		}
		else
		{
			gitem2->data_bytes = data_bytes;
			gitem2->data_size  = data_size ;
		}

		it->second.already_probed_for_fast_items = true;

	}
	if(gitem2) mTurtle->sendTurtleData(virtual_peer_id,gitem2) ;
#endif
    if(gitem)  mTurtle->sendTurtleData(virtual_peer_id,gitem) ;

	return true ;
}

bool p3GxsTunnelService::requestSecuredTunnel(const RsGxsId& to_gxs_id, const RsGxsId& from_gxs_id, RsGxsTunnelId &tunnel_id, uint32_t service_id, uint32_t& error_code)
{
    // should be a parameter.
	
    std::list<RsGxsId> lst ;
    mGixs->getOwnIds(lst) ;

    bool found = false ;
    for(std::list<RsGxsId>::const_iterator it = lst.begin();it!=lst.end();++it)
        if(*it == from_gxs_id)
        {
            found=true;
            break ;
        }

    if(!found)
    {
        std::cerr << "  (EE) Cannot start distant chat, since GXS id " << from_gxs_id << " is not available." << std::endl;
        error_code = RS_GXS_TUNNEL_ERROR_UNKNOWN_GXS_ID ;
        return false ;
    }
    RsGxsId own_gxs_id = from_gxs_id ;

    startClientGxsTunnelConnection(to_gxs_id,own_gxs_id,service_id,tunnel_id) ;

    error_code = RS_GXS_TUNNEL_ERROR_NO_ERROR ;

    return true ;
}

// This method generates an ID that should be unique for each packet sent to a same peer. Rather than a random value,
// we use this counter because outgoing items are sorted in a map by their counter value. Using an increasing value
// ensures that the packets are sent in the same order than received from the service. This can be useful in some cases,
// for instance when services split their items while expecting them to arrive in the same order.

uint64_t p3GxsTunnelService::locked_getPacketCounter()
{
	return mCurrentPacketCounter++ ;
}

bool p3GxsTunnelService::sendData(const RsGxsTunnelId &tunnel_id, uint32_t service_id, const uint8_t *data, uint32_t size)
{
    // make sure that the tunnel ID is registered.
    
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
    std::cerr << "p3GxsTunnelService::sendData()" << std::endl;
    std::cerr << "  tunnel id : " << tunnel_id << std::endl;
    std::cerr << "  data size : " << size << std::endl;
    std::cerr << "  service id: " << std::hex << service_id << std::dec << std::endl;
#endif
    
    RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/
        
    std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::const_iterator it = _gxs_tunnel_contacts.find(tunnel_id) ;
    
    if(it == _gxs_tunnel_contacts.end())
    {
        std::cerr << "  (EE) no tunnel known with this ID. Sorry!" << std::endl;
        return false ;
    }
    
    // make sure the service is registered.
    
    if(mRegisteredServices.find(service_id) == mRegisteredServices.end())
    {
        std::cerr << "  (EE) no service registered with this ID. Please call rsGxsTunnel->registerClientService() at some point." << std::endl;
        return false ;
    }
    
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
    std::cerr << "  verifications fine! Storing in out queue with:" << std::endl;
#endif
    
    RsGxsTunnelDataItem *item = new RsGxsTunnelDataItem ;
            
    item->flags = 0;						// not used yet.
    item->service_id = service_id;
    item->data_size = size;					// encrypted data size
    item->data = (uint8_t*)rs_malloc(size);	// encrypted data
    
    if(item->data == nullptr)
    {
        delete item ;
        std::cerr << "p3GxsTunnel::sendData(): out of memory. Cannot send item." << std::endl;
        return false;
    }
    
    item->unique_item_counter = locked_getPacketCounter() ;// this allows to make the item unique, while respecting the packet order!
    item->PeerId(RsPeerId(tunnel_id)) ;
    memcpy(item->data,data,size) ;
    
    GxsTunnelData& tdata( pendingGxsTunnelDataItems[item->unique_item_counter] ) ;
    
    tdata.data_item = item ;
    tdata.last_sending_attempt = 0 ;	// never sent until now
    
#ifdef DEBUG_GXS_TUNNEL_TRAFFIC
    std::cerr << "  counter id : " << std::hex << item->unique_item_counter << std::dec << std::endl;
#endif
    
    return true ;
}


void p3GxsTunnelService::startClientGxsTunnelConnection(const RsGxsId& to_gxs_id,const RsGxsId& from_gxs_id,uint32_t service_id,RsGxsTunnelId& tunnel_id)
{
    // compute a random hash for that pair, and init the DH session for it so that we can recognise it when we get the virtual peer for it.
    
    RsFileHash hash = randomHashFromDestinationGxsId(to_gxs_id) ;
    
    tunnel_id = makeGxsTunnelId(from_gxs_id,to_gxs_id) ;
    
    {
        RsStackMutex stack(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/

        auto tunnel_it = _gxs_tunnel_contacts.find(tunnel_id);

        if(tunnel_it  != _gxs_tunnel_contacts.end())
        {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
            std::cerr << "GxsTunnelService:: asking GXS tunnel for a configuration that already exits.Ignoring." << std::endl;
#endif
            tunnel_it->second.client_services.insert(service_id) ;
            return ;
        }
    }

    GxsTunnelPeerInfo pinfo ;

    rstime_t now = time(NULL) ;

    pinfo.last_contact = now ;
    pinfo.last_keep_alive_sent = now ;
    pinfo.status = GxsTunnelStatus::UNKNOWN ;
    pinfo.own_gxs_id = from_gxs_id ;
    pinfo.to_gxs_id = to_gxs_id ;
    pinfo.hash = hash ;
    pinfo.direction = RsTurtleGenericTunnelItem::DIRECTION_SERVER ;
    pinfo.preferred_virtual_peer_id.clear();
    pinfo.client_services.insert(service_id) ;
#ifdef TO_REMOVE
    memset(info.aes_key,0,GXS_TUNNEL_AES_KEY_SIZE) ;
#endif

    {
        RsStackMutex stack(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/
        
        _gxs_tunnel_contacts[tunnel_id] = pinfo ;
    }

#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
    std::cerr << "Starting tunnel management to " << to_gxs_id << ", hash = " << hash << ", from " << from_gxs_id << std::endl;
    std::cerr << "Asking turtle router to monitor tunnels for hash " << hash << std::endl;
#endif

    // Now ask the turtle router to manage a tunnel for that hash.
    
    mTurtle->monitorTunnels(hash,this,false) ;
}

TurtleFileHash p3GxsTunnelService::randomHashFromDestinationGxsId(const RsGxsId& destination)
{
    // This is in prevision for the "secured GXS tunnel" service, which will need a service ID to register,
    // just like GRouter does.

    assert(  destination.SIZE_IN_BYTES == 16) ;
    assert(Sha1CheckSum::SIZE_IN_BYTES == 20) ;

    uint8_t bytes[20] ;
    memcpy(&bytes[4],destination.toByteArray(),16) ;
    
    RAND_bytes(&bytes[0],4) ;	// fill the 4 first bytes with random crap. Very important to allow tunnels from different sources and statistically avoid collisions.

    // We could rehash this, with a secret key to get a HMAC. That would allow to publish secret distant chat
    // passphrases. I'll do this later if needed.

    return Sha1CheckSum(bytes) ;	// this does not compute a hash, and that is on purpose.
}

RsGxsId p3GxsTunnelService::destinationGxsIdFromHash(const TurtleFileHash& sum)
{
    assert(     RsGxsId::SIZE_IN_BYTES == 16) ;
    assert(Sha1CheckSum::SIZE_IN_BYTES == 20) ;

    return RsGxsId(&sum.toByteArray()[4]);// takes the last 16 bytes
}

bool p3GxsTunnelService::getTunnelInfo(const RsGxsTunnelId& tunnel_id,GxsTunnelInfo& info)
{
	RS_STACK_MUTEX(mGxsTunnelMtx);

    std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::const_iterator it = _gxs_tunnel_contacts.find(tunnel_id) ;

    if(it == _gxs_tunnel_contacts.end())
	    return false ;

    info.destination_gxs_id = it->second.to_gxs_id;     
    info.source_gxs_id      = it->second.own_gxs_id;	
    info.tunnel_status      = it->second.status;	          
    info.total_size_sent    = it->second.total_sent;	         
    info.total_size_received= it->second.total_received;	  
    info.is_client_side     = (it->second.direction == RsTurtleGenericTunnelItem::DIRECTION_CLIENT);

    // Data packets

    info.pending_data_packets = 0;
    RsPeerId p(tunnel_id);

    for(auto it(pendingGxsTunnelDataItems.begin());it!=pendingGxsTunnelDataItems.end();++it)
        if(it->second.data_item->PeerId() == p)
            ++info.pending_data_packets ;

    info.total_data_packets_sent=0 ;     
    info.total_data_packets_received=0 ;

    return true ;
}

void p3GxsTunnelService::locked_dropPendingData(const RsGxsTunnelId& tunnel_id)
{
    const RsPeerId peer(tunnel_id) ;

    for(auto it(pendingGxsTunnelDataItems.begin());it!=pendingGxsTunnelDataItems.end();)
        if(it->second.data_item->PeerId() == peer)
        {
            delete it->second.data_item ;
            it = pendingGxsTunnelDataItems.erase(it) ;
        }
        else
            ++it ;

    // Same for keep-alives and ACKs: only a successful send removes them.
    for(auto it(pendingGxsTunnelItems.begin());it!=pendingGxsTunnelItems.end();)
        if((*it)->PeerId() == peer)
        {
            delete *it ;
            it = pendingGxsTunnelItems.erase(it) ;
        }
        else
            ++it ;
}

bool p3GxsTunnelService::closeExistingTunnel(const RsGxsTunnelId& tunnel_id, uint32_t service_id)
{
    // two cases: 
    // 	- client needs to stop asking for tunnels => remove the hash from the list of tunnelled files
    // 	- server needs to only close the window and let the tunnel die. But the window should only open 
    // 	  if a message arrives.

    TurtleFileHash hash ;
    TurtleVirtualPeerId vpid ;
    bool close_tunnel = false ;
    int direction ;
    {
	    RsStackMutex stack(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/
	    std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::iterator it = _gxs_tunnel_contacts.find(tunnel_id) ;

	    if(it == _gxs_tunnel_contacts.end())
	    {
            std::cerr << "(EE) Cannot close tunnel connection. No connection openned for tunnel id " << tunnel_id << std::endl;

		    // We cannot stop tunnels, since their peer id is lost. Anyway, they'll die of starving.

		    return false ;
	    }
        vpid = it->second.preferred_virtual_peer_id ;
#ifdef TO_REMOVE
	    std::map<TurtleVirtualPeerId, GxsTunnelDHInfo>::const_iterator it2 = _gxs_tunnel_virtual_peer_ids.find(vpid) ;

	    if(it2 != _gxs_tunnel_virtual_peer_ids.end())
		    hash = it2->second.hash ;
	else
#endif
		hash = it->second.hash ;
        direction = it->second.direction ;

	    // check how many clients are used. If empty, close the tunnel

	    std::set<uint32_t>::iterator it3 = it->second.client_services.find(service_id) ;

	    if(it3 == it->second.client_services.end())
	    {
		    std::cerr << "(EE) service id not currently using that tunnel. This is an error." << std::endl;
		    return false;
	    }

	    it->second.client_services.erase(it3) ;

	    if(it->second.client_services.empty())
		    close_tunnel = true ;
    }

    if(close_tunnel)
    {
	    // send a status item saying that we're closing the connection
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
	    std::cerr << "  Sending a ACK to close the tunnel since we're managing it and it's not used by any service. tunnel id=." << tunnel_id << std::endl;
#endif

	    RsGxsTunnelStatusItem *cs = new RsGxsTunnelStatusItem ;

	    cs->status = RS_GXS_TUNNEL_FLAG_CLOSING_DISTANT_CONNECTION;
	    cs->PeerId(RsPeerId(tunnel_id)) ;

        {
            RsStackMutex stack(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/
            locked_sendEncryptedTunnelData(cs) ;	// that needs to be done off-mutex and before we close the tunnel also ignoring failure.
        }
	    delete cs ;

	    if(direction == RsTurtleGenericTunnelItem::DIRECTION_SERVER) 	// nothing more to do for server side.
	    {
#ifdef DEBUG_GXS_TUNNEL_MANAGEMENT
		    std::cerr << "  This is client side. Stopping tunnel manageement for tunnel_id " << tunnel_id << std::endl;
#endif
		    mTurtle->stopMonitoringTunnels( hash ) ;	// still valid if the hash is null
	    }

	    RsStackMutex stack(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/
	    std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::iterator it = _gxs_tunnel_contacts.find(tunnel_id) ;

	    if(it == _gxs_tunnel_contacts.end())		// server side. Nothing to do.
	    {
            std::cerr << "(EE) Cannot close tunnel id " << tunnel_id << ": not found." << std::endl;
		    return false ;
	    }

	    _gxs_tunnel_contacts.erase(it) ;

	    // Drop the data items still waiting for an ACK on this tunnel. Without a
	    // tunnel they can never be sent again, but flush() would keep trying
	    // (serializing each of them on every tick) and they were never freed.
	    locked_dropPendingData(tunnel_id) ;

	    // GxsTunnelService::removeVirtualPeerId() will be called by the turtle service.
    }
    return true ;
}

bool p3GxsTunnelService::getTunnelsInfo(std::vector<RsGxsTunnelService::GxsTunnelInfo> &infos)
{
    RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/
    
    for(std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::const_iterator it(_gxs_tunnel_contacts.begin());it!=_gxs_tunnel_contacts.end();++it)
    {
        GxsTunnelInfo ti ;
        
        ti.tunnel_id = it->first ;
        ti.destination_gxs_id = it->second.to_gxs_id ;
        ti.source_gxs_id = it->second.own_gxs_id ;
        ti.tunnel_status = it->second.status ;
        ti.total_size_sent = it->second.total_sent ;
        ti.total_size_received = it->second.total_received ;

		ti.pending_data_packets = 0;
        RsPeerId p(it->first);
		for(auto it(pendingGxsTunnelDataItems.begin());it!=pendingGxsTunnelDataItems.end();++it)
			if(it->second.data_item->PeerId() == p)
				++ti.pending_data_packets ;

	    ti.total_data_packets_sent =0;     // not accounted for yet.
	    ti.total_data_packets_received=0 ; // not accounted for yet.

        infos.push_back(ti) ;
    }
    
    return true ;
}

void p3GxsTunnelService::debug_dump()
{
    RS_STACK_MUTEX(mGxsTunnelMtx); /********** STACK LOCKED MTX ******/
    
    rstime_t now = time(NULL) ;
    
    std::cerr << "p3GxsTunnelService::debug_dump()" << std::endl;
    std::cerr << "  Registered client services: " << std::endl;
    
    for(std::map<uint32_t,RsGxsTunnelService::RsGxsTunnelClientService*>::const_iterator it=mRegisteredServices.begin();it!=mRegisteredServices.end();++it)
        std::cerr << std::hex << "    " << it->first << " - " << (void*)it->second << std::dec << std::endl;
        
    std::cerr << "  Active tunnels" << std::endl;
                 
    for(std::map<RsGxsTunnelId,GxsTunnelPeerInfo>::const_iterator it=_gxs_tunnel_contacts.begin();it!=_gxs_tunnel_contacts.end();++it)
    {
        std::cerr << "    tunnel_id=" << it->first << " vpid=" << it->second.preferred_virtual_peer_id << std::endl;
        std::cerr << "        From: " << it->second.own_gxs_id << " to " << it->second.to_gxs_id << std::endl;
        std::cerr << "        Services: " ; for(auto c:it->second.client_services) std::cerr << std::hex << c << std::dec << " " ; std::cerr << std::endl;
        std::cerr << "        status=" << static_cast<int>(it->second.status) << " direction=" << it->second.direction << " last_contact=" << (now-it->second.last_contact) <<" secs ago. Last_keep_alive_sent:" << (now - it->second.last_keep_alive_sent) << " secs ago." << std::endl;
    }
    
    std::cerr << "  Virtual peers:" << std::endl;
    
    for(std::map<TurtleVirtualPeerId,GxsTunnelDHInfo>::const_iterator it=_gxs_tunnel_virtual_peer_ids.begin();it!=_gxs_tunnel_virtual_peer_ids.end();++it)
        std::cerr << "    vpid=" << it->first << " tunnel_id=" << it->second.tunnel_id << " status=" << static_cast<int>(it->second.status) << " direction=" << it->second.direction << " hash=" << it->second.hash << " last data: " << (now - it->second.last_data_TS) << " secs ago."<< std::endl;
    
    std::cerr << "  Pending items: " << std::endl;
    std::cerr << "    DH               : " << pendingDHItems.size() << std::endl;
    std::cerr << "    Tunnel Management: " << pendingGxsTunnelItems.size() << std::endl;
    std::cerr << "    Data (client)    : " << pendingGxsTunnelDataItems.size() << std::endl;
}
















