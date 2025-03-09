/*******************************************************************************
 * libretroshare/src/services: p3wire.cc                                       *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2012-2020 by Robert Fernie <retroshare@lunamutt.com>              *
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
#include "services/p3wire.h"
#include "rsitems/rswireitems.h"

#include "util/rsrandom.h"

/****
 * #define WIRE_DEBUG 1
 ****/

RsWire *rsWire = NULL;

RsWireGroup::RsWireGroup()
	:mGroupPulses(0),mGroupRepublishes(0),mGroupLikes(0),mGroupReplies(0)
	,mRefMentions(0),mRefRepublishes(0),mRefLikes(0),mRefReplies(0)
{
	return;
}

uint32_t RsWirePulse::ImageCount()
{
	uint32_t images = 0;
	if (!mImage1.empty()) {
		images++;
	}
	if (!mImage2.empty()) {
		images++;
	}
	if (!mImage3.empty()) {
		images++;
	}
	if (!mImage4.empty()) {
		images++;
	}
	return images;
}

p3Wire::p3Wire(RsGeneralDataService* gds, RsNetworkExchangeService* nes, RsGixs *gixs)
	:RsGenExchange(gds, nes, new RsGxsWireSerialiser(), RS_SERVICE_GXS_TYPE_WIRE, gixs, wireAuthenPolicy()),
    RsWire(static_cast<RsGxsIface&>(*this)), mWireMtx("WireMtx"),
    mKnownWireMutex("KnownWireMutex")
{

}


const std::string WIRE_APP_NAME = "gxswire";
const uint16_t WIRE_APP_MAJOR_VERSION  =       1;
const uint16_t WIRE_APP_MINOR_VERSION  =       0;
const uint16_t WIRE_MIN_MAJOR_VERSION  =       1;
const uint16_t WIRE_MIN_MINOR_VERSION  =       0;

RsServiceInfo p3Wire::getServiceInfo()
{
	return RsServiceInfo(RS_SERVICE_GXS_TYPE_WIRE,
		WIRE_APP_NAME,
		WIRE_APP_MAJOR_VERSION,
		WIRE_APP_MINOR_VERSION,
		WIRE_MIN_MAJOR_VERSION,
		WIRE_MIN_MINOR_VERSION);
}



uint32_t p3Wire::wireAuthenPolicy()
{
	uint32_t policy = 0;
	uint8_t flag = 0;

	// Edits generally need an authors signature.

	// Wire requires all TopLevel (Orig/Reply) msgs to be signed with both PUBLISH & AUTHOR.
	// Reply References need to be signed by Author.
	flag = GXS_SERV::MSG_AUTHEN_ROOT_PUBLISH_SIGN | GXS_SERV::MSG_AUTHEN_CHILD_AUTHOR_SIGN;
	flag |= GXS_SERV::MSG_AUTHEN_ROOT_AUTHOR_SIGN;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::PUBLIC_GRP_BITS);

	// expect the requirements to be the same for RESTRICTED / PRIVATE groups too.
	// This needs to be worked through / fully evaluated.
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::RESTRICTED_GRP_BITS);
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::PRIVATE_GRP_BITS);

	flag = 0;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::GRP_OPTION_BITS);

	return policy;
}

void p3Wire::service_tick()
{
	return;
}

RsTokenService* p3Wire::getTokenService() {

	return RsGenExchange::getTokenService();
}

static const uint32_t WIRE_CONFIG_MAX_TIME_NOTIFY_STORAGE = 86400*30*2 ; // ignore notifications for 2 months
static const uint8_t  WIRE_CONFIG_SUBTYPE_NOTIFY_RECORD   = 0x01 ;

struct RsWireNotifyRecordsItem: public RsItem
{

    RsWireNotifyRecordsItem()
        : RsItem(RS_PKT_VERSION_SERVICE,RS_SERVICE_GXS_TYPE_WIRE_CONFIG,WIRE_CONFIG_SUBTYPE_NOTIFY_RECORD)
    {}

    virtual ~RsWireNotifyRecordsItem() {}

    void serial_process( RsGenericSerializer::SerializeJob j,
                         RsGenericSerializer::SerializeContext& ctx )
    { RS_SERIAL_PROCESS(records); }

    void clear() {}

    std::map<RsGxsGroupId,rstime_t> records;
};

class WireConfigSerializer : public RsServiceSerializer
{
public:
    WireConfigSerializer() : RsServiceSerializer(RS_SERVICE_GXS_TYPE_WIRE_CONFIG) {}
    virtual ~WireConfigSerializer() {}

    RsItem* create_item(uint16_t service_id, uint8_t item_sub_id) const
    {
        if(service_id != RS_SERVICE_GXS_TYPE_WIRE_CONFIG)
            return NULL;

        switch(item_sub_id)
        {
        case WIRE_CONFIG_SUBTYPE_NOTIFY_RECORD: return new RsWireNotifyRecordsItem();
        default:
            return NULL;
        }
    }
};

bool p3Wire::saveList(bool &cleanup, std::list<RsItem *>&saveList)
{
    cleanup = true ;

    RsWireNotifyRecordsItem *item = new RsWireNotifyRecordsItem ;

    {
        RS_STACK_MUTEX(mKnownWireMutex);
        item->records = mKnownWire ;
    }

    saveList.push_back(item) ;
    return true;
}

bool p3Wire::loadList(std::list<RsItem *>& loadList)
{
    while(!loadList.empty())
    {
        RsItem *item = loadList.front();
        loadList.pop_front();

        rstime_t now = time(NULL);

        RsWireNotifyRecordsItem *fnr = dynamic_cast<RsWireNotifyRecordsItem*>(item) ;

        if(fnr != NULL)
        {
            RS_STACK_MUTEX(mKnownWireMutex);

            mKnownWire.clear();

            for(auto it(fnr->records.begin());it!=fnr->records.end();++it)
                if( now < it->second + WIRE_CONFIG_MAX_TIME_NOTIFY_STORAGE)
                    mKnownWire.insert(*it) ;
        }

        delete item ;
    }
    return true;
}

RsSerialiser* p3Wire::setupSerialiser()
{
    RsSerialiser* rss = new RsSerialiser;
    rss->addSerialType(new WireConfigSerializer());

    return rss;
}

