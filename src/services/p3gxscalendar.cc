/*******************************************************************************
 * libretroshare/src/services: p3gxscalendar.cc                                *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright (c) 2026 RetroShare Team                                          *
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
#include "services/p3gxscalendar.h"
#include "rsitems/rsgxscalendaritems.h"
#include "retroshare/rsgxscircles.h"
#include "util/rsdebug.h"

RsGxsCalendar* rsGxsCalendar = nullptr;

p3GxsCalendar::p3GxsCalendar(RsGeneralDataService* gds, RsNetworkExchangeService* nes, RsGixs* gixs) :
	RsGenExchange(gds, nes, new RsGxsCalendarSerialiser(), RS_SERVICE_GXS_TYPE_CALENDAR, gixs, calendarAuthenPolicy()),
	RsGxsCalendar(static_cast<RsGxsIface&>(*this)), GxsTokenQueue(this)
{
}

uint32_t p3GxsCalendar::calendarAuthenPolicy()
{
	uint32_t policy = 0;
	uint32_t flag = GXS_SERV::MSG_AUTHEN_ROOT_PUBLISH_SIGN | GXS_SERV::MSG_AUTHEN_CHILD_AUTHOR_SIGN;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::PUBLIC_GRP_BITS);

	flag |= GXS_SERV::MSG_AUTHEN_CHILD_PUBLISH_SIGN;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::RESTRICTED_GRP_BITS);
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::PRIVATE_GRP_BITS);

	flag = 0;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::GRP_OPTION_BITS);

	return policy;
}

RsServiceInfo p3GxsCalendar::getServiceInfo()
{
	return RsServiceInfo(RS_SERVICE_GXS_TYPE_CALENDAR,
	                     "gxscalendar",
	                     1, 0, // major, minor version
	                     1, 0); // min major, minor version
}

void p3GxsCalendar::service_tick()
{
	GxsTokenQueue::checkRequests();
}

void p3GxsCalendar::handleResponse(uint32_t /*token*/, uint32_t /*req_type*/, RsTokenService::GxsRequestStatus /*status*/)
{
}

bool p3GxsCalendar::createCalendar(
        const std::string& name,
        const std::string& description,
        const RsGxsId&     authorId,
        uint32_t           circleType,
        const RsGxsCircleId& circleId,
        const RsGxsCircleId& internalCircle,
        uint32_t           groupFlags,
        RsGxsGroupId& calendarId,
        std::string&  errorMessage
        )
{
	RsGxsCalendarGroup group;
	group.mMeta.mGroupName = name;
	group.mMeta.mAuthorId = authorId;
	group.mMeta.mCircleType = circleType;
	group.mMeta.mCircleId = circleId;
	group.mMeta.mInternalCircle = internalCircle;
	group.mMeta.mSignFlags = GXS_SERV::FLAG_GROUP_SIGN_PUBLISH_NONEREQ | GXS_SERV::FLAG_AUTHOR_AUTHENTICATION_REQUIRED;
	group.mMeta.mGroupFlags = groupFlags;
	group.mDescription = description;

	RsGxsCalendarGroupItem* grpItem = new RsGxsCalendarGroupItem();
	grpItem->fromCalendarGroup(group);

	uint32_t token;
	RsGenExchange::publishGroup(token, grpItem);

	if (waitToken(token) != RsTokenService::COMPLETE)
	{
		errorMessage = "GXS token execution failed.";
		return false;
	}

	if (!RsGenExchange::getPublishedGroupMeta(token, group.mMeta))
	{
		errorMessage = "Failed to retrieve published calendar ID.";
		return false;
	}

	calendarId = group.mMeta.mGroupId;
	return true;
}

bool p3GxsCalendar::updateCalendar(
        const RsGxsGroupId& calendarId,
        const std::string& name,
        const std::string& description,
        const RsGxsId&     authorId,
        uint32_t           circleType,
        const RsGxsCircleId& circleId,
        const RsGxsCircleId& internalCircle,
        uint32_t           groupFlags,
        std::string&  errorMessage
        )
{
	RsGxsCalendarGroup group;
	group.mMeta.mGroupId = calendarId;
	group.mMeta.mGroupName = name;
	group.mMeta.mAuthorId = authorId;
	group.mMeta.mCircleType = circleType;
	group.mMeta.mCircleId = circleId;
	group.mMeta.mInternalCircle = internalCircle;
	group.mMeta.mSignFlags = GXS_SERV::FLAG_GROUP_SIGN_PUBLISH_NONEREQ | GXS_SERV::FLAG_AUTHOR_AUTHENTICATION_REQUIRED;
	group.mMeta.mGroupFlags = groupFlags;
	group.mDescription = description;

	RsGxsCalendarGroupItem* grpItem = new RsGxsCalendarGroupItem();
	grpItem->fromCalendarGroup(group);

	uint32_t token;
	RsGenExchange::updateGroup(token, grpItem);

	if (waitToken(token) != RsTokenService::COMPLETE)
	{
		errorMessage = "GXS token execution failed.";
		return false;
	}

	return true;
}

