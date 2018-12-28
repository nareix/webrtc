
#include "wrtcconn.hpp"
#include "input.hpp"
#include "output.hpp"

#include "rtc_base/callback.h"
#include "media/base/videobroadcaster.h"
#include "pc/videotracksource.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "media/base/videocommon.h"
#include "rtc_base/timeutils.h"
#include "pc/videotrack.h"
#include "pc/mediastream.h"
#include "rtc_base/bytebuffer.h"
#include "rtc_base/base64.h"
#include "rtc_base/stringencode.h"

static bool streamOnFrameConvAAC = true;

class RawpktAudio {
public:
    RawpktAudio() {}

    RawpktAudio(AVFrame *f) {
        audiodata = f->data[0];
        samplebits = av_get_bytes_per_sample((AVSampleFormat)f->format)*8;
        samplenr = f->nb_samples;
        channels = f->channels;
        samplerate = f->sample_rate;
    }

    void Marshall(rtc::ByteBufferWriter& b) {
        if (audiodata) {
            b.WriteUInt8(30);
            b.WriteUInt32(4*4);
            b.WriteUInt32(samplebits);
            b.WriteUInt32(samplerate);
            b.WriteUInt32(channels);
            b.WriteUInt32(samplenr);
            b.WriteUInt8(31);
            b.WriteUInt32(audiolen);
            b.WriteBytes((const char *)audiodata, audiolen);
        }
        if (extradata.size() > 0) {
            b.WriteUInt8(32);
            b.WriteUInt32(extradata.size());
            b.WriteBytes((const char *)&extradata[0], extradata.size());
        }
        if (data.size() > 0) {
            b.WriteUInt8(33);
            b.WriteUInt32(data.size());
            b.WriteBytes((const char *)&data[0], data.size());
        }
    }

    bool Unmarshall(uint8_t type, rtc::ByteBufferReader& b) {
        switch (type) {
        case 30:  // audio info
            b.ReadUInt32(&samplebits);
            b.ReadUInt32(&samplerate);
            b.ReadUInt32(&channels);
            b.ReadUInt32(&samplenr);
            return true;
        case 31:  // audio data
            audiodata = b.Data();
            audiolen = b.Length();
            return true;
        case 32: // encoded audio extradata
            extradata = std::vector<uint8_t>(b.Data(), b.Data()+b.Length());
            return true;
        case 33: // encoded audio data
            data = std::vector<uint8_t>(b.Data(), b.Data()+b.Length());
            return true;
        }
        return false;
    }

    uint32_t samplebits = 0;
    uint32_t samplerate = 0;
    uint32_t channels = 0;
    uint32_t samplenr = 0;
    const void *audiodata = nullptr;
    size_t audiolen = 0;

    std::vector<uint8_t> data = std::vector<uint8_t>();
    std::vector<uint8_t> extradata = std::vector<uint8_t>();
};