void p3Wire::notifyChanges(std::vector<RsGxsNotify*> &changes)
{
    std::cerr << "p3Wire::notifyChanges() New stuff";
    std::cerr << std::endl;

    int sizer = changes.size();

    #ifdef GXSWIRE_DEBUG
        RsDbg() << " Processing " << changes.size() << " wire changes..." << std::endl;
    #endif
        /* iterate through and grab any new messages */
//        std::set<RsGxsGroupId> unprocessedGroups;

        std::vector<RsGxsNotify *>::iterator it;
        for(it = changes.begin(); it != changes.end(); ++it)
        {
            // here the changes are for the message
            RsGxsMsgChange *msgChange = dynamic_cast<RsGxsMsgChange *>(*it);
            if (msgChange)
            {
                if (msgChange->getType() == RsGxsNotify::TYPE_RECEIVED_NEW|| msgChange->getType() == RsGxsNotify::TYPE_PUBLISHED)
                {
                    /* message received */
                    if (rsEvents)
                    {
                        auto ev = std::make_shared<RsWireEvent>();
                        ev->mWireMsgId = msgChange->mMsgId;
                        ev->mWireGroupId = msgChange->mGroupId;
                        std::cout<<"################################## the message ##########################"<<std::endl;
                        auto temp =dynamic_cast<RsGxsWirePulseItem*>(msgChange->mNewMsgItem);
                        if(nullptr != temp)
                        {
                            // this condition checks for any new comments/replies (comment and reply are same)
                            if((temp->pulse.mPulseType & ~WIRE_PULSE_TYPE_RESPONSE)==WIRE_PULSE_TYPE_REPLY)
                            {
                                ev->mWireEventCode = RsWireEventCode::NEW_REPLY;
                                ev->mWireThreadId = msgChange->mNewMsgItem->meta.mThreadId;
                                rsEvents->postEvent(ev);
                                std::cout << "new reply"<<std::endl;
                            }
                            // this condition checks for any new likes
                            else if((temp->pulse.mPulseType & ~WIRE_PULSE_TYPE_RESPONSE)==WIRE_PULSE_TYPE_LIKE)
                                {
                                    ev->mWireEventCode = RsWireEventCode::NEW_LIKE;
                                    ev->mWireThreadId = msgChange->mNewMsgItem->meta.mThreadId;
                                    ev->mWireParentId = msgChange->mNewMsgItem->meta.mParentId;
                                    rsEvents->postEvent(ev);
                                    std::cout << "new like"<<std::endl;
                                }
                            // this condition checks for any new pulses and republishes
                            else if((temp->pulse.mPulseType & ~WIRE_PULSE_TYPE_RESPONSE)==WIRE_PULSE_TYPE_ORIGINAL||(temp->pulse.mPulseType & ~WIRE_PULSE_TYPE_RESPONSE)==WIRE_PULSE_TYPE_REPUBLISH)
                                {
                                    ev->mWireEventCode = RsWireEventCode::NEW_POST;
                                    rsEvents->postEvent(ev);
                                    std::cout << "new post"<<std::endl;
                                }

                            else{
                                RS_WARN("Got unknown gxs message subtype: ", temp->pulse.mPulseType);
                                std::cout<<"the number is "<<WIRE_PULSE_TYPE_RESPONSE<<std::endl;
                                std::cout<<"the negation is "<<~WIRE_PULSE_TYPE_RESPONSE<<std::endl;
                                std::cout << "_((((((((((((((((((((((((((((((((((((((((((((((("<<std::endl;
                            }
                        }

                    }
                }

                if (!msgChange->metaChange())
                {
#ifdef GXSWIRE_DEBUG
                    std::cerr << "p3GxsWire::notifyChanges() Found Message Change Notification";
                    std::cerr << std::endl;
#endif
//                    unprocessedGroups.insert(msgChange->mGroupId);
                }
            }

            // here the changes are for the group
            RsGxsGroupChange *grpChange = dynamic_cast<RsGxsGroupChange*>(*it);

            if (grpChange && rsEvents)
            {
#ifdef GXSWIRE_DEBUG
                RsDbg() << " Grp Change Event or type " << grpChange->getType() << ":" << std::endl;
#endif

                std::cout<<"$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$ the type $$$$$$$$$$$$$$$$$$"<<std::endl;
                switch (grpChange->getType())
                {

//                case RsGxsNotify::TYPE_PUBLISHED:	//  happens when the wire user is followed/unfollowed
//                {
//                    auto ev = std::make_shared<RsWireEvent>();
//                    ev->mWireGroupId = grpChange->mGroupId;
//                    ev->mWireEventCode = RsWireEventCode::FOLLOW_STATUS_CHANGED;
//                    rsEvents->postEvent(ev);

//                    unprocessedGroups.insert(grpChange->mGroupId);
//                }
//                    break;

                case RsGxsNotify::TYPE_PROCESSED:	// happens when the post is updated
                    std::cout << "type processed"<<std::endl;
                    break;
//                case RsGxsNotify::TYPE_PROCESSED:	// happens when the post is updated
//                {
//                    auto ev = std::make_shared<RsWireEvent>();
//                    ev->mWireGroupId = grpChange->mGroupId;
//                    ev->mWireEventCode = RsWireEventCode::POST_UPDATED;
//                    rsEvents->postEvent(ev);

////                    unprocessedGroups.insert(grpChange->mGroupId);
//                }
//                    break;
                case RsGxsNotify::TYPE_UPDATED:	// happens when the wire is updated
                {
                    auto ev = std::make_shared<RsWireEvent>();
                    ev->mWireGroupId = grpChange->mGroupId;
                    ev->mWireEventCode = RsWireEventCode::WIRE_UPDATED;
                    rsEvents->postEvent(ev);
                    std::cout << "type updated"<<std::endl;

//                    unprocessedGroups.insert(grpChange->mGroupId);
                }
                    break;

                case RsGxsNotify::TYPE_PUBLISHED:
                    std::cout << "type publish"<<std::endl;
                case RsGxsNotify::TYPE_RECEIVED_NEW:	// happens when the wire is updated
                {
                    auto ev = std::make_shared<RsWireEvent>();
                    ev->mWireGroupId = grpChange->mGroupId;
                    ev->mWireEventCode = RsWireEventCode::NEW_WIRE;
                    rsEvents->postEvent(ev);
                    std::cout << "type received new"<<std::endl;
//                    unprocessedGroups.insert(grpChange->mGroupId);
                }
                    break;

                case RsGxsNotify::TYPE_STATISTICS_CHANGED:
                {
                    auto ev = std::make_shared<RsWireEvent>();
                    ev->mWireGroupId = grpChange->mGroupId;
                    ev->mWireEventCode = RsWireEventCode::STATISTICS_CHANGED;
                    rsEvents->postEvent(ev);
                    std::cout << "type stat changed"<<std::endl;
                    RS_STACK_MUTEX(mKnownWireMutex);
                    mKnownWire[grpChange->mGroupId] = time(nullptr);
                    IndicateConfigChanged();
                }
                    break;

                default:
                    RsErr() << " Got a GXS event of type " << grpChange->getType() << " Currently not handled." << std::endl;
                    break;
                }
            }

            delete *it;
        }
}

		/* Specific Service Data */
bool p3Wire::getGroupData(const uint32_t &token, std::vector<RsWireGroup> &groups)
{
	std::cerr << "p3Wire::getGroupData()";
	std::cerr << std::endl;

	std::vector<RsGxsGrpItem*> grpData;
	bool ok = RsGenExchange::getGroupData(token, grpData);
	
	if(ok)
	{
		std::vector<RsGxsGrpItem*>::iterator vit = grpData.begin();
		
		for(; vit != grpData.end(); ++vit)
		{
			RsGxsWireGroupItem* item = dynamic_cast<RsGxsWireGroupItem*>(*vit);

			if (item)
			{
				RsWireGroup group = item->group;
				group.mMeta = item->meta;
				delete item;
				groups.push_back(group);

				std::cerr << "p3Wire::getGroupData() Adding WireGroup to Vector: ";
				std::cerr << std::endl;
				std::cerr << group;
				std::cerr << std::endl;
			}
			else
			{
				std::cerr << "Not a WireGroupItem, deleting!" << std::endl;
				delete *vit;
			}

		}
	}
	return ok;
}

bool p3Wire::getGroupPtrData(const uint32_t &token, std::map<RsGxsGroupId, RsWireGroupSPtr> &groups)
{
	std::cerr << "p3Wire::getGroupPtrData()";
	std::cerr << std::endl;

	std::vector<RsGxsGrpItem*> grpData;
	bool ok = RsGenExchange::getGroupData(token, grpData);

	if(ok)
	{
		std::vector<RsGxsGrpItem*>::iterator vit = grpData.begin();

		for(; vit != grpData.end(); ++vit)
		{
			RsGxsWireGroupItem* item = dynamic_cast<RsGxsWireGroupItem*>(*vit);

			if (item)
			{
				RsWireGroupSPtr pGroup = std::make_shared<RsWireGroup>(item->group);
				pGroup->mMeta = item->meta;
				delete item;

				groups[pGroup->mMeta.mGroupId] = pGroup;

				std::cerr << "p3Wire::getGroupPtrData() Adding WireGroup to Vector: ";
				std::cerr << pGroup->mMeta.mGroupId.toStdString();
				std::cerr << std::endl;
			}
			else
			{
				std::cerr << "Not a WireGroupItem, deleting!" << std::endl;
				delete *vit;
			}

		}
	}
	return ok;
}


bool p3Wire::getPulseData(const uint32_t &token, std::vector<RsWirePulse> &pulses)
{
	GxsMsgDataMap msgData;
	bool ok = RsGenExchange::getMsgData(token, msgData);
	
	if(ok)
	{
		GxsMsgDataMap::iterator mit = msgData.begin();
		
		for(; mit != msgData.end(); ++mit)
		{
			std::vector<RsGxsMsgItem*>& msgItems = mit->second;
			std::vector<RsGxsMsgItem*>::iterator vit = msgItems.begin();
			
			for(; vit != msgItems.end(); ++vit)
			{
				RsGxsWirePulseItem* item = dynamic_cast<RsGxsWirePulseItem*>(*vit);
				
				if(item)
				{
					RsWirePulse pulse = item->pulse;
					pulse.mMeta = item->meta;
					pulses.push_back(pulse);
					delete item;
				}
				else
				{
					std::cerr << "Not a WirePulse Item, deleting!" << std::endl;
					delete *vit;
				}
			}
		}
	}
	return ok;
}

