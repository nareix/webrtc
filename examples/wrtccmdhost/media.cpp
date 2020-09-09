#include "media.hpp"

using namespace muxer;

//
// AudioResampler
//

const int AudioResampler::CHANNELS;
const AVSampleFormat AudioResampler::SAMPLE_FMT;

AudioResampler::AudioResampler(std::shared_ptr<XLogger> xl)
{
        xl_ = xl;
}

AudioResampler::~AudioResampler()
{
        if (pSwr_ != nullptr) {
                swr_free(&pSwr_);
        }
}

int AudioResampler::Init(IN const std::shared_ptr<MediaFrame>& _pFrame)
{
        if (pSwr_ != nullptr) {
                XWarn("internal: resampler: already init");
                return -1;
        }

        sampleBuffer_.reserve(frameSize * AudioResampler::CHANNELS * 2 * 4);
        sampleBuffer_.resize(0);

        // for fdkaac encoder, input samples should be PCM signed16le, otherwise do resampling
        if (_pFrame->AvFrame()->format != AudioResampler::SAMPLE_FMT ||
            _pFrame->AvFrame()->channel_layout != AudioResampler::CHANNEL_LAYOUT ||
            _pFrame->AvFrame()->sample_rate != AudioResampler::SAMPLE_RATE) {
                XInfo("input sample_format=%d, need sample_format=%d, initiate resampling",
                     _pFrame->AvFrame()->format, AudioResampler::SAMPLE_FMT);
                pSwr_ = swr_alloc();
                av_opt_set_int(pSwr_, "in_channel_layout", av_get_default_channel_layout(_pFrame->AvFrame()->channels), 0);
                av_opt_set_int(pSwr_, "out_channel_layout", AudioResampler::CHANNEL_LAYOUT, 0);
                av_opt_set_int(pSwr_, "in_sample_rate", _pFrame->AvFrame()->sample_rate, 0);
                av_opt_set_int(pSwr_, "out_sample_rate", AudioResampler::SAMPLE_RATE, 0);
                av_opt_set_sample_fmt(pSwr_, "in_sample_fmt", static_cast<AVSampleFormat>(_pFrame->AvFrame()->format), 0);
                av_opt_set_sample_fmt(pSwr_, "out_sample_fmt", AudioResampler::SAMPLE_FMT,  0);
                if (swr_init(pSwr_) != 0) {
                        XError("could not initiate resampling");
                        return -1;
                }
                // save original audio attributes
                nOrigSamplerate_ = _pFrame->AvFrame()->sample_rate;
                nOrigChannels_ = _pFrame->AvFrame()->channels;
                nOrigForamt_ = _pFrame->AvFrame()->format;
        }

        return 0;
}

int AudioResampler::Reset()
{
        if (pSwr_ != nullptr) {
                swr_free(&pSwr_);
                pSwr_ = nullptr;
        }

        return 0;
}

int AudioResampler::Resample(IN const std::shared_ptr<MediaFrame>& _pFrame, std::function<void (const std::shared_ptr<MediaFrame>& out)> callback) {
        size_t nBufSize = sampleBuffer_.size();
        std::vector<uint8_t> buffer;

        // resample to the same audio format
        if (Resample(_pFrame, buffer) != 0) {
                return -1;
        }

        // save resampled data in audio buffer
        sampleBuffer_.resize(sampleBuffer_.size() + buffer.size());
        std::copy(buffer.begin(), buffer.end(), &sampleBuffer_[nBufSize]);

        // if the buffer size meets the min requirement of encoding one frame, build a frame and push upon audio queue
        size_t nSizeEachFrame = frameSize * AudioResampler::CHANNELS * av_get_bytes_per_sample(AudioResampler::SAMPLE_FMT);

        //Info("resample %lu %lu %lu %p", buffer.size(), sampleBuffer_.size(), nSizeEachFrame, &sampleBuffer_);

        while (sampleBuffer_.size() >= nSizeEachFrame) {
                auto pNewFrame = std::make_shared<MediaFrame>();
                pNewFrame->Stream(_pFrame->Stream());
                pNewFrame->Codec(_pFrame->Codec());
                av_frame_copy_props(pNewFrame->AvFrame(), _pFrame->AvFrame());
                pNewFrame->AvFrame()->nb_samples = frameSize;
                pNewFrame->AvFrame()->format = AudioResampler::SAMPLE_FMT;
                pNewFrame->AvFrame()->channels = AudioResampler::CHANNELS;
                pNewFrame->AvFrame()->channel_layout = AudioResampler::CHANNEL_LAYOUT;
                pNewFrame->AvFrame()->sample_rate = AudioResampler::SAMPLE_RATE;
                av_frame_get_buffer(pNewFrame->AvFrame(), 0);
                std::copy(&sampleBuffer_[0], &sampleBuffer_[nSizeEachFrame], pNewFrame->AvFrame()->data[0]);
        
                DebugPCM("/tmp/rtc.re2.s16", &sampleBuffer_[0], nSizeEachFrame);

                // move rest samples to beginning of the buffer
                std::copy(&sampleBuffer_[nSizeEachFrame], &sampleBuffer_[sampleBuffer_.size()], sampleBuffer_.begin());
                sampleBuffer_.resize(sampleBuffer_.size() - nSizeEachFrame);

                callback(pNewFrame);
        }

        return 0;
}

