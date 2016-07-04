/*
 * libretroshare/src/services p3gxsreputation.cc
 *
 * Gxs Reputation Service  for RetroShare.
 *
 * Copyright 2011-2014 by Robert Fernie.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License Version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA.
 *
 * Please report all bugs and problems to "retroshare@lunamutt.com".
 *
 */

#include <math.h>
#include "pqi/p3linkmgr.h"

#include "retroshare/rspeers.h"

#include "services/p3gxsreputation.h"
#include "serialiser/rsgxsreputationitems.h"
#include "serialiser/rsconfigitems.h"

#include <sys/time.h>

#include <set>

/****
 * #define DEBUG_REPUTATION		1
 ****/
#define DEBUG_REPUTATION		1

/************ IMPLEMENTATION NOTES *********************************
 * 
 * p3GxsReputation shares opinions / reputations with peers.
 * This is closely linked to p3IdService, receiving info, updating
 * reputations as needed.
 *
 * It is designed as separate service as the exchange of peer opinions
 * is not well suited to Gxs Groups / Messages...
 *
 * Instead we can broadcast opinions to all peers.
 *
 * To avoid too much traffic, changes are transmitted rather than whole lists.
 * Peer A		   Peer B
 *  last update ----------->
 *	<----------- modified opinions.
 *
 * If not clever enough, this service will have to store a huge amount of data.
 * To make things tractable we do this:
 * 	- do not store reputations when no data is present, or when all friends are neutral
 * 	- only send a neutral opinion when they are a true change over someone's opinion
 * 	- only send a neutral opinion when it is a true change over someone's opinion
 * 	- auto-clean reputations for default values
 *
 * std::map<RsGxsId, Reputation> mReputations.
 * std::multimap<time_t, RsGxsId> mUpdated.
 *
 * std::map<RsPeerId, ReputationConfig> mConfig;
 *
 * Updates from p3GxsReputation -> p3IdService.
 * Updates from p3IdService -> p3GxsReputation.
 *
 * Each peer locally stores reputations for all GXS ids. If not stored, a default value
 * is used, corresponding to a neutral opinion. Peers also share their reputation level
 * with their neighbor nodes. 
 * 
 * The calculation method is the following:
 * 
 * 	Local values:
 * 		Good:      2
 * 		Neutral:   1
 * 		Bad:       0
 *
 * 	Overall reputation score:
 * 		
 * 		if(own_opinion == 0)	// means we dont' care
 * 			r = average_of_friends_opinions
 * 		else
 * 			r = own_opinion
 * 
 * 	Decisions based on reputation score:
 * 
 *              0               x1                1                    x2                   2
 *              | <-----------------------------------------------------------------------> |
 *     ---------+
 *     Lobbies  |  Msgs dropped
 *     Forums   |  Msgs dropped
 *     Messages |  Msgs dropped
 *     ---------+----------------------------------------------------------------------------
 * 
 * 	We select x1=0.5
 * 
 * 	=> to kill an identity, either you, or at least 50% of your friends need to flag it
 * 		as bad.
 * 	Rules:
 * 		* a single peer cannot drastically change the behavior of a given GXS id
 * 		* it should be easy for many peers to globally kill a GXS id
 * 
 * 	Typical examples:
 * 
 *            Friends   |  Friend average     |  Own     |  alpha     | Score
 *           -----------+---------------------+----------+------------+--------------
 *            10        |  0.5                |  1       |  0.25      | 0.375          
 *            10        |  1.0                |  1       |  0.25      | 1.0            
 *            10        |  1.0                |  0       |  0.25      | 1.0            
 * 
 * 	To check:
 *	[X]  Opinions are saved/loaded accross restart
 *	[X]  Opinions are transmitted to friends
 *	[X]  Opinions are transmitted to friends when updated
 * 
 * 	To do:
 * 	[X]  Add debug info
 * 	[X]  Test the whole thing
 * 	[X]  Implement a system to allow not storing info when we don't have it
 */

static const uint32_t LOWER_LIMIT                         = 0;        // used to filter valid Opinion values from serialized data
static const uint32_t UPPER_LIMIT                         = 2;        // used to filter valid Opinion values from serialized data
static const int      kMaximumPeerAge                     = 180;      // half a year.
static const int      kMaximumSetSize                     = 100;      // max set of updates to send at once.
static const int      ACTIVE_FRIENDS_UPDATE_PERIOD        = 600 ;     // 10 minutes
static const int      ACTIVE_FRIENDS_ONLINE_DELAY         = 86400*7 ; // 1 week.
static const int      kReputationRequestPeriod            = 600;      // 10 mins
static const int      kReputationStoreWait                = 180;      // 3 minutes.
static const float    REPUTATION_ASSESSMENT_THRESHOLD_X1  = 0.5f ;    // reputation under which the peer gets killed
static const uint32_t PGP_AUTO_BAN_THRESHOLD_DEFAULT      = 2 ;       // above this, auto ban any GXS id signed by this node
static const uint32_t IDENTITY_FLAGS_UPDATE_DELAY         = 100 ;     // 
static const uint32_t BANNED_NODES_UPDATE_DELAY           = 313 ;     // update approx every 5 mins. Chosen to not be a multiple of IDENTITY_FLAGS_UPDATE_DELAY
static const uint32_t REPUTATION_INFO_KEEP_DELAY          = 86400*35; // remove old reputation info 5 days after last usage limit, in case the ID would come back..

