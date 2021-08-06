#ifndef __MEDIA_HPP__
#define __MEDIA_HPP__

#include "common.hpp"

namespace muxer
{
        class MediaFrame;
        class AudioResampler
        {
        public:
                // TODO move following default values
                static const int CHANNELS = 2;
                static const AVSampleFormat SAMPLE_FMT = AV_SAMPLE_FMT_S16;
                static const int CHANNEL_LAYOUT = AV_CH_LAYOUT_STEREO;
                static const int DEFAULT_FRAME_SIZE = 1024;
                static const int SAMPLE_RATE = 48000;
        public:
                AudioResampler(std::shared_ptr<XLogger> xl);
                ~AudioResampler();
                int Resample(IN const std::shared_ptr<MediaFrame>& _pInFrame, OUT std::vector<uint8_t>& buffer);
                int Resample(IN const std::shared_ptr<MediaFrame>& _pInFrame, std::function<void (const std::shared_ptr<MediaFrame>& out)> callback);

                int frameSize = DEFAULT_FRAME_SIZE;
        private:
                int Init(IN const std::shared_ptr<MediaFrame>& pFrame);
                int Reset();
        private:
                SwrContext* pSwr_ = nullptr; // for resampling
                int nOrigSamplerate_, nOrigChannels_, nOrigForamt_;
                std::vector<uint8_t> sampleBuffer_;
                std::shared_ptr<XLogger> xl_ = nullptr;
        };

        class VideoRescaler
        {
        public:
                // TODO move following default values
                static const AVPixelFormat PIXEL_FMT = AV_PIX_FMT_YUV420P;
                enum {
                        STRETCH_ASPECT_FILL = 0,
                        STRETCH_ASPECT_FIT = 1,
                        STRETCH_SCALE_TO_FIT = 2,

                        STRETCH_DEFAULT = STRETCH_ASPECT_FILL,
                };
        public:
                VideoRescaler(std::shared_ptr<XLogger> xl, IN int nWidth, IN int nHeight, IN const AVPixelFormat format = VideoRescaler::PIXEL_FMT,
                              IN int bStretchMode = STRETCH_DEFAULT, IN int nBgColor = 0x0);
                ~VideoRescaler();
                int Rescale(IN const std::shared_ptr<MediaFrame>& pInFrame, OUT std::shared_ptr<MediaFrame>& pOutFrame);
                int Reset(IN int nWidth, IN int nHeight, IN const AVPixelFormat format = VideoRescaler::PIXEL_FMT,
                          IN int bStretchMode = STRETCH_DEFAULT, IN int nBgColor = 0x0);
                int Reset(IN int nWidth, IN int nHeight, IN int bStretchMode);
                int TargetW();
                int TargetH();
                int TargetbStretchMode();
        private:
                int Init(IN const std::shared_ptr<MediaFrame>& pFrame);
        private:
                std::shared_ptr<XLogger> xl_ = nullptr;
                SwsContext* pSws_ = nullptr;
                int nW_, nH_, nOrigW_, nOrigH_;
                AVPixelFormat format_, origFormat_;

                // by default, Rescaler is init with stretch mode
                int bStretchMode_ = STRETCH_DEFAULT;
                int nZoomBgColor_;
                int nZoomW_, nZoomH_;
                int nZoomX_, nZoomY_; // picture offset after zooming
        };

        namespace sound {
                void Gain(INOUT const std::shared_ptr<MediaFrame>& pFrame, IN int nPercent = 100);
                void Gain(INOUT const uint8_t* pData, IN int nSize, IN int nPercent = 100);
        }

        namespace color {
                inline void RgbToYuv(IN int _nRgb, OUT uint8_t& _nY, uint8_t& _nU, uint8_t& _nV) {
                        uint8_t nB = _nRgb >> 16;
                        uint8_t nG = (_nRgb >> 8) & 0xff;
                        uint8_t nR = _nRgb & 0xff;
                        _nY = static_cast<uint8_t>((0.257 * nR) + (0.504 * nG) + (0.098 * nB) + 16);
                        _nU = static_cast<uint8_t>((0.439 * nR) - (0.368 * nG) - (0.071 * nB) + 128);
                        _nV = static_cast<uint8_t>(-(0.148 * nR) - (0.291 * nG) + (0.439 * nB) + 128);
                }
        }

        namespace merge {
                enum {
                        OVERLAY_NORMAL = 0,
                        OVERLAY_ALPHABLEND = 1,
                };
                void Overlay(IN const std::shared_ptr<MediaFrame>& pFrom, OUT std::shared_ptr<MediaFrame>& pTo, int withAlpha = OVERLAY_NORMAL);
        }

        std::shared_ptr<MediaFrame> getSlienceAudioFrame();
}

#endif