int AudioResampler::Resample(IN const std::shared_ptr<MediaFrame>& _pInFrame, OUT std::vector<uint8_t>& _buffer)
{
        // do nothing if format,layout and samplerate are identical
        if (_pInFrame->AvFrame()->format == AudioResampler::SAMPLE_FMT &&
            _pInFrame->AvFrame()->channel_layout == AudioResampler::CHANNEL_LAYOUT &&
            _pInFrame->AvFrame()->sample_rate == AudioResampler::SAMPLE_RATE) {
                _buffer.resize(_pInFrame->AvFrame()->linesize[0]);
                std::copy(_pInFrame->AvFrame()->data[0], _pInFrame->AvFrame()->data[0] + _pInFrame->AvFrame()->linesize[0],
                          _buffer.begin());
                return 0;
        }

        // initial
        if (pSwr_ == nullptr) {
                if (Init(_pInFrame) != 0) {
                        XError("could not init resampling");
                        return -1;
                }
        }
        // incoming audio attrs are changed during transport
        if (_pInFrame->AvFrame()->sample_rate != nOrigSamplerate_ ||
            _pInFrame->AvFrame()->channels != nOrigChannels_ ||
            _pInFrame->AvFrame()->format != nOrigForamt_) {
                XInfo("resampler: audio parameters have changed, sr=%d, ch=%d, fmt=%d -> sr=%d, ch=%d, fmt=%d", nOrigSamplerate_,
                     nOrigChannels_, nOrigForamt_, _pInFrame->AvFrame()->sample_rate, _pInFrame->AvFrame()->channels,
                     _pInFrame->AvFrame()->format);
                // reset the resampler
                Reset();
                // reinit
                if (Init(_pInFrame) != 0) {
                        pSwr_ = nullptr;
                        return -3;
                }
        }

        int nRetVal;
        uint8_t **pDstData = nullptr;
        int nDstLinesize;
        int nDstBufSize;
        int nDstNbSamples = av_rescale_rnd(_pInFrame->AvFrame()->nb_samples, AudioResampler::SAMPLE_RATE,
                                            _pInFrame->AvFrame()->sample_rate, AV_ROUND_UP);
        int nMaxDstNbSamples = nDstNbSamples;

        // get output buffer
        nRetVal = av_samples_alloc_array_and_samples(&pDstData, &nDstLinesize, AudioResampler::CHANNELS,
                                                 nDstNbSamples, AudioResampler::SAMPLE_FMT, 0);
        if (nRetVal < 0) {
                XError("resampler: could not allocate destination samples");
                return -1;
        }

        // get output samples
        nDstNbSamples = av_rescale_rnd(swr_get_delay(pSwr_, _pInFrame->AvFrame()->sample_rate) + _pInFrame->AvFrame()->nb_samples,
                                       AudioResampler::SAMPLE_RATE, _pInFrame->AvFrame()->sample_rate, AV_ROUND_UP);
        if (nDstNbSamples > nMaxDstNbSamples) {
                av_freep(&pDstData[0]);
                nRetVal = av_samples_alloc(pDstData, &nDstLinesize, AudioResampler::CHANNELS,
                                           nDstNbSamples, AudioResampler::SAMPLE_FMT, 1);
                if (nRetVal < 0) {
                        XError("resampler: could not allocate sample buffer");
                        return -1;
                }
                nMaxDstNbSamples = nDstNbSamples;
        }

        // convert !!
        nRetVal = swr_convert(pSwr_, pDstData, nDstNbSamples, (const uint8_t **)_pInFrame->AvFrame()->extended_data,
                              _pInFrame->AvFrame()->nb_samples);
        if (nRetVal < 0) {
                XError("resampler: converting failed");
                return -1;
        }

        // get output buffer size
        nDstBufSize = av_samples_get_buffer_size(&nDstLinesize, AudioResampler::CHANNELS, nRetVal, AudioResampler::SAMPLE_FMT, 1);
        if (nDstBufSize < 0) {
                XError("resampler: could not get sample buffer size");
                return -1;
        }

        _buffer.resize(nDstBufSize);
        std::copy(pDstData[0], pDstData[0] + nDstBufSize, _buffer.begin());

        DebugPCM("/tmp/rtc.re1.s16", pDstData[0], nDstBufSize);

        // cleanup
        if (pDstData)
                av_freep(&pDstData[0]);
        av_freep(&pDstData);


        return 0;
}

