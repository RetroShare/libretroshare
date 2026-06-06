/*******************************************************************************
 * libretroshare/src/rsitems: rsgxscalendaritems.cc                            *
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
#include "rsgxscalendaritems.h"
#include "serialiser/rstlvbase.h"
#include "serialiser/rsbaseserial.h"
#include "serialiser/rstypeserializer.h"

RsItem *RsGxsCalendarSerialiser::create_item(uint16_t service_id, uint8_t item_subtype) const
{
	if(service_id != RS_SERVICE_GXS_TYPE_CALENDAR)
		return nullptr;

	switch(item_subtype)
	{
	case RS_PKT_SUBTYPE_GXSCALENDAR_GROUP_ITEM: return new RsGxsCalendarGroupItem();
	case RS_PKT_SUBTYPE_GXSCALENDAR_MSG_ITEM:   return new RsGxsCalendarMessageItem();
	default:
		return RsGxsCommentSerialiser::create_item(service_id, item_subtype);
	}
}

void RsGxsCalendarGroupItem::clear()
{
	mDescription.clear();
}

bool RsGxsCalendarGroupItem::fromCalendarGroup(RsGxsCalendarGroup &group)
{
	clear();
	meta = group.mMeta;
	mDescription = group.mDescription;
	return true;
}

bool RsGxsCalendarGroupItem::toCalendarGroup(RsGxsCalendarGroup &group)
{
	group.mMeta = meta;
	group.mDescription = mDescription;
	return true;
}

void RsGxsCalendarGroupItem::serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext& ctx)
{
	RsTypeSerializer::serial_process(j, ctx, TLV_TYPE_STR_DESCR, mDescription, "mDescription");
}

void RsGxsCalendarMessageItem::clear()
{
	mIcsData.clear();
}

bool RsGxsCalendarMessageItem::fromCalendarMsg(RsGxsCalendarMessage &msg)
{
	clear();
	meta = msg.mMeta;
	mIcsData = msg.mIcsData;
	return true;
}

bool RsGxsCalendarMessageItem::toCalendarMsg(RsGxsCalendarMessage &msg)
{
	msg.mMeta = meta;
	msg.mIcsData = mIcsData;
	return true;
}

void RsGxsCalendarMessageItem::serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext& ctx)
{
	RsTypeSerializer::serial_process(j, ctx, TLV_TYPE_STR_MSG, mIcsData, "mIcsData");
}
