#include "cmdhost.hpp"
#include "stream.hpp"
#include "output.hpp"
#include <chrono>
#include <stdlib.h>
#include <iostream>
#include <random>
#include <fstream>

#include "rtc_base/base64.h"

const std::string kReqId = "reqId";
const std::string kId = "id";
const std::string kStreamId = "streamId";
const std::string kSinkId = "sinkId";
const std::string kSupportSEI = "supportSEI";
const std::string kSEIKey = "seiKey";
const std::string kSEIChange = "seiChange";
const std::string kCode = "code";
const std::string kError = "error";
const std::string kSdp = "sdp";
const std::string kCandidate = "candidate";
const int errConnNotFound = 10002;
const std::string errConnNotFoundString = "conn not found";
const int errInvalidParams = 10003;
const std::string errInvalidParamsString = "invalid params";
const int errStreamNotFound = 10004;
const std::string errStreamNotFoundString = "stream not found";
const int errMuxerNotFound = 10005;
const std::string errMuxerNotFoundString = "muxer not found";

const int maxPort = 65535;

const std::string kSdpMLineIndex = "sdpMLineIndex";
const std::string kSdpMid = "sdpMid";

const std::string mtEcho = "echo";
const std::string mtNewConn = "new-conn";
const std::string mtCreateOffer = "create-offer";
const std::string mtCreateOfferSetLocalDesc = "create-offer-set-local-desc";
const std::string mtSetRemoteDesc = "set-remote-desc";
const std::string mtSetLocalDesc = "set-local-desc";
const std::string mtSetRemoteDescCreateAnswer = "set-remote-desc-create-answer";
const std::string mtOnIceCandidate = "on-ice-candidate";
const std::string mtOnIceConnectionChange = "on-ice-conn-state-change";
const std::string mtOnIceGatheringChange = "on-ice-gathering-change";
const std::string mtAddIceCandidate = "add-ice-candidate";
const std::string mtOnConnAddStream = "on-conn-add-stream";
const std::string mtOnConnRemoveStream = "on-conn-remove-stream";
const std::string mtNewLibMuxer = "new-libmuxer";
const std::string mtLibmuxerAddInput = "libmuxer-add-input";
const std::string mtLibmuxerReplaceAllInputs = "libmuxer-replace-all-inputs";
const std::string mtLibmuxerSetInputsOpt = "libmuxer-set-inputs-opt";
const std::string mtLibmuxerRemoveInput = "libmuxer-remove-input";
const std::string mtStreamAddSink = "stream-add-sink";
const std::string mtStreamRemoveSink = "stream-remove-sink";
const std::string mtNewCanvasStream = "new-canvas-stream";
const std::string mtNewUrlStream = "new-url-stream";
const std::string mtConnAddStream = "conn-add-stream";
const std::string mtConnStats = "conn-stats";
const std::string mtNewRawStream = "new-raw-stream";
const std::string mtOnSinkRawpkt = "on-sink-rawpkt";
const std::string mtOnSinkStatus = "on-sink-status";
const std::string mtRawStreamSendPacket = "raw-stream-send-packet";
const std::string mtSinkStats = "sink-stats";
const std::string mtRequestKeyFrame = "request-key-frame";
const std::string mtSinkDontReconnect = "stream-sink-dont-reconnect";
const std::string mtSinkSEIKey = "stream-sink-sei-key";

static void parseOfferAnswerOpt(const json& v, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions& opt) {
    bool hasAudio{ false }, hasVideo{ false };

    if (jsonGetBool(v, "audio", hasAudio) && hasAudio) {
        opt.offer_to_receive_audio = 1;
    }

    if (jsonGetBool(v, "video", hasVideo) && hasVideo) {
        opt.offer_to_receive_video = 1;
    }
}

void CmdHost::Run() {
    wrtc_signal_thread_.Start();
    wrtc_work_thread_.Start();
    pc_factory_ = webrtc::CreatePeerConnectionFactory(
        &wrtc_work_thread_, &wrtc_work_thread_, &wrtc_signal_thread_,
        nullptr, nullptr, nullptr
    );
    Info("wrtc_work_thread_ %p", &wrtc_work_thread_);
    Info("wrtc_signal_thread_ %p", &wrtc_signal_thread_);
    msgpump_->Run();
}

void CmdHost::writeMessage(const std::string& type, const json& msg) {
    msgpump_->WriteMessage(type, msg);
}

static std::string iceStateToString(webrtc::PeerConnectionInterface::IceConnectionState state) {
    switch (state) {
    case webrtc::PeerConnectionInterface::kIceConnectionNew:
        return "new";
    case webrtc::PeerConnectionInterface::kIceConnectionChecking:
        return "checking";
    case webrtc::PeerConnectionInterface::kIceConnectionConnected:
        return "connected";
    case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
        return "completed";
    case webrtc::PeerConnectionInterface::kIceConnectionFailed:
        return "failed";
    case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
        return "disconnected";
    case webrtc::PeerConnectionInterface::kIceConnectionClosed:
        return "closed";
    default:
        return "unknown";
    }
}

static std::string iceGatheringStateToString(webrtc::PeerConnectionInterface::IceGatheringState state) {
    switch (state) {
    case webrtc::PeerConnectionInterface::kIceGatheringNew:
        return "new";
    case webrtc::PeerConnectionInterface::kIceGatheringGathering:
        return "gathering";
    case webrtc::PeerConnectionInterface::kIceGatheringComplete:
        return "complete";
    default:
        return "unknown";
    }
}