//
// VideoRescaler
//

// TODO delete
const AVPixelFormat VideoRescaler::PIXEL_FMT;

VideoRescaler::VideoRescaler(std::shared_ptr<XLogger> xl, IN int _nWidth, IN int _nHeight, IN const AVPixelFormat _format,
                             IN int _bStretchMode, IN int _nBgColor)
{
        xl_ = xl;

        if (_nWidth <= 0 || _nHeight <= 0) {
                XError("rescale: resize to width=%d, height=%d", _nWidth, _nHeight);
                return;
        }

        nW_ = _nWidth;
        nH_ = _nHeight;
        format_ = _format;
        bStretchMode_ = _bStretchMode;
        nZoomBgColor_ = _nBgColor;
}

VideoRescaler::~VideoRescaler()
{
        if (pSws_ != nullptr) {
                sws_freeContext(pSws_);
        }
}

int VideoRescaler::Reset(IN int _nWidth, IN int _nHeight, IN const AVPixelFormat _format,
                         IN int _bStretchMode, IN int _nBgColor)
{
        if (_nWidth <= 0 || _nHeight <= 0) {
                XError("rescale: resize to width=%d, height=%d", _nWidth, _nHeight);
                return -1;
        }

        nW_ = _nWidth;
        nH_ = _nHeight;
        format_ = _format;
        bStretchMode_ = _bStretchMode;
        nZoomBgColor_ = _nBgColor;

        if (pSws_ != nullptr) {
                sws_freeContext(pSws_);
                pSws_ = nullptr;
        }

        return 0;
}

//overload function Reset
int VideoRescaler::Reset(IN int _nWidth, IN int _nHeight, IN int _bStretchMode){
        if (_nWidth <= 0 || _nHeight <= 0) {
                XError("rescale: resize to width=%d, height=%d", _nWidth, _nHeight);
                return -1;
        }

        nW_ = _nWidth;
        nH_ = _nHeight;
        bStretchMode_ = _bStretchMode;

        if (pSws_ != nullptr) {
                sws_freeContext(pSws_);
                pSws_ = nullptr;
        }

        return 0;
}

