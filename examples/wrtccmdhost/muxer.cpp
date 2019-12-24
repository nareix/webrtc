#include "muxer.hpp"

using namespace muxer;

//
// OptionMap
//

bool OptionMap::GetOption(IN const std::string& _key, OUT std::string& _value)
{
        std::lock_guard<std::mutex> lock(paramsLck_);

        auto it = params_.find(_key);
        if (it != params_.end()) {
                _value = it->second;
                return true;
        }
        return false;
}

bool OptionMap::GetOption(IN const std::string& _key, OUT int& _value)
{
        std::lock_guard<std::mutex> lock(intparamsLck_);

        auto it = intparams_.find(_key);
        if (it != intparams_.end()) {
                _value = it->second;
                return true;
        }
        return false;
}

bool OptionMap::GetOption(IN const std::string& _key)
{
        std::lock_guard<std::mutex> lock(paramsLck_);

        if (params_.find(_key) != params_.end()) {
                return true;
        }
        return false;
}

void OptionMap::SetOption(IN const std::string& _key, IN const std::string& _val)
{
        std::lock_guard<std::mutex> lock(paramsLck_);

        params_[_key] = _val;
}

void OptionMap::SetOption(IN const std::string& _key, IN int _val)
{
        std::lock_guard<std::mutex> lock(paramsLck_);
        intparams_[_key] = _val;
}

void OptionMap::SetOption(IN const std::string& _flag)
{
        std::lock_guard<std::mutex> lock(paramsLck_);

        SetOption(_flag, "");
}

void OptionMap::DelOption(IN const std::string& _key)
{
        std::lock_guard<std::mutex> lock(paramsLck_);

        params_.erase(_key);
}

void OptionMap::GetOptions(IN const OptionMap& _opts)
{
        params_ = _opts.params_;
        intparams_ = _opts.intparams_;
}

//
// AvMuxer
//

AvMuxer::AvMuxer(std::shared_ptr<XLogger> xl, IN int _nWidth, IN int _nHeight)
        :xl_(xl), videoMuxer_(_nWidth, _nHeight)
{
        audioOnly_.store(false);
        av_register_all();
        avformat_network_init();
}

AvMuxer::~AvMuxer()
{
}

int AvMuxer::AddOutput(IN const std::string& _name, IN FrameSender* stream)
{
        auto r = std::make_shared<Output>(_name);
        r->Start(stream);
        outputs_.Push(std::move(r));
        return 0;
}

int AvMuxer::ModOutputOption(IN const std::string& _name, IN const std::string& _key, IN const std::string& _val)
{
        auto r = FindOutput(_name);
        if (r == nullptr) {
                return -1;
        }

        r->SetOption(_key, _val);

        return 0;
}

int AvMuxer::ModOutputOption(IN const std::string& _name, IN const std::string& _key, IN int _nVal)
{
        auto r = FindOutput(_name);
        if (r == nullptr) {
                return -1;
        }

        r->SetOption(_key, _nVal);

        return 0;
}

int AvMuxer::DelOutputOption(IN const std::string& _name, IN const std::string& _key)
{
        auto r = FindOutput(_name);
        if (r == nullptr) {
                return -1;
        }

        r->DelOption(_key);

        return 0;
}

int AvMuxer::RemoveOutput(IN const std::string& _key)
{
        return 0;
}


int AvMuxer::AddInput(IN const std::string& _name, IN SinkAddRemover *stream)
{
        auto r = std::make_shared<Input>(std::make_shared<XLogger>(_name), _name);
        r->Start(stream);
        inputs_.Push(std::move(r));
        return 0;
}

int AvMuxer::ModInputOption(IN const std::string& _name, IN const std::string& _key, IN const std::string& _val)
{
        auto r = FindInput(_name);
        if (r == nullptr) {
                return -1;
        }

        r->SetOption(_key, _val);

        return 0;
}

int AvMuxer::ModInputOption(IN const std::string& _name, IN const std::string& _key, IN int _nVal)
{
        auto r = FindInput(_name);
        if (r == nullptr) {
                return -1;
        }

        r->SetOption(_key, _nVal);

        return 0;
}

int AvMuxer::DelInputOption(IN const std::string& _name, IN const std::string& _key)
{
        auto r = FindInput(_name);
        if (r == nullptr) {
                return -1;
        }

        r->DelOption(_key);

        return 0;
}

int AvMuxer::RemoveInput(IN const std::string& _name)
{
        inputs_.CriticalSection([_name](std::deque<std::shared_ptr<Input>>& queue){
                        for (auto it = queue.begin(); it != queue.end(); it++) {
                                if ((*it)->Name().compare(_name) == 0) {
                                        it = queue.erase(it);
                                        return;
                                }
                        }
                });

        return 0;
}

std::shared_ptr<Input> AvMuxer::FindInput(IN const std::string& _name)
{
        std::shared_ptr<Input> p = nullptr;
        inputs_.FindIf([&](std::shared_ptr<Input>& _pInput) -> bool {
                        if (_pInput->Name().compare(_name) == 0) {
                                p = _pInput;
                                return true;
                        }
                        return false;
                });

        return p;
}