p3GxsReputation::p3GxsReputation(p3LinkMgr *lm)
	:p3Service(), p3Config(),
	mReputationMtx("p3GxsReputation"), mLinkMgr(lm) 
{
    addSerialType(new RsGxsReputationSerialiser());

    mPgpAutoBanThreshold = PGP_AUTO_BAN_THRESHOLD_DEFAULT ;
    mRequestTime = 0;
    mStoreTime = 0;
    mReputationsUpdated = false;
    mLastActiveFriendsUpdate = time(NULL) - 0.5*ACTIVE_FRIENDS_UPDATE_PERIOD;	// avoids doing it too soon since the TS from rsIdentity needs to be loaded already
    mAverageActiveFriends = 0 ;
    mLastBannedNodesUpdate = 0 ;
}

const std::string GXS_REPUTATION_APP_NAME = "gxsreputation";
const uint16_t GXS_REPUTATION_APP_MAJOR_VERSION  =       1;
const uint16_t GXS_REPUTATION_APP_MINOR_VERSION  =       0;
const uint16_t GXS_REPUTATION_MIN_MAJOR_VERSION  =       1;
const uint16_t GXS_REPUTATION_MIN_MINOR_VERSION  =       0;

RsServiceInfo p3GxsReputation::getServiceInfo()
{
        return RsServiceInfo(RS_SERVICE_GXS_TYPE_REPUTATION,
                GXS_REPUTATION_APP_NAME,
                GXS_REPUTATION_APP_MAJOR_VERSION,
                GXS_REPUTATION_APP_MINOR_VERSION,
                GXS_REPUTATION_MIN_MAJOR_VERSION,
                GXS_REPUTATION_MIN_MINOR_VERSION);
}

int	p3GxsReputation::tick()
{
	processIncoming();
	sendPackets();

	time_t now = time(NULL);

	if(mLastActiveFriendsUpdate + ACTIVE_FRIENDS_UPDATE_PERIOD < now)
	{
		updateActiveFriends() ;
                cleanup() ;
                
		mLastActiveFriendsUpdate = now ;
	}

    	static time_t last_identity_flags_update = 0 ;
        
        // no more than once per 5 second chunk.
        
        if(now > IDENTITY_FLAGS_UPDATE_DELAY+last_identity_flags_update)
        {
            last_identity_flags_update = now ;
            
            updateIdentityFlags() ;
        }
        if(now > BANNED_NODES_UPDATE_DELAY+mLastBannedNodesUpdate)	// 613 is not a multiple of 100, to avoid piling up work
        {
            mLastBannedNodesUpdate = now ;
            
            updateIdentityFlags() ;	// needed before updateBannedNodesList!
            updateBannedNodesList();
        }
        
#ifdef DEBUG_REPUTATION
	static time_t last_debug_print = time(NULL) ;

	if(now > 10+last_debug_print)
	{
		last_debug_print = now ;
		debug_print() ;
	}
#endif
	return 0;
}

void p3GxsReputation::setNodeAutoBanThreshold(uint32_t n) 
{
    RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/

    if(n != mPgpAutoBanThreshold)
    {
	    mLastBannedNodesUpdate = 0 ;
	    mPgpAutoBanThreshold = n ;
            IndicateConfigChanged() ;
    }
}
uint32_t p3GxsReputation::nodeAutoBanThreshold() 
{
    return mPgpAutoBanThreshold ;
}

int	p3GxsReputation::status()
{
	return 1;
}
class ZeroInitCnt
{
	public:
		ZeroInitCnt(): cnt(0) {}
		uint32_t cnt ;
        
        	operator uint32_t& () { return cnt ; }
        	operator uint32_t() const { return cnt ; }
};

void p3GxsReputation::updateBannedNodesList()
{
#ifdef DEBUG_REPUTATION
	std::cerr << "Updating PGP ban list based on signed GxsIds to ban" << std::endl;
#endif
	std::map<RsGxsId, Reputation> tmpreps ;

	RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/
	tmpreps = mReputations ;

	std::map<RsPgpId,ZeroInitCnt> pgp_ids_to_ban ;

	for( std::map<RsGxsId, Reputation>::iterator rit = tmpreps.begin();rit!=tmpreps.end();++rit)
		if((rit->second.mIdentityFlags & REPUTATION_IDENTITY_FLAG_PGP_LINKED) && !rit->second.mOwnerNode.isNull() && rit->second.mOwnOpinion == p3GxsReputation::OPINION_NEGATIVE)
			++pgp_ids_to_ban[rit->second.mOwnerNode] ;

	mBannedPgpIds.clear() ;

	if(mPgpAutoBanThreshold > 0)
		for(std::map<RsPgpId,ZeroInitCnt>::const_iterator it(pgp_ids_to_ban.begin());it!=pgp_ids_to_ban.end();++it)
		{
#ifdef DEBUG_REPUTATION
			std::cerr << "PGP Id: " << it->first << ". Ban count=" << it->second << " - " << (( it->second >= mPgpAutoBanThreshold)?"Banned!":"OK" ) << std::endl;
#endif
			if(it->second >= mPgpAutoBanThreshold)
				mBannedPgpIds.insert(it->first) ;
		}
}