int VideoRescaler::Init(IN const std::shared_ptr<MediaFrame>& _pFrame)
{
        if (pSws_ != nullptr) {
                XWarn("internal: rescale: already init");
                return -1;
        }

        auto pAvf = _pFrame->AvFrame();

        XInfo("input color_space=%s, need color_space=%s, resize_to=%dx%d, stretch=%d initiate rescaling",
             av_get_pix_fmt_name((AVPixelFormat)pAvf->format), av_get_pix_fmt_name((AVPixelFormat)format_),
             nW_, nH_, bStretchMode_);

                auto fOrigRatio = static_cast<float>(pAvf->width) / pAvf->height;
                auto fTargetRatio = static_cast<float>(nW_) / nH_;

        switch (bStretchMode_) {
        case STRETCH_ASPECT_FILL:
        default:
                if (fTargetRatio > fOrigRatio) {
                        nZoomH_ = nW_ / fOrigRatio;
                        nZoomW_ = nW_;
                        nZoomX_ = 0;
                        nZoomY_ = nH_/2 - nZoomH_/2;
                } else {
                        nZoomW_ = nH_ * fOrigRatio;
                        nZoomH_ = nH_;
                        nZoomX_ = nW_/2 - nZoomW_/2;
                        nZoomY_ = 0;
                }
                break;

        case STRETCH_ASPECT_FIT:
                if (fTargetRatio > fOrigRatio) {
                        nZoomW_ = nH_ * fOrigRatio;
                        nZoomH_ = nH_;
                        nZoomX_ = nW_/2 - nZoomW_/2;
                        nZoomY_ = 0;
                } else {
                        nZoomH_ = nW_ / fOrigRatio;
                        nZoomW_ = nW_;
                        nZoomX_ = 0;
                        nZoomY_ = nH_/2 - nZoomH_/2;
                }
                break;

        case STRETCH_SCALE_TO_FIT:
                nZoomW_ = nW_;
                nZoomH_ = nH_;
                nZoomX_ = 0;
                nZoomY_ = 0;
                break;
        }

        // configure rescaling context
        pSws_ = sws_getContext(pAvf->width, pAvf->height,
                               static_cast<AVPixelFormat>(pAvf->format), nZoomW_, nZoomH_, format_,
                               SWS_BICUBIC, nullptr, nullptr, nullptr);
        if (pSws_ == nullptr) {
                XError("rescaler initialization failed");
                return -1;
        }

        Verbose("scale w=%d h=%d", nZoomW_, nZoomH_);

        // save original sample source configurations
        nOrigW_ = pAvf->width;
        nOrigH_ = pAvf->height;
        origFormat_ = static_cast<AVPixelFormat>(pAvf->format);

        return 0;
}

int VideoRescaler::Rescale(IN const std::shared_ptr<MediaFrame>& _pInFrame, OUT std::shared_ptr<MediaFrame>& _pOutFrame)
{
        if (pSws_ == nullptr) {
                if (Init(_pInFrame) != 0) {
                        pSws_ = nullptr;
                        return -1;
                }
        }

        // the incoming frame resolution changed, reinit the sws
        if (_pInFrame->AvFrame()->width != nOrigW_ || _pInFrame->AvFrame()->height != nOrigH_ ||
            _pInFrame->AvFrame()->format != origFormat_) {
                XInfo("rescaler: video parameters have changed, w=%d, h=%d, fmt=%s -> w=%d, h=%d, fmt=%s", 
                        nOrigW_, nOrigH_, av_get_pix_fmt_name((AVPixelFormat)origFormat_), 
                        _pInFrame->AvFrame()->width, _pInFrame->AvFrame()->height, 
                        av_get_pix_fmt_name((AVPixelFormat)_pInFrame->AvFrame()->format)
                );
                if (Reset(nW_, nH_, format_, bStretchMode_) != 0) {
                        XError("rescaler: reinit failed");
                        return -2;
                }
                if (Init(_pInFrame) != 0) {
                        pSws_ = nullptr;
                        return -3;
                }
        }

        auto pRescaled = std::make_shared<MediaFrame>(nZoomW_, nZoomH_, format_);
        pRescaled->Stream(STREAM_VIDEO);
        pRescaled->Codec(CODEC_H264);

        // scale !!
        int nStatus = sws_scale(pSws_, _pInFrame->AvFrame()->data, _pInFrame->AvFrame()->linesize, 0,
                                _pInFrame->AvFrame()->height, pRescaled->AvFrame()->data, pRescaled->AvFrame()->linesize);
        if (nStatus < 0) {
                XError("rescale: failed, status=%d", nStatus);
                return -1;
        }

        // create result picture
        _pOutFrame = std::make_shared<MediaFrame>(nW_, nH_, format_, nZoomBgColor_);
        _pOutFrame->Stream(STREAM_VIDEO);
        _pOutFrame->Codec(CODEC_H264);

        // set internal offset
        pRescaled->X(nZoomX_);
        pRescaled->Y(nZoomY_);

        // copy rescaled picture to the center of output frame
        merge::Overlay(pRescaled, _pOutFrame);

        return 0;
}