class ConnObserver: public WRTCConn::ConnObserver {
public:
    ConnObserver(CmdHost *h) : h_(h) {}
    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
        std::string sdp;
        candidate->ToString(&sdp);
        json res;
        json c;
        c[kSdpMid] = candidate->sdp_mid();
        c[kSdpMLineIndex] = std::to_string(candidate->sdp_mline_index());
        c[kCandidate] = sdp;
        res[kCandidate] = c.dump();
        writeMessage(mtOnIceCandidate, res);
    }
    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) {
        json res;
        res["state"] = iceStateToString(new_state);
        writeMessage(mtOnIceConnectionChange, res);
    }
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) {
        json res;
        res["state"] = iceGatheringStateToString(new_state);
        writeMessage(mtOnIceGatheringChange, res);
    }
    void OnAddStream(const std::string& id, const std::string& stream_id, Stream *stream) {
        {
            std::lock_guard<std::mutex> lock(h_->streams_map_lock_);
            h_->streams_map_[stream_id] = stream;
        }
        json res;
        res[kId] = id;
        res[kStreamId] = stream_id;
        writeMessage(mtOnConnAddStream, res);
    }
    void OnRemoveStream(const std::string& id, const std::string& stream_id) {
        json res;
        res[kId] = id;
        res[kStreamId] = stream_id;
        writeMessage(mtOnConnRemoveStream, res);
    }
    void writeMessage(const std::string& type, json& res) {
        res[kId] = id_;
        h_->writeMessage(type, res);
    }
    virtual ~ConnObserver() {}
    CmdHost *h_;
    std::string id_;
};

void CmdHost::handleNewConn(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    webrtc::PeerConnectionInterface::RTCConfiguration rtcconf = {};

    bool isRawpkt{ false };
    if (jsonGetBool(req, "rawpkt", isRawpkt) && isRawpkt) {
        rtcconf.set_rawpkt(true);
    }

    bool isDumpRawpkt{ false };
    if (jsonGetBool(req, "dumpRawpkt", isDumpRawpkt) && isDumpRawpkt) {
        rtcconf.set_dump_rawpkt(true);
    }

    int min_port = 0, max_port = 0;
    (void)jsonGetInt(req, "minPort", min_port);
    (void)jsonGetInt(req, "maxPort", max_port);
    if (min_port < 0 || min_port > maxPort || max_port < 0 || max_port > maxPort || min_port > max_port) {
        min_port = 0;
        max_port = 0;
    }

    rtcconf.set_ice_port_range(min_port, max_port);

    auto iceServersIt = req.find("iceServers");
    if (iceServersIt != req.end() && iceServersIt->is_array() && iceServersIt->size() > 0) {
        webrtc::PeerConnectionInterface::IceServer icesrv = {};
        for (const auto& ice_server : *iceServersIt) {
            if (ice_server.is_object()) {
                auto urlsIt = ice_server.find("urls");
                if (urlsIt != ice_server.end() && urlsIt->is_array()) {
                    for (const auto& url : *urlsIt) {
                        if (url.is_string()) {
                            auto s = url.get<std::string>();
                            icesrv.urls.push_back(s);
                            Verbose("NewConnIceAddUrl %s", s.c_str());
                        }
                    }
                }
            }
        }

        rtcconf.servers.push_back(icesrv);
    }

    auto conn_observer = new ConnObserver(this);
    auto conn = new WRTCConn(pc_factory_, rtcconf, conn_observer, &wrtc_signal_thread_);
    conn_observer->id_ = conn->ID();

    {
        std::lock_guard<std::mutex> lock(conn_map_lock_);
        conn_map_[conn->ID()] = conn;
    }

    json res;
    res[kId] = conn->ID();
    observer->OnSuccess(res);
}

Stream* CmdHost::checkStream(const std::string& id, rtc::scoped_refptr<CmdDoneObserver> observer) {
    Stream *stream = nullptr;
    {
        std::lock_guard<std::mutex> lock(streams_map_lock_);
        auto it = streams_map_.find(id);
        if (it != streams_map_.end()) {
            stream = it->second;
        }
    }
    if (stream == nullptr) {
        if (observer != nullptr) {
            observer->OnFailure(errStreamNotFound, errStreamNotFoundString);
        }
        return nullptr;
    }

    return stream;
}

WRTCConn* CmdHost::checkConn(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    std::string id;
    if (!jsonGetString(req, kId, id)) {
        observer->OnFailure(errInvalidParams, errInvalidParamsString);
        return nullptr;
    }

    WRTCConn *conn = nullptr;
    {
        std::lock_guard<std::mutex> lock(conn_map_lock_);
        auto it = conn_map_.find(id);
        if (it != conn_map_.end()) {
            conn = it->second;
        }
    }
    if (conn == nullptr) {
        observer->OnFailure(errConnNotFound, errConnNotFoundString);
        return nullptr;
    }

    return conn;
}

class CreateDescObserver: public WRTCConn::CreateDescObserver {
public:
    CreateDescObserver(rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) : observer_(observer) {}

    void OnSuccess(const std::string& desc) {
        json res;
        res[kSdp] = desc;
        observer_->OnSuccess(res);
    }
    void OnFailure(const std::string& error) {
        observer_->OnFailure(errInvalidParams, error);
    }

    rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer_;
};

void CmdHost::handleCreateOffer(const json& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == nullptr) {
        return;
    }

    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions offeropt = {};
    parseOfferAnswerOpt(req, offeropt);
    conn->CreateOffer(offeropt, new rtc::RefCountedObject<CreateDescObserver>(observer));
}