bool p3Wire::getPulsePtrData(const uint32_t &token, std::list<RsWirePulseSPtr> &pulses)
{
	GxsMsgDataMap msgData;
	bool ok = RsGenExchange::getMsgData(token, msgData);

	if(ok)
	{
		GxsMsgDataMap::iterator mit = msgData.begin();

		for(; mit != msgData.end(); ++mit)
		{
			std::vector<RsGxsMsgItem*>& msgItems = mit->second;
			std::vector<RsGxsMsgItem*>::iterator vit = msgItems.begin();

			for(; vit != msgItems.end(); ++vit)
			{
				RsGxsWirePulseItem* item = dynamic_cast<RsGxsWirePulseItem*>(*vit);

				if(item)
				{
					RsWirePulseSPtr pPulse = std::make_shared<RsWirePulse>(item->pulse);
					pPulse->mMeta = item->meta;
					pulses.push_back(pPulse);
					delete item;
				}
				else
				{
					std::cerr << "Not a WirePulse Item, deleting!" << std::endl;
					delete *vit;
				}
			}
		}
	}
	return ok;
}

bool p3Wire::getRelatedPulseData(const uint32_t &token, std::vector<RsWirePulse> &pulses)
{
	GxsMsgRelatedDataMap msgData;
	std::cerr << "p3Wire::getRelatedPulseData()";
	std::cerr << std::endl;
	bool ok = RsGenExchange::getMsgRelatedData(token, msgData);

	if (ok)
	{
		std::cerr << "p3Wire::getRelatedPulseData() is OK";
		std::cerr << std::endl;
		GxsMsgRelatedDataMap::iterator mit = msgData.begin();

		for(; mit != msgData.end(); ++mit)
		{
			std::vector<RsGxsMsgItem*>& msgItems = mit->second;
			std::vector<RsGxsMsgItem*>::iterator vit = msgItems.begin();

			for(; vit != msgItems.end(); ++vit)
			{
				RsGxsWirePulseItem* item = dynamic_cast<RsGxsWirePulseItem*>(*vit);

				if(item)
				{
					RsWirePulse pulse = item->pulse;
					pulse.mMeta = item->meta;
					pulses.push_back(pulse);
					delete item;
				}
				else
				{
					std::cerr << "Not a WirePulse Item, deleting!" << std::endl;
					delete *vit;
				}
			}
		}
	}
	else
	{
		std::cerr << "p3Wire::getRelatedPulseData() is NOT OK";
		std::cerr << std::endl;
	}

	return ok;
}


bool p3Wire::createGroup(uint32_t &token, RsWireGroup &group)
{
	RsGxsWireGroupItem* groupItem = new RsGxsWireGroupItem();
	groupItem->group = group;
	groupItem->meta = group.mMeta;

	std::cerr << "p3Wire::createGroup(): ";
	std::cerr << std::endl;
	std::cerr << group;
	std::cerr << std::endl;

	std::cerr << "p3Wire::createGroup() pushing to RsGenExchange";
	std::cerr << std::endl;

	RsGenExchange::publishGroup(token, groupItem);
	return true;
}

// Function which will edit the information in the wire group
bool p3Wire::editWire(RsWireGroup& wire)
{
    uint32_t token;
    if(!updateGroup(token, wire))
    {
        std::cerr << __PRETTY_FUNCTION__ << " Error! Failed updating group."
                  << std::endl;
        return false;
    }

    if(waitToken(token) != RsTokenService::COMPLETE)
    {
        std::cerr << __PRETTY_FUNCTION__ << " Error! GXS operation failed."
                  << std::endl;
        return false;
    }

    if(!RsGenExchange::getPublishedGroupMeta(token, wire.mMeta))
    {
        std::cerr << __PRETTY_FUNCTION__ << " Error! Failure getting updated "
                  << " group data." << std::endl;
        return false;
    }

    return true;
}

bool p3Wire::createPulse(uint32_t &token, RsWirePulse &pulse)
{
	std::cerr << "p3Wire::createPulse(): " << pulse;
	std::cerr << std::endl;

	RsGxsWirePulseItem* pulseItem = new RsGxsWirePulseItem();
	pulseItem->pulse = pulse;
	pulseItem->meta = pulse.mMeta;

	RsGenExchange::publishMsg(token, pulseItem);
	return true;
}

// Blocking Interfaces.
bool p3Wire::createGroup(RsWireGroup &group)
{
	uint32_t token;
	return createGroup(token, group) && waitToken(token) == RsTokenService::COMPLETE;
}

// Function which will update the edited information in the wire group
bool p3Wire::updateGroup(uint32_t &token, RsWireGroup & group)
{
#ifdef GXSWIRE_DEBUG
    std::cerr << "p3wire::updateGroup()" << std::endl;
#endif

    RsGxsWireGroupItem* grpItem = new RsGxsWireGroupItem();
    grpItem->fromWireGroup(group, true);

    RsGenExchange::updateGroup(token, grpItem);
    return true;
}

bool p3Wire::getGroups(const std::list<RsGxsGroupId> groupIds, std::vector<RsWireGroup> &groups)
{
	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_GROUP_DATA;

	if (groupIds.empty())
	{
        if (!requestGroupInfo(token, opts) || waitToken(token,std::chrono::milliseconds(5000)) != RsTokenService::COMPLETE )
		{
			return false;
		}
	}
	else
	{
		if (!requestGroupInfo(token, opts, groupIds) || waitToken(token) != RsTokenService::COMPLETE )
		{
			return false;
		}
	}
	return getGroupData(token, groups) && !groups.empty();
}


std::ostream &operator<<(std::ostream &out, const RsWireGroup &group)
{
	out << "RsWireGroup [ ";
	out << " Name: " << group.mMeta.mGroupName;
	out << " Tagline: " << group.mTagline;
	out << " Location: " << group.mLocation;
	out << " ]";
	return out;
}

std::ostream &operator<<(std::ostream &out, const RsWirePulse &pulse)
{
	out << "RsWirePulse [ ";
	out << "Title: " << pulse.mMeta.mMsgName;
	out << "PulseText: " << pulse.mPulseText;
	out << "]";
	return out;
}

/***** FOR TESTING *****/

std::string p3Wire::genRandomId()
{
	std::string randomId;
	for(int i = 0; i < 20; i++)
	{
		randomId += (char) ('a' + (RSRandom::random_u32() % 26));
	}

	return randomId;
}

void p3Wire::generateDummyData()
{

}