int VideoRescaler::TargetW()
{
        return nW_;
}

int VideoRescaler::TargetH()
{
        return nH_;
}

int VideoRescaler::TargetbStretchMode(){
        return bStretchMode_;
}

void sound::Gain(INOUT const std::shared_ptr<MediaFrame>& _pFrame, IN int _nPercent)
{
        sound::Gain(_pFrame->AvFrame()->data[0], _pFrame->AvFrame()->linesize[0], _nPercent);
}

void sound::Gain(INOUT const uint8_t* _pData, IN int _nSize, IN int _nPercent)
{
        if (_nSize <= 0 || _nSize % 2 != 0) {
                return;
        }
        if (_nPercent < 0) {
                _nPercent = 0;
        }
        if (_nPercent > 300) {
                _nPercent = 300;
        }

        int16_t* p16 = (int16_t*)_pData;
        for (int i = 0; i < _nSize; i += 2) {
                int32_t nGained = static_cast<int32_t>(*p16) * _nPercent / 100;
                if (nGained < -0x80000) {
                        nGained = -0x80000;
                } else if (nGained > 0x7fff) {
                        nGained = 0x7fff;
                }
                *p16++ = nGained;
        }
}

static void overlayRowOp(uint8_t *pdst, uint8_t *psrc, uint8_t *psrca, int linesize, int mode, int alplaSkip = 1) {
        if (mode == merge::OVERLAY_NORMAL) {
                while (linesize > 0) {
                        *pdst = *psrc;
                        pdst++;
                        psrc++;
                        linesize--;
                }
        } else {
                while (linesize > 0) {
                        uint16_t a = *psrca;
                        uint16_t dst = *pdst;
                        uint16_t src = *psrc;
                        *pdst = (uint8_t)((src*a + dst*(255-a))>>8);
                        pdst++;
                        psrc++;
                        psrca += alplaSkip;
                        linesize--;
                }
        }
}

