/*******************************************************************************
 * libretroshare/src/rsitems: rsgxscalendaritems.h                             *
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
#ifndef RS_GXS_CALENDAR_ITEMS_H
#define RS_GXS_CALENDAR_ITEMS_H

#include "rsitems/rsserviceids.h"
#include "rsitems/rsgxscommentitems.h"
#include "rsitems/rsgxsitems.h"
#include "retroshare/rsgxscalendar.h"
#include "serialiser/rsserializer.h"

const uint8_t RS_PKT_SUBTYPE_GXSCALENDAR_GROUP_ITEM = 0x01;
const uint8_t RS_PKT_SUBTYPE_GXSCALENDAR_MSG_ITEM   = 0x02;

class RsGxsCalendarGroupItem : public RsGxsGrpItem
{
public:
	RsGxsCalendarGroupItem(): RsGxsGrpItem(RS_SERVICE_GXS_TYPE_CALENDAR, RS_PKT_SUBTYPE_GXSCALENDAR_GROUP_ITEM) {}
	virtual ~RsGxsCalendarGroupItem() override = default;

	void clear();

	virtual void serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext& ctx) override;

	bool fromCalendarGroup(RsGxsCalendarGroup &group);
	bool toCalendarGroup(RsGxsCalendarGroup &group);

	std::string mDescription;
};

class RsGxsCalendarMessageItem : public RsGxsMsgItem
{
public:
	RsGxsCalendarMessageItem(): RsGxsMsgItem(RS_SERVICE_GXS_TYPE_CALENDAR, RS_PKT_SUBTYPE_GXSCALENDAR_MSG_ITEM) {}
	virtual ~RsGxsCalendarMessageItem() override = default;

	void clear();

	virtual void serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext& ctx) override;

	bool fromCalendarMsg(RsGxsCalendarMessage &msg);
	bool toCalendarMsg(RsGxsCalendarMessage &msg);

	std::string mIcsData;
};

class RsGxsCalendarSerialiser : public RsGxsCommentSerialiser
{
public:
	RsGxsCalendarSerialiser() : RsGxsCommentSerialiser(RS_SERVICE_GXS_TYPE_CALENDAR) {}
	virtual ~RsGxsCalendarSerialiser() override = default;

	virtual RsItem *create_item(uint16_t service_id, uint8_t item_subtype) const override;
};

#endif /* RS_GXS_CALENDAR_ITEMS_H */
