#include "packet.hpp"

using namespace muxer;

MediaPacket::MediaPacket(IN const AVStream& _avStream, IN const AVPacket* _pAvPacket)
{
        // save codec pointer
        pAvCodecPar_ = _avStream.codecpar;
        StreamType stream = static_cast<StreamType>(pAvCodecPar_->codec_type);
        if (stream != STREAM_AUDIO && stream != STREAM_VIDEO) {
                stream = STREAM_DATA;
        }
        Stream(stream);
        Codec(static_cast<CodecType>(pAvCodecPar_->codec_id));
        Width(pAvCodecPar_->width);
        Height(pAvCodecPar_->height);
        SampleRate(pAvCodecPar_->sample_rate);
        Channels(pAvCodecPar_->channels);

        // copy packet
        pAvPacket_ = const_cast<AVPacket*>(_pAvPacket);
}

MediaPacket::MediaPacket(
        IN StreamType stream, IN CodecType codec, 
        const std::vector<uint8_t>& extradata, const std::vector<uint8_t>& data
) : stream_(stream), codec_(codec), extradata(extradata), data(data)
{
        pAvPacket_ = av_packet_alloc();
        av_init_packet(pAvPacket_);
        pAvPacket_->data = (uint8_t *)&data[0];
        pAvPacket_->size = data.size();
}

MediaPacket::MediaPacket()
{
        pAvPacket_ = av_packet_alloc();
        av_init_packet(pAvPacket_);
}

MediaPacket::~MediaPacket()
{
        av_packet_free(&pAvPacket_);
}

AVPacket* MediaPacket::AvPacket() const
{
        return const_cast<AVPacket*>(pAvPacket_);
}

AVCodecParameters* MediaPacket::AvCodecParameters() const
{
        return pAvCodecPar_;
}

static uint64_t ffmepgDtsToDts(int64_t dts) {
        if (dts == AV_NOPTS_VALUE) {
                return 0;
        } else if (dts < 0) {
                return 0;
        } else {
                return dts;
        }
}

uint64_t MediaPacket::Pts() const
{
        return ffmepgDtsToDts(pAvPacket_->pts);
}

void MediaPacket::Pts(uint64_t _pts)
{
        pAvPacket_->pts = _pts;
}

uint64_t MediaPacket::Dts() const
{
        return ffmepgDtsToDts(pAvPacket_->dts);
}

void MediaPacket::Dts(uint64_t _dts)
{
        pAvPacket_->dts = _dts;
}

StreamType MediaPacket::Stream() const
{
        return stream_;
}

void MediaPacket::Stream(StreamType _type)
{
        stream_ = _type;
}

CodecType MediaPacket::Codec() const
{
        return codec_;
}

void MediaPacket::Codec(CodecType _type)
{
        codec_ = _type;
}

char* MediaPacket::Data()const
{
        return reinterpret_cast<char*>(pAvPacket_->data);
}

int MediaPacket::Size() const
{
        return static_cast<int>(pAvPacket_->size);
}

void MediaPacket::Print() const
{
        Info("packet: pts=%lu, dts=%lu, stream=%d, codec=%d, size=%lu",
             static_cast<unsigned long>(pAvPacket_->pts), static_cast<unsigned long>(pAvPacket_->dts),
             Stream(), Codec(), static_cast<unsigned long>(pAvPacket_->size));
}

void MediaPacket::Dump(const std::string& _title) const
{
        Debug("%spts=%lu, dts=%lu, stream=%d, codec=%d, size=%lu", _title.c_str(),
              static_cast<unsigned long>(pAvPacket_->pts), static_cast<unsigned long>(pAvPacket_->dts),
              Stream(), Codec(), static_cast<unsigned long>(pAvPacket_->size));
        global::PrintMem(Data(), Size());
}

int MediaPacket::Width() const
{
        return nWidth_;
}

int MediaPacket::Height() const
{
        return nHeight_;
}

void MediaPacket::Width(int _nValue)
{
        nWidth_ = _nValue;
}

void MediaPacket::Height(int _nValue)
{
        nHeight_ = _nValue;
}

int MediaPacket::SampleRate() const
{
        return nSampleRate_;
}

int MediaPacket::Channels() const
{
        return nChannels_;
}

void MediaPacket::SampleRate(int _nValue)
{
        nSampleRate_ = _nValue;
}

void MediaPacket::Channels(int _nValue)
{
        nChannels_ = _nValue;
}

bool MediaPacket::IsKey() const
{
        return ((pAvPacket_->flags & AV_PKT_FLAG_KEY) != 0);
}

void MediaPacket::SetKey()
{
        pAvPacket_->flags |= AV_PKT_FLAG_KEY;
}

int MediaPacket::AppendSEI(const uint8_t *buffer, int length)
{
        if (auto ret = av_grow_packet(pAvPacket_, length) < 0) {
                return ret;
        }

        memcpy(pAvPacket_->data+pAvPacket_->size, buffer, length);

        return 0;
}

//
// MediaFrame
//

MediaFrame::MediaFrame(IN const std::string& rawpkt): rawpkt(rawpkt), stream_(STREAM_RAWPACKET)
{
}

