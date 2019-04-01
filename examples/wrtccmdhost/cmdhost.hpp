#include "msgpump.hpp"
#include "wrtcconn.hpp"
#include "stream.hpp"

#include "muxer.hpp"
#include "common.hpp"
#include "pc/peerconnectionfactory.h"
#include "rtc_base/thread.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/json.h"
#include "rtc_base/refcount.h"

class CmdHost {
public:
    CmdHost();

    void Run();

    class CmdDoneObserver: public rtc::RefCountInterface {
    public:
        virtual void OnSuccess() {
            Json::Value res;
            OnSuccess(res);
        }
        virtual void OnSuccess(Json::Value& res) = 0;
        virtual void OnFailure(int code, const std::string& error) = 0;
    };

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

    void writeMessage(const std::string& type, const Json::Value& msg);
    WRTCConn *checkConn(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    Stream *checkStream(const std::string& id, rtc::scoped_refptr<CmdDoneObserver> observer);
    muxer::AvMuxer *checkLibmuxer(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);

    void handleCreateOffer(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleCreateOfferSetLocalDesc(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleSetRemoteDesc(const Json::Value& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer);
    void handleSetRemoteDescCreateAnswer(const Json::Value& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer);
    void handleSetLocalDesc(const Json::Value& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer);
    void handleAddIceCandidate(const Json::Value& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer);
    void handleNewConn(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleNewLibmuxer(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleLibmuxerAddInput(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleLibmuxerRemoveInput(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleLibmuxerSetInputsOpt(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleStreamAddSink(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleStreamRemoveSink(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleNewCanvasStream(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleNewUrlStream(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleConnAddStream(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleConnStats(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleNewRawStream(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleRawStreamSendPacket(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleSinkStats(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleRequestKeyFrame(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer);
    void handleReq(rtc::scoped_refptr<MsgPump::Request> req);
    void handleMsg(const std::string& type, const Json::Value& body);
};