// New Interfaces.
bool p3Wire::fetchPulse(RsGxsGroupId grpId, RsGxsMessageId msgId, RsWirePulseSPtr &pPulse)
{
	std::cerr << "p3Wire::fetchPulse(" << grpId << ", " << msgId << ") waiting for token";
	std::cerr << std::endl;

	uint32_t token;
	{
		RsTokReqOptions opts;
		opts.mReqType = GXS_REQUEST_TYPE_MSG_DATA;
		opts.mOptions = RS_TOKREQOPT_MSG_LATEST;

		GxsMsgReq ids;
		std::set<RsGxsMessageId> &set_msgIds = ids[grpId];
		set_msgIds.insert(msgId);

		getTokenService()->requestMsgInfo(
			token, RS_TOKREQ_ANSTYPE_DATA, opts, ids);
	}

	// wait for pulse request to completed.
	std::cerr << "p3Wire::fetchPulse() waiting for token";
	std::cerr << std::endl;

	int result = waitToken(token);
	if (result != RsTokenService::COMPLETE)
	{
		std::cerr << "p3Wire::fetchPulse() token FAILED, result: " << result;
		std::cerr << std::endl;
		return false;
	}

	// retrieve Pulse.
	std::cerr << "p3Wire::fetchPulse() retrieving token";
	std::cerr << std::endl;
	{
		bool okay = true;
		std::vector<RsWirePulse> pulses;
		if (getPulseData(token, pulses)) {
			if (pulses.size() == 1) {
				// save to output pulse.
				pPulse = std::make_shared<RsWirePulse>(pulses[0]);
				std::cerr << "p3Wire::fetchPulse() retrieved token: " << *pPulse;
				std::cerr << std::endl;
				std::cerr << "p3Wire::fetchPulse() ANS GrpId: " << pPulse->mMeta.mGroupId;
				std::cerr << " MsgId: " << pPulse->mMeta.mMsgId;
				std::cerr << " OrigMsgId: " << pPulse->mMeta.mOrigMsgId;
				std::cerr << std::endl;
			} else {
				std::cerr << "p3Wire::fetchPulse() ERROR multiple pulses";
				std::cerr << std::endl;
				okay = false;
			}
		} else {
			std::cerr << "p3Wire::fetchPulse() ERROR failed to retrieve token";
			std::cerr << std::endl;
			okay = false;
		}

		if (!okay) {
			std::cerr << "p3Wire::fetchPulse() tokenPulse ERROR";
			std::cerr << std::endl;
			// TODO cancel other request.
			return false;
		}
	}
	return true;
}

// New Interfaces.
bool p3Wire::createOriginalPulse(const RsGxsGroupId &grpId, RsWirePulseSPtr pPulse)
{
	// request Group.
	std::list<RsGxsGroupId> groupIds = { grpId };
	std::vector<RsWireGroup> groups;
	bool groupOkay = getGroups(groupIds, groups);
	if (!groupOkay) {
		std::cerr << "p3Wire::createOriginalPulse() getGroups failed";
		std::cerr << std::endl;
		return false;
	}

	if (groups.size() != 1) {
		std::cerr << "p3Wire::createOriginalPulse() getGroups invalid size";
		std::cerr << std::endl;
		return false;
	}

	// ensure Group is suitable.
	RsWireGroup group = groups[0];
	if ((group.mMeta.mGroupId != grpId) ||
		(!(group.mMeta.mSubscribeFlags & GXS_SERV::GROUP_SUBSCRIBE_PUBLISH)))
	{
		std::cerr << "p3Wire::createOriginalPulse() Group unsuitable";
		std::cerr << std::endl;
		return false;
	}

	// Create Msg.
	// Start fresh, just to be sure nothing dodgy happens in UX world.
	RsWirePulse pulse;

	pulse.mMeta.mGroupId  = group.mMeta.mGroupId;
	pulse.mMeta.mAuthorId = group.mMeta.mAuthorId;
	pulse.mMeta.mThreadId.clear();
	pulse.mMeta.mParentId.clear();
	pulse.mMeta.mOrigMsgId.clear();

	// copy info over
	pulse.mPulseType = WIRE_PULSE_TYPE_ORIGINAL;
	pulse.mSentiment = pPulse->mSentiment;
	pulse.mPulseText = pPulse->mPulseText;
	pulse.mImage1 = pPulse->mImage1;
	pulse.mImage2 = pPulse->mImage2;
	pulse.mImage3 = pPulse->mImage3;
	pulse.mImage4 = pPulse->mImage4;

	// all mRefs should empty.

	uint32_t token;
	createPulse(token, pulse);

	int result = waitToken(token);
	if (result != RsTokenService::COMPLETE)
	{
		std::cerr << "p3Wire::createOriginalPulse() Failed to create Pulse";
		std::cerr << std::endl;
		return false;
	}

	return true;
}