bool p3GxsCalendar::subscribeToCalendar(
        const RsGxsGroupId& calendarId,
        bool subscribe,
        std::string& errorMessage
        )
{
	uint32_t token;
	if (!RsGxsIfaceHelper::subscribeToGroup(token, calendarId, subscribe))
	{
		errorMessage = "Failed to queue subscription request.";
		return false;
	}

	if (waitToken(token) != RsTokenService::COMPLETE)
	{
		errorMessage = "GXS token execution failed.";
		return false;
	}

	return true;
}

bool p3GxsCalendar::publishCalendarIcs(
        const RsGxsGroupId& calendarId,
        const std::string& icsData,
        const RsGxsId& authorId,
        RsGxsMessageId& msgId,
        std::string& errorMessage
        )
{
	RsGxsCalendarMessage msg;
	msg.mMeta.mGroupId = calendarId;
	msg.mMeta.mAuthorId = authorId;
	msg.mMeta.mParentId.clear();
	msg.mMeta.mOrigMsgId.clear();
	msg.mIcsData = icsData;

	RsGxsCalendarMessageItem* msgItem = new RsGxsCalendarMessageItem();
	msgItem->fromCalendarMsg(msg);

	uint32_t token;
	publishMsg(token, msgItem);

	if (waitToken(token) != RsTokenService::COMPLETE)
	{
		errorMessage = "GXS token execution failed.";
		return false;
	}

	std::pair<RsGxsGroupId, RsGxsMessageId> grpMsgId;
	if (!RsGenExchange::acknowledgeTokenMsg(token, grpMsgId))
	{
		errorMessage = "Failed to acknowledge published message.";
		return false;
	}

	msgId = grpMsgId.second;
	return true;
}

bool p3GxsCalendar::getCalendarsSummaries(std::list<RsGroupMetaData>& calendars)
{
	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_GROUP_META;
	if( !requestGroupInfo(token, opts) || waitToken(token) != RsTokenService::COMPLETE )
		return false;
	return getGroupSummary(token, calendars);
}

bool p3GxsCalendar::getCalendarContent(
        const RsGxsGroupId& calendarId,
        std::vector<RsGxsCalendarMessage>& messages
        )
{
	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_MSG_DATA;

	if( !requestMsgInfo(token, opts, std::list<RsGxsGroupId>({calendarId})) || waitToken(token) != RsTokenService::COMPLETE )
		return false;

	GxsMsgDataMap msgData;
	if(!RsGenExchange::getMsgData(token, msgData))
	{
		return false;
	}

	for(auto& pair : msgData)
	{
		for(auto* item : pair.second)
		{
			auto* msgItem = dynamic_cast<RsGxsCalendarMessageItem*>(item);
			if(msgItem)
			{
				RsGxsCalendarMessage msg;
				msgItem->toCalendarMsg(msg);
				messages.push_back(msg);
				delete msgItem;
			}
			else
			{
				delete item;
			}
		}
	}
	return true;
}

bool p3GxsCalendar::service_checkIfGroupIsStillUsed(const RsGxsGrpMetaData& meta)
{
	return static_cast<bool>(meta.mSubscribeFlags & GXS_SERV::GROUP_SUBSCRIBE_SUBSCRIBED);
}

void p3GxsCalendar::notifyChanges(std::vector<RsGxsNotify*>& changes)
{
	if (!rsEvents) return;
	RsEventType calendarEventType = (RsEventType)rsEvents->getDynamicEventType("GXS_CALENDAR");

	for (auto* change : changes)
	{
		auto* msgChange = dynamic_cast<RsGxsMsgChange*>(change);
		if (msgChange)
		{
			if (msgChange->getType() == RsGxsNotify::TYPE_RECEIVED_NEW || msgChange->getType() == RsGxsNotify::TYPE_PUBLISHED)
			{
				auto ev = std::make_shared<RsGxsCalendarEvent>(calendarEventType);
				ev->mCalendarGroupId = msgChange->mGroupId;
				ev->mCalendarMsgId = msgChange->mMsgId;
				ev->mCalendarEventCode = RsCalendarEventCode::NEW_EVENT;
				rsEvents->postEvent(ev);
			}
		}

		auto* grpChange = dynamic_cast<RsGxsGroupChange*>(change);
		if (grpChange)
		{
			auto ev = std::make_shared<RsGxsCalendarEvent>(calendarEventType);
			ev->mCalendarGroupId = grpChange->mGroupId;
			switch (grpChange->getType())
			{
			case RsGxsNotify::TYPE_PROCESSED:
				ev->mCalendarEventCode = RsCalendarEventCode::SUBSCRIBE_STATUS_CHANGED;
				break;
			case RsGxsNotify::TYPE_UPDATED:
				ev->mCalendarEventCode = RsCalendarEventCode::UPDATED_CALENDAR;
				break;
			case RsGxsNotify::TYPE_PUBLISHED:
			case RsGxsNotify::TYPE_RECEIVED_NEW:
				ev->mCalendarEventCode = RsCalendarEventCode::NEW_CALENDAR;
				break;
			default:
				continue;
			}
			rsEvents->postEvent(ev);
		}
	}
}

RsSerialiser* p3GxsCalendar::setupSerialiser()
{
	return nullptr;
}

bool p3GxsCalendar::saveList(bool &cleanup, std::list<RsItem *>& /*saveList*/)
{
	cleanup = false;
	return true;
}

bool p3GxsCalendar::loadList(std::list<RsItem *>& /*loadList*/)
{
	return true;
}