class WRTCStream: public Stream, rtc::VideoSinkInterface<webrtc::VideoFrame>, webrtc::AudioTrackSinkInterface {
public:
    WRTCStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream, const std::string& id, bool rawpkt) 
        : id_(id), rawpkt(rawpkt) 
    {
        webrtc::VideoTrackVector vtracks = stream->GetVideoTracks();
        webrtc::AudioTrackVector atracks = stream->GetAudioTracks();
        if (!vtracks.empty()) {
            webrtc::VideoTrackInterface* track = vtracks[0];
            DebugR("AddVideoTrackSink state=%d enabled=%d", id.c_str(), track->state(), track->enabled());
            track->AddOrUpdateSink(this, rtc::VideoSinkWants());
        }
        if (!atracks.empty()) {
            webrtc::AudioTrackInterface *track = atracks[0];
            DebugR("AddAudioSink", id.c_str());
            track->AddSink(this);
        }

        start_ts_ = std::chrono::high_resolution_clock::now();
        audio_ts_ = start_ts_;
    }

    void OnFrame(const webrtc::VideoFrame& rtcframe) {
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::ratio<1,1>> elapsed_d(now - start_ts_);

        if (rawpkt) {
            DebugR("OnFrameVideoRawpkt %zu", id_.c_str(), rtcframe.rawpkt->size());

            std::shared_ptr<muxer::MediaFrame> frame = std::make_shared<muxer::MediaFrame>(*rtcframe.rawpkt);
            SendFrame(frame);
            return;
        }

        DebugR("OnFrameVideo ts=%lf", id_.c_str(), elapsed_d.count());

        if (rtcframe.width() == 0 || rtcframe.height() == 0) {
            return;
        }

        auto rtcfb = rtcframe.video_frame_buffer();
        auto i420 = rtcfb->ToI420();
        if (rtcframe.rotation() != webrtc::kVideoRotation_0) {
            i420 = webrtc::I420Buffer::Rotate(*i420, rtcframe.rotation());
        }

        std::shared_ptr<muxer::MediaFrame> frame = std::make_shared<muxer::MediaFrame>();
        frame->Stream(muxer::STREAM_VIDEO);
        frame->Codec(muxer::CODEC_H264);
        frame->AvFrame()->format = AV_PIX_FMT_YUV420P;
        frame->AvFrame()->height = i420->height();
        frame->AvFrame()->width = i420->width();
        frame->AvFrame()->pts = int(elapsed_d.count()*1000);
        av_frame_get_buffer(frame->AvFrame(), 32);

        const uint8_t* rtcdata[3] = {
            i420->DataY(),
            i420->DataU(),
            i420->DataV(),
        };
        int rtclinesize[3] = {
            i420->StrideY(),
            i420->StrideU(),
            i420->StrideV(),
        };
        int height[3] = {
            i420->height(),
            i420->ChromaHeight(),
            i420->ChromaHeight(),
        };

        for (int i = 0; i < 3; i++) {
            yuv::CopyLine(frame->AvFrame()->data[i], frame->AvFrame()->linesize[i], 
                    rtcdata[i], rtclinesize[i], height[i]
            );
        }

        SendFrame(frame);
    }

    std::shared_ptr<muxer::MediaFrame> getAudioFrame(const void* audio_data,
        int bits_per_sample,
        int sample_rate,
        size_t number_of_channels,
        size_t number_of_frames) 
    {
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::ratio<1,1>> diff_d(now - audio_ts_);
        if (diff_d.count() > 0.2) {
            audio_ts_ = now;
        }
        std::chrono::duration<double, std::ratio<1,1>> elapsed_d(audio_ts_ - start_ts_);
        std::chrono::nanoseconds inc((long long)(1e9 / (double)sample_rate * (double)number_of_frames));
        audio_ts_ += inc;

        std::chrono::duration<double, std::ratio<1,1>> elapsed2_d(now - start_ts_);

        std::shared_ptr<muxer::MediaFrame> frame = std::make_shared<muxer::MediaFrame>();

        uint64_t channel_layout = AV_CH_LAYOUT_MONO;
        if (number_of_channels == 2) {
            channel_layout = AV_CH_LAYOUT_STEREO;
        }

        frame->Stream(muxer::STREAM_AUDIO);
        frame->Codec(muxer::CODEC_AAC);
        frame->AvFrame()->format = AV_SAMPLE_FMT_S16;
        frame->AvFrame()->channel_layout = channel_layout;
        frame->AvFrame()->sample_rate = sample_rate;
        frame->AvFrame()->channels = number_of_channels;
        frame->AvFrame()->nb_samples = number_of_frames;
        frame->AvFrame()->pts = int(elapsed_d.count()*1000);
        av_frame_get_buffer(frame->AvFrame(), 0);

        memcpy(frame->AvFrame()->data[0], audio_data, bits_per_sample/8*number_of_frames*number_of_channels);

        return frame;        
    }

    void OnData(const void* audio_data,
        int bits_per_sample,
        int sample_rate,
        size_t number_of_channels,
        size_t number_of_frames) 
    {
        if (rawpkt) {
            if (streamOnFrameConvAAC) {
                if (encoder == nullptr) {
                    encoder = std::make_shared<muxer::AvEncoder>(nullptr);
                    encoder->useGlobalHeader = true;
                    resampler = std::make_shared<muxer::AudioResampler>(nullptr);
                }

                auto encodeCb = [&](IN const std::shared_ptr<muxer::MediaPacket>& pkt) -> int {
                    rtc::ByteBufferWriter b;

                    DebugR("OnFrameAudioRawpktEncoded", id_.c_str());

                    b.WriteUInt8(1);
                    b.WriteUInt32(1);
                    b.WriteUInt8(3); // encoded audio

                    RawpktAudio audio;
                    audio.data = std::vector<uint8_t>(pkt->Data(), pkt->Data()+pkt->Size());
                    audio.extradata = encoder->Extradata();

                    audio.Marshall(b);
                    auto frame = std::make_shared<muxer::MediaFrame>(std::string(b.Data(), b.Length()));
                    SendFrame(frame);

                    return 0;
                };

                auto resampleCb = [&](const std::shared_ptr<muxer::MediaFrame>& out) -> int {
                    std::shared_ptr<muxer::MediaFrame> frame = out;
                    return encoder->Encode(frame, encodeCb);
                };

                auto frame = getAudioFrame(audio_data, bits_per_sample, sample_rate, number_of_channels, number_of_frames);
                resampler->Resample(frame, resampleCb);
            } else {
                DebugR("OnFrameAudioRawpkt", id_.c_str());

                rtc::ByteBufferWriter b;
                RawpktAudio audio;

                b.WriteUInt8(1);
                b.WriteUInt32(1);
                b.WriteUInt8(2); // raw audio
                audio.audiodata = audio_data;
                audio.audiolen = bits_per_sample/8*number_of_frames*number_of_channels;
                audio.samplebits = bits_per_sample;
                audio.samplenr = number_of_frames;
                audio.samplerate = sample_rate;
                audio.channels = number_of_channels;

                audio.Marshall(b);
                auto buf = std::string(b.Data(), b.Length());
                auto frame = std::make_shared<muxer::MediaFrame>(buf);
                SendFrame(frame);
            }

            return;
        }

        auto frame = getAudioFrame(audio_data, bits_per_sample, sample_rate, number_of_channels, number_of_frames);

        DebugR("OnFrameAudio %zu %d %d %zu",
            id_.c_str(), number_of_frames, sample_rate, bits_per_sample, number_of_channels
            );

        SendFrame(frame);
        DebugPCM("/tmp/rtc.orig.s16", audio_data, bits_per_sample/8*number_of_frames*number_of_channels);
    }

    std::chrono::high_resolution_clock::time_point start_ts_, audio_ts_;
    std::string id_;
    std::shared_ptr<muxer::AvEncoder> encoder = nullptr;
    std::shared_ptr<muxer::AudioResampler> resampler = nullptr;
    bool rawpkt = false;
};