void p3GxsReputation::updateIdentityFlags()
{
    std::list<RsGxsId> to_update ;

    // we need to gather the list to be used in a non locked frame
    {
	    RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/

#ifdef DEBUG_REPUTATION
	    std::cerr << "Updating reputation identity flags" << std::endl;
#endif

	    for( std::map<RsGxsId, Reputation>::iterator rit = mReputations.begin();rit!=mReputations.end();++rit)
		    if(rit->second.mIdentityFlags & REPUTATION_IDENTITY_FLAG_NEEDS_UPDATE)
			    to_update.push_back(rit->first) ;
    }

    for(std::list<RsGxsId>::const_iterator rit(to_update.begin());rit!=to_update.end();++rit)
    {
	    RsIdentityDetails details;

	    if(!rsIdentity->getIdDetails(*rit,details))
	    {
#ifdef DEBUG_REPUTATION
		    std::cerr << "  cannot obtain info for " << *rit << ". Will do it later." << std::endl;
#endif
		    continue ;
	    }

	    RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/

	    std::map<RsGxsId,Reputation>::iterator it = mReputations.find(*rit) ;

	    if(it == mReputations.end())
	    {
		    std::cerr << "  Weird situation: item " << *rit << " has been deleted from the list??" << std::endl;
		    continue ;
	    }
	    it->second.mIdentityFlags = 0 ;

	    if(details.mFlags & RS_IDENTITY_FLAGS_PGP_LINKED) 
	    {
		    it->second.mIdentityFlags |= REPUTATION_IDENTITY_FLAG_PGP_LINKED ;
		    it->second.mOwnerNode = details.mPgpId ;
	    }
	    if(details.mFlags & RS_IDENTITY_FLAGS_PGP_KNOWN ) it->second.mIdentityFlags |= REPUTATION_IDENTITY_FLAG_PGP_KNOWN ;

#ifdef DEBUG_REPUTATION
	    std::cerr << "  updated flags for " << *rit << " to " << std::hex << it->second.mIdentityFlags << std::dec << std::endl;
#endif

	    it->second.updateReputation() ;
	    IndicateConfigChanged();		
    }
}

void p3GxsReputation::cleanup()
{
    // remove opinions from friends that havn't been seen online for more than the specified delay

#ifdef DEBUG_REPUTATION
#endif
    std::cerr << "p3GxsReputation::cleanup() " << std::endl;

    // remove optionions about identities that do not exist anymore. That will in particular avoid asking p3idservice about deleted
    // identities, which would cause an excess of hits to the database.

    bool updated = false ;
    time_t now = time(NULL) ;

    RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/

    for(std::map<RsGxsId,Reputation>::iterator it(mReputations.begin());it!=mReputations.end();)
        if(it->second.mOpinions.empty() && it->second.mOwnOpinion == RsReputations::OPINION_NEUTRAL)
        {
            std::map<RsGxsId,Reputation>::iterator tmp(it) ;
            ++tmp ;
            mReputations.erase(it) ;
            it = tmp ;
#ifdef DEBUG_REPUTATION
            std::cerr << "  ID " << it->first << ": own is neutral and no opinions from friends => remove entry" << std::endl;
#endif
            updated = true ;
        }
        else if(rsIdentity->getLastUsageTS(it->first) + REPUTATION_INFO_KEEP_DELAY < now)
        {
#ifdef DEBUG_REPUTATION
            std::cerr << "  Identity " << it->first << " has a last usage TS of " << now - rsIdentity->getLastUsageTS(it->first) << " secs ago: deleting it." << std::endl;
#endif
            std::map<RsGxsId,Reputation>::iterator tmp(it) ;
            ++tmp ;
            mReputations.erase(it) ;
            it = tmp ;
            updated = true ;
        }
        else
            ++it ;

    if(updated)
        IndicateConfigChanged() ;
}

void p3GxsReputation::updateActiveFriends()
{
    RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/

    // keep track of who is recently connected.  That will give a value to average friend
    // for this, we count all friends that have been online in the last week.
 
    time_t now = time(NULL) ;
    
    std::list<RsPeerId> idList ;
    mLinkMgr->getFriendList(idList) ;

    mAverageActiveFriends = 0 ;
#ifdef DEBUG_REPUTATION
    std::cerr << "  counting recently online peers." << std::endl;
#endif

    for(std::list<RsPeerId>::const_iterator it(idList.begin());it!=idList.end();++it)
    {
	    RsPeerDetails details ;
#ifdef DEBUG_REPUTATION
        	std::cerr << "    "  << *it << ": last seen " << now - details.lastConnect << " secs ago" << std::endl;
#endif
            
	    if(rsPeers->getPeerDetails(*it, details) && now < details.lastConnect + ACTIVE_FRIENDS_ONLINE_DELAY)
	                    ++mAverageActiveFriends ;
    }
#ifdef DEBUG_REPUTATION
    std::cerr << "  new count: " << mAverageActiveFriends << std::endl;
#endif

}

const float RsReputations::REPUTATION_THRESHOLD_ANTI_SPAM = 1.4f ;
const float RsReputations::REPUTATION_THRESHOLD_DEFAULT   = 1.0f ;


