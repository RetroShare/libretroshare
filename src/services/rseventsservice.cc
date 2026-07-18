/*******************************************************************************
 * Retroshare events service                                                   *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright (C) 2019-2020  Gioacchino Mazzurco <gio@retroshare.cc>             *
 * Copyright (C) 2019-2020  Retroshare Team <contact@retroshare.cc>            *
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

#include <string>
#include <thread>

#include "services/rseventsservice.h"


/*extern*/ RsEvents* rsEvents = nullptr;

RsEvent::~RsEvent() = default;
RsEvents::~RsEvents() = default;

/*static*/ const RsEventsErrorCategory RsEventsErrorCategory::instance;

std::error_condition RsEventsErrorCategory::default_error_condition(int ev)
const noexcept
{
	switch(static_cast<RsEventsErrorNum>(ev))
	{
	case RsEventsErrorNum::INVALID_HANDLER_ID: // [[fallthrough]];
	case RsEventsErrorNum::NULL_EVENT_POINTER: // [[fallthrough]];
	case RsEventsErrorNum::EVENT_TYPE_UNDEFINED: // [[fallthrough]];
	case RsEventsErrorNum::EVENT_TYPE_OUT_OF_RANGE:
		return std::errc::invalid_argument;
	default:
		return std::error_condition(ev, *this);
	}
}

std::error_condition RsEventsService::isEventTypeInvalid(RsEventType eventType)
{
	if(eventType == RsEventType::__NONE)
		return RsEventsErrorNum::EVENT_TYPE_UNDEFINED;

	if( eventType < RsEventType::__NONE ||
            static_cast<uint32_t>(eventType) >= mHandlerMaps.size() )
		return RsEventsErrorNum::EVENT_TYPE_OUT_OF_RANGE;

	return std::error_condition();
}

std::error_condition RsEventsService::isEventInvalid(
        std::shared_ptr<const RsEvent> event)
{
	if(!event) return RsEventsErrorNum::NULL_EVENT_POINTER;
	return isEventTypeInvalid(event->mType);
}

std::error_condition RsEventsService::postEvent(
        std::shared_ptr<const RsEvent> event )
{
	if(std::error_condition ec = isEventInvalid(event)) return ec;

	RS_STACK_MUTEX(mEventQueueMtx);
	mEventQueue.push_back(event);
	return std::error_condition();
}

std::error_condition RsEventsService::sendEvent(
        std::shared_ptr<const RsEvent> event )
{
	if(std::error_condition ec = isEventInvalid(event)) return ec;
	handleEvent(event);
	return std::error_condition();
}

RsEventsHandlerId_t RsEventsService::generateUniqueHandlerId()
{
	RS_STACK_MUTEX(mHandlerMapMtx);
	return generateUniqueHandlerId_unlocked();
}

RsEventType RsEventsService::getDynamicEventType(const std::string& unique_service_identifier)
{
    RS_STACK_MUTEX(mHandlerMapMtx);

    auto it = mRegisteredExtraEventTypes.find(unique_service_identifier);

    if(it == mRegisteredExtraEventTypes.end())
    {
        mRegisteredExtraEventTypes[unique_service_identifier] = static_cast<RsEventType>(mHandlerMaps.size());
        mHandlerMaps.push_back(  std::map<RsEventsHandlerId_t,std::function<void(std::shared_ptr<const RsEvent>)> >());

        it = mRegisteredExtraEventTypes.find(unique_service_identifier);

        RsInfo() << "Registered new dynamic event Type " << (int)it->second << " for service \"" << unique_service_identifier << "\"" << std::endl;
    }

    return it->second;
}

RsEventsHandlerId_t RsEventsService::generateUniqueHandlerId_unlocked()
{
	if(++mLastHandlerId) return mLastHandlerId; // Avoid 0 after overflow
	return 1;
}

std::error_condition RsEventsService::registerEventsHandler(
        std::function<void(std::shared_ptr<const RsEvent>)> multiCallback,
        RsEventsHandlerId_t& hId, RsEventType eventType )
{
	RS_STACK_MUTEX(mHandlerMapMtx);

	if(eventType != RsEventType::__NONE)
		if(std::error_condition ec = isEventTypeInvalid(eventType))
			return ec;

    if(hId > mLastHandlerId)
    {
        print_stacktrace();
        RsErr() << "You are probably using an uninitialized handler ID, which is not permitted. Allocating a new one" ;
        hId=0;
    }

    if(!hId)
        hId = generateUniqueHandlerId_unlocked();
    else
    {
        /* A non-zero hId is a legitimate, documented use case: the caller may
         * provide an id previously obtained from generateUniqueHandlerId() (see
         * registerEventsHandler() doc in rsevents.h). This is exactly what the
         * JSON API event-stream wrapper does, because its SSE callbacks capture
         * the id in order to unregister themselves later. Only a hId that is
         * actually already registered is a true override worth reporting. */
        bool alreadyRegistered = false;
        for(const auto& handlerMap : mHandlerMaps)
            if(handlerMap.find(hId) != handlerMap.end())
            {
                alreadyRegistered = true;
                break;
            }

        if(alreadyRegistered)
        {
            print_stacktrace();
            RsWarn() << "Overriding an existing event handler ID with a new callback. This is very unexpected. Make sure you know what you are doing." ;
        }
    }

	mHandlerMaps[static_cast<std::size_t>(eventType)][hId] = multiCallback;
	return std::error_condition();
}