class PeerConnectionObserver: public webrtc::PeerConnectionObserver {
public:
    PeerConnectionObserver(
        const std::string& id, WRTCConn::ConnObserver* conn_observer,
        webrtc::PeerConnectionInterface::RTCConfiguration rtcconf
    ) : id_(id), conn_observer_(conn_observer), rtcconf(rtcconf) {}
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) {
        //Info("OnSignalingChange");
    }
    void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
        webrtc::VideoTrackVector vtracks = stream->GetVideoTracks();
        webrtc::AudioTrackVector atracks = stream->GetAudioTracks();
        InfoR("OnAddStream vtracks=%lu atracks=%lu id=%s", id_.c_str(), vtracks.size(), atracks.size(), stream->label().c_str());

        conn_observer_->OnAddStream(id_, stream->label(), new WRTCStream(stream, stream->label(), rtcconf.rawpkt()));
    }
    void OnAddTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver, 
            const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) 
    {
        InfoR("OnAddTrack streams=%lu", id_.c_str(), streams.size());
        if (streams.size() == 0) {
            return;
        }
        //auto stream = streams[0];
        //OnAddStream(stream);
    }
    void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
        conn_observer_->OnRemoveStream(id_, stream->label());
    }
    void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) {}
    void OnRenegotiationNeeded() {}
    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) {
        conn_observer_->OnIceConnectionChange(new_state);
    }
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) {}
    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
        conn_observer_->OnIceCandidate(candidate);
    }
    virtual ~PeerConnectionObserver() {}
    std::string id_;
    WRTCConn::ConnObserver* conn_observer_;
    webrtc::PeerConnectionInterface::RTCConfiguration rtcconf;
};

std::string WRTCConn::ID() {
    return id_;
}