static RsReputations::Opinion safe_convert_uint32t_to_opinion(uint32_t op)
{
	return RsReputations::Opinion(std::min((uint32_t)op,UPPER_LIMIT)) ;
}
/***** Implementation ******/

bool p3GxsReputation::processIncoming()
{
	/* for each packet - pass to specific handler */
	RsItem *item = NULL;
	while(NULL != (item = recvItem()))
	{
#ifdef DEBUG_REPUTATION
		std::cerr << "p3GxsReputation::processingIncoming() Received Item:";
		std::cerr << std::endl;
		item->print(std::cerr);
		std::cerr << std::endl;
#endif
		bool itemOk = true;
		switch(item->PacketSubType())
		{
			default:
			case RS_PKT_SUBTYPE_GXS_REPUTATION_CONFIG_ITEM:
			case RS_PKT_SUBTYPE_GXS_REPUTATION_SET_ITEM:
				std::cerr << "p3GxsReputation::processingIncoming() Unknown Item";
				std::cerr << std::endl;
				itemOk = false;
				break;

			case RS_PKT_SUBTYPE_GXS_REPUTATION_REQUEST_ITEM:
			{
				RsGxsReputationRequestItem *requestItem =  dynamic_cast<RsGxsReputationRequestItem *>(item);
				if (requestItem)
					SendReputations(requestItem);
				else
					itemOk = false;
			}
				break;

			case RS_PKT_SUBTYPE_GXS_REPUTATION_UPDATE_ITEM:
			{
				RsGxsReputationUpdateItem *updateItem =  dynamic_cast<RsGxsReputationUpdateItem *>(item);
                
				if (updateItem)
					RecvReputations(updateItem);
				else
					itemOk = false;
			}
				break;
		}

		if (!itemOk)
		{
			std::cerr << "p3GxsReputation::processingIncoming() Error with Item";
			std::cerr << std::endl;
		}

		/* clean up */
		delete item;
	}
	return true ;
} 
	

bool p3GxsReputation::SendReputations(RsGxsReputationRequestItem *request)
{
#ifdef DEBUG_REPUTATION
	std::cerr << "p3GxsReputation::SendReputations()" << std::endl;
#endif

	RsPeerId peerId = request->PeerId();
	time_t last_update = request->mLastUpdate;
	time_t now = time(NULL);

	RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/

	std::multimap<time_t, RsGxsId>::iterator tit;
	tit = mUpdated.upper_bound(last_update); // could skip some - (fixed below).

	int count = 0;
	int totalcount = 0;
	RsGxsReputationUpdateItem *pkt = new RsGxsReputationUpdateItem();
    
	pkt->PeerId(peerId);
	for(;tit != mUpdated.end(); ++tit)
	{
		/* find */
		std::map<RsGxsId, Reputation>::iterator rit = mReputations.find(tit->second);
        
		if (rit == mReputations.end())
		{
			std::cerr << "p3GxsReputation::SendReputations() ERROR Missing Reputation";
			std::cerr << std::endl;
			// error.
			continue;
		}

		if (rit->second.mOwnOpinionTs == 0)
		{
			std::cerr << "p3GxsReputation::SendReputations() ERROR OwnOpinionTS = 0";
			std::cerr << std::endl;
			// error.
			continue;
		}

		RsGxsId gxsId = rit->first;
		pkt->mOpinions[gxsId] = rit->second.mOwnOpinion;
		pkt->mLatestUpdate = rit->second.mOwnOpinionTs;
        
		if (pkt->mLatestUpdate == (uint32_t) now)
		{
			// if we could possibly get another Update at this point (same second).
			// then set Update back one second to ensure there are none missed.
			pkt->mLatestUpdate--;
		}
		
		count++;
		totalcount++;

		if (count > kMaximumSetSize)
		{
#ifdef DEBUG_REPUTATION
			std::cerr << "p3GxsReputation::SendReputations() Sending Full Packet";
			std::cerr << std::endl;
#endif

			sendItem(pkt);
            
			pkt = new RsGxsReputationUpdateItem();
			pkt->PeerId(peerId);
			count = 0;
		}
	}

	if (!pkt->mOpinions.empty())
	{
#ifdef DEBUG_REPUTATION
		std::cerr << "p3GxsReputation::SendReputations() Sending Final Packet";
		std::cerr << std::endl;
#endif

		sendItem(pkt);
	}
	else
	{
		delete pkt;
	}

#ifdef DEBUG_REPUTATION
	std::cerr << "p3GxsReputation::SendReputations() Total Count: " << totalcount;
	std::cerr << std::endl;
#endif

	return true;
}