void CmdHost::handleCreateOfferSetLocalDesc(const json& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == nullptr) {
        return;
    }

    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions offeropt = {};
    parseOfferAnswerOpt(req, offeropt);
    conn->CreateOfferSetLocalDesc(offeropt, new rtc::RefCountedObject<CreateDescObserver>(observer));
}

class SetDescObserver: public WRTCConn::SetDescObserver {
public:
    SetDescObserver(rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) : observer_(observer) {}

    void OnSuccess() {
        json res;
        observer_->OnSuccess(res);
    }
    void OnFailure(const std::string& error) {
        observer_->OnFailure(errInvalidParams, error);
    }

    rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer_;
};

void CmdHost::handleSetLocalDesc(const json& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == nullptr) {
        return;
    }

    std::string type;
    if (!jsonGetString(req, "type", type) || type.empty()) {
        type = "offer";
    }

    std::string sdp;
    if (!jsonGetString(req, kSdp, sdp) || sdp.empty()) {
        observer->OnFailure(errInvalidParams, errInvalidParamsString);
        return;
    }

    webrtc::SdpParseError err;
    auto desc = CreateSessionDescription(type, sdp, &err);
    if (!desc) {
        observer->OnFailure(errInvalidParams, err.description);
        return;
    }

    conn->SetLocalDesc(desc, new rtc::RefCountedObject<SetDescObserver>(observer));
}

void CmdHost::handleSetRemoteDesc(const json& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == nullptr) {
        return;
    }

    std::string type;
    if (!jsonGetString(req, "type", type) || type.empty()) {
        type = "answer";
    }

    std::string sdp;
    if (!jsonGetString(req, kSdp, sdp) || sdp.empty()) {
        observer->OnFailure(errInvalidParams, errInvalidParamsString);
        return;
    }

    webrtc::SdpParseError err;
    auto desc = CreateSessionDescription(type, sdp, &err);
    if (!desc) {
        observer->OnFailure(errInvalidParams, err.description);
        return;
    }

    conn->SetRemoteDesc(desc, new rtc::RefCountedObject<SetDescObserver>(observer));
}

void CmdHost::handleSetRemoteDescCreateAnswer(const json& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == nullptr) {
        return;
    }

    std::string sdp;
    if (!jsonGetString(req, kSdp, sdp) || sdp.empty()) {
        observer->OnFailure(errInvalidParams, errInvalidParamsString);
        return;
    }

    webrtc::SdpParseError err;
    auto desc = CreateSessionDescription("offer", sdp, &err);
    if (!desc) {
        observer->OnFailure(errInvalidParams, err.description);
        return;
    }

    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions answeropt = {};
    parseOfferAnswerOpt(req, answeropt);
    conn->SetRemoteDescCreateAnswer(answeropt, desc, new rtc::RefCountedObject<CreateDescObserver>(observer));
}

void CmdHost::handleAddIceCandidate(const json& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == nullptr) {
        return;
    }

    std::string jsonCandidate;
    if (!jsonGetString(req, kCandidate, jsonCandidate) || jsonCandidate.empty()) {
        observer->OnFailure(errInvalidParams, "parse req failed");
        return;
    }

    json c = json::parse(jsonCandidate);
    if (c.is_discarded()) {
        observer->OnFailure(errInvalidParams, "parse candidate failed");
        return;
    }

    std::string sdpMid;
    if (!jsonGetString(c, kSdpMid, sdpMid)) {
        observer->OnFailure(errInvalidParams, "parse kSdpMid failed");
        return;
    }


    std::string sdpMlineIndex;
    if (!jsonGetString(c, kSdpMLineIndex, sdpMlineIndex)) {
        observer->OnFailure(errInvalidParams, "parse kSdpMLineIndex failed");
        return;
    }
    int sdp_mlineindex = atoi(sdpMlineIndex.c_str());

    std::string strCandidate;
    if (!jsonGetString(c, kCandidate, strCandidate)) {
        observer->OnFailure(errInvalidParams, "parse kCandidate failed");
        return;
    }

    webrtc::SdpParseError err;
    auto candidate = webrtc::CreateIceCandidate(sdpMid, sdp_mlineindex, strCandidate, &err);
    if (!candidate) {
        observer->OnFailure(errInvalidParams, err.description);
        return;
    }

    if (!conn->AddIceCandidate(candidate)) {
        observer->OnFailure(errInvalidParams, "AddIceCandidate failed");
        return;
    }

    json v;
    observer->OnSuccess(v);
}

muxer::AvMuxer* CmdHost::checkLibmuxer(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    std::string id;
    if (!jsonGetString(req, kId, id)) {
        observer->OnFailure(errInvalidParams, errInvalidParamsString);
        return nullptr;
    }

    muxer::AvMuxer *m = nullptr;
    {
        std::lock_guard<std::mutex> lock(muxers_map_lock_);
        auto it = muxers_map_.find(id);
        if (it != muxers_map_.end()) {
            m = it->second;
        }
    }

    if (m == nullptr) {
        observer->OnFailure(errMuxerNotFound, errMuxerNotFoundString);
        return nullptr;
    }

    return m;
}

class LibmuxerOutputStream: public Stream {
public:
    LibmuxerOutputStream() {}
};

