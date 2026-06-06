/*******************************************************************************
 * libretroshare/src/retroshare: rsgxscalendar.h                               *
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
#pragma once

#include <string>
#include <list>
#include <vector>

#include "retroshare/rstokenservice.h"
#include "retroshare/rsgxsifacehelper.h"
#include "retroshare/rsgxscommon.h"
#include "serialiser/rsserializable.h"
#include "util/rsmemory.h"

class RsGxsCalendar;

extern RsGxsCalendar* rsGxsCalendar;

struct RsGxsCalendarGroup : RsSerializable, RsGxsGenericGroupData
{
	RsGxsCalendarGroup() {}

	std::string mDescription;

	/// @see RsSerializable
	virtual void serial_process(
	        RsGenericSerializer::SerializeJob j,
	        RsGenericSerializer::SerializeContext& ctx ) override
	{
		RS_SERIAL_PROCESS(mMeta);
		RS_SERIAL_PROCESS(mDescription);
	}

	~RsGxsCalendarGroup() override = default;
};

struct RsGxsCalendarMessage : RsSerializable, RsGxsGenericMsgData
{
	RsGxsCalendarMessage() {}

	std::string mIcsData;

	/// @see RsSerializable
	virtual void serial_process(
	        RsGenericSerializer::SerializeJob j,
	        RsGenericSerializer::SerializeContext& ctx ) override
	{
		RS_SERIAL_PROCESS(mMeta);
		RS_SERIAL_PROCESS(mIcsData);
	}

	~RsGxsCalendarMessage() override = default;
};

enum class RsCalendarEventCode: uint8_t
{
	UNKNOWN                         = 0x00,
	NEW_CALENDAR                    = 0x01, // emitted when new calendar is received
	UPDATED_CALENDAR                = 0x02, // emitted when existing calendar is updated
	NEW_EVENT                       = 0x03, // new event/task received (as a message payload in the calendar group)
	UPDATED_EVENT                   = 0x04, // event/task updated
	SUBSCRIBE_STATUS_CHANGED        = 0x05, // subscription status changed
};

struct RsGxsCalendarEvent: RsEvent
{
	RsGxsCalendarEvent(RsEventType type): RsEvent(type), mCalendarEventCode(RsCalendarEventCode::UNKNOWN) {}

	RsCalendarEventCode mCalendarEventCode;
	RsGxsGroupId mCalendarGroupId;
	RsGxsMessageId mCalendarMsgId;

	///* @see RsEvent @see RsSerializable
	void serial_process( RsGenericSerializer::SerializeJob j,RsGenericSerializer::SerializeContext& ctx) override
	{
		RsEvent::serial_process(j, ctx);

		RS_SERIAL_PROCESS(mCalendarEventCode);
		RS_SERIAL_PROCESS(mCalendarGroupId);
		RS_SERIAL_PROCESS(mCalendarMsgId);
	}
};

class RsGxsCalendar: public RsGxsIfaceHelper
{
public:
	explicit RsGxsCalendar(RsGxsIface& gxs) : RsGxsIfaceHelper(gxs) {}
	virtual ~RsGxsCalendar() = default;

	virtual bool createCalendar(
	        const std::string& name,
	        const std::string& description,
	        const RsGxsId&     authorId = RsGxsId(),
	        RsGxsGroupId& calendarId = RS_DEFAULT_STORAGE_PARAM(RsGxsGroupId),
	        std::string&  errorMessage = RS_DEFAULT_STORAGE_PARAM(std::string)
	        ) = 0;

	virtual bool updateCalendar(
	        const RsGxsGroupId& calendarId,
	        const std::string& name,
	        const std::string& description,
	        const RsGxsId&     authorId = RsGxsId(),
	        std::string&  errorMessage = RS_DEFAULT_STORAGE_PARAM(std::string)
	        ) = 0;

	virtual bool subscribeToCalendar(
	        const RsGxsGroupId& calendarId,
	        bool subscribe,
	        std::string& errorMessage = RS_DEFAULT_STORAGE_PARAM(std::string)
	        ) = 0;

	virtual bool publishCalendarIcs(
	        const RsGxsGroupId& calendarId,
	        const std::string& icsData,
	        const RsGxsId& authorId = RsGxsId(),
	        RsGxsMessageId& msgId = RS_DEFAULT_STORAGE_PARAM(RsGxsMessageId),
	        std::string& errorMessage = RS_DEFAULT_STORAGE_PARAM(std::string)
	        ) = 0;

	virtual bool getCalendarsSummaries(std::list<RsGroupMetaData>& calendars) = 0;

	virtual bool getCalendarContent(
	        const RsGxsGroupId& calendarId,
	        std::vector<RsGxsCalendarMessage>& messages
	        ) = 0;
};