void p3GxsReputation::locked_updateOpinion(const RsPeerId& from,const RsGxsId& about,RsReputations::Opinion op)
{
    /* find matching Reputation */
    std::map<RsGxsId, Reputation>::iterator rit = mReputations.find(about);

    RsReputations::Opinion new_opinion = safe_convert_uint32t_to_opinion(op);
    RsReputations::Opinion old_opinion = RsReputations::OPINION_NEUTRAL ;	// default if not set

    bool updated = false ;

#ifdef DEBUG_REPUTATION
    std::cerr << "p3GxsReputation::update opinion of " << about << " from " << from << " to " << op << std::endl;
#endif
    // now 4 cases;
    //    Opinion already stored          
    //        New opinion is same:         nothing to do
    //        New opinion is different:    if neutral, remove entry
    //    Nothing stored
    //        New opinion is neutral:      nothing to do
    //        New opinion is != 1:         create entry and store

    if (rit == mReputations.end())	
    {
#ifdef DEBUG_REPUTATION
	    std::cerr << "  no preview record"<< std::endl;
#endif

	    if(new_opinion != RsReputations::OPINION_NEUTRAL)
	    {
		    mReputations[about] = Reputation(about);
		    rit = mReputations.find(about);
	    }
	    else
	    {
#ifdef DEBUG_REPUTATION
		    std::cerr << "  no changes!"<< std::endl;
#endif
		    return ;	// nothing to do
	    }
    }

    Reputation& reputation = rit->second;

    std::map<RsPeerId,RsReputations::Opinion>::iterator it2 = reputation.mOpinions.find(from) ;

    if(it2 == reputation.mOpinions.end())
    {
	    if(new_opinion != RsReputations::OPINION_NEUTRAL)
	    {
		    reputation.mOpinions[from] = new_opinion;	// filters potentially tweaked reputation score sent by friend
		    updated = true ;
	    }
    }
    else
    {
	    old_opinion = it2->second ;

	    if(new_opinion == RsReputations::OPINION_NEUTRAL)
	    {
		    reputation.mOpinions.erase(it2) ;		// don't store when the opinion is neutral
		    updated = true ;
	    }
	    else if(new_opinion != old_opinion)
	    {
		    it2->second = new_opinion ;
		    updated = true ;
	    }
    }

    if(reputation.mOpinions.empty() && reputation.mOwnOpinion == RsReputations::OPINION_NEUTRAL)
    {
	    mReputations.erase(rit) ;
#ifdef DEBUG_REPUTATION
	    std::cerr << "  own is neutral and no opinions from friends => remove entry" << std::endl;
#endif
        updated = true ;
    }
    else if(updated)
    {
#ifdef DEBUG_REPUTATION
	    std::cerr << "  reputation changed. re-calculating." << std::endl;
#endif
	    reputation.updateReputation() ;
    }
    
    if(updated)
	    IndicateConfigChanged() ;
}

bool p3GxsReputation::RecvReputations(RsGxsReputationUpdateItem *item)
{
#ifdef DEBUG_REPUTATION
	std::cerr << "p3GxsReputation::RecvReputations() from " << item->PeerId() << std::endl;
#endif

	RsPeerId peerid = item->PeerId();

	for( std::map<RsGxsId, uint32_t>::iterator it = item->mOpinions.begin(); it != item->mOpinions.end(); ++it)
	{
		RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/

		locked_updateOpinion(peerid,it->first,safe_convert_uint32t_to_opinion(it->second));
	}

	updateLatestUpdate(peerid,item->mLatestUpdate);

	return true;
}


bool p3GxsReputation::updateLatestUpdate(RsPeerId peerid,time_t latest_update)
{
	RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/

    std::map<RsPeerId, ReputationConfig>::iterator it = mConfig.find(peerid);

    if (it == mConfig.end())
	{
        mConfig[peerid] = ReputationConfig(peerid);
        it = mConfig.find(peerid) ;
	}
	it->second.mLatestUpdate = latest_update ;

	mReputationsUpdated = true;	
	// Switched to periodic save due to scale of data.
    
	IndicateConfigChanged();		

	return true;
}

/********************************************************************
 * Opinion
 ****/

bool p3GxsReputation::getReputationInfo(const RsGxsId& gxsid, const RsPgpId& owner_id, RsReputations::ReputationInfo& info)
{
    if(gxsid.isNull())
        return false ;
        
    RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/

#ifdef DEBUG_REPUTATION
    std::cerr << "getReputationInfo() for " << gxsid << std::endl;
#endif
    std::map<RsGxsId,Reputation>::const_iterator it = mReputations.find(gxsid) ;

    if(it == mReputations.end())
    {
        info.mOwnOpinion = RsReputations::OPINION_NEUTRAL ;
        info.mOverallReputationScore = RsReputations::REPUTATION_THRESHOLD_DEFAULT ;
    info.mFriendAverage = REPUTATION_THRESHOLD_DEFAULT ;
    }
    else
    {
        const Reputation& rep(it->second) ;

        info.mOwnOpinion = RsReputations::Opinion(rep.mOwnOpinion) ;
        info.mOverallReputationScore = rep.mReputation ;
        info.mFriendAverage = rep.mFriendAverage ;
    }

    if(!owner_id.isNull() && (mBannedPgpIds.find(owner_id) != mBannedPgpIds.end()))
    {
#ifdef DEBUG_REPUTATION
        std::cerr << "p3GxsReputations: identity " << gxsid << " is banned because owner node ID " << owner_id << " is banned." << std::endl;
#endif
        info.mAssessment = RsReputations::ASSESSMENT_BAD ;
    }
    else if(info.mOverallReputationScore <= REPUTATION_ASSESSMENT_THRESHOLD_X1)
        info.mAssessment = RsReputations::ASSESSMENT_BAD ;
    else
        info.mAssessment = RsReputations::ASSESSMENT_OK ;

#ifdef DEBUG_REPUTATION
	    std::cerr << "  information present. OwnOp = " << info.mOwnOpinion << ", overall score=" << info.mAssessment << std::endl;
#endif

    return true ;
}