void CmdHost::handleNewLibmuxer(const json& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    int w = 0, h = 0;
    (void)jsonGetInt(req, "w", w);
    (void)jsonGetInt(req, "h", h);

    std::string reqId;
    if (!jsonGetString(req, kReqId, reqId) || reqId.empty()) {
        reqId = newReqId();
    }

    auto m = new muxer::AvMuxer(std::make_shared<XLogger>(reqId), w, h);
    auto id = newReqId();
    {
        std::lock_guard<std::mutex> lock(muxers_map_lock_);
        muxers_map_[id] = m;
    }

    auto audioOnlyIt = req.find("audioOnly");
    if (audioOnlyIt != req.end() && audioOnlyIt->is_boolean()) {
        m->audioOnly_.store(audioOnlyIt->get<bool>());
    }

    if (!m->audioOnly_.load() && (w == 0 || h == 0)) {
        observer->OnFailure(errInvalidParams, "invalid w or h");
        return;
    }

    int fps = 0;
    if (jsonGetInt(req, "fps", fps) && fps > 0) {
        m->fps_ = fps;
    }

    auto stream = new LibmuxerOutputStream();
    auto stream_id = newReqId();
    {
        std::lock_guard<std::mutex> lock(streams_map_lock_);
        streams_map_[stream_id] = stream;
    }
    m->AddOutput(stream_id, stream);

    m->Start();

    json res;
    res[kId] = id;
    res["outputStreamId"] = stream_id;
    observer->OnSuccess(res);
}


static void libmuxerSetInputOpt(const std::shared_ptr<muxer::Input>& lin, const json& opt) {
    if (!opt.is_object()) {
        return;
    }

    int w = 0, h = 0;
    bool hidden = false;
    (void)jsonGetInt(opt, "w", w);
    (void)jsonGetInt(opt, "h", h);
    if (w == 0 || h == 0) {
        hidden = true;
    } else {
        (void)jsonGetBool(opt, "hidden", hidden);
    }
    lin->SetOption("hidden", hidden);
    lin->SetOption("w", w);
    lin->SetOption("h", h);

    bool supportSEI = false;
    (void)jsonGetBool(opt, "supportSEI", supportSEI);
    lin->SetOption("supportSEI", supportSEI);

    int x = 0, y = 0, z = 0;
    (void)jsonGetInt(opt, "x", x);
    (void)jsonGetInt(opt, "y", y);
    (void)jsonGetInt(opt, "z", z);
    lin->SetOption("x", x);
    lin->SetOption("y", y);
    lin->SetOption("z", z);

    bool muted = false;
    (void)jsonGetBool(opt, "muted", muted);
    lin->SetOption("muted", muted);

    std::string stretchMode;
    if (jsonGetString(opt, muxer::options::stretchMode, stretchMode) && !stretchMode.empty()) {
        int mode = muxer::VideoRescaler::STRETCH_ASPECT_FILL;
        if (stretchMode == muxer::options::stretchAspectFill) {
            mode = muxer::VideoRescaler::STRETCH_ASPECT_FILL;
        } else if (stretchMode == muxer::options::stretchAspectFit) {
            mode = muxer::VideoRescaler::STRETCH_ASPECT_FIT;
        } else if (stretchMode == muxer::options::stretchScaleToFit) {
            mode = muxer::VideoRescaler::STRETCH_SCALE_TO_FIT;
        }
        lin->SetOption(muxer::options::stretchMode, mode);
    }
}

bool CmdHost::addInput(muxer::AvMuxer *m, const json& req, json &res,
    std::shared_ptr<muxer::Input> &input, Stream *&stream,
    rtc::scoped_refptr<CmdDoneObserver> observer)
{
    stream = nullptr;
    std::string streamId;

    if (jsonGetString(req, kStreamId, streamId)) {
        stream = checkStream(streamId, observer);
    }
    if (stream == nullptr) {
        return false;
    }

    std::string key = "";
    auto optIt = req.find("opt");
    if (optIt != req.end() && optIt->is_object()) {
        bool supportSEI{false};
        (void)jsonGetBool(*optIt, kSupportSEI, supportSEI);
        if (supportSEI) {
            auto reqId = newReqId();
            key = streamId + "." + reqId;
            std::list<AVPacket *>* queue = new std::list<AVPacket *>;
            auto iter = SeiQueues.find(key);
            if (iter != SeiQueues.end()) {
                if (iter->second) {
                    delete(iter->second);
                }
                SeiQueues.erase(iter);
            }
            SeiQueues.emplace(std::make_pair(key, queue));
            m->SetInputKey(streamId, key);
        }
    }

    res[kId] = streamId;
    res[kSEIKey] = key;

    input = std::make_shared<muxer::Input>(std::make_shared<XLogger>(streamId), streamId);
    if (optIt != req.end() && optIt->is_object()) {
        libmuxerSetInputOpt(input, *optIt);
    }

    if (stream->lastVideo_ != nullptr){
      input->SetVideo(stream->lastVideo_);
    }

    return true;
}

void CmdHost::handleLibmuxerReplaceAllInputs(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto m = checkLibmuxer(req, observer);
    if (m == nullptr) {
        return;
    }

    auto opts = req.find("opts");
    if (opts == req.end() || !opts->is_array()) {
        observer->OnFailure(errInvalidParams, "opts");
        return;
    }

    json resArr = json::array();
    std::vector<std::shared_ptr<muxer::Input>> inputs;
    std::vector<Stream*> streams;

    for (const auto& ireq: *opts) {
        json ires;
        std::shared_ptr<muxer::Input> input;
        Stream *stream;

        if (!addInput(m, ireq, ires, input, stream, observer)) {
            return;
        }

        inputs.push_back({std::move(input)});
        streams.push_back(stream);

        resArr.push_back(ires);
    }



    for (size_t i = 0; i < inputs.size(); i++) {
        inputs[i]->Start(streams[i]);
    }
    // sleep 500ms make sure new input have data.
     usleep(500*1000);

    m->ReplaceAllInputs(inputs);

    json res;
    res["res"] = resArr;
    observer->OnSuccess(res);
}