WRTCConn::WRTCConn(
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory, 
    webrtc::PeerConnectionInterface::RTCConfiguration rtcconf,
    WRTCConn::ConnObserver* conn_observer,
    rtc::Thread* signal_thread
) {
    id_ = newReqId();
    pc_factory_ = pc_factory;
    pc_ = pc_factory->CreatePeerConnection(rtcconf, nullptr, nullptr, nullptr, new PeerConnectionObserver(id_, conn_observer, rtcconf));
    signal_thread_ = signal_thread;
    rtcconf = rtcconf;
}

class SetLocalDescObserver: public webrtc::SetSessionDescriptionObserver {
public:
    SetLocalDescObserver(
        rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer,
        std::string desc
    ) : observer_(observer), desc_(desc) {}

    void OnSuccess() {
        observer_->OnSuccess(desc_);
    }

    void OnFailure(const std::string& error) {
        observer_->OnFailure(error);
    }

    ~SetLocalDescObserver() {}

    rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer_;
    std::string desc_;
};

class CreateOfferSetDescObserver: public webrtc::CreateSessionDescriptionObserver {
public:
    CreateOfferSetDescObserver(
        rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
        rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer
    ) : pc_(pc), observer_(observer) {}

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) {
        std::string descstr;
        desc->ToString(&descstr);
        pc_->SetLocalDescription(new rtc::RefCountedObject<SetLocalDescObserver>(observer_, descstr), desc);
    }

    void OnFailure(const std::string& error) {
        observer_->OnFailure(error);
    }

    ~CreateOfferSetDescObserver() {}

    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
    rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer_;
};

class CreateOfferObserver: public webrtc::CreateSessionDescriptionObserver {
public:
    CreateOfferObserver(
        rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
        rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer
    ) : pc_(pc), observer_(observer) {}

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) {
        std::string descstr;
        desc->ToString(&descstr);
        observer_->OnSuccess(descstr);
    }

    void OnFailure(const std::string& error) {
        observer_->OnFailure(error);
    }

    ~CreateOfferObserver() {}

    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
    rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer_;
};

void WRTCConn::SetLocalDesc(
    webrtc::SessionDescriptionInterface* desc,
    rtc::scoped_refptr<WRTCConn::SetDescObserver> observer
) {
    pc_->SetLocalDescription(observer, desc);
}

void WRTCConn::CreateOffer(
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions offeropt, 
    rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer
) {
    pc_->CreateOffer(new rtc::RefCountedObject<CreateOfferObserver>(pc_, observer), offeropt);
}

void WRTCConn::CreateOfferSetLocalDesc(
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions offeropt, 
    rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer
) {
    pc_->CreateOffer(new rtc::RefCountedObject<CreateOfferSetDescObserver>(pc_, observer), offeropt);
}

void WRTCConn::SetRemoteDesc(
    webrtc::SessionDescriptionInterface* desc,
    rtc::scoped_refptr<WRTCConn::SetDescObserver> observer
) {
    pc_->SetRemoteDescription(observer, desc);
}

class CreateAnswerObserver: public webrtc::CreateSessionDescriptionObserver {
public:
    CreateAnswerObserver(
        rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
        rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer
    ) : pc_(pc), observer_(observer) {}

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) {
        std::string descstr;
        desc->ToString(&descstr);
        pc_->SetLocalDescription(new rtc::RefCountedObject<SetLocalDescObserver>(observer_, descstr), desc);        
    }

    void OnFailure(const std::string& error) {
        observer_->OnFailure(error);
    }   

    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
    rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer_;
};

class SetDescCreateAnswerObserver: public webrtc::SetSessionDescriptionObserver {
public:
    SetDescCreateAnswerObserver(
        rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
        rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer,
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions answeropt
    ) : pc_(pc), observer_(observer), answeropt_(answeropt) {}

    void OnSuccess() {
        pc_->CreateAnswer(new rtc::RefCountedObject<CreateAnswerObserver>(pc_, observer_), answeropt_);
    }

    void OnFailure(const std::string& error) {
        observer_->OnFailure(error);
    }

    ~SetDescCreateAnswerObserver() {}

    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
    rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer_;
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions answeropt_;
};

void WRTCConn::SetRemoteDescCreateAnswer(
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions answeropt, 
    webrtc::SessionDescriptionInterface* desc,
    rtc::scoped_refptr<WRTCConn::CreateDescObserver> observer
) {
    pc_->SetRemoteDescription(new rtc::RefCountedObject<SetDescCreateAnswerObserver>(pc_, observer, answeropt), desc);
}

