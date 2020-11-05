#include "cmdhost.hpp"
#include "stream.hpp"
#include "output.hpp"
#include <stdlib.h>

#include "rtc_base/base64.h"

const std::string kReqid = "reqid";
const std::string kId = "id";
const std::string kStreamId = "stream_id";
const std::string kSinkId = "sink_id";
const std::string kSupportSEI = "supportsei";
const std::string kSEIKey = "sei_key";
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
const std::string mtLibmuxerSetInputsOpt = "libmuxer-set-inputs-opt";
const std::string mtLibmuxerRemoveInput = "libmuxer-remove-input";
const std::string mtStreamAddSink = "stream-add-sink";
const std::string mtStreamRemoveSink = "stream-remove-sink";
const std::string mtNewCanvasStream = "new-canvas-stream";
const std::string mtNewUrlStream = "new-url-stream";
const std::string mtConnAddStream = "conn-add-stream";
const std::string mtConnStats = "conn-stats";
const std::string mtNewRawStream = "new-raw-stream";
const std::string mtSinkRawpkt = "on-sink-rawpkt";
const std::string mtRawStreamSendPacket = "raw-stream-send-packet";
const std::string mtSinkStats = "sink-stats";
const std::string mtRequestKeyFrame = "request-key-frame";
const std::string mtSinkDontReconnect = "stream-sink-dont-reconnect";
const std::string mtSinkSEIKey = "stream-sink-sei-key";

