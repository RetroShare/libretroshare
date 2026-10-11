// This class is responsible for managing secured "Dispatch" messages between a set of GxsId.
// The goals are:
//
//                 Client requests a dispatch between a set of identities 
//                                          |
//                      a new dispatch ID is created for the client
//                                          |
//                       The client sends/receives data and ACKs, 
//                       requests info about the dispatch status,
//                                          |
//                   The client may "invite" new GxsIds in the dispatch,
//                               or possibly leave it.
//                       
// Specifications:
//
//      * a dispatch is never explicitly closed: when all but 1 GxsIds have left, it naturally disappears.
//
//      * a dispatch made of a collection of links between nodes for the given GxsIds, each link being a GxsTunnel.
//        That means the data is encrypted/decrypted along each link. The advantages are:
//            - changing the list of peers (add/remove) doesn't need to share some keys.
//            - the data is always kept private within the set of peers because it is always sent encrypted between ID pairs
//
//      * GxsIds can be invited only by someone already in the dispatch (the dispatch Id is known to each of them). 
//        Possibly in the future we may implement joining from outside using a link (basically the Id of the dispatch). 
//
//      * the routing between peers is done by the same algorithm than chat lobbies. It's unlikely to be fruitful to
//        try to re-use the bouncing code of ChatLobbies since it is very intricated with ChatLobbies themselves. 
//
// Coding  
//
//	1 - create and specify the p3GxsDispatch class
//	2 - look into ChatLobbies code for the bouncing code. What can we re-use?
//	3 - implement the GxsDispatch service (monitor links, send data to the bouncing code, etc) 
//
// Methods
//      - getDistachInfo(GxsDispatchId, GxsDisptachInfo) tells whether the dispatch runs, is fully connected, etc.
//      - sendData(unsigned char *data,int size) is used to send data. p3GxsDispatch makes sure it is received.
//      - 
//                  
// Possible uses:
//      - multi peers secured distant chat. 
//

#include <retroshare/rsids.h>
#include <retroshare/rsgxstunnel.h>
#include <services/p3service.h>

typedef uint64_t RsGxsDispatchDataId;

struct DispatchEntry: public RsGxsDispatchInfo
{
    // Routing information

    std::map<RsDispatchDataId,rstime_t> msg_cache ;		// dispatch data ids with time. Used in the bouncing algorithm.
    std::map<RsGxsTunnelId,RsGxsId> tunnelled_ids ;		// list ids directly connected by tunnels. Used to broadcast sent messages.
};


class p3GxsDispatch: public p3Service, public RsGxsTunnelService::RsGxsTunnelClientService, public RsGxsDispatch
{
    public:
        p3GxsDispatch();

        // Implementation of API from RsGxsDispatch

        bool createGxsDispatch(const RsGxsId& own_id,const std::list<RsGxsId>& participants,uint32_t service_id,RsGxsDispatchId& dispatch_id) override;
        bool leaveGxsDispatch(RsGxsDispatchId dispatch_id) override;
        bool sendData(void *data,uint32_t size) override;

        bool registerClientService(uint32_t service_id,RsGxsDispatchClient *client) const override;
        bool getDispatchInfo(RsGxsDispatchId id) const override;
                
    protected:

        // derived from p3service

        virtual int tick() override;
        virtual RsServiceInfo getServiceInfo() override;

        // derived from RsGxsTunnelClient

        virtual void notifyTunnelStatus(const RsGxsTunnelId& tunnel_id,uint32_t tunnel_status);
        virtual void receiveData(const RsGxsTunnelId& id,unsigned char *data,uint32_t data_size);
        virtual void connectToGxsTunnelService(RsGxsTunnelService *tunnel_service);
        virtual bool acceptDataFromPeer(const RsGxsId& gxs_id,const RsGxsTunnelId& tunnel_id,bool am_I_client_side);

    private:
        std::map<RsGxsDispatchId,DispatchEntry> _dispatches;
        std::map<uint32_t,RsGxsDispatchClient *> _clients;

        bool managePeers(); // peer management for each dispatch: requests new tunnels, close inactive ones etc.
        void debug_dump();
};


