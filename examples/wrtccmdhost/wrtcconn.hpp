#ifndef __WRTCCONN_HPP__
#define __WRTCCONN_HPP__

#include "common.hpp"
#include "pc/peerconnectionfactory.h"
#include "rtc_base/thread.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/json.h"
#include "rtc_base/refcount.h"
#include "stream.hpp"
#include "pc/peerconnectionfactory.h"

class WRTCConn
{
public:
    class ConnObserver {
    public:
        virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) = 0;
        virtual void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) = 0;
        virtual void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) = 0;
        virtual void OnAddStream(const std::string& id, const std::string& stream_id, Stream* stream) = 0;
        virtual void OnRemoveStream(const std::string& id, const std::string& stream_id) = 0;
   protected:
        ~ConnObserver() {}
    };

    class CreateDescObserver: public rtc::RefCountInterface {
    public:
        virtual void OnSuccess(const std::string& desc) = 0;
        virtual void OnFailure(const std::string& error) = 0;
    };
    
    typedef webrtc::SetSessionDescriptionObserver SetDescObserver;

    WRTCConn(
        rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory, 
        webrtc::PeerConnectionInterface::RTCConfiguration rtcconf,
        WRTCConn::ConnObserver* conn_observer,
        rtc::Thread* signal_thread
    );
    std::string ID();

    void CreateOffer(
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions offeropt,
        rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer
    );
    void CreateOfferSetLocalDesc(
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions offeropt,
        rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer
    );
    void SetLocalDesc(
        webrtc::SessionDescriptionInterface* desc,
        rtc::scoped_refptr<WRTCConn::SetDescObserver> observer
    );
    void SetRemoteDesc(
        webrtc::SessionDescriptionInterface* desc,
        rtc::scoped_refptr<WRTCConn::SetDescObserver> observer
    );
    void SetRemoteDescCreateAnswer(
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions answeropt,
        webrtc::SessionDescriptionInterface* desc,
        rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer
    );
    bool AddIceCandidate(webrtc::IceCandidateInterface* candidate);

    void GetStats(webrtc::RTCStatsCollectorCallback* cb);

    bool AddStream(SinkAddRemover* stream, std::vector<rtc::scoped_refptr<webrtc::MediaStreamTrackInterface>> &tracks);

private:
    std::string id_;
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory_;
    rtc::Thread* signal_thread_;
    webrtc::PeerConnectionInterface::RTCConfiguration rtcconf;
};


#endif