bool p3GxsReputation::isIdentityBanned(const RsGxsId &id,const RsPgpId& owner_node)
{
    RsReputations::ReputationInfo info ;
    
    if(!getReputationInfo(id,owner_node,info))
        return false ;

#ifdef DEBUG_REPUTATION
    std::cerr << "isIdentityBanned(): returning " << (info.mAssessment == RsReputations::ASSESSMENT_BAD) << " for GXS id " << id << std::endl;
#endif
    return info.mAssessment == RsReputations::ASSESSMENT_BAD ;
}

bool p3GxsReputation::setOwnOpinion(const RsGxsId& gxsid, const RsReputations::Opinion& opinion)
{
#ifdef DEBUG_REPUTATION
    std::cerr << "setOwnOpinion(): for GXS id " << gxsid << " to " << opinion << std::endl;
#endif
    if(gxsid.isNull())
    {
        std::cerr << "  ID " << gxsid << " is rejected. Look for a bug in calling method." << std::endl;
        return false ;
    }
                     
	RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/

	std::map<RsGxsId, Reputation>::iterator rit;

	/* find matching Reputation */
	rit = mReputations.find(gxsid);
    
	if (rit == mReputations.end())
	{
		mReputations[gxsid] = Reputation(gxsid);
		rit = mReputations.find(gxsid);
	}

	// we should remove previous entries from Updates...
	Reputation &reputation = rit->second;
	if (reputation.mOwnOpinionTs != 0)
	{
		if (reputation.mOwnOpinion == opinion)
		{
			// if opinion is accurate, don't update.
			return false;
		}

		std::multimap<time_t, RsGxsId>::iterator uit, euit;
		uit = mUpdated.lower_bound(reputation.mOwnOpinionTs);
		euit = mUpdated.upper_bound(reputation.mOwnOpinionTs);
		for(; uit != euit; ++uit)
		{
			if (uit->second == gxsid)
			{
				mUpdated.erase(uit);
				break;
			}
		}
	}

	time_t now = time(NULL);
	reputation.mOwnOpinion = opinion;
	reputation.mOwnOpinionTs = now;
	reputation.updateReputation();

	mUpdated.insert(std::make_pair(now, gxsid));
	mUpdatedReputations.insert(gxsid);
	mReputationsUpdated = true;	
	mLastBannedNodesUpdate = 0 ;	// for update of banned nodes
    
	// Switched to periodic save due to scale of data.
	IndicateConfigChanged();		
    
	return true;
}


/********************************************************************
 * Configuration.
 ****/

RsSerialiser *p3GxsReputation::setupSerialiser()
{
        RsSerialiser *rss = new RsSerialiser ;
	rss->addSerialType(new RsGxsReputationSerialiser());
	rss->addSerialType(new RsGeneralConfigSerialiser());
        return rss ;
}

bool p3GxsReputation::saveList(bool& cleanup, std::list<RsItem*> &savelist)
{
	cleanup = true;
	RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/

#ifdef DEBUG_REPUTATION
    std::cerr << "p3GxsReputation::saveList()" << std::endl;
#endif
	/* save */
	std::map<RsPeerId, ReputationConfig>::iterator it;
	for(it = mConfig.begin(); it != mConfig.end(); ++it)
	{
		if (!rsPeers->isFriend(it->first))
		{
			// discard info from non-friends.
			continue;
		}

		RsGxsReputationConfigItem *item = new RsGxsReputationConfigItem();
		item->mPeerId = it->first;
		item->mLatestUpdate = it->second.mLatestUpdate;
		item->mLastQuery = it->second.mLastQuery;
		savelist.push_back(item);
	}

	int count = 0;
 	std::map<RsGxsId, Reputation>::iterator rit;
	for(rit = mReputations.begin(); rit != mReputations.end(); ++rit, count++)
	{
		RsGxsReputationSetItem *item = new RsGxsReputationSetItem();
		item->mGxsId = rit->first;
		item->mOwnOpinion = rit->second.mOwnOpinion;
		item->mOwnOpinionTS = rit->second.mOwnOpinionTs;
		item->mIdentityFlags = rit->second.mIdentityFlags;

		std::map<RsPeerId, RsReputations::Opinion>::iterator oit;
		for(oit = rit->second.mOpinions.begin(); oit != rit->second.mOpinions.end(); ++oit)
		{
			// should be already limited.
			item->mOpinions[oit->first] = (uint32_t)oit->second;
		}

		savelist.push_back(item);
		count++;
	}
    
	RsConfigKeyValueSet *vitem = new RsConfigKeyValueSet ;
	RsTlvKeyValue kv;
	kv.key = "AUTO_BAN_NODES_THRESHOLD" ;
	rs_sprintf(kv.value, "%d", mPgpAutoBanThreshold);
	vitem->tlvkvs.pairs.push_back(kv) ;

	savelist.push_back(vitem) ;

	return true;
}

void p3GxsReputation::saveDone()
{
	return;
}