static void parseOfferAnswerOpt(const json& v, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions& opt) {
    auto audioIt = v.find("audio");
    if (audioIt != v.end() && audioIt->is_boolean() && audioIt->get<bool>()) {
        opt.offer_to_receive_audio = 1;
    }

    auto videoIt = v.find("video");
    if (videoIt != v.end() && videoIt->is_boolean() && videoIt->get<bool>()) {
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

    auto rawpktIt = req.find("rawpkt");
    if (rawpktIt != req.end() && rawpktIt->is_boolean() && rawpktIt->get<bool>()) {
        rtcconf.set_rawpkt(true);
    }

    auto dumpRawpktIt = req.find("dumpRawpkt");
    if (dumpRawpktIt != req.end() && dumpRawpktIt->is_boolean() && dumpRawpktIt->get<bool>()) {
        rtcconf.set_dump_rawpkt(true);
    }

    int min_port = 0;
    int max_port = 0;

    auto minPortIt = req.find("minPort");
    if (minPortIt != req.end() && minPortIt->is_number_integer()) {
        min_port = minPortIt->get<int>();
    }

    auto maxPortIt = req.find("maxPort");
    if (maxPortIt != req.end() && maxPortIt->is_number_integer()) {
        max_port = maxPortIt->get<int>();
    }

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
    Stream *stream = NULL;
    {
        std::lock_guard<std::mutex> lock(streams_map_lock_);
        auto it = streams_map_.find(id);
        if (it != streams_map_.end()) {
            stream = it->second;
        }
    }
    if (stream == NULL) {
        observer->OnFailure(errStreamNotFound, errStreamNotFoundString);
        return NULL;
    }
    return stream;
}

WRTCConn* CmdHost::checkConn(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto idIt = req.find(kId);
    if (idIt == req.end() || !idIt->is_string()) {
        observer->OnFailure(errInvalidParams, errInvalidParamsString);
        return NULL;
    }

    WRTCConn *conn = NULL;
    {
        std::lock_guard<std::mutex> lock(conn_map_lock_);
        auto it = conn_map_.find(idIt->get<std::string>());
        if (it != conn_map_.end()) {
            conn = it->second;
        }
    }
    if (conn == NULL) {
        observer->OnFailure(errConnNotFound, errConnNotFoundString);
        return NULL;
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
    if (conn == NULL) {
        return;
    }

    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions offeropt = {};
    parseOfferAnswerOpt(req, offeropt);
    conn->CreateOffer(offeropt, new rtc::RefCountedObject<CreateDescObserver>(observer));
}

void CmdHost::handleCreateOfferSetLocalDesc(const json& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == NULL) {
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
    if (conn == NULL) {
        return;
    }

    std::string type = "offer";
    auto typeIt = req.find("type");
    if (typeIt != req.end() && typeIt->is_string() && !typeIt->get<std::string>().empty()) {
        type = typeIt->get<std::string>();
    }

    auto sdpIt = req.find(kSdp);
    if (sdpIt == req.end() || !sdpIt->is_string()) {
        return;
    }

    webrtc::SdpParseError err;
    auto desc = CreateSessionDescription(type, sdpIt->get<std::string>(), &err); 
    if (!desc) {
        observer->OnFailure(errInvalidParams, err.description);
        return;
    }

    conn->SetLocalDesc(desc, new rtc::RefCountedObject<SetDescObserver>(observer));
}

void CmdHost::handleSetRemoteDesc(const json& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == NULL) {
        return;
    }

    std::string type = "answer";
    auto typeIt = req.find("type");
    if (typeIt != req.end() && typeIt->is_string() && !typeIt->get<std::string>().empty()) {
        type = typeIt->get<std::string>();
    }

    auto sdpIt = req.find(kSdp);
    if (sdpIt == req.end() || !sdpIt->is_string()) {
        return;
    }

    webrtc::SdpParseError err;
    auto desc = CreateSessionDescription(type, sdpIt->get<std::string>(), &err);
    if (!desc) {
        observer->OnFailure(errInvalidParams, err.description);
        return;
    }

    conn->SetRemoteDesc(desc, new rtc::RefCountedObject<SetDescObserver>(observer));
}

void CmdHost::handleSetRemoteDescCreateAnswer(const json& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    auto conn = checkConn(req, observer);
    if (conn == NULL) {
        return;
    }

    auto sdpIt = req.find(kSdp);
    if (sdpIt == req.end() || !sdpIt->is_string()) {
        return;
    }

    webrtc::SdpParseError err;
    auto desc = CreateSessionDescription("offer", sdpIt->get<std::string>(), &err);
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
    if (conn == NULL) {
        return;
    }

    auto csIt = req.find(kCandidate);
    if (csIt == req.end() || !csIt->is_string()) {
        observer->OnFailure(errInvalidParams, "parse req failed");
        return;
    }

    json c = json::parse(csIt->get<std::string>());
    if (c.is_discarded()) {
        observer->OnFailure(errInvalidParams, "parse candidate failed");
        return;
    }

    auto sdpMidIt = c.find(kSdpMid);
    if (sdpMidIt == c.end() || !sdpMidIt->is_string()) {
        observer->OnFailure(errInvalidParams, "parse kSdpMid failed");
        return;
    }

    auto sdpMlineIndexIt = c.find(kSdpMLineIndex);
    if (sdpMlineIndexIt == c.end() || !sdpMlineIndexIt->is_string()) {
        observer->OnFailure(errInvalidParams, "parse kSdpMLineIndex failed");
        return;
    }
    int sdp_mlineindex = atoi(sdpMlineIndexIt->get<std::string>().c_str());

    auto candidateIt = c.find(kCandidate);
    if (candidateIt == c.end() || !candidateIt->is_string()) {
        observer->OnFailure(errInvalidParams, "parse kCandidate failed");
        return;
    }

    webrtc::SdpParseError err;
    auto candidate = webrtc::CreateIceCandidate(sdpMidIt->get<std::string>(), sdp_mlineindex, sdpMlineIndexIt->get<std::string>(), &err);
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
    auto idIt = req.find(kId);
    if (idIt == req.end() || !idIt->is_string()) {
        observer->OnFailure(errInvalidParams, errInvalidParamsString);
        return NULL;
    }

    muxer::AvMuxer *m = NULL;
    {
        std::lock_guard<std::mutex> lock(muxers_map_lock_);
        auto it = muxers_map_.find(idIt->get<std::string>());
        if (it != muxers_map_.end()) {
            m = it->second;
        }
    }

    if (m == NULL) {
        observer->OnFailure(errMuxerNotFound, errMuxerNotFoundString);
        return NULL;
    }

    return m;
}

class LibmuxerOutputStream: public Stream {
public:
    LibmuxerOutputStream() {}
};

void CmdHost::handleNewLibmuxer(const json& req, rtc::scoped_refptr<CmdHost::CmdDoneObserver> observer) {
    int w = 0;
    int h = 0;

    auto wIt = req.find("w");
    if (wIt != req.end() && wIt->is_number_integer()) {
        w = wIt->get<int>();
    }
    auto hIt = req.find("h");
    if (hIt != req.end() && hIt->is_number_integer()) {
        h = hIt->get<int>();
    }
    if (w == 0 || h == 0) {
        observer->OnFailure(errInvalidParams, "invalid w or h");
        return;
    }

    auto reqid = jsonAsString(req[kReqid]);
    if (reqid == "") {
        reqid = newReqId();
    }

    auto m = new muxer::AvMuxer(std::make_shared<XLogger>(reqid), w, h);
    auto id = newReqId();
    {
        std::lock_guard<std::mutex> lock(muxers_map_lock_);
        muxers_map_[id] = m;
    }

    auto audioOnlyIt = req.find("audioOnly");
    if (audioOnlyIt != req.end() && audioOnlyIt->is_boolean()) {
        m->audioOnly_.store(audioOnlyIt->get<bool>());
    }

    auto fpsIt = req.find("fps");
    if (fpsIt != req.end() && fpsIt->is_number() && fpsIt->get<int>() > 0) {
        m->fps_ = fpsIt->get<int>();
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

    int w = 0;
    int h = 0;
    bool hidden = false;

    auto wIt = opt.find("w");
    if (wIt != opt.end() && wIt->is_number_integer()) {
        w = wIt->get<int>();
    }
    auto hIt = opt.find("h");
    if (hIt != opt.end() && hIt->is_number_integer()) {
        h = hIt->get<int>();
    }

    if (w == 0 || h == 0) {
        hidden = true;
    } else {
        auto hiddenIt = opt.find("hidden");
        if (hiddenIt != opt.end() && hiddenIt->is_boolean()) {
            hidden = hiddenIt->get<bool>();
        }
    }

    lin->SetOption("hidden", hidden);
    lin->SetOption("w", w);
    lin->SetOption("h", h);

    auto xIt = opt.find("x");
    if (xIt != opt.end() && xIt->is_number_integer()) {
        lin->SetOption("x", xIt->get<int>());
    }

    auto yIt = opt.find("y");
    if (yIt != opt.end() && yIt->is_number_integer()) {
        lin->SetOption("y", yIt->get<int>());
    }

    auto zIt = opt.find("z");
    if (zIt != opt.end() && zIt->is_number_integer()) {
        lin->SetOption("z", zIt->get<int>());
    }

    auto mutedIt = opt.find("muted");
    if (mutedIt != opt.end() && mutedIt->is_boolean()) {
        lin->SetOption("muted", mutedIt->get<bool>());
    }

    auto stretchModeIt = opt.find(muxer::options::stretchMode);
    if (stretchModeIt != opt.end() && stretchModeIt->is_string() && !stretchModeIt->get<std::string>().empty()) {
        auto s = stretchModeIt->get<std::string>();
        int mode = muxer::VideoRescaler::STRETCH_ASPECT_FILL;
        if (s == muxer::options::stretchAspectFill) {
            mode = muxer::VideoRescaler::STRETCH_ASPECT_FILL;
        } else if (s == muxer::options::stretchAspectFit) {
            mode = muxer::VideoRescaler::STRETCH_ASPECT_FIT;
        } else if (s == muxer::options::stretchScaleToFit) {
            mode = muxer::VideoRescaler::STRETCH_SCALE_TO_FIT;
        }
        lin->SetOption(muxer::options::stretchMode, mode);
    }
}

void CmdHost::handleLibmuxerAddInput(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto m = checkLibmuxer(req, observer);
    if (m == NULL) {
        return;
    }

    Stream *stream = nullptr;
    std::string id;
    auto streamIdIt = req.find(kStreamId);
    if (streamIdIt != req.end() && streamIdIt->is_string()) {
        id = streamIdIt->get<std::string>();
        stream = checkStream(id, observer);
    }
    if (stream == NULL) {
        return;
    }

    std::string key = "";
    auto support = jsonAsBool(req[kSupportSEI]);
    if (support) {
        auto reqid = jsonAsString(req[kReqid]);
        key = id + "." + reqid;
        std::list<AVPacket *>* queue = new std::list<AVPacket *>;
        auto iter = SeiQueues.find(key);
        if (iter != SeiQueues.end()) {
            if (iter->second) {
                free(iter->second);
            }
            SeiQueues.erase(iter);
        }
        SeiQueues.emplace(std::make_pair(key, queue));
        m->SetInputKey(id, key);
    }

    m->AddInput(id, stream);

    auto optIt = req.find("opt");
    if (optIt != req.end() && optIt->is_object()) {
        libmuxerSetInputOpt(m->FindInput(id), *optIt);
    }

    m->PrintInputs();

    json res;
    res[kId] = id;
    res[kSEIKey] = key;
    observer->OnSuccess(res);
}

void CmdHost::handleLibmuxerRemoveInput(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto m = checkLibmuxer(req, observer);
    if (m == NULL) {
        return;
    }

    auto id = jsonAsString(req[kStreamId]);

    auto key = m->GetInputKey(id);
    auto iter = SeiQueues.find(key);
    if (iter != SeiQueues.end()) {
        if (iter->second) {
            free(iter->second);
        }
        SeiQueues.erase(key);
    }

    m->RemoveInput(id);

    auto streamIdIt = req.find(kStreamId);
    if (streamIdIt != req.end() && streamIdIt->is_string()) {
        m->RemoveInput(streamIdIt->get<std::string>());
    }

    json res;
    observer->OnSuccess(res);
}

void CmdHost::handleLibmuxerSetInputsOpt(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto m = checkLibmuxer(req, observer);
    if (m == NULL) {
        return;
    }

    auto inputsIt = req.find("inputs");
    if (inputsIt != req.end() && inputsIt->is_array()) {
        for (const auto& input : *inputsIt) {
            auto idIt = input.find(kId);
            auto optIt = input.find("opt");
            if (idIt != input.end() && idIt->is_string() && optIt != input.end() && optIt->is_object()) {
                auto lin = m->FindInput(idIt->get<std::string>());
                if (lin != nullptr) {
                    libmuxerSetInputOpt(lin, *optIt);
                }
            }
        }
    }

    json res;
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
        h->writeMessage(mtSinkRawpkt, res);
    }

private:
    CmdHost *h;
    std::string sinkid;
};

void CmdHost::handleStreamAddSink(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto idIt = req.find(kId);
    if (idIt == req.end() || !idIt->is_string()) {
        observer->OnFailure(errInvalidParams, "id");
        return;
    }

    auto stream = checkStream(idIt->get<std::string>(), observer);
    if (stream == nullptr) {
        observer->OnFailure(errInvalidParams, "stream id");
        return;
    }

    auto sinkid = newReqId();

    auto rawIt = req.find("raw");
    if (rawIt != req.end() && rawIt->is_boolean() && rawIt->get<bool>()) {
        auto sink = new RawpktSink(this, sinkid);
        stream->AddSink(sinkid, sink);

        json res;
        res[kId] = sinkid;
        observer->OnSuccess(res);
        return;
    }

    auto urlIt = req.find("url");
    if (urlIt == req.end() || !(urlIt->is_string()) || urlIt->get<std::string>().empty()) {
        observer->OnFailure(errInvalidParams, "url invalid");
        return;
    }

    std::string reqid;
    auto reqidIt = req.find("reqid");
    if (reqidIt != req.end() && reqidIt->is_string() && !reqidIt->get<std::string>().empty()) {
        reqid = reqidIt->get<std::string>();
    } else {
        reqid = newReqId();
    }

    auto sink = new muxer::RtmpSink(urlIt->get<std::string>(), std::make_shared<XLogger>(reqid));

    auto kbpsIt = req.find("kbps");
    if (kbpsIt != req.end() && kbpsIt->is_number() && kbpsIt->get<int>() != 0) {
        sink->videoKbps = kbpsIt->get<int>();
    }

    auto minRateIt = req.find("minRate");
    if (minRateIt != req.end() && minRateIt->is_number() && minRateIt->get<int>() != 0) {
        sink->videoMinRate = minRateIt->get<int>();
    }

    auto maxRateIt = req.find("maxRate");
    if (maxRateIt != req.end() && maxRateIt->is_number() && maxRateIt->get<int>() != 0) {
        sink->videoMaxRate = maxRateIt->get<int>();
    }

    auto gopIt = req.find("gop");
    if (gopIt != req.end() && gopIt->is_number() && gopIt->get<int>() != 0) {
        sink->videoGop = gopIt->get<int>();
    }

    auto fpsIt = req.find("fps");
    if (fpsIt != req.end() && fpsIt->is_number() && fpsIt->get<int>() != 0) {
        sink->videoFps = fpsIt->get<int>();
    }

    stream->AddSink(sinkid, sink);

    json res;
    res[kId] = sinkid;
    observer->OnSuccess(res);
}

void CmdHost::handleStreamRemoveSink(const json& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto idIt = req.find(kId);
    if (idIt == req.end() || !idIt->is_string()) {
        observer->OnFailure(errInvalidParams, "id");
        return;
    }

    auto stream = checkStream(idIt->get<std::string>(), observer);
    if (stream == nullptr) {
        return;
    }

    auto sinkIdIt = req.find("sinkId");
    if (sinkIdIt != req.end()
      && sinkIdIt->is_string()
      && !stream->RemoveSink(sinkIdIt->get<std::string>()))
    {
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
    std::string reqid;

    auto reqidIt = req.find("reqid");
    if (reqidIt != req.end() && reqidIt->is_string() && !reqidIt->get<std::string>().empty()) {
        reqid = reqidIt->get<std::string>();
    } else {
        reqid = stream_id;
    }

    muxer::Input* input = new muxer::Input(std::make_shared<XLogger>(reqid), "");
    input->nativeRate_ = true;
    input->doResample_ = false;

    auto isPicIt = req.find("isPic");
    if (isPicIt != req.end() && isPicIt->is_boolean() && isPicIt->get<bool>()) {
        input->singleFrame_ = true;
        input->doRescale_ = true;
    }

    auto urlIt = req.find("url");
    if (urlIt == req.end() || !urlIt->is_string()) {
        observer->OnFailure(errInvalidParams, "url");
        return;
    }
    //input->resampler_.frameSize = int(muxer::AudioResampler::SAMPLE_RATE * 0.01);
    input->Start(urlIt->get<std::string>());

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

    auto fpsIt = req.find("fps");
    if (fpsIt != req.end() && fpsIt->is_number_integer() && fpsIt->get<int>() > 0) {
        stream->fps_.store(fpsIt->get<int>());
    }

    auto wIt = req.find("w");
    if (wIt != req.end() && wIt->is_number_integer() && wIt->get<int>() > 0) {
        stream->w_.store(wIt->get<int>());
    }

    auto hIt = req.find("h");
    if (hIt != req.end() && hIt->is_number_integer() && hIt->get<int>() > 0) {
        stream->h_.store(hIt->get<int>());
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
    auto streamIdIt = req.find("streamId");
    if (streamIdIt != req.end() && streamIdIt->is_string()) {
        stream = checkStream(streamIdIt->get<std::string>(), observer);
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
    if (conn == NULL) {
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
    auto idIt = req.find(kId);
    if (idIt != req.end() && idIt->is_string()) {
        stream = checkStream(idIt->get<std::string>(), observer);
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
    auto idIt = req.find(kId);
    if (idIt != req.end() && idIt->is_string()) {
        stream = checkStream(idIt->get<std::string>(), observer);
    }
    if (stream == nullptr) {
        return;
    }

    SinkObserver *sink = nullptr;
    auto sinkIdIt = req.find(kSinkId);
    if (sinkIdIt != req.end() && sinkIdIt->is_string()) {
        sink = stream->FindSink(sinkIdIt->get<std::string>());
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
    auto idIt = req.find(kId);
    if (idIt != req.end() && idIt->is_string()) {
        stream = checkStream(idIt->get<std::string>(), observer);
    }
    if (stream == nullptr) {
        return;
    }

    SinkObserver *sink = nullptr;
    auto sinkIdIt = req.find(kSinkId);
    if (sinkIdIt != req.end() && sinkIdIt->is_string()) {
        sink = stream->FindSink(sinkIdIt->get<std::string>());
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
    auto idIt = req.find(kId);
    if (idIt != req.end() && idIt->is_string()) {
        stream = checkStream(idIt->get<std::string>(), observer);
    }
    if (stream == nullptr) {
        return;
    }

    SinkObserver *sink = nullptr;
    auto sinkIdIt = req.find(kSinkId);
    if (sinkIdIt != req.end() && sinkIdIt->is_string()) {
        sink = stream->FindSink(sinkIdIt->get<std::string>());
    }
    if (sink == nullptr) {
        observer->OnFailure(errInvalidParams, "sink not found");
        return;
    }

    auto rtmpSink = static_cast<muxer::RtmpSink *>(sink);

    auto dontReconnectIt = req.find("dontReconnect");
    if (dontReconnectIt != req.end() && dontReconnectIt->is_boolean() && rtmpSink != nullptr) {
        rtmpSink->dont_reconnect = dontReconnectIt->get<bool>();
    }

    observer->OnSuccess();
}

void CmdHost::handleSinkSEIKey(const Json::Value& req, rtc::scoped_refptr<CmdDoneObserver> observer) {
    auto stream = checkStream(jsonAsString(req[kId]), observer);
    if (stream == NULL) {
        return;
    }

    auto sink = stream->FindSink(jsonAsString(req[kSinkId]));
    if (sink == NULL) {
        observer->OnFailure(errInvalidParams, "sink not found");
        return;
    }

    auto rtmpSink = static_cast<muxer::RtmpSink *>(sink);
    if (rtmpSink != NULL) {
        rtmpSink->SetSeiKey(jsonAsString(req[kSEIKey]));
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
