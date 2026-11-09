/*******************************************************************************
 * libretroshare/src/gxsdispatch: rsgxsdispatchitems.h                         *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2016 by Retroshare Team <retroshare.project@gmail.com>            *
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

#include <cstdlib>

#include "rsitems/rsserviceids.h"
#include "rsitems/itempriorities.h"
#include "rsitems/rsitem.h"

#include "retroshare/rstypes.h"
#include "retroshare/rsgxsdispatch.h"
#include "serialiser/rsserial.h"


/* chat Flags */
const uint32_t RS_GXS_DISPATCH_FLAG_CLOSING_DISTANT_CONNECTION = 0x0400;
const uint32_t RS_GXS_DISPATCH_FLAG_ACK_DISTANT_CONNECTION     = 0x0800;
const uint32_t RS_GXS_DISPATCH_FLAG_KEEP_ALIVE                 = 0x1000;

const uint8_t RS_PKT_SUBTYPE_GXS_DISPATCH_DATA           = 0x01 ;
const uint8_t RS_PKT_SUBTYPE_GXS_DISPATCH_STATUS         = 0x02 ;
const uint8_t RS_PKT_SUBTYPE_GXS_DISPATCH_DATA_ACK       = 0x03 ;

class RsGxsDispatchItem: public RsItem
{
    public:
        // The item PeerId() is the dispatch id it is made for

        explicit RsGxsDispatchItem(uint8_t item_subtype) : RsItem(RS_PKT_VERSION_SERVICE,RsServiceType::GXS_DISPATCH,item_subtype,QOS_PRIORITY_RS_CHAT_ITEM),
          unique_item_counter(0),service_id(0)
        {
        }

        virtual ~RsGxsDispatchItem() {}
        virtual void clear() {}

        virtual void serial_process(RsGenericSerializer::SerializeJob j,RsGenericSerializer::SerializeContext& ctx) override;

        RsGxsId orig_id;
        RsGxsDispatchId dispatch_id;
        uint64_t unique_item_counter;		// this allows to make the item unique. The counter is dispatch-related.
        uint32_t service_id ;				// service id to forward that data to
};

/*!
 * @see p3GxsDispatchService
 */
class RsGxsDispatchDataItem: public RsGxsDispatchItem
{
public:
    RsGxsDispatchDataItem()
        : RsGxsDispatchItem(RS_PKT_SUBTYPE_GXS_DISPATCH_DATA),
          flags(0),
           data_size(0), data(NULL) {}

    // data is rs_malloc()'ed by the tunnel service (send side) or by the
    // deserialiser (receive side); the receive path nulls it before handing
    // it over to the client service.

    virtual ~RsGxsDispatchDataItem() { free(data); }
    virtual void clear() { free(data); data = NULL; data_size = 0; }

    virtual void serial_process(RsGenericSerializer::SerializeJob j,RsGenericSerializer::SerializeContext& ctx) override;

    uint32_t flags;						// mainly NEEDS_HACK?
    uint32_t data_size ;				// data size
    unsigned char *data ;				// data bytes
};

// Used to send status of connection. This can be closing orders, flushing orders, etc.
// These items are always sent encrypted.

class RsGxsDispatchStatusItem: public RsGxsDispatchItem
{
    public:
        RsGxsDispatchStatusItem() :RsGxsDispatchItem(RS_PKT_SUBTYPE_GXS_DISPATCH_STATUS) , status(0) {}
        RsGxsDispatchStatusItem(void *data,uint32_t size) ; // deserialization

        virtual ~RsGxsDispatchStatusItem() {}

        virtual void serial_process(RsGenericSerializer::SerializeJob j,RsGenericSerializer::SerializeContext& ctx) override;

        uint32_t status ;
};

// Used to confirm reception of an encrypted item.

class RsGxsDispatchDataAckItem: public RsGxsDispatchItem
{
    public:
        RsGxsDispatchDataAckItem() :RsGxsDispatchItem(RS_PKT_SUBTYPE_GXS_DISPATCH_DATA_ACK), unique_item_counter(0) {}
        RsGxsDispatchDataAckItem(void *data,uint32_t size) ; // deserialization

        virtual ~RsGxsDispatchDataAckItem() {}

        virtual void serial_process(RsGenericSerializer::SerializeJob j,RsGenericSerializer::SerializeContext& ctx) override;

        uint64_t unique_item_counter ;					// unique identifier for that item
};

class RsGxsDispatchSerialiser: public RsServiceSerializer
{
public:
    RsGxsDispatchSerialiser() :RsServiceSerializer((uint16_t)RsServiceType::GXS_DISPATCH) {}

    virtual RsItem *create_item(uint16_t service,uint8_t item_subtype) const ;
};