std::error_condition RsEventsService::unregisterEventsHandler(
        RsEventsHandlerId_t hId )
{
	std::error_condition retval = RsEventsErrorNum::INVALID_HANDLER_ID;

	{
		RS_STACK_MUTEX(mHandlerMapMtx);

		for(uint32_t i=0; i<mHandlerMaps.size(); ++i)
		{
			auto it = mHandlerMaps[i].find(hId);
			if(it != mHandlerMaps[i].end())
			{
				mHandlerMaps[i].erase(it);
				retval = std::error_condition();
				break;
			}
		}
	}

	/* The handler can no longer be picked up by a *future* dispatch (removed from
	 * the map above under mHandlerMapMtx, which also serialises with the snapshot
	 * in handleEvent()). It may still be running in an *ongoing* dispatch though,
	 * because callbacks run outside mHandlerMapMtx. Wait until this specific
	 * handler is no longer executing on any *other* thread, so a caller that
	 * unregisters from its destructor can then be destroyed safely. We never wait
	 * on our own thread: a handler that unregisters itself (or is re-entered via a
	 * synchronous sendEvent) would otherwise deadlock waiting on itself, and in
	 * that case the owner is running its own code, not being destroyed
	 * concurrently. Only fence when we actually removed a handler. See
	 * mHandlersInFlight doc. */
	if(!retval)
	{
		const std::thread::id self = std::this_thread::get_id();
		std::unique_lock<std::mutex> lock(mDispatchStateMtx);
		mDispatchStateCv.wait(lock, [&]()
		{
			auto it = mHandlersInFlight.find(hId);
			if(it == mHandlersInFlight.end()) return true;
			for(const std::thread::id& tid: it->second)
				if(tid != self) return false;
			return true;
		});
	}

	return retval;
}

void RsEventsService::threadTick()
{
	auto nextRunAt = std::chrono::system_clock::now() +
	        std::chrono::milliseconds(200);

	std::shared_ptr<const RsEvent> eventPtr(nullptr);
	size_t futureEventsCounter = 0;

dispatchEventFromQueueLock:
	mEventQueueMtx.lock();
	if(mEventQueue.size() > futureEventsCounter)
	{
		eventPtr = mEventQueue.front();
		mEventQueue.pop_front();

		if(eventPtr->mTimePoint >= nextRunAt)
		{
			mEventQueue.push_back(eventPtr);
			++futureEventsCounter;
		}
	}
	mEventQueueMtx.unlock();

	if(eventPtr)
	{
		/* It is relevant that this stays out of mEventQueueMtx */
		handleEvent(eventPtr);
		eventPtr = nullptr; // ensure refcounter is decremented before sleep
		goto dispatchEventFromQueueLock;
	}

	std::this_thread::sleep_until(nextRunAt);
}

void RsEventsService::handleEvent(std::shared_ptr<const RsEvent> event)
{
	if(std::error_condition ec = isEventInvalid(event))
	{
		RsErr() << __PRETTY_FUNCTION__ << " " << ec << std::endl;
		print_stacktrace();
		return;
	}

	const std::thread::id self = std::this_thread::get_id();

	/* (hId, callback) pairs to run for this event. The snapshot and the
	 * "in-flight" bookkeeping below are both done under mHandlerMapMtx, so they
	 * are atomic with respect to unregisterEventsHandler()'s erase: that is what
	 * lets unregister reliably wait for an already-snapshotted callback instead
	 * of racing it (see mHandlersInFlight doc). */
	std::list< std::pair< RsEventsHandlerId_t,
	        std::function<void(std::shared_ptr<const RsEvent>)> > > callbacks;
	{
		RS_STACK_MUTEX(mHandlerMapMtx);
		/* It is important to NOT call the callback under mHandlerMapMtx
		 * protection to allow callbacks to send other events or unregister
		 * themselves, which would otherwise deadlock. */

		// Call all clients that registered a callback for this event type
		for(auto& cbit: mHandlerMaps[static_cast<uint32_t>(event->mType)])
			callbacks.push_back(std::make_pair(cbit.first, cbit.second));

		/* Also call all clients that registered with NONE, meaning that they
		 * expect all events */
		for(auto& cbit: mHandlerMaps[static_cast<uint32_t>(RsEventType::__NONE)])
			callbacks.push_back(std::make_pair(cbit.first, cbit.second));

		/* Mark every handler we are about to run as in-flight on this thread,
		 * still under mHandlerMapMtx. */
		std::lock_guard<std::mutex> stateLock(mDispatchStateMtx);
		for(auto& cb: callbacks)
			mHandlersInFlight[cb.first].insert(self);
	}

	/* Remove one in-flight mark for hId on this thread. Caller must hold
	 * mDispatchStateMtx. */
	auto clearInFlight = [this, self](RsEventsHandlerId_t hId)
	{
		auto it = mHandlersInFlight.find(hId);
		if(it == mHandlersInFlight.end()) return;
		auto tit = it->second.find(self);
		if(tit != it->second.end()) it->second.erase(tit);
		if(it->second.empty()) mHandlersInFlight.erase(it);
	};

	auto cbit = callbacks.begin();
	try
	{
		for(; cbit != callbacks.end(); ++cbit)
		{
			cbit->second(event);

			/* Clear this handler's in-flight mark and wake any
			 * unregisterEventsHandler() that is waiting for it. */
			std::lock_guard<std::mutex> stateLock(mDispatchStateMtx);
			clearInFlight(cbit->first);
			mDispatchStateCv.notify_all();
		}
	}
	catch(...)
	{
		/* A callback threw: clear the in-flight marks it and the not-yet-run
		 * handlers still hold, otherwise a later unregisterEventsHandler() for
		 * one of them would block forever. Then propagate as before: the ticking
		 * thread installs no handler, so an async throw still terminates the
		 * process exactly as it did previously. */
		std::lock_guard<std::mutex> stateLock(mDispatchStateMtx);
		for(; cbit != callbacks.end(); ++cbit)
			clearInFlight(cbit->first);
		mDispatchStateCv.notify_all();
		throw;
	}
}