bool WRTCConn::AddIceCandidate(webrtc::IceCandidateInterface* candidate) {
    return pc_->AddIceCandidate(candidate);
}

void WRTCConn::GetStats(webrtc::RTCStatsCollectorCallback* cb) {
    pc_->GetStats(cb);
}

class AudioBroadcaster: public webrtc::AudioSourceInterface {
public:
    void AddSink(webrtc::AudioTrackSinkInterface* sink) {
        std::lock_guard<std::mutex> lock(sinks_lock_);
        auto it = std::find_if(
            sinks_.begin(), sinks_.end(),
            [sink](webrtc::AudioTrackSinkInterface* i) -> bool { return i == sink; });
        if (it != sinks_.end()) {
            return;
        }
        sinks_.push_back(sink);
    }

    void RemoveSink(webrtc::AudioTrackSinkInterface* sink) {
        std::lock_guard<std::mutex> lock(sinks_lock_);
        sinks_.erase(std::remove_if(sinks_.begin(), sinks_.end(),
                                    [sink](webrtc::AudioTrackSinkInterface* i) -> bool {
                                        return i == sink;
                                    }), sinks_.end());
    }

    void OnData(const void* audio_data,
                      int bits_per_sample,
                      int sample_rate,
                      size_t number_of_channels,
                      size_t number_of_frames) 
    {
        std::lock_guard<std::mutex> lock(sinks_lock_);

        //Debug("AudioBroadcaster %zu %d %d %zu sinks=%zu",
        //   number_of_frames, sample_rate, bits_per_sample, number_of_channels, sinks_.size());

        DebugPCM("/tmp/rtc.src.orig.s16", audio_data, bits_per_sample/8*number_of_frames);

        for (auto sink: sinks_) {
            sink->OnData(audio_data, bits_per_sample, sample_rate, number_of_channels, number_of_frames);
        }
    }

    void RegisterObserver(webrtc::ObserverInterface* observer) {
    }

    void UnregisterObserver(webrtc::ObserverInterface* observer) {
    }

    webrtc::MediaSourceInterface::SourceState state() const {
        return webrtc::MediaSourceInterface::kLive;
    }

    bool remote() const {
        return true;
    }

    std::mutex sinks_lock_;
    std::vector<webrtc::AudioTrackSinkInterface*> sinks_;
};

class AVBroadcasterStreamSink: public SinkObserver {
public:
    AVBroadcasterStreamSink(rtc::VideoBroadcaster *vsrc, AudioBroadcaster* asrc, bool dumpRawpkt) : vsrc_(vsrc), asrc_(asrc), audiobuf_() {}

