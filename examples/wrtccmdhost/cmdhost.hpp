#include "msgpump.hpp"
#include "wrtcconn.hpp"
#include "stream.hpp"

#include "muxer.hpp"
#include "common.hpp"
#include "pc/peerconnectionfactory.h"
#include "rtc_base/thread.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/refcount.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

class CmdHost: public muxer::RtmpSink::Observer {
public:
    CmdHost();

    void Run();

    class CmdDoneObserver: public rtc::RefCountInterface {
    public:
        virtual void OnSuccess() {
            json res;
            OnSuccess(res);
        }
        virtual void OnSuccess(json& res) = 0;
        virtual void OnFailure(int code, const std::string& error) = 0;
    };

    // Implement for muxer::RtmpSink::Observer
    void OnRtmpSinkStatus(std::string id, std::string connectStatus);

    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory_;

    rtc::Thread wrtc_work_thread_;
    rtc::Thread wrtc_signal_thread_;

    std::mutex conn_map_lock_;
    std::map<std::string, WRTCConn*> conn_map_;

    std::mutex streams_map_lock_;
    std::map<std::string, Stream*> streams_map_;

    std::mutex muxers_map_lock_;
    std::map<std::string, muxer::AvMuxer*> muxers_map_;

    MsgPump* msgpump_;

    void writeMessage(const std::string& type, const json& msg);
    WRTCConn *checkConn(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    Stream *checkStream(const std::string& id, rtc::scoped_refptr<CmdDoneObserver> observer);
    muxer::AvMuxer *checkLibmuxer(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);

    void handleCreateOffer(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleCreateOfferSetLocalDesc(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleSetRemoteDesc(const json& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer);
    void handleSetRemoteDescCreateAnswer(const json& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer);
    void handleSetLocalDesc(const json& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer);
    void handleAddIceCandidate(const json& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer);
    void handleNewConn(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleNewLibmuxer(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleLibmuxerAddInput(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleLibmuxerRemoveInput(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleLibmuxerSetInputsOpt(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleStreamAddSink(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleStreamRemoveSink(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleNewCanvasStream(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleNewUrlStream(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleConnAddStream(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleConnStats(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleNewRawStream(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleRawStreamSendPacket(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleSinkStats(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleRequestKeyFrame(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleSinkDontReconnect(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleSinkSEIKey(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleReq(rtc::scoped_refptr<MsgPump::Request> req);
    void handleMsg(const std::string& type, const json& body);
};
