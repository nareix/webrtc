#include "input.hpp"

using namespace muxer;

//
// AvReceiver
//

AvReceiver::AvReceiver(std::shared_ptr<XLogger> xl)
{
        xl_ = xl;
}

AvReceiver::~AvReceiver()
{
        avformat_close_input(&pAvContext_);
        pAvContext_ = nullptr;
}

int AvReceiver::AvInterruptCallback(void* _pContext)
{
        using namespace std::chrono;
        AvReceiver* pReceiver = reinterpret_cast<AvReceiver*>(_pContext);
        auto xl_ = pReceiver->xl_;
        high_resolution_clock::time_point now = high_resolution_clock::now();
        if (duration_cast<milliseconds>(now - pReceiver->start_).count() > pReceiver->nTimeout_) {
                XError("receiver timeout, %lu milliseconds", pReceiver->nTimeout_);
                return -1;
        }

        return 0;
}

int AvReceiver::Receive(IN const std::string& _url, IN PacketHandlerType& _callback)
{
        if (pAvContext_ != nullptr) {
                XWarn("internal: reuse of Receiver is not recommended");
        }

        // allocate AV context
        pAvContext_ = avformat_alloc_context();
        if (pAvContext_ == nullptr) {
                XError("av context could not be created");
                return -1;
        }

        // for timeout timer
        std::string option;
        nTimeout_ = 10 * 1000; // 10 seconds
        XInfo("receiver timeout=%lu milliseconds", nTimeout_);
        pAvContext_->interrupt_callback.callback = AvReceiver::AvInterruptCallback;
        pAvContext_->interrupt_callback.opaque = this;
        start_ = std::chrono::high_resolution_clock::now();

        // open input stream
        XInfo("input URL: %s", _url.c_str());
        int nStatus = avformat_open_input(&pAvContext_, _url.c_str(), 0, 0);
        if (nStatus < 0) {
                XError("could not open input stream: %s, %s", av_err2str(nStatus), _url.c_str());
                sleep(1);
                return -1;
        }

        // get stream info
        nStatus = avformat_find_stream_info(pAvContext_, 0);
        if (nStatus < 0) {
                XError("could not get stream info");
                return -1;
        }

        std::vector<AVStream *> streams;
        for (unsigned int i = 0; i < pAvContext_->nb_streams; i++) {
                AVStream * pAvStream = pAvContext_->streams[i];

                streams.push_back(pAvStream);
                XInfo("stream is found: avstream=%d, avcodec=%d",
                     pAvStream->codecpar->codec_type, pAvStream->codecpar->codec_id);
        }

        while (true) {
                AVPacket* pAvPacket = av_packet_alloc();
                av_init_packet(pAvPacket);
                if (av_read_frame(pAvContext_, pAvPacket) == 0) {
                        if (pAvPacket->stream_index < 0 ||
                            static_cast<unsigned int>(pAvPacket->stream_index) >= pAvContext_->nb_streams) {
                                XWarn("invalid stream index in packet");
                                av_packet_free(&pAvPacket);
                                continue;
                        }

                        // if avformat detects another stream during transport, we have to ignore the packets of the stream
                        if (static_cast<size_t>(pAvPacket->stream_index) < streams.size()) {
                                // we need all PTS/DTS use milliseconds, sometimes they are macroseconds such as TS streams
                                AVRational tb = AVRational{1, 1000};
                                AVRounding r = static_cast<AVRounding>(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
                                pAvPacket->dts = av_rescale_q_rnd(pAvPacket->dts, streams[pAvPacket->stream_index]->time_base, tb, r);
                                pAvPacket->pts = av_rescale_q_rnd(pAvPacket->pts, streams[pAvPacket->stream_index]->time_base, tb, r);

                                int nStatus = _callback(std::make_unique<MediaPacket>(*streams[pAvPacket->stream_index], pAvPacket));
                                if (nStatus != 0) {
                                        return nStatus;
                                }
                        }
                        start_ = std::chrono::high_resolution_clock::now();
                } else {
                        break;
                }
        }

        return 0;
}

//
// AvDecoder
//

AvDecoder::AvDecoder(std::shared_ptr<XLogger> xl)
{
        xl_ = xl;
}

AvDecoder::~AvDecoder()
{
        if (bIsDecoderAvailable_) {
                avcodec_close(pAvDecoderContext_);
        }
        if (pAvDecoderContext_ != nullptr) {
                avcodec_free_context(&pAvDecoderContext_);
        }
}

int AvDecoder::Init(IN const std::unique_ptr<MediaPacket>& _pPacket)
{
        // create decoder
        if (pAvDecoderContext_ == nullptr) {
                // find decoder
                AVCodec *pAvCodec = avcodec_find_decoder(static_cast<AVCodecID>(_pPacket->Codec()));
                if (pAvCodec == nullptr) {
                        XError("could not find AV decoder for codec_id=%d", _pPacket->Codec());
                        return -1;
                }

                // initiate AVCodecContext
                pAvDecoderContext_ = avcodec_alloc_context3(pAvCodec);
                if (pAvDecoderContext_ == nullptr) {
                        XError("could not allocate AV codec context");
                        return -1;
                }

                // if the packet is from libavformat
                // just use context parameters in AVStream to get one directly otherwise fake one
                if (_pPacket->AvCodecParameters() != nullptr) {
                        if (avcodec_parameters_to_context(pAvDecoderContext_, _pPacket->AvCodecParameters()) < 0){
                                XError("could not copy decoder context");
                                return -1;
                        }
                }

                if (_pPacket->extradata.size() > 0) {
                        pAvDecoderContext_->extradata = (uint8_t *)av_mallocz(2 + AV_INPUT_BUFFER_PADDING_SIZE);
                        pAvDecoderContext_->extradata_size = _pPacket->extradata.size();
                        memcpy(pAvDecoderContext_->extradata, &_pPacket->extradata[0], _pPacket->extradata.size());
                }

                // open it
                if (avcodec_open2(pAvDecoderContext_, pAvCodec, nullptr) < 0) {
                        XError("could not open decoder");
                        return -1;
                } else {
                        XInfo("open decoder: stream=%d, codec=%d", _pPacket->Stream(), _pPacket->Codec());
                        bIsDecoderAvailable_ = true;
                }
        }

        return 0;
}

int AvDecoder::Decode(IN const std::unique_ptr<MediaPacket>& _pPacket, IN FrameHandlerType& _callback)
{
        if (Init(_pPacket) < 0) {
                return -1;
        }

       // int nStatus;

        //
        // decode ! and get one frame to encode
        //
        do {
                bool bNeedSendAgain = false;
                int nStatus = avcodec_send_packet(pAvDecoderContext_, _pPacket->AvPacket());
                if (nStatus != 0) {
                        if (nStatus == AVERROR(EAGAIN)) {
                                XWarn("decoder internal: assert failed, we should not get EAGAIN");
                                bNeedSendAgain = true;
                        } else {
                                XError("decoder: could not send frame, status=%d", nStatus);
                                _pPacket->Print();
                                return -1;
                        }
                }

                while (1) {
                        // allocate a frame for outputs
                        auto pFrame = std::make_shared<MediaFrame>();
                        pFrame->Stream(_pPacket->Stream());
                        pFrame->Codec(_pPacket->Codec());

                        nStatus = avcodec_receive_frame(pAvDecoderContext_, pFrame->AvFrame());
                        if (nStatus == 0) {
                                int nStatus = _callback(pFrame);
                                if (nStatus < 0) {
                                        return nStatus;
                                }
                                if (bNeedSendAgain) {
                                        break;
                                }
                        } else if (nStatus == AVERROR(EAGAIN)) {
                                return 0;
                        } else {
                                XError("decoder: could not receive frame, status=%d", nStatus);
                                _pPacket->Print();
                                return -1;
                        }
                }
        } while(1);

        return 0;
}

//
// Input
//

Input::Input(std::shared_ptr<XLogger> xl, IN const std::string& _name)
        :OptionMap(),
        resampler_(xl),
        audioQ_(Input::AUDIO_Q_LEN),
        videoQ_(Input::VIDEO_Q_LEN),
        name_(_name)
{
        xl_ = xl;
        bReceiverExit_.store(false);
}

class InputSinkObserver: public SinkObserver {
public:
        InputSinkObserver(Input *input) : input_(input) {}

        void OnFrame(const std::shared_ptr<muxer::MediaFrame>& frame) {
                // format video/audio data
                if (frame->Stream() == STREAM_VIDEO) {
                        input_->SetVideo(frame);
                } else if (frame->Stream() == STREAM_AUDIO) {
                        input_->SetAudio(frame);
                }
        }

private:
        Input *input_;
};

void Input::Start(IN SinkAddRemover *stream) {
        stream_ = stream;

        sink_ = new InputSinkObserver(this);
        sink_id_ = newReqId();
        stream_->AddSink(sink_id_, sink_);

        onStop_ = [this]() {
                stream_->RemoveSink(sink_id_);
        };
}

// start thread => receiver loop => decoder loop
void Input::Start(IN const std::string& _url)
{
        auto recv = [this, _url] {
                double tmstart = 0;
                double ptsdifftot = 0;
                double lastpts = 0;

                while (bReceiverExit_.load() == false) {
                        auto avReceiver = std::make_unique<AvReceiver>(xl_);
                        auto vDecoder = std::make_unique<AvDecoder>(xl_);
                        auto aDecoder = std::make_unique<AvDecoder>(xl_);

                        auto receiverHook = [&](IN const std::unique_ptr<MediaPacket> _pPacket) -> int {
                                if (bReceiverExit_.load() == true) {
                                        return -1;
                                }

                                auto decoderHook = [&](const std::shared_ptr<MediaFrame>& _pFrame) -> int {
                                        if (bReceiverExit_.load() == true) {
                                                return -1;
                                        }

                                        if (this->nativeRate_) {
                                                if (_pFrame->AvFrame()->pts != AV_NOPTS_VALUE) {
                                                        if (tmstart == 0) {
                                                                tmstart = now_f();
                                                                lastpts = double(_pFrame->AvFrame()->pts) / 1e3;
                                                        } else {
                                                                double pts = double(_pFrame->AvFrame()->pts) / 1e3;
                                                                double ptsdiff = pts - lastpts;
                                                                if (ptsdiff < -1.0 || ptsdiff > 1.0) {
                                                                        lastpts = pts;
                                                                } else if (ptsdiff > 0.1) {
                                                                        lastpts = pts;
                                                                        ptsdifftot += ptsdiff;
                                                                        double elapsed = now_f() - tmstart;
                                                                        double sleep = ptsdifftot - elapsed;
                                                                        if (sleep > 0) {
                                                                                usleep(useconds_t(sleep*1e6));
                                                                        }
                                                                }
                                                        }
                                                }
                                        }

                                        if (singleFrame_) {
                                                auto start = std::chrono::high_resolution_clock::now();

                                                int64_t vpts = 0, apts = 0;

                                                while (bReceiverExit_.load() == false) {
                                                        _pFrame->AvFrame()->pts = vpts;
                                                        SetVideo(_pFrame);

                                                        auto now = std::chrono::high_resolution_clock::now();
                                                        std::chrono::duration<double, std::ratio<1,1>> diff_d(now - start);
                                                        int64_t curapts = diff_d.count()*1000;

                                                        while (apts < curapts) {
                                                                auto a = getSlienceAudioFrame();
                                                                a->AvFrame()->pts = apts;
                                                                int64_t dur = (int64_t)((double)a->AvFrame()->nb_samples / (double)a->AvFrame()->sample_rate * 1000);
                                                                apts += dur;
                                                                //Info("SetAudio pts=%lld curapts=%lld", a->AvFrame()->pts, curapts);
                                                                SetAudio(a);
                                                        }

                                                        usleep(1000*40);
                                                        vpts += 40;
                                                }
                                                return 0;
                                        }

                                        // format video/audio data
                                        if (_pFrame->Stream() == STREAM_VIDEO) {
                                                SetVideo(_pFrame);
                                        } else if (_pFrame->Stream() == STREAM_AUDIO) {
                                                SetAudio(_pFrame);
                                        }

                                        return 0;
                                };

                                // start decoder loop
                                if (_pPacket->Stream() == STREAM_VIDEO) {
                                        vDecoder->Decode(_pPacket, decoderHook);
                                } else if (_pPacket->Stream() == STREAM_AUDIO) {
                                        aDecoder->Decode(_pPacket, decoderHook);
                                }

                                return 0;
                        };

                        // start receiver loop
                        avReceiver->Receive(_url, receiverHook);
                }
        };

        receiver_ = std::thread(recv);

        onStop_ = [this]() {
                bReceiverExit_.store(true);
                if (receiver_.joinable()) {
                        receiver_.join();
                }
        };
}

void Input::Stop()
{
        onStop_();
}

void Input::SetVideo(const std::shared_ptr<MediaFrame>& _pFrame)
{
        if (!doRescale_) {
                SendFrame(_pFrame);
                return;
        }

        // convert color space to YUV420p and rescale the image
        auto pAvf = _pFrame->AvFrame();

        int nW = 0;
        int nH = 0;
        int stretchMode = VideoRescaler::STRETCH_DEFAULT;
        GetOption(options::width, nW);
        GetOption(options::height, nH);
        GetOption(options::stretchMode, stretchMode);
        if (nW == 0) {
                nW = pAvf->width;
        }
        if (nH == 0) {
                nH = pAvf->height;
        }

        bool bNeedRescale = false;
        if (pRescaler_ == nullptr) {
                if (nW != pAvf->width) {
                        bNeedRescale = true;
                }
                if (nH != pAvf->height) {
                        bNeedRescale = true;
                }
                if (pAvf->format != VideoRescaler::PIXEL_FMT) {
                        bNeedRescale = true;
                }
                if (bNeedRescale) {
                        auto format = pAvf->format;
                        switch (format) {
                        case AV_PIX_FMT_ARGB:
                        case AV_PIX_FMT_RGBA:
                        case AV_PIX_FMT_ABGR:
                        case AV_PIX_FMT_BGRA:
                                format = AV_PIX_FMT_YUVA420P;
                                break;
                        }
                        pRescaler_ = std::make_shared<VideoRescaler>(xl_, nW, nH, (AVPixelFormat)format, stretchMode);
                }
        } else {
                // if target w or h is changed, reinit the rescaler
                if (nW != pRescaler_->TargetW()) {
                        bNeedRescale = true;
                }
                if (nH != pRescaler_->TargetH()) {
                        bNeedRescale = true;
                }
                if (stretchMode != pRescaler_->TargetbStretchMode()) {
                        bNeedRescale = true;
                }
                if (bNeedRescale) {
                        pRescaler_->Reset(nW, nH, stretchMode);
                }
        }

        // rescale the video frame
        auto pFrame = _pFrame;
        if (pRescaler_ != nullptr) {
                pRescaler_->Rescale(_pFrame, pFrame);
        }
        pFrame->AvFrame()->pts = _pFrame->AvFrame()->pts;

        // set x,y,z coordinate
        int nX, nY, nZ;
        if (GetOption(options::x, nX) == true) {
                pFrame->X(nX);
        }
        if (GetOption(options::y, nY) == true) {
                pFrame->Y(nY);
        }
        if (GetOption(options::z, nZ) == true) {
                pFrame->Z(nZ);
        }

        videoQ_.ForcePush(pFrame);
        SendFrame(pFrame);
}

void Input::SetAudio(const std::shared_ptr<MediaFrame>& _pFrame)
{
        if (doResample_) {
                resampler_.Resample(_pFrame, [&](const std::shared_ptr<MediaFrame>& pNewFrame) {
                        SendFrame(pNewFrame);
                        audioQ_.ForcePush(pNewFrame);
                });
        } else {
                SendFrame(_pFrame);
                audioQ_.ForcePush(_pFrame);
        }
}

bool Input::GetVideo(OUT std::shared_ptr<MediaFrame>& _pFrame, OUT size_t& _nQlen)
{
        // make sure preserve at least one video frame
        bool bOk = videoQ_.TryPop(pLastVideo_);
        _nQlen = videoQ_.Size();
        if (bOk == true){
                _pFrame = pLastVideo_;
        } else {
                if (pLastVideo_ == nullptr) {
                        return false;
                }
                _pFrame = pLastVideo_;
        }
        return true;
}

bool Input::GetAudioLatest(OUT std::shared_ptr<MediaFrame>& _pFrame, size_t limit)
{
        while (audioQ_.Size() > limit) {
                audioQ_.Pop();
        }
        return audioQ_.TryPop(_pFrame);
}

bool Input::GetAudio(OUT std::shared_ptr<MediaFrame>& _pFrame, OUT size_t& _nQlen)
{
        //size_t nSizeEachFrame = AudioResampler::FRAME_SIZE * AudioResampler::CHANNELS * av_get_bytes_per_sample(AudioResampler::SAMPLE_FMT);

        bool bOk = audioQ_.TryPop(_pFrame);

        /*
        if (bOk) {
                Info("getaudio pop");
        }
        */

        //_nQlen = audioQ_.Size();
        /*
        if (bOk == true) {
                auto nLen = audioQ_.Size();
                if (nLen == Input::AUDIO_Q_LEN - 1) {
                        Warn("[%s] input audio queue is full", Name().c_str());
                } else if (nLen == Input::AUDIO_Q_LEN - 5) {
                        Warn("[%s] input audio queue is almost full", Name().c_str());
                } else if (nLen == 5) {
                        Warn("[%s] input audio queue is almost empty", Name().c_str());
                } else if (nLen == 1) {
                        Warn("[%s] input audio queue is empty", Name().c_str());
                }
                return true;
        } else {
                Info("samplebuffer slience %lu", sampleBuffer_.size());
                if (sampleBuffer_.size() > 0 && sampleBuffer_.size() < nSizeEachFrame) {
                        // sample buffer does contain data but less than frame size
                        _pFrame = std::make_shared<MediaFrame>();
                        _pFrame->Stream(STREAM_AUDIO);
                        _pFrame->Codec(CODEC_AAC);
                        _pFrame->AvFrame()->nb_samples = AudioResampler::FRAME_SIZE;
                        _pFrame->AvFrame()->format = AudioResampler::SAMPLE_FMT;
                        _pFrame->AvFrame()->channels = AudioResampler::CHANNELS;
                        _pFrame->AvFrame()->channel_layout = AudioResampler::CHANNEL_LAYOUT;
                        _pFrame->AvFrame()->sample_rate = AudioResampler::SAMPLE_RATE;
                        av_frame_get_buffer(_pFrame->AvFrame(), 0);
                        av_samples_set_silence(_pFrame->AvFrame()->data, 0, _pFrame->AvFrame()->nb_samples, _pFrame->AvFrame()->channels,
                                               (AVSampleFormat)_pFrame->AvFrame()->format);

                        // copy existing buffer contents
                        std::copy(&sampleBuffer_[0], &sampleBuffer_[sampleBuffer_.size()], _pFrame->AvFrame()->data[0]);
                        sampleBuffer_.resize(0);

                        return true;
                }
        }
        */

        return bOk;
}

Input::~Input()
{
        Stop();
}

std::string Input::Name()
{
        return name_;
}