void CmdHost::handleLibmuxerAddInput(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto m = checkLibmuxer(req, observer);
    if (m == nullptr) {
        return;
    }

    std::shared_ptr<muxer::Input> input;
    Stream *stream;

    json res;
    if (!addInput(m, req, res, input, stream, observer)) {
        return;
    }

    input->Start(stream);
    m->AddInput(input);

    observer->OnSuccess(res);
}

void CmdHost::handleLibmuxerRemoveInput(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto m = checkLibmuxer(req, observer);
    if (m == nullptr) {
        return;
    }

    std::string streamId;
    if (jsonGetString(req, kStreamId, streamId)) {
        auto key = m->GetInputKey(streamId);
        auto iter = SeiQueues.find(key);
        if (iter != SeiQueues.end()) {
            if (iter->second) {
                delete(iter->second);
            }
            SeiQueues.erase(key);
        }

        m->RemoveInput(streamId);
    }

    json res;
    observer->OnSuccess(res);
}

void CmdHost::handleLibmuxerSetInputsOpt(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto m = checkLibmuxer(req, observer);
    if (m == nullptr) {
        return;
    }
    auto seiChange = false;
    std::string key = "";
    auto inputsIt = req.find("inputs");
    if (inputsIt != req.end() && inputsIt->is_array()) {
        for (const auto& input : *inputsIt) {
            auto idIt = input.find(kId);
            auto optIt = input.find("opt");
            if (idIt != input.end() && idIt->is_string() && optIt != input.end() && optIt->is_object()) {
                auto id = idIt->get<std::string>();
                auto lin = m->FindInput(id);
                if (lin != nullptr) {
                    bool prevSupportSEI{ false }, currSupportSEI{ false };
                    lin->GetOption(muxer::options::supportSEI, prevSupportSEI);
                    libmuxerSetInputOpt(lin, *optIt);
                    (void)jsonGetBool(*optIt, kSupportSEI, currSupportSEI);
                    seiChange = (prevSupportSEI != currSupportSEI) ? true : false;
                    if (seiChange && currSupportSEI) {
                        auto reqId = newReqId();
                        key = id + "." + reqId;
                        std::list<AVPacket *>* queue = new std::list<AVPacket *>;
                        auto iter = SeiQueues.find(key);
                        if (iter != SeiQueues.end()) {
                            if (iter->second) {
                                delete(iter->second);
                            }
                            SeiQueues.erase(iter);
                        }
                        SeiQueues.emplace(std::make_pair(key, queue));
                        m->SetInputKey(id, key);
                    }
                }
            }
        }
    }
    json res;
    res[kSEIChange] = seiChange;
    res[kSEIKey] = key;

    observer->OnSuccess(res);
}

class RawpktSink: public SinkObserver {
public:
    RawpktSink(CmdHost *h, const std::string sinkid): h(h), sinkid(sinkid) {
    }

    void OnFrame(const std::shared_ptr<muxer::MediaFrame>& frame) {
        json res;
        res[kId] = sinkid;
        res["pts"] = uint64_t(frame->TimeStamp());
        auto rawData = std::vector<uint8_t>(frame->rawpkt.data(), frame->rawpkt.data() + frame->rawpkt.size());
        res["rawpkt"] = json::binary(rawData);
        h->writeMessage(mtOnSinkRawpkt, res);
    }

private:
    CmdHost *h;
    std::string sinkid;
};

static void checkGray(const std::string &url, const std::string &reqId, muxer::RtmpSink *sink) {
    std::string prefix = "rtmp://";
    if (url.compare(0, prefix.size(), prefix)) {
        return;
    }

    int l = reqId.find_first_of(".");
    int r = reqId.find_first_of(":");
    if (l == -1 || r == -1) {
        return;
    }

    std::string appid = reqId.substr(l + 1, r - l - 1);

    std::ifstream ifs("wrtccmdhost_config.json");
    if (ifs.fail()) {
        return;
    }

    json config = json::parse(ifs, nullptr, false);

    auto appConfig = config.find("app_config");
    if (appConfig == config.end() || !appConfig->is_object()) {
        return;
    }

    auto entry = appConfig->find(appid);
    if (entry == appConfig->end() || !entry->is_object()) {
        return;
    }

    auto grayVideoPreset = entry->find("gray_video_preset");
    if (grayVideoPreset != entry->end() && grayVideoPreset->is_object()) {
        auto value = grayVideoPreset->find("value");
        auto ratio = grayVideoPreset->find("ratio");
        if (value != grayVideoPreset->end() && value->is_string() &&
            ratio != grayVideoPreset->end() && ratio->is_number_float() ) {
            auto valueS = value->get<std::string>();
            auto ratioF = ratio->get<double>();

            static std::mt19937_64 rng(std::chrono::high_resolution_clock::now().time_since_epoch().count());
            std::uniform_real_distribution<double> realdis(0, 1);
            double rn = realdis(rng);

            if (rn < ratioF) {
                sink->videoPreset = valueS;
            }
        }
    }
}

