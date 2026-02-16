/*******************************************************************************
 * libretroshare/src/rsitems: rstrafficstatsitems.h                            *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright (C) 2024                                                          *
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
#ifndef RS_TRAFFIC_STATS_ITEMS_H
#define RS_TRAFFIC_STATS_ITEMS_H

#include <map>

#include "rsitems/rsitem.h"
#include "rsitems/rsserviceids.h"
#include "retroshare/rsconfig.h"
#include "serialiser/rsserializer.h"
#include "serialiser/rstypeserializer.h"

// Use BANDWIDTH_CONTROL service type for config items
const uint8_t RS_PKT_SUBTYPE_TRAFFIC_STATS_ITEM = 0x10;

/**************************************************************************/

class RsTrafficStatsConfigItem : public RsItem
{
public:
    RsTrafficStatsConfigItem() : RsItem(RS_PKT_VERSION_SERVICE, RS_SERVICE_TYPE_BWCTRL, RS_PKT_SUBTYPE_TRAFFIC_STATS_ITEM)
    {}
    
    virtual ~RsTrafficStatsConfigItem() {}
    
    virtual void clear() 
    { 
        peerStats.clear(); 
        serviceStats.clear(); 
    }

    void serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext& ctx)
    {
        RsTypeSerializer::serial_process(j, ctx, peerStats, "peerStats");
        RsTypeSerializer::serial_process(j, ctx, serviceStats, "serviceStats");
    }

    std::map<RsPeerId, RsCumulativeTrafficStats> peerStats;
    std::map<uint16_t, RsCumulativeTrafficStats> serviceStats;
};

class RsTrafficStatsSerialiser : public RsServiceSerializer
{
public:
    RsTrafficStatsSerialiser() : RsServiceSerializer(RS_SERVICE_TYPE_BWCTRL) {}
    virtual ~RsTrafficStatsSerialiser() {}

    RsItem *create_item(uint16_t service, uint8_t item_sub_id) const
    {
        if (service != RS_SERVICE_TYPE_BWCTRL)
            return nullptr;

        switch (item_sub_id)
        {
        case RS_PKT_SUBTYPE_TRAFFIC_STATS_ITEM:
            return new RsTrafficStatsConfigItem();
        default:
            return nullptr;
        }
    }
};

/**************************************************************************/

#endif /* RS_TRAFFIC_STATS_ITEMS_H */
