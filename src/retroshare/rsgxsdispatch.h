// RsGxsDispatch: a service to securely distribute data among groups of RsGxsIds.
//
// How to use it:
//
//		0 - derive your client class (for instance some chat client) from RsGxsDispatchClient, and re-implement receiveData()
//
//				class MyChatClass: public RsGxsDispatchClient, ...
//
//		1 - register the service ID (some number >0 that will be used to recognise the destination service at other participants)
//
//			MyChatClass::init()
//          {
//				rsGxsDispatch->registerClientService(SOME_UINT32,this);
//				...
//          }
//
//		2 - create new dispatches. Request the status to know whether you can tlk to them or not.
//

#include <retroshare/rsids.h>
#include <retroshare/rsgxstunnel.h>
#include <services/p3service.h>

typedef uint64_t RsGxsDispatchId;

struct RsGxsDispatchInfo
{
public:
    RsGxsDispatchId dispatch_id ;			// unique id of the lobby
    RsGxsId own_gxs_id ;					// ID used to participate in the dispatch

    std::map<RsGxsId, rstime_t> gxs_ids ;	// list of non tunnelled people in the dispatch. Used to display only. Larger than the tunnelled_ids.
    rstime_t last_activity ;				// last recorded activity. Useful for removing dead dispatches.

    uint32_t client_service;

    virtual void clear() { gxs_ids.clear(); last_activity = 0; client_service=0; }
};

class RsGxsDispatchClient
{
        public:
                virtual bool receiveData(RsGxsDispatchId id,void *data,uint32_t size) =0;
};

class RsGxsDispatch
{
    public:
        bool createGxsDispatch(const RsGxsId& own_id,const std::list<RsGxsId>& participants,uint32_t service_id,RsGxsDispatchId& dispatch_id) =0;
        bool leaveGxsDispatch(RsGxsDispatchId dispatch_id) =0;
        bool sendData(RsGxsDispatchId dispatch_id,void *data,uint32_t size) =0;

        bool registerClientService(uint32_t service_id,RsGxsDispatchClient *client) const =0;
        bool getDispatchInfo(RsGxsDispatchId id,RsGxsDispatchInfo& info) const =0;
};

RsGxsDispatch *rsGxsDispatch = nullptr;