std::shared_ptr<Output> AvMuxer::FindOutput(IN const std::string& _name)
{
        std::shared_ptr<Output> p = nullptr;
        outputs_.FindIf([&](std::shared_ptr<Output>& _pOutput) -> bool {
                        if (_pOutput->Name().compare(_name) == 0) {
                                p = _pOutput;
                                return true;
                        }
                        return false;
                });

        return p;
}

int AvMuxer::Start()
{
        tstart_ = now_f();

        XDebug("Fps=%d", fps_);

        auto muxVideo = [&]() {
                std::vector<std::shared_ptr<MediaFrame>> videoFrames;
                std::shared_ptr<MediaFrame> pOutFrame;
                size_t nQlen;

                while (true) {
                        if (!audioOnly_.load()) {
                                inputs_.Foreach([&](std::shared_ptr<Input>& _pInput){
                                        std::shared_ptr<MediaFrame> pFrame;
                                        int hidden = 1;
                                        _pInput->GetOption(options::hidden, hidden);
                                        if (!hidden && _pInput->GetVideo(pFrame, nQlen)) {
                                                videoFrames.push_back(pFrame);
                                        }
                                });

                                // background color
                                int nRGB;
                                if (GetOption(options::bgcolor, nRGB) == true) {
                                        videoMuxer_.BgColor(nRGB);
                                }

                                // mux pictures
                                if (videoMuxer_.Mux(videoFrames, pOutFrame) == 0) {
                                        FeedOutputs(pOutFrame, now_f()-tstart_);
                                }
                                videoFrames.clear();
                        }

                        usleep((useconds_t)(1.0/double(fps_)*1e6));
                }
        };

        auto muxAudio = [&]() {
                std::shared_ptr<MediaFrame> pOutFrame;
                std::vector<std::shared_ptr<MediaFrame>> audioFrames;
                double last = tstart_;
                double diff = 0;
                double pts = 0;
                double frame_dur = double(AudioResampler::DEFAULT_FRAME_SIZE)/AudioResampler::SAMPLE_RATE;

                while (true) {
                        double cur = now_f();
                        diff += cur - last;
                        last = cur;

                        while (diff > frame_dur) {
                                int i = 0;
                                inputs_.Foreach([&](std::shared_ptr<Input>& _pInput) {
                                        i++;
                                        int muted = 1;
                                        _pInput->GetOption(options::muted, muted);
                                        if (muted) {
                                                return;
                                        }
                                        std::shared_ptr<MediaFrame> pFrame;
                                        //Verbose("frame#%d s=%zu", i, _pInput->audioQ_.Size());
                                        if (_pInput->GetAudioLatest(pFrame, 20)) {
                                                audioFrames.push_back(pFrame);
                                        }
                                });

                                //DebugPCM("/tmp/rtc.mix0.s16", audioFrames[0]->AvFrame()->data[0], audioFrames[0]->AvFrame()->linesize[0]);
                                audioMixer_.Mix(audioFrames, pOutFrame);
                                DebugPCM("/tmp/rtc.mix.s16", pOutFrame->AvFrame()->data[0], pOutFrame->AvFrame()->linesize[0]);
                                FeedOutputs(pOutFrame, pts);
                                audioFrames.clear();
                                
                                pts += frame_dur;
                                diff -= frame_dur;
                        }

                        usleep(useconds_t(frame_dur*1e6));
                }
        };

        videoMuxerThread_ = std::thread(muxVideo);
        audioMuxerThread_ = std::thread(muxAudio);

        return 0;
}

void AvMuxer::FeedOutputs(IN std::shared_ptr<MediaFrame>& _pFrame, double pts)
{
        _pFrame->AvFrame()->pts = int64_t(pts*1e3);

        outputs_.Foreach([&](std::shared_ptr<Output>& _pOutput) {
                bool ok = _pOutput->Push(_pFrame);
                if (!ok) {
                        XDebug("FeedOutputs PushFailed isaudio %d", _pFrame->Stream() == StreamType::STREAM_AUDIO);
                }
        });
}

//
// VideoMuxer
//

VideoMuxer::VideoMuxer(IN int _nW, IN int _nH)
{
        nCanvasW_ = _nW;
        nCanvasH_ = _nH;
		
		_bEnterMuxMode = 0;
		_nFrameCount = 0;
		_nFrameCountThreshold = 25*4;
}

VideoMuxer::~VideoMuxer()
{
	_bEnterMuxMode = 0;
	_nFrameCount = 0;
}

void VideoMuxer::BgColor(int _nRGB)
{
        nBackground_ = _nRGB;
}