bool p3Wire::createReplyPulse(RsGxsGroupId grpId, RsGxsMessageId msgId, RsGxsGroupId replyWith, uint32_t reply_type, RsWirePulseSPtr pPulse)
{
	// check reply_type. can only be ONE.
	if (!((reply_type == WIRE_PULSE_TYPE_REPLY) ||
		(reply_type == WIRE_PULSE_TYPE_REPUBLISH) ||
		(reply_type == WIRE_PULSE_TYPE_LIKE)))
	{
		std::cerr << "p3Wire::createReplyPulse() reply_type is invalid";
		std::cerr << std::endl;
		return false;
	}

	// request both groups.
	std::list<RsGxsGroupId> groupIds = { grpId, replyWith };
	std::vector<RsWireGroup> groups;
	bool groupOkay = getGroups(groupIds, groups);
	if (!groupOkay) {
		std::cerr << "p3Wire::createReplyPulse() getGroups failed";
		std::cerr << std::endl;
		return false;
	}

	// extract group info.
	RsWireGroup replyToGroup;
	RsWireGroup replyWithGroup;

	if (grpId == replyWith)
	{
		if (groups.size() != 1) {
			std::cerr << "p3Wire::createReplyPulse() getGroups != 1";
			std::cerr << std::endl;
			return false;
		}

		replyToGroup = groups[0];
		replyWithGroup = groups[0];
	}
	else
	{
		if (groups.size() != 2) {
			std::cerr << "p3Wire::createReplyPulse() getGroups != 2";
			std::cerr << std::endl;
			return false;
		}

		if (groups[0].mMeta.mGroupId == grpId) {
			replyToGroup = groups[0];
			replyWithGroup = groups[1];
		} else {
			replyToGroup = groups[1];
			replyWithGroup = groups[0];
		}
	}

	// check groupIds match
	if ((replyToGroup.mMeta.mGroupId != grpId) ||
		(replyWithGroup.mMeta.mGroupId != replyWith))
	{
		std::cerr << "p3Wire::createReplyPulse() groupid mismatch";
		std::cerr << std::endl;
		return false;
	}

	// ensure Group is suitable.
	if ((!(replyToGroup.mMeta.mSubscribeFlags & GXS_SERV::GROUP_SUBSCRIBE_SUBSCRIBED)) ||
		(!(replyWithGroup.mMeta.mSubscribeFlags & GXS_SERV::GROUP_SUBSCRIBE_PUBLISH)))
	{
		std::cerr << "p3Wire::createReplyPulse() Group unsuitable";
		std::cerr << std::endl;
		return false;
	}

	// **********************************************************
	RsWirePulseSPtr replyToPulse;
	if (!fetchPulse(grpId, msgId, replyToPulse))
	{
		std::cerr << "p3Wire::createReplyPulse() fetchPulse FAILED";
		std::cerr << std::endl;
		return false;
	}

	// create Reply Msg.
	RsWirePulse responsePulse;

	responsePulse.mMeta.mGroupId  = replyWithGroup.mMeta.mGroupId;
	responsePulse.mMeta.mAuthorId = replyWithGroup.mMeta.mAuthorId;
	responsePulse.mMeta.mThreadId.clear();
	responsePulse.mMeta.mParentId.clear();
	responsePulse.mMeta.mOrigMsgId.clear();

	responsePulse.mPulseType = WIRE_PULSE_TYPE_RESPONSE | reply_type;
	responsePulse.mSentiment = pPulse->mSentiment;
	responsePulse.mPulseText = pPulse->mPulseText;
	responsePulse.mImage1 = pPulse->mImage1;
	responsePulse.mImage2 = pPulse->mImage2;
	responsePulse.mImage3 = pPulse->mImage3;
	responsePulse.mImage4 = pPulse->mImage4;

	// mRefs refer to parent post.
	responsePulse.mRefGroupId   = replyToPulse->mMeta.mGroupId;
	responsePulse.mRefGroupName = replyToGroup.mMeta.mGroupName;
	responsePulse.mRefOrigMsgId = replyToPulse->mMeta.mOrigMsgId;
	responsePulse.mRefAuthorId  = replyToPulse->mMeta.mAuthorId;
	responsePulse.mRefPublishTs = replyToPulse->mMeta.mPublishTs;
	responsePulse.mRefPulseText = replyToPulse->mPulseText;
	responsePulse.mRefImageCount = replyToPulse->ImageCount();

	std::cerr << "p3Wire::createReplyPulse() create Response Pulse";
	std::cerr << std::endl;

	uint32_t token;
	if (!createPulse(token, responsePulse))
	{
		std::cerr << "p3Wire::createReplyPulse() FAILED to create Response Pulse";
		std::cerr << std::endl;
		return false;
	}

	int result = waitToken(token);
	if (result != RsTokenService::COMPLETE)
	{
		std::cerr << "p3Wire::createReplyPulse() FAILED(2) to create Response Pulse";
		std::cerr << std::endl;
		return false;
	}

	// get MsgId.
	std::pair<RsGxsGroupId, RsGxsMessageId> responsePair;
	if (!acknowledgeMsg(token, responsePair))
	{
		std::cerr << "p3Wire::createReplyPulse() FAILED acknowledgeMsg for Response Pulse";
		std::cerr << std::endl;
		return false;
	}

	std::cerr << "p3Wire::createReplyPulse() Response Pulse ID: (";
	std::cerr << responsePair.first.toStdString() << ", ";
	std::cerr << responsePair.second.toStdString() << ")";
	std::cerr << std::endl;

	// retrieve newly generated message.
	// **********************************************************
	RsWirePulseSPtr createdResponsePulse;
	if (!fetchPulse(responsePair.first, responsePair.second, createdResponsePulse))
	{
		std::cerr << "p3Wire::createReplyPulse() fetch createdReponsePulse FAILED";
		std::cerr << std::endl;
		return false;
	}

	/* Check that pulses is created properly */
	if ((createdResponsePulse->mMeta.mGroupId != responsePulse.mMeta.mGroupId) ||
	    (createdResponsePulse->mPulseText != responsePulse.mPulseText) ||
	    (createdResponsePulse->mRefGroupId != responsePulse.mRefGroupId) ||
	    (createdResponsePulse->mRefOrigMsgId != responsePulse.mRefOrigMsgId))
	{
		std::cerr << "p3Wire::createReplyPulse() fetch createdReponsePulse FAILED";
		std::cerr << std::endl;
		return false;
	}

	// create ReplyTo Ref Msg.
    std::cerr << "PulseAddDialog::postRefPulse() create Reference!";
	std::cerr << std::endl;

	// Reference Pulse. posted on Parent's Group.
    RsWirePulse refPulse;

    refPulse.mMeta.mGroupId  = replyToPulse->mMeta.mGroupId;
    refPulse.mMeta.mAuthorId = replyWithGroup.mMeta.mAuthorId; // own author Id.
    refPulse.mMeta.mThreadId = replyToPulse->mMeta.mOrigMsgId;
    refPulse.mMeta.mParentId = replyToPulse->mMeta.mOrigMsgId;
    refPulse.mMeta.mOrigMsgId.clear();

    refPulse.mPulseType = WIRE_PULSE_TYPE_REFERENCE | reply_type;
    refPulse.mSentiment = 0; // should this be =? createdResponsePulse->mSentiment;

    // Dont put parent PulseText into refPulse - it is available on Thread Msg.
    // otherwise gives impression it is correctly setup Parent / Reply...
    // when in fact the parent PublishTS, and AuthorId are wrong.
    refPulse.mPulseText = "";

    // refs refer back to own Post.
    refPulse.mRefGroupId   = replyWithGroup.mMeta.mGroupId;
    refPulse.mRefGroupName = replyWithGroup.mMeta.mGroupName;
    refPulse.mRefOrigMsgId = createdResponsePulse->mMeta.mOrigMsgId;
    refPulse.mRefAuthorId  = replyWithGroup.mMeta.mAuthorId;
    refPulse.mRefPublishTs = createdResponsePulse->mMeta.mPublishTs;
    refPulse.mRefPulseText = createdResponsePulse->mPulseText;
    refPulse.mRefImageCount = createdResponsePulse->ImageCount();

    // publish Ref Msg.
    if (!createPulse(token, refPulse))
    {
        std::cerr << "p3Wire::createReplyPulse() FAILED to create Ref Pulse";
        std::cerr << std::endl;
        return false;
    }

    result = waitToken(token);
    if (result != RsTokenService::COMPLETE)
    {
        std::cerr << "p3Wire::createReplyPulse() FAILED(2) to create Ref Pulse";
        std::cerr << std::endl;
        return false;
    }

    // get MsgId.
    std::pair<RsGxsGroupId, RsGxsMessageId> refPair;
    if (!acknowledgeMsg(token, refPair))
    {
        std::cerr << "p3Wire::createReplyPulse() FAILED acknowledgeMsg for Ref Pulse";
        std::cerr << std::endl;
        return false;
    }

    std::cerr << "p3Wire::createReplyPulse() Success: Ref Pulse ID: (";
    std::cerr << refPair.first.toStdString() << ", ";
    std::cerr << refPair.second.toStdString() << ")";
    std::cerr << std::endl;

	return true;
}


	// Blocking, request structures for display.
#if 0
bool p3Wire::createReplyPulse(uint32_t &token, RsWirePulse &pulse)
{

	return true;
}

bool p3Wire::createRepublishPulse(uint32_t &token, RsWirePulse &pulse)
{

	return true;
}

bool p3Wire::createLikePulse(uint32_t &token, RsWirePulse &pulse)
{

	return true;
}
#endif

	// WireGroup Details.
bool p3Wire::getWireGroup(const RsGxsGroupId &groupId, RsWireGroupSPtr &grp)
{
	std::list<RsGxsGroupId> groupIds = { groupId };
	std::map<RsGxsGroupId, RsWireGroupSPtr> groups;
	if (!fetchGroupPtrs(groupIds, groups))
	{
		std::cerr << "p3Wire::getWireGroup() failed to fetchGroupPtrs";
		std::cerr << std::endl;
		return false;
	}

	if (groups.size() != 1)
	{
		std::cerr << "p3Wire::getWireGroup() invalid group size";
		std::cerr << std::endl;
		return false;
	}

	grp = groups.begin()->second;

	// TODO Should fill in Counters of pulses/likes/republishes/replies
	return true;
}

// TODO Remove duplicate ...
bool p3Wire::getWirePulse(const RsGxsGroupId &groupId, const RsGxsMessageId &msgId, RsWirePulseSPtr &pPulse)
{
	return fetchPulse(groupId, msgId, pPulse);
}




bool compare_time(const RsWirePulseSPtr& first, const RsWirePulseSPtr &second)
{
	return first->mMeta.mPublishTs > second->mMeta.mPublishTs;
}

	// should this filter them in some way?
	// date, or count would be more likely.
bool p3Wire::getPulsesForGroups(const std::list<RsGxsGroupId> &groupIds, std::list<RsWirePulseSPtr> &pulsePtrs)
{
	// request all the pulses (Top-Level Thread Msgs).
	std::cerr << "p3Wire::getPulsesForGroups()";
	std::cerr << std::endl;

	uint32_t token;
	{
		RsTokReqOptions opts;
		opts.mReqType = GXS_REQUEST_TYPE_MSG_DATA;
		opts.mOptions = RS_TOKREQOPT_MSG_LATEST | RS_TOKREQOPT_MSG_THREAD;

		getTokenService()->requestMsgInfo(
			token, RS_TOKREQ_ANSTYPE_DATA, opts, groupIds);
	}

	// wait for pulse request to completed.
	std::cerr << "p3Wire::getPulsesForGroups() waiting for token";
	std::cerr << std::endl;

	int result = waitToken(token);
	if (result != RsTokenService::COMPLETE)
	{
		std::cerr << "p3Wire::getPulsesForGroups() token FAILED, result: " << result;
		std::cerr << std::endl;
		return false;
	}

	// retrieve Pulses.
	std::cerr << "p3Wire::getPulsesForGroups() retrieving token";
	std::cerr << std::endl;
	if (!getPulsePtrData(token, pulsePtrs))
	{
		std::cerr << "p3Wire::getPulsesForGroups() tokenPulse ERROR";
		std::cerr << std::endl;
		return false;
	}

	std::cerr << "p3Wire::getPulsesForGroups() size = " << pulsePtrs.size();
	std::cerr << std::endl;
	{
		std::list<RsWirePulseSPtr>::iterator it;
		for (it = pulsePtrs.begin(); it != pulsePtrs.end(); it++)
		{
			std::cerr << "p3Wire::getPulsesForGroups() Flags: ";
			std::cerr << (*it)->mPulseType << " Msg: " << (*it)->mPulseText;
			std::cerr << std::endl;
		}
	}

	std::cerr << "p3Wire::getPulsesForGroups() size = " << pulsePtrs.size();
	std::cerr << " sorting and trimming";
	std::cerr << std::endl;

	// sort and filter list.
	pulsePtrs.sort(compare_time);

	// trim to N max.
	uint32_t N = 10;
	if (pulsePtrs.size() > N) {
		pulsePtrs.resize(N);
	}

	// for each fill in details.
	std::list<RsWirePulseSPtr>::iterator it;
	for (it = pulsePtrs.begin(); it != pulsePtrs.end(); it++)
	{
		if (!updatePulse(*it, 1))
		{
			std::cerr << "p3Wire::getPulsesForGroups() Failed to updatePulse";
			std::cerr << std::endl;
			return false;
		}
	}

	// update GroupPtrs for all pulsePtrs.
	if (!updateGroups(pulsePtrs))
	{
		std::cerr << "p3Wire::getPulsesForGroups() failed to updateGroups";
		std::cerr << std::endl;
		return false;
	}

	return true;
}