MediaFrame::MediaFrame(IN const AVFrame* _pAvFrame)
{
        pAvFrame_ = const_cast<AVFrame*>(_pAvFrame);
}

MediaFrame::MediaFrame()
{
        pAvFrame_ = av_frame_alloc();
}

MediaFrame::MediaFrame(IN int _nSamples, IN int _nChannels, IN AVSampleFormat _format, IN bool _bSilence)
        :MediaFrame()
{
        // for audio
        pAvFrame_->nb_samples = _nSamples;
        pAvFrame_->format = _format;
        pAvFrame_->channels = _nChannels;
        pAvFrame_->channel_layout = av_get_default_channel_layout(_nChannels);
        av_frame_get_buffer(pAvFrame_, 0);

        // cleanup any waves
        if (_bSilence) {
                av_samples_set_silence(pAvFrame_->data, 0, pAvFrame_->nb_samples, pAvFrame_->channels,
                                       (AVSampleFormat)pAvFrame_->format);
        }
}

MediaFrame::MediaFrame(IN int _nWidth, IN int _nHeight, IN AVPixelFormat _format, IN int _nColor)
        :MediaFrame()
{
        // for video
        pAvFrame_->format = _format;
        pAvFrame_->width = _nWidth;
        pAvFrame_->height = _nHeight;
        av_frame_get_buffer(pAvFrame_, 32);

        // fill up the picture with pure color
        if (_nColor >= 0) {
                uint8_t nY, nU, nV;
                color::RgbToYuv(_nColor, nY, nU, nV);
                memset(pAvFrame_->data[0], nY, pAvFrame_->linesize[0] * _nHeight);
                memset(pAvFrame_->data[1], nU, pAvFrame_->linesize[1] * (_nHeight / 2));
                memset(pAvFrame_->data[2], nV, pAvFrame_->linesize[2] * (_nHeight / 2));
                if (pAvFrame_->data[3]) {
                        memset(pAvFrame_->data[3], 0xff, pAvFrame_->linesize[3] * _nHeight);
                }
        }
}

MediaFrame::~MediaFrame()
{
        if (pAvFrame_ != NULL) {
                av_frame_free(&pAvFrame_);
                pAvFrame_ = NULL;
        }

        if (pExtraBuf_ != nullptr) {
                av_free(pExtraBuf_);
                pExtraBuf_ = nullptr;
        }
}

MediaFrame::MediaFrame(IN const MediaFrame& _frame)
        :MediaFrame()
{
        if (_frame.AvFrame() == nullptr) {
                return;
        }

        switch (_frame.Stream()) {
        case STREAM_VIDEO:
                pAvFrame_->format = _frame.AvFrame()->format;
                pAvFrame_->width = _frame.AvFrame()->width;
                pAvFrame_->height = _frame.AvFrame()->height;
                av_frame_get_buffer(pAvFrame_, 32);
                break;
        case STREAM_AUDIO:
                pAvFrame_->nb_samples = _frame.AvFrame()->nb_samples;
                pAvFrame_->format = _frame.AvFrame()->format;
                pAvFrame_->channels = _frame.AvFrame()->channels;
                pAvFrame_->channel_layout = _frame.AvFrame()->channel_layout;
                pAvFrame_->sample_rate = _frame.AvFrame()->sample_rate;
                av_frame_get_buffer(pAvFrame_, 0);
                break;
        default:
                return;
        }

        av_frame_copy(pAvFrame_, _frame.AvFrame());
        av_frame_copy_props(pAvFrame_, _frame.AvFrame());

        // attr held by instance
        stream_ = _frame.stream_;
        codec_ = _frame.codec_;
        nX_ = _frame.nX_;
        nY_ = _frame.nY_;
        nZ_ = _frame.nZ_;
}

AVFrame* MediaFrame::AvFrame() const
{
        return pAvFrame_;
}

StreamType MediaFrame::Stream() const
{
        return stream_;
}

void MediaFrame::Stream(StreamType _type)
{
        stream_ = _type;
}

CodecType MediaFrame::Codec() const
{
        return codec_;
}

void MediaFrame::Codec(CodecType _type)
{
        codec_ = _type;
}

void MediaFrame::ExtraBuffer(unsigned char* _pBuf)
{
        pExtraBuf_ = _pBuf;
}

void MediaFrame::Print() const
{
        Info("frame: pts=%lu, stream=%d, codec=%d, linesize=%lu",
             static_cast<unsigned long>(pAvFrame_->pts), Stream(), Codec(), static_cast<unsigned long>(pAvFrame_->linesize[0]));
}

int MediaFrame::X() const
{
        return nX_;
}

void MediaFrame::X(IN int _nX)
{
        nX_ = _nX;
}

int MediaFrame::Y() const
{
        return nY_;
}

void MediaFrame::Y(IN int _nY)
{
        nY_ = _nY;
}

int MediaFrame::Z() const
{
        return nZ_;
}

void MediaFrame::Z(IN int _nZ)
{
        nZ_ = _nZ;
}