void merge::Overlay(IN const std::shared_ptr<MediaFrame>& _pFrom, OUT std::shared_ptr<MediaFrame>& _pTo, int mode)
{
        if (mode == merge::OVERLAY_ALPHABLEND) {
                if (_pFrom->AvFrame()->format != AV_PIX_FMT_YUVA420P) {
                        mode = OVERLAY_NORMAL;
                }
        }

        AVFrame* pFrom = _pFrom->AvFrame();
        AVFrame* pTo = _pTo->AvFrame();
        auto nX = _pFrom->X();
        auto nY = _pFrom->Y();

        if (pFrom == nullptr || pTo == nullptr) {
                return;
        }

        // x or y is beyond width and height of target frame
        if (nX >= pTo->width || nY >= pTo->height) {
                return;
        }

        // Y plane

        // left-top offset point of source frame from which source frame will be copied
        int32_t nFromOffset     = 0;
        int32_t nFromOffsetX    = 0;
        int32_t nFromOffsetY    = 0;

        // left-top offset point of target frame to which source frame will copy data
        int32_t nToOffset       = 0;
        int32_t nToOffsetX      = 0;
        int32_t nToOffsetY      = 0;

        // final resolution of the source frame
        int32_t nTargetH        = 0;
        int32_t nTargetW        = 0;

        if (nX < 0) {
                if (nX + pFrom->width < 0) {
                        // whole frame is to the left side of target
                        return;
                }
                nFromOffsetX = -nX;
                nToOffsetX = 0;
                nTargetW = (pFrom->width + nX < pTo->width) ? (pFrom->width + nX) : pTo->width;
        } else {
                nFromOffsetX = 0;
                nToOffsetX = nX;
                nTargetW = (nToOffsetX + pFrom->width > pTo->width) ? (pTo->width - nToOffsetX) : pFrom->width;
        }

        if (nY < 0) {
                if (nY + pFrom->height < 0) {
                        // whole original frame is beyond top side of target
                        return;
                }
                nFromOffsetY = -nY;
                nToOffsetY = 0;
                nTargetH = (pFrom->height + nY < pTo->height) ? (pFrom->height + nY) : pTo->height;
        } else {
                nFromOffsetY = 0;
                nToOffsetY = nY;
                nTargetH = (pFrom->height + nToOffsetY > pTo->height) ? (pTo->height - nToOffsetY) : pFrom->height;
        }

        // linesize[] might not be equal to the actual width of the video due to alignment
        // (refer to the last arg of av_frame_get_buffer())
        nFromOffset = pFrom->linesize[0] * nFromOffsetY + nFromOffsetX;
        nToOffset = pTo->linesize[0] * nToOffsetY + nToOffsetX;

        // copy Y plane data from src frame to dst frame
        for (int32_t i = 0; i < nTargetH; ++i) {
                uint8_t *src = pTo->data[0] + pTo->linesize[0] * i + nToOffset;
                uint8_t *dst = pFrom->data[0] + pFrom->linesize[0] * i + nFromOffset;
                uint8_t *alpha = pFrom->data[3]+pFrom->linesize[3]*i;
                overlayRowOp(src, dst, alpha, nTargetW, mode);
        }

        // UV plane

        int32_t nFromUVOffsetX  = nFromOffsetX / 2;     // row UV data offset of pFrom
        int32_t nFromUVOffsetY  = nFromOffsetY / 2;     // colume UV data offset of pFrom
        int32_t nToUVOffsetX    = nToOffsetX / 2;       // row UV data offset of pTo
        int32_t nToUVOffsetY    = nToOffsetY / 2;       // colume UV data offset of pTo
        int32_t nTargetUVX      = nTargetW / 2;         // width of mix area for UV
        int32_t nTargetUVY      = nTargetH / 2;         // height of mix area for UV

        int32_t nFromUVOffset   = pFrom->linesize[1] * nFromUVOffsetY + nFromUVOffsetX;
        int32_t nToUVOffset     = pTo->linesize[1] * nToUVOffsetY + nToUVOffsetX;

        // copy UV plane data from src to dst
        for (int32_t j = 0; j < nTargetUVY; ++j) {
                uint8_t *alpha = pFrom->data[3]+pFrom->linesize[3]*j*2;
                overlayRowOp(
                        pTo->data[1] + nToUVOffset + pTo->linesize[1] * j, 
                        pFrom->data[1] + nFromUVOffset + pFrom->linesize[1] * j, 
                        alpha, nTargetUVX, mode, 2
                );
                overlayRowOp(
                        pTo->data[2] + nToUVOffset + pTo->linesize[2] * j, 
                        pFrom->data[2] + nFromUVOffset + pFrom->linesize[2] * j, 
                        alpha, nTargetUVX, mode, 2
                );
        }

        if (mode == merge::OVERLAY_NORMAL && _pFrom->AvFrame()->format == AV_PIX_FMT_YUVA420P
                && _pTo->AvFrame()->format == AV_PIX_FMT_YUVA420P) {
                for (int32_t i = 0; i < nTargetH; ++i) {
                        uint8_t *src = pTo->data[3] + pTo->linesize[3] * i + nToOffset;
                        uint8_t *dst = pFrom->data[3] + pFrom->linesize[3] * i + nFromOffset;
                        overlayRowOp(src, dst, NULL, nTargetW, merge::OVERLAY_NORMAL);
                }
        }
}

std::shared_ptr<MediaFrame> muxer::getSlienceAudioFrame() {
        // get a silent audio frame
        auto pMuted = std::make_shared<MediaFrame>();
        pMuted->Stream(STREAM_AUDIO);
        pMuted->Codec(CODEC_AAC);
        pMuted->AvFrame()->nb_samples = AudioResampler::DEFAULT_FRAME_SIZE;
        pMuted->AvFrame()->format = AudioResampler::SAMPLE_FMT;
        pMuted->AvFrame()->channels = AudioResampler::CHANNELS;
        pMuted->AvFrame()->channel_layout = AudioResampler::CHANNEL_LAYOUT;
        pMuted->AvFrame()->sample_rate = AudioResampler::SAMPLE_RATE;
        av_frame_get_buffer(pMuted->AvFrame(), 0);
        av_samples_set_silence(pMuted->AvFrame()->data, 0, pMuted->AvFrame()->nb_samples, pMuted->AvFrame()->channels,
                               (AVSampleFormat)pMuted->AvFrame()->format);
        return pMuted;
}