bool p3Wire::getPulseFocus(const RsGxsGroupId &groupId, const RsGxsMessageId &msgId, int /* type */, RsWirePulseSPtr &pPulse)
{
	std::cerr << "p3Wire::getPulseFocus(";
	std::cerr << "grpId: " << groupId << " msgId: " << msgId;
	std::cerr << " )";
	std::cerr << std::endl;

	if (!fetchPulse(groupId, msgId, pPulse))
	{
		std::cerr << "p3Wire::getPulseFocus() failed to fetch Pulse";
		std::cerr << std::endl;
		return false;
	}

	if (!updatePulse(pPulse, 3))
	{
		std::cerr << "p3Wire::getPulseFocus() failed to update Pulse";
		std::cerr << std::endl;
		return false;
	}

	/* Fill in GroupPtrs */
	std::list<RsWirePulseSPtr> pulsePtrs;
	pulsePtrs.push_back(pPulse);

	if (!updateGroups(pulsePtrs))
	{
		std::cerr << "p3Wire::getPulseFocus() failed to updateGroups";
		std::cerr << std::endl;
		return false;
	}

	return true;
}


// function to update a pulse with the (Ref) child with actual data.
bool p3Wire::updatePulse(RsWirePulseSPtr pPulse, int levels)
{
	bool okay = true;

	// setup logging label.
	std::ostringstream out;
	out << "pulse[" << (void *) pPulse.get() << "], " << levels;
	std::string label = out.str();

	std::cerr << "p3Wire::updatePulse(" << label << ") starting";
	std::cerr << std::endl;

	// is pPulse is a REF, then request the original.
	// if no original available the done.
	if (pPulse->mPulseType & WIRE_PULSE_TYPE_REFERENCE)
	{
		RsWirePulseSPtr fullPulse;
		std::cerr << "p3Wire::updatePulse(" << label << ") fetching REF (";
		std::cerr << "grpId: " << pPulse->mRefGroupId << " msgId: " << pPulse->mRefOrigMsgId;
		std::cerr << " )";
		std::cerr << std::endl;
		if (!fetchPulse(pPulse->mRefGroupId, pPulse->mRefOrigMsgId, fullPulse))
		{
			std::cerr << "p3Wire::updatePulse(" << label << ") failed to fetch REF";
			std::cerr << std::endl;
			return false;
		}
		std::cerr << "p3Wire::updatePulse(" << label << ") replacing REF";
		std::cerr << std::endl;

		*pPulse = *fullPulse;
	}

	// Request children: (Likes / Retweets / Replies)
	std::cerr << "p3Wire::updatePulse(" << label << ") requesting children";
	std::cerr << std::endl;

	uint32_t token;
	{
		RsTokReqOptions opts;
		opts.mReqType = GXS_REQUEST_TYPE_MSG_RELATED_DATA;
		// OR opts.mOptions = RS_TOKREQOPT_MSG_LATEST | RS_TOKREQOPT_MSG_PARENT;
		opts.mOptions = RS_TOKREQOPT_MSG_LATEST | RS_TOKREQOPT_MSG_THREAD;

		std::vector<RsGxsGrpMsgIdPair> msgIds = { 
			std::make_pair(pPulse->mMeta.mGroupId, pPulse->mMeta.mOrigMsgId)
		};

		getTokenService()->requestMsgRelatedInfo(
			token, RS_TOKREQ_ANSTYPE_DATA, opts, msgIds);
	}

	// wait for request to complete
	std::cerr << "p3Wire::updatePulse(" << label << ") waiting for token";
	std::cerr << std::endl;

	int result = waitToken(token);
	if (result != RsTokenService::COMPLETE)
	{
		std::cerr << "p3Wire::updatePulse(" << label << ") token FAILED, result: " << result;
		std::cerr << std::endl;
		return false;
	}

	/* load children */
	okay = updatePulseChildren(pPulse, token);
	if (!okay)
	{
		std::cerr << "p3Wire::updatePulse(" << label << ") FAILED to update Children";
		std::cerr << std::endl;
		return false;
	}

	/* if down to last level, no need to updateChildren */
	if (levels <= 1)
	{
		std::cerr << "p3Wire::updatePulse(" << label << ") Level <= 1 finished";
		std::cerr << std::endl;
		return okay;
	}

	/* recursively update children */
	std::cerr << "p3Wire::updatePulse(" << label << ") updating children recursively";
	std::cerr << std::endl;
	std::list<RsWirePulseSPtr>::iterator it;
	for (it = pPulse->mReplies.begin(); it != pPulse->mReplies.end(); it++)
	{
		bool childOkay = updatePulse(*it, levels - 1);
		if (!childOkay) {
			std::cerr << "p3Wire::updatePulse(" << label << ") update children (reply) failed";
			std::cerr << std::endl;
		}
	}

	for (it = pPulse->mRepublishes.begin(); it != pPulse->mRepublishes.end(); it++)
	{
		bool childOkay = updatePulse(*it, levels - 1);
		if (!childOkay) {
			std::cerr << "p3Wire::updatePulse(" << label << ") update children (repub) failed";
			std::cerr << std::endl;
		}
	}

	return okay;
}


// function to update the (Ref) child with actual data.
bool p3Wire::updatePulseChildren(RsWirePulseSPtr pParent,  uint32_t token)
{
	{
		bool okay = true;
		std::vector<RsWirePulse> pulses;
		if (getRelatedPulseData(token, pulses)) {
			std::vector<RsWirePulse>::iterator it;
			for (it = pulses.begin(); it != pulses.end(); it++)
			{
				std::cerr << "p3Wire::updatePulseChildren() retrieved child: " << *it;
				std::cerr << std::endl;

				RsWirePulseSPtr pPulse = std::make_shared<RsWirePulse>(*it);
				// switch on type.
				if (it->mPulseType & WIRE_PULSE_TYPE_LIKE) {
					pParent->mLikes.push_back(pPulse);
					std::cerr << "p3Wire::updatePulseChildren() adding Like";
					std::cerr << std::endl;
				}
				else if (it->mPulseType & WIRE_PULSE_TYPE_REPUBLISH) {
					pParent->mRepublishes.push_back(pPulse);
					std::cerr << "p3Wire::updatePulseChildren() adding Republish";
					std::cerr << std::endl;
				}
				else if (it->mPulseType & WIRE_PULSE_TYPE_REPLY) {
					pParent->mReplies.push_back(pPulse);
					std::cerr << "p3Wire::updatePulseChildren() adding Reply";
					std::cerr << std::endl;
				}
				else {
					std::cerr << "p3Wire::updatePulseChildren() unknown child type: " << it->mPulseType;
					std::cerr << std::endl;
				}
			}
		} else {
			std::cerr << "p3Wire::updatePulseChildren() ERROR failed to retrieve token";
			std::cerr << std::endl;
			okay = false;
		}

		if (!okay) {
			std::cerr << "p3Wire::updatePulseChildren() token ERROR";
			std::cerr << std::endl;
		}
		return okay;
	}
}

