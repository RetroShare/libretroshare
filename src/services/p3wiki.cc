/*******************************************************************************
 * libretroshare/src/services: p3wiki.h                                        *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2012-2012 by Robert Fernie <retroshare@lunamutt.com>              *
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
#include "services/p3wiki.h"
#include "retroshare/rsgxsflags.h"
#include "rsitems/rswikiitems.h"
#include "util/rsrandom.h"
#include "retroshare/rsevents.h"

RsWiki *rsWiki = NULL;

p3Wiki::p3Wiki(RsGeneralDataService* gds, RsNetworkExchangeService* nes, RsGixs *gixs)
	:RsGenExchange(gds, nes, new RsGxsWikiSerialiser(), RS_SERVICE_GXS_TYPE_WIKI, gixs, wikiAuthenPolicy()), 
	RsWiki(static_cast<RsGxsIface&>(*this))
{
}

RsServiceInfo p3Wiki::getServiceInfo()
{
    return RsServiceInfo(RS_SERVICE_GXS_TYPE_WIKI, "gxswiki", 1, 0, 1, 0);
}

uint32_t p3Wiki::wikiAuthenPolicy()
{
	uint32_t policy = 0;
	uint8_t flag = GXS_SERV::MSG_AUTHEN_ROOT_PUBLISH_SIGN | GXS_SERV::MSG_AUTHEN_CHILD_AUTHOR_SIGN;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::PUBLIC_GRP_BITS);
	flag |= GXS_SERV::MSG_AUTHEN_CHILD_PUBLISH_SIGN;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::RESTRICTED_GRP_BITS);
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::PRIVATE_GRP_BITS);
	return policy;
}

void p3Wiki::service_tick() {}

void p3Wiki::notifyChanges(std::vector<RsGxsNotify*>& changes)
{
    if (rsEvents) {
        /* Get the same dynamic event type ID used in the GUI */
        RsEventType wikiEventType = (RsEventType)rsEvents->getDynamicEventType("GXS_WIKI");

        for(auto change : changes) {
            /* Create event using the dynamic ID */
            std::shared_ptr<RsGxsWikiEvent> event = std::make_shared<RsGxsWikiEvent>(wikiEventType);
            event->mWikiGroupId = change->mGroupId; 
            
            if (dynamic_cast<RsGxsMsgChange*>(change)) {
                event->mWikiEventCode = RsWikiEventCode::UPDATED_SNAPSHOT;
            } else {
                event->mWikiEventCode = RsWikiEventCode::UPDATED_COLLECTION;
            }
            rsEvents->postEvent(event);
            delete change;
        }
    } else {
        for(auto change : changes) delete change;
    }
    changes.clear();
}