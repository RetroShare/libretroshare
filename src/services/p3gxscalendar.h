/*******************************************************************************
 * libretroshare/src/services: p3gxscalendar.h                                 *
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

#include "retroshare/rsgxscalendar.h"
#include "services/p3gxscommon.h"
#include "gxs/rsgenexchange.h"
#include "gxs/gxstokenqueue.h"

class p3GxsCalendar : public RsGenExchange, public RsGxsCalendar,
	public GxsTokenQueue, public p3Config
{
public:
	p3GxsCalendar(RsGeneralDataService* gds, RsNetworkExchangeService* nes, RsGixs* gixs);
	virtual ~p3GxsCalendar() override = default;

	virtual RsServiceInfo getServiceInfo() override;
	virtual void service_tick() override;
	virtual void handleResponse(uint32_t token, uint32_t req_type, RsTokenService::GxsRequestStatus status) override;

	// RsGxsCalendar abstract interface implementation
	virtual bool createCalendar(
	        const std::string& name,
	        const std::string& description,
	        const RsGxsId&     authorId = RsGxsId(),
	        uint32_t           circleType = 0x0001,
	        const RsGxsCircleId& circleId = RsGxsCircleId(),
	        const RsGxsCircleId& internalCircle = RsGxsCircleId(),
	        uint32_t           groupFlags = GXS_SERV::FLAG_PRIVACY_PUBLIC,
	        RsGxsGroupId& calendarId = RS_DEFAULT_STORAGE_PARAM(RsGxsGroupId),
	        std::string&  errorMessage = RS_DEFAULT_STORAGE_PARAM(std::string)
	        ) override;

	virtual bool updateCalendar(
	        const RsGxsGroupId& calendarId,
	        const std::string& name,
	        const std::string& description,
	        const RsGxsId&     authorId = RsGxsId(),
	        uint32_t           circleType = 0x0001,
	        const RsGxsCircleId& circleId = RsGxsCircleId(),
	        const RsGxsCircleId& internalCircle = RsGxsCircleId(),
	        uint32_t           groupFlags = GXS_SERV::FLAG_PRIVACY_PUBLIC,
	        std::string&  errorMessage = RS_DEFAULT_STORAGE_PARAM(std::string)
	        ) override;

	virtual bool subscribeToCalendar(
	        const RsGxsGroupId& calendarId,
	        bool subscribe,
	        std::string& errorMessage = RS_DEFAULT_STORAGE_PARAM(std::string)
	        ) override;

	virtual bool publishCalendarIcs(
	        const RsGxsGroupId& calendarId,
	        const std::string& icsData,
	        const RsGxsId& authorId = RsGxsId(),
	        RsGxsMessageId& msgId = RS_DEFAULT_STORAGE_PARAM(RsGxsMessageId),
	        std::string& errorMessage = RS_DEFAULT_STORAGE_PARAM(std::string)
	        ) override;

	virtual bool getCalendarsSummaries(std::list<RsGroupMetaData>& calendars) override;

	virtual bool getCalendarContent(
	        const RsGxsGroupId& calendarId,
	        std::vector<RsGxsCalendarMessage>& messages
	        ) override;

protected:
	// RsGenExchange virtual overrides
	virtual bool service_checkIfGroupIsStillUsed(const RsGxsGrpMetaData& meta) override;
	virtual void notifyChanges(std::vector<RsGxsNotify*>& changes) override;
	virtual bool keepOldMsgVersions() const override { return true; }

	// p3Config virtual overrides
	virtual RsSerialiser* setupSerialiser() override;
	virtual bool saveList(bool &cleanup, std::list<RsItem *>&saveList) override;
	virtual bool loadList(std::list<RsItem *>& loadList) override;

private:
	static uint32_t calendarAuthenPolicy();
};