/* High-level utility function to update mGroupPtr / mRefGroupPtr links.
 * fetches associated groups and reference them from pulses
 *
 * extractGroupIds (owner + refs).
 * fetch all available GroupIDs. (just IDs - so light).
 * do intersection of IDs.
 * apply IDs.
 */

bool p3Wire::updateGroups(std::list<RsWirePulseSPtr> &pulsePtrs)
{
	std::set<RsGxsGroupId> pulseGroupIds;

	std::list<RsWirePulseSPtr>::iterator it;
	for (it = pulsePtrs.begin(); it != pulsePtrs.end(); it++)
	{
		if (!extractGroupIds(*it, pulseGroupIds))
		{
			std::cerr << "p3Wire::updateGroups() failed to extractGroupIds";
			std::cerr << std::endl;
			return false;
		}
	}

	std::list<RsGxsGroupId> availGroupIds;
	if (!trimToAvailGroupIds(pulseGroupIds, availGroupIds))
	{
		std::cerr << "p3Wire::updateGroups() failed to trimToAvailGroupIds";
		std::cerr << std::endl;
		return false;
	}

	std::map<RsGxsGroupId, RsWireGroupSPtr> groups;
	if (!fetchGroupPtrs(availGroupIds, groups))
	{
		std::cerr << "p3Wire::updateGroups() failed to fetchGroupPtrs";
		std::cerr << std::endl;
		return false;
	}

	for (it = pulsePtrs.begin(); it != pulsePtrs.end(); it++)
	{
		if (!updateGroupPtrs(*it, groups))
		{
			std::cerr << "p3Wire::updateGroups() failed to updateGroupPtrs";
			std::cerr << std::endl;
			return false;
		}
	}
	return true;
}


// this function doesn't depend on p3Wire, could make static.
bool p3Wire::extractGroupIds(RsWirePulseConstSPtr pPulse, std::set<RsGxsGroupId> &groupIds)
{
	std::cerr << "p3Wire::extractGroupIds()";
	std::cerr << std::endl;

	if (!pPulse) {
		std::cerr << "p3Wire::extractGroupIds() INVALID pPulse";
		std::cerr << std::endl;
		return false;
	}

	// install own groupId.
	groupIds.insert(pPulse->mMeta.mGroupId);

	/* do this recursively */
	if (pPulse->mPulseType & WIRE_PULSE_TYPE_REFERENCE) {
		// REPLY: mRefGroupId, PARENT was in mMeta.mGroupId.
		groupIds.insert(pPulse->mRefGroupId);
		/* skipping */
		return true;
	}


	if (pPulse->mPulseType & WIRE_PULSE_TYPE_RESPONSE) {
		// REPLY: meta.mGroupId, PARENT: mRefGroupId
		groupIds.insert(pPulse->mRefGroupId);
	}

	/* iterate through children, recursively */
	std::list<RsWirePulseSPtr>::const_iterator it;
	for (it = pPulse->mReplies.begin(); it != pPulse->mReplies.end(); it++)
	{
		bool childOkay = extractGroupIds(*it, groupIds);
		if (!childOkay) {
			std::cerr << "p3Wire::extractGroupIds() update children (reply) failed";
			std::cerr << std::endl;
			return false;
		}
	}

	for (it = pPulse->mRepublishes.begin(); it != pPulse->mRepublishes.end(); it++)
	{
		bool childOkay = extractGroupIds(*it, groupIds);
		if (!childOkay) {
			std::cerr << "p3Wire::extractGroupIds() update children (repub) failed";
			std::cerr << std::endl;
			return false;
		}
	}

	// not bothering with LIKEs at the moment. TODO.
	return true;
}

bool p3Wire::updateGroupPtrs(RsWirePulseSPtr pPulse, const std::map<RsGxsGroupId, RsWireGroupSPtr> &groups)
{
	std::map<RsGxsGroupId, RsWireGroupSPtr>::const_iterator git;
	git = groups.find(pPulse->mMeta.mGroupId);
	if (git == groups.end()) {
		// error
		return false;
	}

	pPulse->mGroupPtr = git->second;

	// if REF, fill in mRefGroupPtr based on mRefGroupId.
	if (pPulse->mPulseType & WIRE_PULSE_TYPE_REFERENCE) {
		// if RefGroupId is in list, fill in. No error if its not there.
		std::map<RsGxsGroupId, RsWireGroupSPtr>::const_iterator rgit;
		rgit = groups.find(pPulse->mRefGroupId);
		if (rgit != groups.end()) {
			pPulse->mRefGroupPtr = rgit->second;
		}

		// no children for REF pulse, so can return now.
		return true;
	}

	// if Response, fill in mRefGroupPtr based on mRefGroupId.
	if (pPulse->mPulseType & WIRE_PULSE_TYPE_RESPONSE) {
		// if RefGroupId is in list, fill in. No error if its not there.
		std::map<RsGxsGroupId, RsWireGroupSPtr>::const_iterator rgit;
		rgit = groups.find(pPulse->mRefGroupId);
		if (rgit != groups.end()) {
			pPulse->mRefGroupPtr = rgit->second;
		}
		// do children as well.
	}

	/* recursively apply to children */
	std::list<RsWirePulseSPtr>::iterator it;
	for (it = pPulse->mReplies.begin(); it != pPulse->mReplies.end(); it++)
	{
		bool childOkay = updateGroupPtrs(*it, groups);
		if (!childOkay) {
			std::cerr << "p3Wire::updateGroupPtrs() update children (reply) failed";
			std::cerr << std::endl;
			return false;
		}
	}

	for (it = pPulse->mRepublishes.begin(); it != pPulse->mRepublishes.end(); it++)
	{
		bool childOkay = updateGroupPtrs(*it, groups);
		if (!childOkay) {
			std::cerr << "p3Wire::updateGroupPtrs() update children (repub) failed";
			std::cerr << std::endl;
			return false;
		}
	}

	// not bothering with LIKEs at the moment. TODO.
	return true;
}

bool p3Wire::trimToAvailGroupIds(const std::set<RsGxsGroupId> &pulseGroupIds,
	std::list<RsGxsGroupId> &availGroupIds)
{
	/* request all groupIds */
	std::cerr << "p3Wire::trimToAvailGroupIds()";
	std::cerr << std::endl;

	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_GROUP_IDS;

	if (!requestGroupInfo(token, opts) || waitToken(token) != RsTokenService::COMPLETE )
	{
		std::cerr << "p3Wire::trimToAvailGroupIds() failed to fetch groups";
		std::cerr << std::endl;
		return false;
	}

	std::list<RsGxsGroupId> localGroupIds;
	if (!RsGenExchange::getGroupList(token, localGroupIds))
	{
		std::cerr << "p3Wire::trimToAvailGroupIds() failed to get GroupIds";
		std::cerr << std::endl;
		return false;
	}

	/* do intersection between result ^ pulseGroups -> availGroupIds */
	std::set_intersection(localGroupIds.begin(), localGroupIds.end(),
		pulseGroupIds.begin(), pulseGroupIds.end(),
		std::back_inserter(availGroupIds));

	return true;
}

bool p3Wire::fetchGroupPtrs(const std::list<RsGxsGroupId> &groupIds,
	std::map<RsGxsGroupId, RsWireGroupSPtr> &groups)
{
	std::cerr << "p3Wire::fetchGroupPtrs()";
	std::cerr << std::endl;

	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_GROUP_DATA;

	if (!requestGroupInfo(token, opts, groupIds) || waitToken(token) != RsTokenService::COMPLETE )
	{
		std::cerr << "p3Wire::fetchGroupPtrs() failed to fetch groups";
		std::cerr << std::endl;
		return false;
	}
	return getGroupPtrData(token, groups);
}

bool p3Wire::getWireGroupStatistics(const RsGxsGroupId& groupId,GxsGroupStatistic& stat)
{
    uint32_t token;
    if(!RsGxsIfaceHelper::requestGroupStatistic(token, groupId) || waitToken(token) != RsTokenService::COMPLETE)
        return false;

    return RsGenExchange::getGroupStatistic(token,stat);
}