void CmdHost::handleStreamAddSink(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    std::string id;
    if (!jsonGetString(req, kId, id)) {
        observer->OnFailure(errInvalidParams, "id");
        return;
    }

    auto stream = checkStream(id, observer);
    if (stream == nullptr) {
        observer->OnFailure(errInvalidParams, "stream id");
        return;
    }

    auto sinkid = newReqId();

    bool isRaw{ false };
    if (jsonGetBool(req, "raw", isRaw) && isRaw) {
        auto sink = new RawpktSink(this, sinkid);
        stream->AddSink(sinkid, sink);

        json res;
        res[kId] = sinkid;
        observer->OnSuccess(res);
        return;
    }

    std::string url;
    if (!jsonGetString(req, "url", url) || url.empty()) {
        observer->OnFailure(errInvalidParams, "url invalid");
        return;
    }

    std::string reqId;
    if (!jsonGetString(req, "reqId", reqId) || reqId.empty()) {
        reqId = newReqId();
    }

    auto sink = new muxer::RtmpSink(this, sinkid, url, std::make_shared<XLogger>(reqId));

    int kbps = 0;
    if (jsonGetInt(req, "kbps", kbps) && kbps > 0) {
        sink->videoKbps = kbps;
    }

    int minRate = 0;
    if (jsonGetInt(req, "minRate", minRate) && minRate > 0) {
        sink->videoMinRate = minRate;
    }

    int maxRate = 0;
    if (jsonGetInt(req, "maxRate", maxRate) && maxRate > 0) {
        sink->videoMaxRate = maxRate;
    }

    int gop = 0;
    if (jsonGetInt(req, "gop", gop) && gop > 0) {
        sink->videoGop = gop;
    }

    int fps = 0;
    if (jsonGetInt(req, "fps", fps) && fps > 0) {
        sink->videoFps = fps;
    }

    checkGray(url, reqId, sink);

    stream->AddSink(sinkid, sink);

    json res;
    res[kId] = sinkid;
    observer->OnSuccess(res);
}

void CmdHost::handleStreamRemoveSink(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    std::string id;
    if (!jsonGetString(req, kId, id)) {
        observer->OnFailure(errInvalidParams, "id");
        return;
    }

    auto stream = checkStream(id, observer);
    if (stream == nullptr) {
        return;
    }

    std::string sinkId;
    if (jsonGetString(req, "sinkId", sinkId) && !stream->RemoveSink(sinkId)) {
        observer->OnFailure(errInvalidParams, "sink not found");
        return;
    }

    observer->OnSuccess();
}

class CanvasStream: public Stream {
public:
    CanvasStream() : fps_(25), w_(320), h_(240), bg_(0xff0000) {
    }

    void Start() {
        auto gen_video = [this] {
            int ts_ms = 0;
            while (exit_.load() == false) {
                std::shared_ptr<muxer::MediaFrame> frame = std::make_shared<muxer::MediaFrame>();
                frame->Stream(muxer::STREAM_VIDEO);
                frame->Codec(muxer::CODEC_H264);

                auto avframe = frame->AvFrame();
                avframe->format = AV_PIX_FMT_YUV420P;
                avframe->height = h_.load();
                avframe->width = w_.load();
                avframe->pts = ts_ms;
                av_frame_get_buffer(avframe, 32);

                uint32_t bg = bg_.load();
                uint8_t nR = bg >> 16;
                uint8_t nG = (bg >> 8) & 0xff;
                uint8_t nB = bg & 0xff;
                uint8_t nY = static_cast<uint8_t>((0.257 * nR) + (0.504 * nG) + (0.098 * nB) + 16);
                uint8_t nU = static_cast<uint8_t>((0.439 * nR) - (0.368 * nG) - (0.071 * nB) + 128);
                uint8_t nV = static_cast<uint8_t>(-(0.148 * nR) - (0.291 * nG) + (0.439 * nB) + 128);

                memset(avframe->data[0], nY, avframe->linesize[0] * avframe->height);
                memset(avframe->data[1], nU, avframe->linesize[1] * (avframe->height/2));
                memset(avframe->data[2], nV, avframe->linesize[2] * (avframe->height/2));

                SendFrame(frame);

                double sleep_s = 1.0/((double)fps_.load());
                usleep((useconds_t)(sleep_s*1e6));
                ts_ms += int(sleep_s*1e3);
            }
        };
        video_thread_ = std::thread(gen_video);

        auto gen_audio = [this] {
            int ts_ms = 0;
            auto sample_rate = 8000;
            double dur = 0.01;
            auto nb_samples = (int)((double)sample_rate*dur);
            double sint = 0.0;

            while (exit_.load() == false) {
                std::shared_ptr<muxer::MediaFrame> frame = std::make_shared<muxer::MediaFrame>();
                frame->Stream(muxer::STREAM_AUDIO);
                frame->Codec(muxer::CODEC_AAC);

                auto avframe = frame->AvFrame();
                avframe->format = AV_SAMPLE_FMT_S16;
                avframe->channel_layout = AV_CH_LAYOUT_MONO;
                avframe->sample_rate = sample_rate;
                avframe->nb_samples = nb_samples;
                avframe->pts = ts_ms;
                av_frame_get_buffer(avframe, 0);

                int16_t *p = (int16_t*)avframe->data[0];
                for (int i = 0; i < nb_samples; i++) {
                    p[i] = (int16_t)(sin(sint*2*M_PI*440)*0x7fff);
                    sint += 1.0/(double)sample_rate;
                }
                //memset(avframe->data[0], 0, avframe->linesize[0]);

                SendFrame(frame);

                usleep((useconds_t)(dur*0.5*1e6));
                ts_ms += int(dur*1e3);
            }
        };
        audio_thread_ = std::thread(gen_audio);
    }

    void Stop() {
        exit_.store(true);
        if (video_thread_.joinable()) {
            video_thread_.join();
        }
        if (audio_thread_.joinable()) {
            audio_thread_.join();
        }
    }