    void OnFrame(const std::shared_ptr<muxer::MediaFrame>& frame) {
        AVFrame* avframe = frame->AvFrame();

        if (frame->Stream() == muxer::STREAM_VIDEO) {
            Verbose("AVBroadcasterOnFrameVideo");

            rtc::scoped_refptr<webrtc::I420Buffer> buffer(
                webrtc::I420Buffer::Create(avframe->width, avframe->height)
            );
            buffer->InitializeData();

            const uint8_t* data_y = avframe->data[0];
            const uint8_t* data_u = avframe->data[1];
            const uint8_t* data_v = avframe->data[2];

            yuv::CopyLine(buffer->MutableDataY(), buffer->StrideY(), data_y,
                        avframe->linesize[0], avframe->height);
            yuv::CopyLine(buffer->MutableDataU(), buffer->StrideU(), data_u,
                        avframe->linesize[1], avframe->height / 2);
            yuv::CopyLine(buffer->MutableDataV(), buffer->StrideV(), data_v,
                        avframe->linesize[2], avframe->height / 2);

            webrtc::VideoFrame vframe = webrtc::VideoFrame(buffer, webrtc::kVideoRotation_0,
                                                0 / rtc::kNumNanosecsPerMicrosec);
            vsrc_->OnFrame(vframe);
        } else if (frame->Stream() == muxer::STREAM_AUDIO) {
            Verbose("AVBroadcasterOnFrameAudio");
            
            if (dumpRawpkt) {
                //rtc::ByteBufferWriter bw(rtc::ByteBuffer::ByteOrder::ORDER_NETWORK);
                //LOG(LS_VERBOSE) << "DumpRawpkt " << rtc::hex_encode(bw.Data(), bw.Length());
            }
        } else if (frame->Stream() == muxer::STREAM_RAWPACKET) {
            rtc::ByteBufferReader b(
                frame->rawpkt.c_str(), frame->rawpkt.size(),
                rtc::ByteBuffer::ByteOrder::ORDER_NETWORK
            );

            uint8_t type;
            uint32_t size;
            uint8_t pkttype = 0;
            RawpktAudio audio;

            for (;;) {
                if (!b.ReadUInt8(&type)) {
                    break;
                }
                if (!b.ReadUInt32(&size)) {
                    break;
                }
                rtc::ByteBufferReader b2(b.Data(), size, b.Order());
                if (type == 1) {
                    b2.ReadUInt8(&pkttype);
                } else if (audio.Unmarshall(type, b2)) {
                    // audio
                }
                if (!b.Consume(size)) {
                    break;
                }
            }

            auto resampleCb = [&](const std::shared_ptr<muxer::MediaFrame>& out) {
                RawpktAudio audio(out->AvFrame());
                Verbose("AVBroadcasterOnFrameAudioRawpktDecoded %d %d %d %d", 
                    audio.samplebits, audio.samplerate, audio.channels, audio.samplenr);
                asrc_->OnData(audio.audiodata, audio.samplebits, audio.samplerate, audio.channels, audio.samplenr);
            };

            auto decodeCb = [&](const std::shared_ptr<muxer::MediaFrame>& frame) -> int {
                resampler->Resample(frame, resampleCb);
                return 0;
            };

            switch (pkttype) {
            case 1: { // video
                    Verbose("AVBroadcasterOnFrameVideoRawpkt");
                    auto rawpkt = std::make_shared<std::string>(frame->rawpkt);
                    vsrc_->OnFrame(webrtc::VideoFrame(rawpkt));
                }
                break;
            case 2: // raw audio
                Verbose("AVBroadcasterOnFrameAudioRawpkt %d %d %d %d", 
                    audio.samplebits, audio.samplerate, audio.channels, audio.samplenr);
                asrc_->OnData(audio.audiodata, audio.samplebits, audio.samplerate, audio.channels, audio.samplenr);
                break;
            case 3: // encoded audio
                if (decoder == nullptr) {
                    decoder = std::make_shared<muxer::AvDecoder>(nullptr);
                    resampler = std::make_shared<muxer::AudioResampler>(nullptr);
                    resampler->frameSize = muxer::AudioResampler::SAMPLE_RATE/100;
                }
                auto pkt = std::make_unique<muxer::MediaPacket>(muxer::STREAM_AUDIO, muxer::CODEC_AAC, audio.extradata, audio.data);
                decoder->Decode(pkt, decodeCb);
                break;
            }
        }
    }

    rtc::VideoBroadcaster* vsrc_;
    AudioBroadcaster* asrc_;
    std::vector<uint8_t> audiobuf_;
    std::shared_ptr<muxer::AvEncoder> encoder = nullptr;
    std::shared_ptr<muxer::AvDecoder> decoder = nullptr;
    std::shared_ptr<muxer::AudioResampler> resampler = nullptr;
    bool dumpRawpkt = false;
};

bool WRTCConn::AddStream(SinkAddRemover* stream, std::vector<rtc::scoped_refptr<webrtc::MediaStreamTrackInterface>> &tracks) {
    auto media_stream = pc_factory_->CreateLocalMediaStream(newReqId());
    auto asrc = new rtc::RefCountedObject<AudioBroadcaster>();
    rtc::VideoBroadcaster *vsrc = new rtc::VideoBroadcaster();
    webrtc::VideoTrackSource *vtrksrc = new rtc::RefCountedObject<webrtc::VideoTrackSource>(vsrc, false);
    auto vtrk = pc_factory_->CreateVideoTrack(newReqId(), vtrksrc);
    auto atrk = pc_factory_->CreateAudioTrack(newReqId(), asrc);
    media_stream->AddTrack(vtrk);
    media_stream->AddTrack(atrk);
    tracks.push_back(vtrk);
    tracks.push_back(atrk);
    stream->AddSink(newReqId(), new AVBroadcasterStreamSink(vsrc, asrc, rtcconf.dump_rawpkt()));
    return pc_->AddStream(media_stream);
}