bool p3Wire::getContentSummaries(
        const RsGxsGroupId& groupId, std::vector<RsMsgMetaData>& summaries )
{
    uint32_t token;
    RsTokReqOptions opts;
    opts.mReqType = GXS_REQUEST_TYPE_MSG_META;

    std::list<RsGxsGroupId> groupIds;
    groupIds.push_back(groupId);

    if( !requestMsgInfo(token, opts, groupIds) ||
            waitToken(token, std::chrono::seconds(5)) != RsTokenService::COMPLETE )
        return false;

    GxsMsgMetaMap metaMap;
    bool res = RsGenExchange::getMsgMeta(token, metaMap);
    summaries = metaMap[groupId];

    return res;
}


template<class T> void sortPostMetas(std::vector<T>& pulses,
                                     const std::function< RsMsgMetaData& (T&) > get_meta,
                                     std::map<RsGxsMessageId,std::pair<uint32_t,std::set<RsGxsMessageId> > >& original_versions)
{
    // The hierarchy of pulses may contain edited pulses. In the new model (03/2023), mOrigMsgId points to the original
    // top-level post in the hierarchy of edited pulses. However, in the old model, mOrigMsgId points to the edited post.
    // Therefore the algorithm below is made to cope with both models at once.
    //
    // In the future, using the new model, it will be possible to delete old versions from the db, and detect new versions
    // because they all share the same mOrigMsgId.
    //
    // We proceed as follows:
    //
    //	1 - create a search map to convert post IDs into their index in the pulses tab
    //  2 - recursively climb up the post mOrigMsgId until no parent is found. At top level, create the original post, and add all previous elements as newer versions.
    //	3 - go through the list of original pulses, select among them the most recent version, and set all others as older versions.
    //
    // The algorithm handles the case where some parent has been deleted.

    // Output: original_version is a map containing for each most ancient parent, the index of the most recent version in pulses array,
    //         and the set of all versions of that oldest post.

#ifdef DEBUG_WIRE_MODEL
    std::cerr << "Inserting wire pulses" << std::endl;
#endif

    //	1 - create a search map to convert post IDs into their index in the pulses tab

#ifdef DEBUG_WIRE_MODEL
    std::cerr << "  Given list: " << std::endl;
#endif
    std::map<RsGxsMessageId,uint32_t> search_map ;

    for (uint32_t i=0;i<pulses.size();++i)
    {
#ifdef DEBUG_WIRE_MODEL
        std::cerr << "    " << i << ": " << get_meta(pulses[i]).mMsgId << " orig=" << get_meta(pulses[i]).mOrigMsgId << " publish TS =" << get_meta(pulses[i]).mPublishTs << std::endl;
#endif
        search_map[get_meta(pulses[i]).mMsgId] = i ;
    }

    //  2 - recursively climb up the post mOrigMsgId until no parent is found. At top level, create the original post, and add all previous elements as newer versions.

#ifdef DEBUG_WIRE_MODEL
    std::cerr << "  Searching for top-level pulses..." << std::endl;
#endif

    for (uint32_t i=0;i<pulses.size();++i)
    {
#ifdef DEBUG_WIRE_MODEL
        std::cerr << "    Post " << i;
#endif

        // We use a recursive function here, so as to collect versions when climbing up to the top level post, and
        // set the top level as the orig for all visited pulses on the way back.

        std::function<RsGxsMessageId (uint32_t,std::set<RsGxsMessageId>& versions,rstime_t newest_time,uint32_t newest_index,int depth)> recurs_find_top_level
                = [&pulses,&search_map,&recurs_find_top_level,&original_versions,&get_meta](uint32_t index,
                                                                                std::set<RsGxsMessageId>& collected_versions,
                                                                                rstime_t newest_time,
                                                                                uint32_t newest_index,
                                                                                int depth)
                -> RsGxsMessageId
        {
            const auto& m(get_meta(pulses[index]));

            if(m.mPublishTs > newest_time)
            {
                newest_index = index;
                newest_time = m.mPublishTs;
            }
            collected_versions.insert(m.mMsgId);

            RsGxsMessageId top_level_id;
            std::map<RsGxsMessageId,uint32_t>::const_iterator it;

            if(m.mOrigMsgId.isNull() || m.mOrigMsgId==m.mMsgId)	// we have a top-level post.
                top_level_id = m.mMsgId;
            else if( (it = search_map.find(m.mOrigMsgId)) == search_map.end())	// we don't have the post. Never mind, we store the
            {
                top_level_id = m.mOrigMsgId;
                collected_versions.insert(m.mOrigMsgId);	// this one will never be added to the set by the previous call
            }
            else
            {
                top_level_id = recurs_find_top_level(it->second,collected_versions,newest_time,newest_index,depth+1);
                get_meta(pulses[index]).mOrigMsgId = top_level_id;	// this fastens calculation because it will skip already seen pulses.

                return top_level_id;
            }

#ifdef DEBUG_WIRE_MODEL
            std::cerr << std::string(2*depth,' ') << "  top level = " << top_level_id ;
#endif
            auto vit = original_versions.find(top_level_id);

            if(vit != original_versions.end())
            {
                if(get_meta(pulses[vit->second.first]).mPublishTs < newest_time)
                    vit->second.first = newest_index;

#ifdef DEBUG_WIRE_MODEL
                std::cerr << "  already existing. " << std::endl;
#endif
            }
            else
            {
                original_versions[top_level_id].first = newest_index;
#ifdef DEBUG_WIRE_MODEL
                std::cerr << "  new. " << std::endl;
#endif
            }
            original_versions[top_level_id].second.insert(collected_versions.begin(),collected_versions.end());

            return top_level_id;
        };

        auto versions_set = std::set<RsGxsMessageId>();
        recurs_find_top_level(i,versions_set,get_meta(pulses[i]).mPublishTs,i,0);
    }
}


bool p3Wire::getWireStatistics(const RsGxsGroupId& groupId, RsWireStatistics& stat)
{
    std::vector<RsMsgMetaData> metas;

    if(!getContentSummaries(groupId,metas))
        return false;

    std::vector<RsMsgMetaData> post_metas;

    stat.mNumberOfRepliesAndLikes = 0;
    stat.mNumberOfPulses = 0;
    stat.mNumberOfNewPulses = 0;
    stat.mNumberOfUnreadPulses = 0;

    for(uint32_t i=0;i<metas.size();++i)
        if(metas[i].mThreadId.isNull() && metas[i].mParentId.isNull())	// make sure we have a pulse and not a reply or like
            post_metas.push_back(metas[i]);
        else
            ++stat.mNumberOfRepliesAndLikes;

        // now, remove old pulses,

    std::vector<RsWirePulse> mPulses;
    std::function< RsMsgMetaData& (RsMsgMetaData&) > get_meta = [](RsMsgMetaData& p)->RsMsgMetaData& { return p; };
    std::map<RsGxsMessageId,std::pair<uint32_t,std::set<RsGxsMessageId> > > original_versions;

    sortPostMetas(post_metas, get_meta, original_versions);

    for(const auto& ov_entry:original_versions)
    {
        auto& m( post_metas[ov_entry.second.first] );

        ++stat.mNumberOfPulses;

        if(m.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_NEW)
            ++stat.mNumberOfNewPulses;

        if(m.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD)
            ++stat.mNumberOfUnreadPulses;
    }

    return true;
}

/********************************************************************************************/
/********************************************************************************************/

void p3Wire::setMessageReadStatus(uint32_t &token, const RsGxsGrpMsgIdPair &msgId, bool read)
{
#ifdef GXSWIRE_DEBUG
    std::cerr << "p3Wire::setMessageReadStatus()";
    std::cerr << std::endl;
#endif

    /* Always remove status unprocessed */
    uint32_t mask = GXS_SERV::GXS_MSG_STATUS_GUI_NEW | GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD;
    uint32_t status = GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD;
    if (read) status = 0;

    setMsgStatusFlags(token, msgId, status, mask);

    if (rsEvents)
    {
        auto ev = std::make_shared<RsWireEvent>();

        ev->mWireMsgId = msgId.second;
        ev->mWireGroupId = msgId.first;
        ev->mWireEventCode = RsWireEventCode::READ_STATUS_CHANGED;
        rsEvents->postEvent(ev);
    }
}

/********************************************************************************************/
/********************************************************************************************/
