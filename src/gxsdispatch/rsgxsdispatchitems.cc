/*******************************************************************************
 * libretroshare/src/gxsdispatch: rsgxsdispatchitems.cc                        *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2026 by Retroshare Team <retroshare.project@gmail.com>            *
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

#include <stdexcept>
#include "rsitems/rsserviceids.h"
#include "serialiser/rsbaseserial.h"
#include "serialiser/rstypeserializer.h"

#include "gxsdispatch/rsgxsdispatchitems.h"

//#define GXS_DISPATCH_ITEM_DEBUG 1

RsItem *RsGxsDispatchSerialiser::create_item(uint16_t service,uint8_t item_subtype) const
{
    if(service != (uint16_t)RsServiceType::GXS_DISPATCH)
        return nullptr ;

    switch(item_subtype)
    {
    case RS_PKT_SUBTYPE_GXS_DISPATCH_DATA:          return new RsGxsDispatchDataItem();
    case RS_PKT_SUBTYPE_GXS_DISPATCH_DATA_ACK:      return new RsGxsDispatchDataAckItem();
    case RS_PKT_SUBTYPE_GXS_DISPATCH_STATUS:        return new RsGxsDispatchStatusItem();
    default:
        return nullptr ;
    }
}

void RsGxsDispatchStatusItem::serial_process(RsGenericSerializer::SerializeJob j,RsGenericSerializer::SerializeContext& ctx)
{
    RsTypeSerializer::serial_process<uint32_t>(j,ctx,status,"status") ;
}

void RsGxsDispatchDataItem::serial_process(RsGenericSerializer::SerializeJob j,RsGenericSerializer::SerializeContext& ctx)
{
    RsTypeSerializer::serial_process<uint64_t>(j,ctx,unique_item_counter,"unique_item_counter") ;
    RsTypeSerializer::serial_process<uint32_t>(j,ctx,flags              ,"flags") ;
    RsTypeSerializer::serial_process<uint32_t>(j,ctx,service_id         ,"service_id") ;

    RsTypeSerializer::TlvMemBlock_proxy mem(data,data_size) ;
    RsTypeSerializer::serial_process(j,ctx,mem,"data") ;
}
void RsGxsDispatchDataAckItem::serial_process(RsGenericSerializer::SerializeJob j,RsGenericSerializer::SerializeContext& ctx)
{
    RsTypeSerializer::serial_process<uint64_t>(j,ctx,unique_item_counter,"unique_item_counter") ;
}


