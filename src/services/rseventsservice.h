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
#include <mutex>
#include <condition_variable>
#include <set>
#include <thread>

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

	/** Per-handler in-flight barrier for unregisterEventsHandler().
	 *
	 * handleEvent() runs callbacks on a *copy* taken outside mHandlerMapMtx (on
	 * purpose, so a callback may send events or unregister itself). Without a
	 * barrier, a callback whose owner is being destroyed on another thread could
	 * still fire against a dangling object -> use-after-free (typically a SIGSEGV
	 * in qobject_cast<QThread*> inside RsQThreadUtils::postToObject at shutdown).
	 *
	 * At snapshot time (still under mHandlerMapMtx) handleEvent() records each
	 * handler id it is about to run, together with the dispatching thread, in
	 * mHandlersInFlight, and clears each entry as the corresponding callback
	 * returns. unregisterEventsHandler() erases the handler from the map (so no
	 * *future* dispatch can pick it up) and then waits on mDispatchStateCv until
	 * that specific handler is no longer running on any *other* thread. Once it
	 * returns, the handler is guaranteed neither running nor about to start, so a
	 * caller that unregisters from its destructor can be destroyed safely.
	 *
	 * Recording under the same lock as the snapshot is what makes this race-free:
	 * unregister either erases a handler before handleEvent() snapshots it (it is
	 * never run) or after (its in-flight mark is already visible and unregister
	 * waits for it).
	 *
	 * Unlike a single mutex held across the whole dispatch, callbacks still run
	 * with NO events-service lock held, and a slow or deliberately blocking
	 * handler (e.g. the synchronous passphrase / plugin-confirmation dialogs sent
	 * through sendEvent(), which block the caller via Qt::BlockingQueuedConnection)
	 * only ever delays unregister of *that same* handler, never of an unrelated
	 * one -> no cross-thread deadlock between a widget teardown and a blocking
	 * handler.
	 *
	 * The dispatching thread is intentionally not waited on: a callback that
	 * unregisters itself (or is re-entered through a synchronous sendEvent) on
	 * the dispatching thread must not deadlock waiting on itself, and in that
	 * case the owner is running its own code, not being destroyed concurrently. */
	std::mutex mDispatchStateMtx;
	std::condition_variable mDispatchStateCv;
	std::map< RsEventsHandlerId_t, std::multiset<std::thread::id> >
	        mHandlersInFlight;

	RsEventsHandlerId_t mLastHandlerId;

	/** Storage for event handlers, keep 10 extra types for plugins that might
	 * be released indipendently */
    std::vector<
	    std::map<
	        RsEventsHandlerId_t,
            std::function<void(std::shared_ptr<const RsEvent>)> >
	> mHandlerMaps;

    /** Extra event types registered by plugins */
    std::map<std::string,RsEventType> mRegisteredExtraEventTypes;

	RsMutex mEventQueueMtx;
	std::deque< std::shared_ptr<const RsEvent> > mEventQueue;

	void threadTick() override; /// @see RsTickingThread

	void handleEvent(std::shared_ptr<const RsEvent> event);
	RsEventsHandlerId_t generateUniqueHandlerId_unlocked();

	RS_SET_CONTEXT_DEBUG_LEVEL(3)
};
