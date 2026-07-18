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
        mHandlerMaps.emplace_back();

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

	auto handler = std::make_shared<Handler>();
	handler->callback = std::move(multiCallback);
	mHandlerMaps[static_cast<std::size_t>(eventType)][hId] = std::move(handler);
	return std::error_condition();
}

namespace {
/* Handlers running on the current thread, innermost first (sendEvent() can
 * nest). Lets unregisterEventsHandler() skip waiting on itself when a callback
 * unregisters its own handler. */
struct RunningHandler
{
	explicit RunningHandler(const void* h): handler(h), prev(top) { top = this; }
	~RunningHandler() { top = prev; }

	const void* handler;
	RunningHandler* prev;
	static thread_local RunningHandler* top;
};
thread_local RunningHandler* RunningHandler::top = nullptr;
}

std::error_condition RsEventsService::unregisterEventsHandler(
        RsEventsHandlerId_t hId )
{
	std::shared_ptr<Handler> removed;
	{
		RS_STACK_MUTEX(mHandlerMapMtx);

		for(auto& handlerMap: mHandlerMaps)
		{
			auto it = handlerMap.find(hId);
			if(it != handlerMap.end())
			{
				removed = std::move(it->second);
				handlerMap.erase(it);
				break;
			}
		}
	}

	if(!removed) return RsEventsErrorNum::INVALID_HANDLER_ID;

	/* No future dispatch can pick it up anymore, but one snapshotted before
	 * the erase may still be running it: wait for that so a caller
	 * unregistering from its destructor can then be destroyed safely. */
	waitNotRunning(*removed);
	return std::error_condition();
}

void RsEventsService::waitNotRunning(const Handler& h)
{
	unsigned onThisThread = 0;
	for(auto* r = RunningHandler::top; r; r = r->prev)
		if(r->handler == &h) ++onThisThread;

	auto done = [&]{ return h.inFlight.load() <= onThisThread; };
	if(done()) return;

	/* Waiters counter first, so releaseHandler() either sees it or we see its
	 * decrement (both are seq_cst) */
	++mUnregisterWaiters;
	{
		std::unique_lock<std::mutex> lock(mUnregisterMtx);
		mUnregisterCv.wait(lock, done);
	}
	--mUnregisterWaiters;
}

void RsEventsService::releaseHandler(Handler& h)
{
	--h.inFlight;
	if(mUnregisterWaiters.load())
	{
		std::lock_guard<std::mutex> lock(mUnregisterMtx);
		mUnregisterCv.notify_all();
	}
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

	/* Snapshot the handlers and mark them in flight under mHandlerMapMtx, so
	 * unregisterEventsHandler() either erases a handler before it is
	 * snapshotted or sees it in flight and waits. Callbacks run with no lock
	 * held so they may send events or unregister themselves. */
	std::vector<std::shared_ptr<Handler>> handlers;
	{
		RS_STACK_MUTEX(mHandlerMapMtx);

		auto& typed = mHandlerMaps[static_cast<std::size_t>(event->mType)];
		// Clients registered with __NONE expect all events
		auto& all = mHandlerMaps[static_cast<std::size_t>(RsEventType::__NONE)];

		handlers.reserve(typed.size() + all.size());
		for(auto* handlerMap: {&typed, &all})
			for(auto& it: *handlerMap)
			{
				++it.second->inFlight;
				handlers.push_back(it.second);
			}
	}

	size_t next = 0;
	try
	{
		for(; next < handlers.size(); ++next)
		{
			{
				RunningHandler running(handlers[next].get());
				handlers[next]->callback(event);
			}
			releaseHandler(*handlers[next]);
		}
	}
	catch(...)
	{
		// Or a later unregister of one of them would wait forever
		for(; next < handlers.size(); ++next) releaseHandler(*handlers[next]);
		throw;
	}
}