bool p3GxsReputation::loadList(std::list<RsItem *>& loadList)
{
#ifdef DEBUG_REPUTATION
    std::cerr << "p3GxsReputation::saveList()" << std::endl;
#endif
    std::list<RsItem *>::iterator it;
    std::set<RsPeerId> peerSet;

    for(it = loadList.begin(); it != loadList.end(); ++it)
    {
	    RsGxsReputationConfigItem *item = dynamic_cast<RsGxsReputationConfigItem *>(*it);

	    // Configurations are loaded first. (to establish peerSet).
	    if (item)
	    {
		    RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/
		    RsPeerId peerId(item->mPeerId);
		    ReputationConfig &config = mConfig[peerId];
		    config.mPeerId = peerId;
		    config.mLatestUpdate = item->mLatestUpdate;
		    config.mLastQuery = 0;

		    peerSet.insert(peerId);
	    }

	    RsGxsReputationSetItem *set = dynamic_cast<RsGxsReputationSetItem *>(*it);
	    if (set)
		    loadReputationSet(set, peerSet);

	    RsConfigKeyValueSet *vitem = dynamic_cast<RsConfigKeyValueSet *>(*it);

	    if(vitem)
		    for(std::list<RsTlvKeyValue>::const_iterator kit = vitem->tlvkvs.pairs.begin(); kit != vitem->tlvkvs.pairs.end(); ++kit) 
		    {
			    if(kit->key == "AUTO_BAN_NODES_THRESHOLD")
			    {
				    int val ;
				    if (sscanf(kit->value.c_str(), "%d", &val) == 1)
				    {
					    mPgpAutoBanThreshold = val ;
					    std::cerr << "Setting AutoBanNode threshold to " << val << std::endl ;
					    mLastBannedNodesUpdate = 0 ;	// force update
				    }
			    };
		    }

	    delete (*it);
    }

    loadList.clear() ;
    return true;
}

bool p3GxsReputation::loadReputationSet(RsGxsReputationSetItem *item, const std::set<RsPeerId> &peerSet)
{
	RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/

	std::map<RsGxsId, Reputation>::iterator rit;

    	if(item->mGxsId.isNull())	// just a protection against potential errors having put 00000 into ids.
            return false ;
        
	/* find matching Reputation */
	RsGxsId gxsId(item->mGxsId);
	rit = mReputations.find(gxsId);
	if (rit != mReputations.end())
	{
		std::cerr << "ERROR";
		std::cerr << std::endl;
	}

	Reputation &reputation = mReputations[gxsId];

	// install opinions.
	std::map<RsPeerId, uint32_t>::const_iterator oit;
	for(oit = item->mOpinions.begin(); oit != item->mOpinions.end(); ++oit)
	{
		// expensive ... but necessary.
		RsPeerId peerId(oit->first);
		if (peerSet.end() != peerSet.find(peerId))
			reputation.mOpinions[peerId] = safe_convert_uint32t_to_opinion(oit->second);
	}

	reputation.mOwnOpinion = item->mOwnOpinion;
	reputation.mOwnOpinionTs = item->mOwnOpinionTS;

	// if dropping entries has changed the score -> must update.
    
    	//float old_reputation = reputation.mReputation ;
	//mUpdatedReputations.insert(gxsId) ;
    
	reputation.updateReputation() ;

	mUpdated.insert(std::make_pair(reputation.mOwnOpinionTs, gxsId));
	return true;
}


/********************************************************************
 * Send Requests.
 ****/

int	p3GxsReputation::sendPackets()
{
	time_t now = time(NULL);
	time_t requestTime, storeTime;
	{
		RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/
		requestTime = mRequestTime;
		storeTime = mStoreTime;
	}

	if (now > requestTime + kReputationRequestPeriod)
	{
		sendReputationRequests();

		RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/

#ifdef DEBUG_REPUTATION
		std::cerr << "p3GxsReputation::sendPackets() Regular Broadcast";
		std::cerr << std::endl;
#endif
		mRequestTime = now;
		mStoreTime = now + kReputationStoreWait;
	}

	if (now > storeTime)
	{
		RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/

#ifdef DEBUG_REPUTATION
		std::cerr << "p3GxsReputation::sendPackets() Regular Broadcast";
		std::cerr << std::endl;
#endif
		// push it into the future.
		// store time will be reset when requests are send.
		mStoreTime = now + kReputationRequestPeriod;

		if (mReputationsUpdated)
		{
			IndicateConfigChanged();
			mReputationsUpdated = false;
		}
	}

	return true ;
}

void p3GxsReputation::sendReputationRequests()
{
	/* we ping our peers */
	/* who is online? */
	std::list<RsPeerId> idList;

	mLinkMgr->getOnlineList(idList);

	/* prepare packets */
	std::list<RsPeerId>::iterator it;
	for(it = idList.begin(); it != idList.end(); ++it)
		sendReputationRequest(*it);
}