    std::thread video_thread_;
    std::thread audio_thread_;
    std::atomic<bool> exit_;
    std::atomic<int> fps_;
    std::atomic<int> w_;
    std::atomic<int> h_;
    std::atomic<uint32_t> bg_;
};

void CmdHost::handleNewUrlStream(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto stream_id = newReqId();
    std::string reqId;

    if (!jsonGetString(req, kReqId, reqId) || reqId.empty()) {
        reqId = stream_id;;
    }

    muxer::Input* input = new muxer::Input(std::make_shared<XLogger>(reqId), "");
    input->nativeRate_ = true;
    input->doResample_ = false;

    bool isPic{ false };
    if (jsonGetBool(req, "isPic", isPic) && isPic) {
        input->singleFrame_ = true;
        input->doRescale_ = true;
    }

    std::string url;
    if (!jsonGetString(req, "url", url)) {
        observer->OnFailure(errInvalidParams, "url");
        return;
    }

    //input->resampler_.frameSize = int(muxer::AudioResampler::SAMPLE_RATE * 0.01);
    input->Start(url);

    {
        std::lock_guard<std::mutex> lock(streams_map_lock_);
        streams_map_[stream_id] = input;
    }
    json res;
    res[kId] = stream_id;
    observer->OnSuccess(res);
}

void CmdHost::handleNewCanvasStream(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    CanvasStream *stream = new CanvasStream();

    int fps = 0;
    if (jsonGetInt(req, "fps", fps) && fps > 0) {
        stream->fps_.store(fps);
    }

    int w = 0;
    if (jsonGetInt(req, "w", w) && w > 0) {
        stream->w_.store(w);
    }

    int h = 0;
    if (jsonGetInt(req, "h", h) && h > 0) {
        stream->h_.store(h);
    }

    auto bgIt = req.find("bg");
    if (bgIt != req.end() && bgIt->is_number_integer()) {
        stream->bg_.store(bgIt->get<uint32_t>());
    }

    stream->Start();

    auto stream_id = newReqId();
    {
        std::lock_guard<std::mutex> lock(streams_map_lock_);
        streams_map_[stream_id] = stream;
    }
    json res;
    res[kId] = stream_id;
    observer->OnSuccess(res);
}

void CmdHost::handleConnAddStream(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == nullptr) {
        return;
    }

    Stream *stream = nullptr;
    std::string streamId;
    if (jsonGetString(req, kStreamId, streamId)) {
        stream = checkStream(streamId, observer);
    }
    if (stream == nullptr) {
        return;
    }

    std::vector<rtc::scoped_refptr<webrtc::MediaStreamTrackInterface>> tracks;
    if (!conn->AddStream(stream, tracks)) {
        observer->OnFailure(errInvalidParams, "add stream failed");
        return;
    }

    json res;
    for (auto track : tracks) {
        json v;
        v["id"] = track->id();
        v["kind"] = track->kind();
        res["tracks"].push_back(v);
    }

    observer->OnSuccess(res);
}

class ConnGetStatsObserver: public webrtc::RTCStatsCollectorCallback {
public:
    ConnGetStatsObserver(rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) : observer_(observer) {}

    void OnStatsDelivered(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
        json res;
        res["stats"] = report->ToJson();
        observer_->OnSuccess(res);
    }

    int AddRef() const { return 0; }
    int Release() const { return 0; }

    rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer_;
};

void CmdHost::handleConnStats(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == nullptr) {
        return;
    }
    conn->GetStats(new ConnGetStatsObserver(observer));
}

void CmdHost::handleNewRawStream(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto stream_id = newReqId();
    auto stream = new Stream();
    {
        std::lock_guard<std::mutex> lock(streams_map_lock_);
        streams_map_[stream_id] = stream;
    }
    json res;
    res[kId] = stream_id;
    observer->OnSuccess(res);
}

void CmdHost::handleRawStreamSendPacket(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    Stream *stream = nullptr;
    std::string id;
    if (jsonGetString(req, kId, id)) {
        stream = checkStream(id, observer);
    }
    if (stream == nullptr) {
        return;
    }

    auto rawpktIt = req.find("rawpkt");
    if (rawpktIt != req.end() && rawpktIt->is_binary()) {
        auto& rawpktVector = rawpktIt->get_binary();
        if(rawpktVector.size()>0){ // To avoid bad access
            stream->SendFrame(std::make_shared<muxer::MediaFrame>((char *)std::addressof(rawpktVector[0]), rawpktVector.size()));
            observer->OnSuccess();
        }else{
            observer->OnFailure(errInvalidParams, "no rawpkt sent");
        }
    }
}

void CmdHost::handleSinkStats(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    Stream *stream = nullptr;
    std::string id;
    if (jsonGetString(req, kId, id)) {
        stream = checkStream(id, observer);
    }
    if (stream == nullptr) {
        return;
    }

    SinkObserver *sink = nullptr;
    std::string sinkId;
    if (jsonGetString(req, kSinkId, sinkId)) {
        sink = stream->FindSink(sinkId);
    }
    if (sink == nullptr) {
        observer->OnFailure(errInvalidParams, "sink not found");
        return;
    }

    int64_t bytes = 0;
    sink->OnStatBytes(bytes);

    json res;
    res["bytes"] = int(bytes);
    observer->OnSuccess(res);
}

