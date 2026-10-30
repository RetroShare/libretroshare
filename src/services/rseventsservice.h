/*******************************************************************************
 * Retroshare events service                                                   *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright (C) 2019-2020  Gioacchino Mazzurco <gio@retroshare.cc>             *
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

#include <memory>
#include <cstdint>
#include <deque>
#include <array>
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <condition_variable>

#include "retroshare/rsevents.h"
#include "util/rsthreads.h"
#include "util/rsdebug.h"

class RsEventsService :
        public RsEvents, public RsTickingThread
{
public:
	RsEventsService():
        mHandlerMapMtx("RsEventsService::mHandlerMapMtx"),
        mLastHandlerId(1),
        mHandlerMaps(static_cast<std::size_t>(RsEventType::__MAX)),
        mEventQueueMtx("RsEventsService::mEventQueueMtx")  {}

    /// @see RsEvents
	std::error_condition postEvent(
	        std::shared_ptr<const RsEvent> event ) override;

	/// @see RsEvents
	std::error_condition sendEvent(
	        std::shared_ptr<const RsEvent> event ) override;

	/// @see RsEvents
	RsEventsHandlerId_t generateUniqueHandlerId() override;

    /// @see RsEvents
    RsEventType getDynamicEventType(const std::string& unique_service_identifier) override;

    /// @see RsEvents
	std::error_condition registerEventsHandler(
	        std::function<void(std::shared_ptr<const RsEvent>)> multiCallback,
	        RsEventsHandlerId_t& hId = RS_DEFAULT_STORAGE_PARAM(RsEventsHandlerId_t, 0),
	        RsEventType eventType = RsEventType::__NONE ) override;

	/// @see RsEvents
	std::error_condition unregisterEventsHandler(
	        RsEventsHandlerId_t hId ) override;

protected:
	std::error_condition isEventTypeInvalid(RsEventType eventType);
	std::error_condition isEventInvalid(std::shared_ptr<const RsEvent> event);

	RsMutex mHandlerMapMtx;

	/** One registered callback. inFlight counts the dispatches currently
	 * running it, so unregisterEventsHandler() can wait for the callback to
	 * finish on other threads while callbacks keep running with no lock held.
	 * Without that wait a callback whose owner is being destroyed on another
	 * thread could still fire on a dangling object. Waiting per handler instead
	 * of on a global dispatch lock avoids deadlocking against handlers that
	 * block on the GUI thread (passphrase, plugin confirmation). */
	struct Handler
	{
		std::function<void(std::shared_ptr<const RsEvent>)> callback;
		std::atomic<unsigned> inFlight {0};
	};

	/// Block until h is not running on any other thread
	void waitNotRunning(const Handler& h);

	/// Undo one inFlight mark and wake unregisterEventsHandler() if it waits
	void releaseHandler(Handler& h);

	std::mutex mUnregisterMtx;
	std::condition_variable mUnregisterCv;
	std::atomic<unsigned> mUnregisterWaiters {0};

	RsEventsHandlerId_t mLastHandlerId;

	/** Storage for event handlers, keep 10 extra types for plugins that might
	 * be released indipendently */
	std::vector< std::map<RsEventsHandlerId_t, std::shared_ptr<Handler>> >
	        mHandlerMaps;

    /** Extra event types registered by plugins */
    std::map<std::string,RsEventType> mRegisteredExtraEventTypes;

	RsMutex mEventQueueMtx;
	std::deque< std::shared_ptr<const RsEvent> > mEventQueue;

	void threadTick() override; /// @see RsTickingThread

	void handleEvent(std::shared_ptr<const RsEvent> event);
	RsEventsHandlerId_t generateUniqueHandlerId_unlocked();

	RS_SET_CONTEXT_DEBUG_LEVEL(3)
};