int p3GxsReputation::sendReputationRequest(RsPeerId peerid)
{
#ifdef DEBUG_REPUTATION
    time_t now = time(NULL) ;
	std::cerr << "  p3GxsReputation::sendReputationRequest(" << peerid << ") " ;
#endif

	/* */
	RsGxsReputationRequestItem *requestItem =  new RsGxsReputationRequestItem();
	requestItem->PeerId(peerid);

	{
		RsStackMutex stack(mReputationMtx); /****** LOCKED MUTEX *******/

		/* find the last timestamp we have */
		std::map<RsPeerId, ReputationConfig>::iterator it = mConfig.find(peerid);
        
		if (it != mConfig.end())
		{
#ifdef DEBUG_REPUTATION
            		std::cerr << "  lastUpdate = " << now - it->second.mLatestUpdate << " secs ago. Requesting only more recent." << std::endl;
#endif
			requestItem->mLastUpdate = it->second.mLatestUpdate;
		}
		else
		{
#ifdef DEBUG_REPUTATION
            		std::cerr << "  lastUpdate = never. Requesting all!" << std::endl;
#endif
			// get whole list.
			requestItem->mLastUpdate = 0;
		}
	}

	sendItem(requestItem);
	return 1;
}

void Reputation::updateReputation() 
{
    // the calculation of reputation makes the whole thing   

    int friend_total = 0;

    // accounts for all friends. Neutral opinions count for 1-1=0
    // because the average is performed over only accessible peers (not the total number) we need to shift to 1

    for(std::map<RsPeerId,RsReputations::Opinion>::const_iterator it(mOpinions.begin());it!=mOpinions.end();++it)
	    friend_total += it->second - 1;

    if(mOpinions.empty())	// includes the case of no friends!
	    mFriendAverage = 1.0f ;
    else
    {
	    static const float REPUTATION_FRIEND_FACTOR_ANON       =  2.0f ;
	    static const float REPUTATION_FRIEND_FACTOR_PGP_LINKED =  5.0f ;
	    static const float REPUTATION_FRIEND_FACTOR_PGP_KNOWN  = 10.0f ;

	    // For positive votes, start from 1 and slowly tend to 2
	    // for negative votes, start from 1 and slowly tend to 0
	    // depending on signature state, the ID is harder (signed ids) or easier (anon ids) to ban or to promote.
	    //
	    // when REPUTATION_FRIEND_VARIANCE = 3, that gives the following values:
	    //
	    // total votes  |  mFriendAverage anon |  mFriendAverage PgpLinked | mFriendAverage PgpKnown  |
	    //              |        F=2.0         |        F=5.0              |      F=10.0              |
	    // -------------+----------------------+---------------------------+--------------------------+
	    // -10          |  0.00  Banned        |  0.13  Banned             | 0.36 Banned              |
	    // -5           |  0.08  Banned        |  0.36  Banned             | 0.60                     |
	    // -4           |  0.13  Banned        |  0.44  Banned             | 0.67                     |
	    // -3           |  0.22  Banned        |  0.54                     | 0.74                     |
	    // -2           |  0.36  Banned        |  0.67                     | 0.81                     |
	    // -1           |  0.60                |  0.81                     | 0.90                     |
	    //  0           |  1.0                 |  1.0                      | 1.00                     |
	    //  1           |  1.39                |  1.18                     | 1.09                     |
	    //  2           |  1.63                |  1.32                     | 1.18                     |
	    //  3           |  1.77                |  1.45                     | 1.25                     |
	    //  4           |  1.86                |  1.55                     | 1.32                     |
	    //  5           |  1.91                |  1.63                     | 1.39                     |
	    //
	    // Banning info is provided by the reputation system, and does not depend on PGP-sign state.
	    //
	    // However, each service might have its own rules for the different cases. For instance
	    // PGP-favoring forums might want a score > 1.4 for anon ids, and >= 1.0 for PGP-signed.

	    float reputation_bias ;

	    if(mIdentityFlags & REPUTATION_IDENTITY_FLAG_PGP_KNOWN)
		    reputation_bias = REPUTATION_FRIEND_FACTOR_PGP_KNOWN ;
	    else if(mIdentityFlags & REPUTATION_IDENTITY_FLAG_PGP_LINKED)
		    reputation_bias = REPUTATION_FRIEND_FACTOR_PGP_LINKED ;
	    else
		    reputation_bias = REPUTATION_FRIEND_FACTOR_ANON ;

	    if(friend_total > 0)
		    mFriendAverage = 2.0f-exp(-friend_total / reputation_bias) ;
	    else
		    mFriendAverage =      exp( friend_total / reputation_bias) ;
    }

    // now compute a bias for PGP-signed ids.
    
    if(mOwnOpinion == RsReputations::OPINION_NEUTRAL)
	    mReputation = mFriendAverage ;
    else
	    mReputation = (float)mOwnOpinion ;
}

void p3GxsReputation::debug_print()
{
    std::cerr << "Reputations database: " << std::endl;
    std::cerr << "  Average number of peers: " << mAverageActiveFriends << std::endl;
    
    time_t now = time(NULL) ;
    
    for(std::map<RsGxsId,Reputation>::const_iterator it(mReputations.begin());it!=mReputations.end();++it)
    {
	std::cerr << "  ID=" << it->first << ", own: " << it->second.mOwnOpinion << ", Friend average: " << it->second.mFriendAverage << ", global_score: " << it->second.mReputation 
              << ", last own update: " << now - it->second.mOwnOpinionTs << " secs ago." << std::endl;
    
    	for(std::map<RsPeerId,RsReputations::Opinion>::const_iterator it2(it->second.mOpinions.begin());it2!=it->second.mOpinions.end();++it2)
            std::cerr << "    " << it2->first << ": " << it2->second << std::endl;
    }
}