int VideoMuxer::Mux(IN std::vector<std::shared_ptr<MediaFrame>>& _frames, OUT std::shared_ptr<MediaFrame>& _pOut)
{
        auto pMuxed = std::make_shared<MediaFrame>();
        pMuxed->Stream(STREAM_VIDEO);
        pMuxed->Codec(CODEC_H264);
        pMuxed->AvFrame()->format = VideoRescaler::PIXEL_FMT;
        pMuxed->AvFrame()->width = nCanvasW_;
        pMuxed->AvFrame()->height = nCanvasH_;
        av_frame_get_buffer(pMuxed->AvFrame(), 32);

        // by default the canvas is pure black, or customized background color
        uint8_t nR = nBackground_ >> 16;
        uint8_t nG = (nBackground_ >> 8) & 0xff;
        uint8_t nB = nBackground_ & 0xff;
        uint8_t nY = static_cast<uint8_t>((0.257 * nR) + (0.504 * nG) + (0.098 * nB) + 16);
        uint8_t nU = static_cast<uint8_t>((0.439 * nR) - (0.368 * nG) - (0.071 * nB) + 128);
        uint8_t nV = static_cast<uint8_t>(-(0.148 * nR) - (0.291 * nG) + (0.439 * nB) + 128);

        memset(pMuxed->AvFrame()->data[0], nY, pMuxed->AvFrame()->linesize[0] * nCanvasH_);
        memset(pMuxed->AvFrame()->data[1], nU, pMuxed->AvFrame()->linesize[1] * (nCanvasH_ / 2));
        memset(pMuxed->AvFrame()->data[2], nV, pMuxed->AvFrame()->linesize[2] * (nCanvasH_ / 2));

        // sort by Z coordinate
        std::sort(_frames.begin(), _frames.end(),
                  [](const std::shared_ptr<MediaFrame>& i, const std::shared_ptr<MediaFrame>& j) {
                          return i->Z() < j->Z();
                });

        // mux pictures
        for (auto& pFrame : _frames) {
                if (pFrame == nullptr) {
                        Warn("internal: got 1 null frame, something was wrong");
                        continue;
                }
                merge::Overlay(pFrame, pMuxed, true);
        }

        _pOut = pMuxed;
		
		if (_bEnterMuxMode==0)
		{
			int bBgColor = isBgColor(_pOut);
			if (bBgColor==0 || _nFrameCount>=_nFrameCountThreshold)
				_bEnterMuxMode = 1;
		}
		
		_nFrameCount++;
		if (_bEnterMuxMode==0)
			return -1;
		else
			return 0;
}

int VideoMuxer::isBgColor(IN std::shared_ptr<MediaFrame>& pFrame)
{
	uint8_t nR = nBackground_ >> 16;
    uint8_t nG = (nBackground_ >> 8) & 0xff;
    uint8_t nB = nBackground_ & 0xff;
    uint8_t nY = static_cast<uint8_t>((0.257 * nR) + (0.504 * nG) + (0.098 * nB) + 16);
    uint8_t nU = static_cast<uint8_t>((0.439 * nR) - (0.368 * nG) - (0.071 * nB) + 128);
    uint8_t nV = static_cast<uint8_t>(-(0.148 * nR) - (0.291 * nG) + (0.439 * nB) + 128);
	
	int yflag = 1, uvflag = 1;
	for ( int i=0; i<pFrame->AvFrame()->linesize[1] * nCanvasH_/2; i++ )
	{
		if ((pFrame->AvFrame()->data[1][i]!=nU) || (pFrame->AvFrame()->data[2][i]!=nV) )
		{
			uvflag = 0;
			break;
		}
	}
	if (uvflag==0)
		return 0;
	
	for ( int i=0; i<pFrame->AvFrame()->linesize[0] * nCanvasH_; i++ )
	{
		if (pFrame->AvFrame()->data[0][i]!=nY)
		{
			yflag = 0;
			break;
		}
	}
	if (yflag==0)
		return 0;
	
	return 1;
}

//
// AudioMixer
//

AudioMixer::AudioMixer()
{
}

AudioMixer::~AudioMixer()
{
}

int AudioMixer::Mix(IN const std::vector<std::shared_ptr<MediaFrame>>& _frames, OUT std::shared_ptr<MediaFrame>& _pOut)
{
        auto pMuted = getSlienceAudioFrame();

        // mixer works here
        for (auto& pFrame : _frames) {
                if (pFrame == nullptr) {
                        Warn("internal: got 1 null frame, something was wrong");
                        continue;
                }
                SimpleMix(pFrame, pMuted);
        }

        _pOut = pMuted;

        return 0;
}

void AudioMixer::SimpleMix(IN const std::shared_ptr<MediaFrame>& _pFrom, OUT std::shared_ptr<MediaFrame>& _pTo)
{
        AVFrame* pF = _pFrom->AvFrame();
        AVFrame* pT = _pTo->AvFrame();

        int16_t* pF16 = (int16_t*)pF->data[0];
        int16_t* pT16 = (int16_t*)pT->data[0];

        for (int i = 0; i < pF->linesize[0] && i < pT->linesize[0]; i+=2) {
                int32_t nMixed = *pF16 + *pT16;
                if (nMixed > 32767) {
                        nMixed = 32767;
                } else if (nMixed < -32768) {
                        nMixed = -32768;
                }
                *pT16 = static_cast<int16_t>(nMixed);

                pF16++;
                pT16++;
        }
}