void CmdHost::handleRequestKeyFrame(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    Stream *stream = nullptr;
    std::string id;
    if (jsonGetString(req, kId, id)) {
        stream = checkStream(id, observer);
    }
    if (stream == nullptr) {
        return;
    }

    SinkObserver *sink = nullptr;
    std::string sinkId;
    if (jsonGetString(req, kSinkId, sinkId)) {
        sink = stream->FindSink(sinkId);
    }
    if (sink == nullptr) {
        observer->OnFailure(errInvalidParams, "sink not found");
        return;
    }

    sink->SetRequestKeyFrame(true);
    observer->OnSuccess();
}

void CmdHost::handleSinkDontReconnect(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    Stream *stream = nullptr;
    std::string id;
    if (jsonGetString(req, kId, id)) {
        stream = checkStream(id, observer);
    }
    if (stream == nullptr) {
        return;
    }

    SinkObserver *sink = nullptr;
    std::string sinkId;
    if (jsonGetString(req, kSinkId, sinkId)) {
        sink = stream->FindSink(sinkId);
    }
    if (sink == nullptr) {
        observer->OnFailure(errInvalidParams, "sink not found");
        return;
    }

    auto rtmpSink = static_cast<muxer::RtmpSink *>(sink);

    bool isDontReconnect{ false }; // = req.find("dontReconnect");
    if (jsonGetBool(req, "dontReconnect", isDontReconnect)) {
        rtmpSink->dont_reconnect = isDontReconnect;
    }

    observer->OnSuccess();
}

void CmdHost::handleSinkSEIKey(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    Stream *stream = nullptr;
    std::string id;
    if (jsonGetString(req, kId, id)) {
        stream = checkStream(id, observer);
    }
    if (stream == nullptr) {
        return;
    }

    SinkObserver *sink = nullptr;
    std::string sinkId;
    if (jsonGetString(req, kSinkId, sinkId)) {
        sink = stream->FindSink(sinkId);
    }
    if (sink == nullptr) {
        observer->OnFailure(errInvalidParams, "sink not found");
        return;
    }

    std::string seiKey ;
    if (!jsonGetString(req, kSEIKey, seiKey)) {
        observer->OnFailure(errInvalidParams, "sei key not found");
        return;
    }

    auto rtmpSink = static_cast<muxer::RtmpSink *>(sink);
    if (rtmpSink != nullptr) {
        rtmpSink->SetSeiKey(seiKey);
    } else {
        observer->OnFailure(errInvalidParams, "rtmp sink not found");
        return;
    }

    observer->OnSuccess();
}

class CmdDoneWriteResObserver: public CmdHost::CmdDoneObserver {
public:
    CmdDoneWriteResObserver(rtc::scoped_refptr<MsgPump::Request> req) : req_(req) {}
    void OnSuccess(json& res) {
        res[kCode] = 0;
        req_->WriteResponse(res);
    }
    void OnFailure(int code, const std::string& error) {
        json res;
        res[kCode] = code;
        res[kError] = error;
        req_->WriteResponse(res);
    }
    rtc::scoped_refptr<MsgPump::Request> req_;
};

void CmdHost::handleMsg(const std::string& type, const json& body) {
}

void CmdHost::handleReq(rtc::scoped_refptr<MsgPump::Request> req) {
    auto type = req->type;
    if (type == mtEcho) {
        json res = req->body;
        req->WriteResponse(res);
    } else if (type == mtNewConn) {
        handleNewConn(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtCreateOffer) {
        handleCreateOffer(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtCreateOfferSetLocalDesc) {
        handleCreateOfferSetLocalDesc(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtSetRemoteDesc) {
        handleSetRemoteDesc(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtSetLocalDesc) {
        handleSetLocalDesc(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtSetRemoteDescCreateAnswer) {
        handleSetRemoteDescCreateAnswer(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtAddIceCandidate) {
        handleAddIceCandidate(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtNewLibMuxer) {
        handleNewLibmuxer(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtLibmuxerReplaceAllInputs) {
        handleLibmuxerReplaceAllInputs(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtLibmuxerAddInput) {
        handleLibmuxerAddInput(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtLibmuxerSetInputsOpt) {
        handleLibmuxerSetInputsOpt(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtLibmuxerRemoveInput) {
        handleLibmuxerRemoveInput(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtStreamAddSink) {
        handleStreamAddSink(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtStreamRemoveSink) {
        handleStreamRemoveSink(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtNewCanvasStream) {
        handleNewCanvasStream(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtConnAddStream) {
        handleConnAddStream(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtNewUrlStream) {
        handleNewUrlStream(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtConnStats) {
        handleConnStats(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtNewRawStream) {
        handleNewRawStream(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtRawStreamSendPacket) {
        handleRawStreamSendPacket(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtSinkStats) {
        handleSinkStats(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtRequestKeyFrame) {
        handleRequestKeyFrame(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtSinkDontReconnect) {
        handleSinkDontReconnect(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    } else if (type == mtSinkSEIKey) {
        handleSinkSEIKey(req->body, new rtc::RefCountedObject<CmdDoneWriteResObserver>(req));
    }
}

void CmdHost::OnRtmpSinkStatus(std::string id, std::string connectStatus) {
    json res;
    res[kSinkId] = id;
    res["status"] = connectStatus;
    this->writeMessage(mtOnSinkStatus, res);
}

class MsgPumpObserver: public MsgPump::Observer {
public:
    MsgPumpObserver(CmdHost *h) : h_(h) {}
    void OnRequest(rtc::scoped_refptr<MsgPump::Request> req) {
        h_->handleReq(req);
    }
    void OnMessage(const std::string& type, const json& body) {
        h_->handleMsg(type, body);
    }
    CmdHost* h_;
};

CmdHost::CmdHost(): conn_map_() {
    msgpump_ = new MsgPump(new MsgPumpObserver(this));
}
