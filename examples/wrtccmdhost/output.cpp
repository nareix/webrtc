#include "output.hpp"

using namespace muxer;

//
// AvEncoder
//
std::mutex AvEncoder::lock_;

AvEncoder::AvEncoder(std::shared_ptr<XLogger> xl)
{
        xl_ = xl;
}

AvEncoder::~AvEncoder()
{
        Deinit();
}

void AvEncoder::Deinit() {
        if (pAvEncoderContext_ != nullptr) {
                if (bIsEncoderAvailable_) {
                        avcodec_close(pAvEncoderContext_);
                }
                avcodec_free_context(&pAvEncoderContext_);
                bIsEncoderAvailable_ = false;
                pAvEncoderContext_ = nullptr;
        }
        if (pSwr_ != nullptr) {
                swr_free(&pSwr_);
                pSwr_ = nullptr;
        }
}

int AvEncoder::Init(IN const std::shared_ptr<MediaFrame>& _pFrame)
{
        if (pAvEncoderContext_ != nullptr) {
                if (_pFrame->Stream() == STREAM_VIDEO) {
                        int oldw = pAvEncoderContext_->width;
                        int oldh = pAvEncoderContext_->height;
                        int neww = _pFrame->AvFrame()->width;
                        int newh = _pFrame->AvFrame()->height;
                        if (oldw == neww && oldh == newh) {
                                return 0;
                        }
                        Deinit();
                        XInfo("video resolution change %dx%d -> %dx%d, reinit codec", oldw,oldh,neww,newh);
                } else {
                        return 0;
                }
        }

        // find encoder
        AVCodec* pAvCodec = nullptr;
        switch (_pFrame->Stream()) {
        case STREAM_AUDIO: pAvCodec = avcodec_find_encoder_by_name("libfdk_aac"); break;
        case STREAM_VIDEO: pAvCodec = avcodec_find_encoder_by_name("libx264"); break;
        default: return -1;
        }

        if (pAvCodec == nullptr) {
                XError("could not find encoder");
                return -1;
        }

        // initiate AVCodecContext
        pAvEncoderContext_ = avcodec_alloc_context3(pAvCodec);
        if (pAvEncoderContext_ == nullptr) {
                XError("could not allocate AV codec context for encoder");
                return -1;
        }

        // set encoder parameters
        Preset(_pFrame);

        if (useGlobalHeader) {
                pAvEncoderContext_->flags |= CODEC_FLAG_GLOBAL_HEADER;
        }

        // open encoder
        {
            std::lock_guard<std::mutex> lock(lock_);
            if (avcodec_open2(pAvEncoderContext_, pAvCodec, nullptr) < 0) {
                    XError("could not open encoder");
                    pAvEncoderContext_ = nullptr;
                    return -1;
            } else {
                    XInfo("open encoder: stream=%d, codec=%d", _pFrame->Stream(), _pFrame->Codec());
                    bIsEncoderAvailable_ = true;
            }
        }

        XInfo("encoder extradata_size=%d", pAvEncoderContext_->extradata_size);

        return 0;
}

std::vector<uint8_t> AvEncoder::Extradata() {
        return std::vector<uint8_t>(pAvEncoderContext_->extradata, pAvEncoderContext_->extradata+pAvEncoderContext_->extradata_size);
}


int AvEncoder::Preset(IN const std::shared_ptr<MediaFrame>& _pFrame)
{
        switch(_pFrame->Stream()) {
        case STREAM_AUDIO: return PresetAac(_pFrame);
        case STREAM_VIDEO: return PresetH264(_pFrame);
        default: XError("no preset");
        }
        return -1;
}

int AvEncoder::PresetAac(IN const std::shared_ptr<MediaFrame>& _pFrame)
{
        // configure encoder
        pAvEncoderContext_->sample_fmt = AV_SAMPLE_FMT_S16; // fdk-aac must use S16
        pAvEncoderContext_->sample_rate = _pFrame->AvFrame()->sample_rate;
        pAvEncoderContext_->channels = _pFrame->AvFrame()->channels;
        pAvEncoderContext_->channel_layout = av_get_default_channel_layout(_pFrame->AvFrame()->channels);
        pAvEncoderContext_->profile = FF_PROFILE_AAC_LOW;

        if (nBitrate_ > 0) {
                pAvEncoderContext_->bit_rate = nBitrate_ * 1000;
        } else {
                pAvEncoderContext_->bit_rate = 128000;
        }

        XInfo("default aac preset: sample_fmt=s16, sample_rate=%d, channels=%d, bit_rate=%d",
             pAvEncoderContext_->sample_rate, pAvEncoderContext_->channels, (int)pAvEncoderContext_->bit_rate);

        // reserve buffer
        const int nBytesPerSample = av_get_bytes_per_sample(pAvEncoderContext_->sample_fmt);
        const int nBytesToEncodeEachTime = pAvEncoderContext_->frame_size * nBytesPerSample * pAvEncoderContext_->channels;
        frameBuffer_.reserve(nBytesToEncodeEachTime);

        return 0;
}

int AvEncoder::PresetH264(IN const std::shared_ptr<MediaFrame>& _pFrame)
{
        XInfo("default h.264 preset: width=%d, height=%d, gop=%d, rate=%d, min_rate=%d, max_rate=%d fps=%d", 
                _pFrame->AvFrame()->width, _pFrame->AvFrame()->height,
                gop_, nBitrate_, nMinRate_, nMaxRate_, fps_);

        pAvEncoderContext_->pix_fmt = AV_PIX_FMT_YUV420P;

        // user options
        std::string option;

        // keep original dimension
        pAvEncoderContext_->width = _pFrame->AvFrame()->width;
        pAvEncoderContext_->height = _pFrame->AvFrame()->height;

        pAvEncoderContext_->time_base.num = 1;
        pAvEncoderContext_->time_base.den = fps_;
        pAvEncoderContext_->gop_size = gop_;
        pAvEncoderContext_->qmin = 10;
        pAvEncoderContext_->qmax = 51;

        if (nMinRate_ > 0) {
                pAvEncoderContext_->rc_min_rate = nMinRate_ * 1000;
        }
        if (nMaxRate_ > 0) {
                pAvEncoderContext_->rc_max_rate = nMaxRate_ * 1000;
        }

        if (nBitrate_ > 0) {
                pAvEncoderContext_->bit_rate = nBitrate_ * 1000; // bps
        } else {
                pAvEncoderContext_->bit_rate = 800 * 1000; // bps
        }

        if (nMinRate_ == nMaxRate_ && nMinRate_ == nBitrate_) {
                pAvEncoderContext_->rc_buffer_size = nBitrate_ * 1000; //bps
                pAvEncoderContext_->rc_initial_buffer_occupancy = nBitrate_ * 1000 * 3/4; //bps
                pAvEncoderContext_->bit_rate_tolerance = nBitrate_ * 1000; //bps
        }

        //pAvEncoderContext_->thread_type = FF_THREAD_FRAME;

        // set params
        av_opt_set(pAvEncoderContext_->priv_data, "preset", "ultrafast", 0);
        av_opt_set(pAvEncoderContext_->priv_data, "tune", "zerolatency", 0);

        return 0;
}

int AvEncoder::Encode(IN std::shared_ptr<MediaFrame>& _pFrame, IN EncoderHandlerType& _callback)
{
        if (Init(_pFrame) != 0) {
                return -1;
        }

        switch(_pFrame->Stream()) {
        case STREAM_AUDIO: return EncodeAac(_pFrame, _callback);
        case STREAM_VIDEO: return EncodeH264(_pFrame, _callback);
        default: XError("no preset");
        }
        return -1;        
}

int AvEncoder::EncodeAac(IN std::shared_ptr<MediaFrame>& _pFrame, IN EncoderHandlerType& _callback)
{
        // get number of bytes to encode
        int nBytesPerSample = av_get_bytes_per_sample(pAvEncoderContext_->sample_fmt);
        int nBytesToEncode = _pFrame->AvFrame()->nb_samples * nBytesPerSample * pAvEncoderContext_->channels;
        //const int nBytesToEncodeEachTime = pAvEncoderContext_->frame_size * nBytesPerSample * pAvEncoderContext_->channels;
        const int nBytesToEncodeEachTime = _pFrame->AvFrame()->linesize[0];

        // notice, for aac, each time encoder only accept 1 frame (1024bytes per channel) no more no less
        _pFrame->AvFrame()->nb_samples = pAvEncoderContext_->frame_size;
        _pFrame->AvFrame()->format = pAvEncoderContext_->sample_fmt;

        // if we have data left from last encoding , append to the beginning of frame
        if (frameBuffer_.size() > 0) {
                std::vector<char> tmp;
                int nOffset = nBytesToEncode - frameBuffer_.size();
                if (nOffset >= 0) {
                        tmp.resize(frameBuffer_.size());
                        // confusing part: we need do following steps
                        // frame[12345] buffer[xx] tmp[..] => frame[12345] buffer[xx] tmp[45]
                        std::copy(_pFrame->AvFrame()->data[0] + nOffset, _pFrame->AvFrame()->data[0] + nBytesToEncode, tmp.begin());
                        // frame[12345] buffer[xx] tmp[45] =>  frame[12123] buffer[xx] tmp[45]
                        std::copy_backward(_pFrame->AvFrame()->data[0], _pFrame->AvFrame()->data[0] + nOffset,
                                           _pFrame->AvFrame()->data[0] + nBytesToEncode);
                        // frame[12123] buffer[xx] tmp[45] =>  frame[xx123] buffer[xx] tmp[45]
                        std::copy(frameBuffer_.begin(), frameBuffer_.end(), _pFrame->AvFrame()->data[0]);
                        // frame[xx123] buffer[xx] tmp[45] => frame[xx123] buffer[45] tmp[xx]
                        frameBuffer_.swap(tmp);
                } else {
                        tmp.resize(nBytesToEncode);
                        // frame[56] buffer[1234] tmp[..] => frame[56] buffer[1234] tmp[56]
                        std::copy(_pFrame->AvFrame()->data[0], _pFrame->AvFrame()->data[0] + nBytesToEncode, tmp.begin());
                        // frame[56] buffer[1234] tmp[56] => frame[12] buffer[1234] tmp[56]
                        std::copy(&frameBuffer_[0], &frameBuffer_[nBytesToEncode], _pFrame->AvFrame()->data[0]);
                        // frame[12] buffer[1234] tmp[56] => frame[12] buffer[3434] tmp[56]
                        std::copy(&frameBuffer_[nBytesToEncode], &frameBuffer_[frameBuffer_.size()], frameBuffer_.begin());
                        // frame[12] buffer[3434] tmp[56] => frame[12] buffer[3456] tmp[56]
                        std::copy(tmp.begin(), tmp.end(), &frameBuffer_[frameBuffer_.size() - nBytesToEncode]);
                }
        }

        // encoder loop
        while (nBytesToEncode > 0) {
                int nStatus;

                // fill bytes into encoder
                while (nBytesToEncode > 0) {
                        if (nBytesToEncode < nBytesToEncodeEachTime) {
                                int nOldSize = frameBuffer_.size();
                                // confusing part: we need do following steps
                                // restBytes[xxx] buffer[yy] => restBytes[xxx] buffer[yy...]
                                frameBuffer_.resize(frameBuffer_.size() + nBytesToEncode);
                                // restBytes[xxx] buffer[yy...] => restBytes[xxx] buffer[yy.yy]
                                std::copy_backward(&frameBuffer_[0], &frameBuffer_[nOldSize], frameBuffer_.end());
                                // restBytes[xxx] buffer[yy.yy] => restBytes[xxx] buffer[xxxyy]
                                std::copy(_pFrame->AvFrame()->data[0], _pFrame->AvFrame()->data[0] + nBytesToEncode , frameBuffer_.begin());

                                if (frameBuffer_.size() >= static_cast<unsigned>(nBytesToEncodeEachTime)) {
                                        // now data in buffer can be encoded to 1 frame
                                        // confusing part: we need do following steps, suppose nBytesToEncodeEachTime=5bytes
                                        // buffer[1234567] frame[] => buffer[1234567] frame[12345]
                                        std::copy(&frameBuffer_[0], &frameBuffer_[nBytesToEncodeEachTime], _pFrame->AvFrame()->data[0]);
                                        // buffer[1234567] frame[12345] => buffer[6734567] frame[12345]
                                        std::copy(&frameBuffer_[nBytesToEncodeEachTime], &frameBuffer_[frameBuffer_.size()], &frameBuffer_[0]);
                                        // buffer[6734567] frame[12345] => buffer[67] frame[12345]
                                        frameBuffer_.resize(frameBuffer_.size() - nBytesToEncodeEachTime);
                                        nBytesToEncode = nBytesToEncodeEachTime;
                                } else {
                                        nBytesToEncode = 0;
                                        break;
                                }
                        }

                        DebugPCM("/tmp/rtc.en2.s16", _pFrame->AvFrame()->data[0], _pFrame->AvFrame()->linesize[0]);

                        nStatus = avcodec_send_frame(pAvEncoderContext_, _pFrame->AvFrame());
                        if (nStatus == 0) {
                                // adjust pts of next frame
                                _pFrame->AvFrame()->pts += pAvEncoderContext_->frame_size * 1000 / pAvEncoderContext_->sample_rate;

                                if (nBytesToEncode == 0) {
                                        break;
                                }
                                if (nBytesToEncode >= nBytesToEncodeEachTime) {
                                        // 1 frame
                                        std::copy(_pFrame->AvFrame()->data[0] + nBytesToEncodeEachTime,
                                                  _pFrame->AvFrame()->data[0] + nBytesToEncode, _pFrame->AvFrame()->data[0]);
                                        nBytesToEncode -= nBytesToEncodeEachTime;
                                }
                        } else if (nStatus == AVERROR(EAGAIN)) {
                                break; // goto receive packet
                        } else {
                                XError("aac encoder: could not send frame, status=%d", nStatus);
                                return -1;
                        }
                }

                // read bytes from encoder
                while (1) {
                        auto pPacket = std::make_shared<MediaPacket>();
                        pPacket->Stream(STREAM_AUDIO);
                        pPacket->Codec(CODEC_AAC);
                        pPacket->SampleRate(pAvEncoderContext_->sample_rate);
                        pPacket->Channels(pAvEncoderContext_->channels);
                        pPacket->AvPacket()->data = nullptr;
                        pPacket->AvPacket()->size = 0;

                        nStatus = avcodec_receive_packet(pAvEncoderContext_, pPacket->AvPacket());
                        if (nStatus == 0) {
                                nStatus = _callback(pPacket);
                                if (nStatus < 0) {
                                        return nStatus;
                                }
                        } else if (nStatus == AVERROR(EAGAIN)) {
                                break; // back to send packet
                        } else {
                                XError("aac encoder: could not receive frame, status=%d", nStatus);
                                pPacket->Print();
                                return -1;
                        }
                }
        }

        return 0;
}

int AvEncoder::EncodeH264(IN std::shared_ptr<MediaFrame>& _pFrame, IN EncoderHandlerType& _callback)
{
        // set initial param before encoding
        _pFrame->AvFrame()->pict_type = AV_PICTURE_TYPE_NONE;

        do {
                bool bNeedSendAgain = false;
                int nStatus = avcodec_send_frame(pAvEncoderContext_, _pFrame->AvFrame());
                if (nStatus != 0) {
                        if (nStatus == AVERROR(EAGAIN)) {
                                //Warn("internal: assert failed, we should not get EAGAIN");
                                bNeedSendAgain = true;
                        } else {
                                XError("h264 encoder: could not send frame, status=%d", nStatus);
                                return -1;
                        }
                }

                while (1) {
                        auto pPacket = std::make_shared<MediaPacket>();
                        pPacket->Stream(STREAM_VIDEO);
                        pPacket->Codec(CODEC_H264);
                        pPacket->Width(pAvEncoderContext_->width);
                        pPacket->Height(pAvEncoderContext_->height);
                        pPacket->AvPacket()->data = nullptr;
                        pPacket->AvPacket()->size = 0;

                        nStatus = avcodec_receive_packet(pAvEncoderContext_, pPacket->AvPacket());
                        if (nStatus == 0) {
                                if (!SeiQueue.empty()) {
                                        auto packet = SeiQueue.front();
                                        XInfo("SeiQueue size %lu, packet size %d", SeiQueue.size(), packet->size);
                                        if (auto ret = pPacket->AppendSEI(packet->data, packet->size) < 0 ) {
                                                XError("h264 encoder: could not AppendSEI");
                                        }
                                        SeiQueue.pop_front();
                                        XInfo("SeiQueue size %lu, packet size %d pop", SeiQueue.size(), packet->size);
                                        av_free_packet(packet);
                                }

                                nStatus = _callback(pPacket);
                                if (nStatus < 0) {
                                        return nStatus;
                                }
                                if (bNeedSendAgain) {
                                        break;
                                }
                        } else if (nStatus == AVERROR(EAGAIN)) {
                                return 0;
                        } else {
                                XError("h264 encoder: could not receive frame, status=%d", nStatus);
                                pPacket->Print();
                                return -1;
                        }

                        /*
                        if (!SeiQueue.empty()) {
                               auto iter = SeiQueue.rbegin();
                               if(iter != SeiQueue.rend()) {
                                        auto packet = *iter;
                                        auto mPacket = std::make_shared<MediaPacket>();
                                        mPacket->Stream(STREAM_VIDEO);
                                        mPacket->Codec(CODEC_H264);
                                        mPacket->Width(pAvEncoderContext_->width);
                                        mPacket->Height(pAvEncoderContext_->height);
                                        memcpy(mPacket->AvPacket()->data, packet->data, packet->size);
                                        mPacket->AvPacket()->size = packet->size;
                                        av_free_packet(packet);
                                        nStatus = _callback(mPacket);
                                        if (nStatus < 0) {
                                                return nStatus;
                                        }
                               }
                        }*/
                }
        } while(1);

        return 0;
}

void AvEncoder::Bitrate(IN int _nBitrate)
{
        nBitrate_ = _nBitrate;
}

//
// meta data
//

FlvMeta::FlvMeta()
{
        payload_.reserve(128);
        PutDefaultMetaInfo();
}

char* FlvMeta::Data() const
{
        return const_cast<char*>(payload_.data());
}

int FlvMeta::Size() const
{
        return payload_.size();
}

void FlvMeta::PutByte(IN uint8_t _nValue)
{
        payload_.push_back(static_cast<char>(_nValue));
}

void FlvMeta::PutBe16(IN uint16_t _nValue)
{
        char output[2];
        output[1] = _nValue & 0xff;
        output[0] = _nValue >> 8;
        payload_.insert(payload_.end(), output, output + 2);
}

void FlvMeta::PutBe24(IN uint32_t _nValue)
{
        char output[3];
        output[2] = _nValue & 0xff;
        output[1] = _nValue >> 8;
        output[0] = _nValue >> 16;
        payload_.insert(payload_.end(), output, output + 3);
}

void FlvMeta::PutBe32(IN uint32_t _nValue)
{
        char output[4];
        output[3] = _nValue & 0xff;
        output[2] = _nValue >> 8;
        output[1] = _nValue >> 16;
        output[0] = _nValue >> 24;
        payload_.insert(payload_.end(), output, output + 4);
}

void FlvMeta::PutBe64(IN uint64_t _nValue)
{
        PutBe32(_nValue >> 32);
        PutBe32(_nValue);
}

void FlvMeta::PutAmfString(IN const char* _pString)
{
        uint16_t nLength = strlen(_pString);
        PutBe16(nLength);
        payload_.insert(payload_.end(), _pString, _pString + nLength);
}

void FlvMeta::PutAmfDouble(IN double _dValue)
{
        char output[9];
        output[0] = AMF_NUMBER;  /* type: Number */
        {
                char *ci, *co;
                ci = reinterpret_cast<char*>(&_dValue);
                co = &output[1];
                co[0] = ci[7];
                co[1] = ci[6];
                co[2] = ci[5];
                co[3] = ci[4];
                co[4] = ci[3];
                co[5] = ci[2];
                co[6] = ci[1];
                co[7] = ci[0];
        }
        payload_.insert(payload_.end(), output, output + 9);
}

void FlvMeta::PutPacketMetaInfo(IN const MediaPacket& _packet)
{
        // put packet info into the metadata group
        if (_packet.Stream() == STREAM_AUDIO) {
                if (_packet.SampleRate() > 0) {
                        PutAmfString("samplerate");
                        PutAmfDouble(_packet.SampleRate());
                }
        } else if (_packet.Stream() == STREAM_VIDEO) {
                if (_packet.Width() > 0) {
                        PutAmfString("width");
                        PutAmfDouble(_packet.Width());
                }
                if (_packet.Height() > 0) {
                        PutAmfString("height");
                        PutAmfDouble(_packet.Height());
                }
        }
}

void FlvMeta::PutDefaultMetaInfo()
{
        PutByte(AMF_STRING);
        PutAmfString("@setDataFrame");
        PutByte(AMF_STRING);
        PutAmfString("onMetaData");
        PutByte(AMF_OBJECT);
        PutAmfString("copyright");
        PutByte(AMF_STRING);
        PutAmfString("pub_toolkit:v3.0");
}

//
// AdtsHeader
//

bool AdtsHeader::Parse(IN const char* _pBuffer)
{
        // headers begin with FFFxxxxx...
        if ((unsigned char)_pBuffer[0] == 0xff && (((unsigned char)_pBuffer[1] & 0xf0) == 0xf0)) {
                nSyncWord = ((_pBuffer[0] << 4) | (_pBuffer[1] >> 4)) & 0xfff;
                nId = ((unsigned int)_pBuffer[1] & 0x08) >> 3;
                nLayer = ((unsigned int)_pBuffer[1] & 0x06) >> 1;
                nProtectionAbsent = (unsigned int)_pBuffer[1] & 0x01;
                nProfile = ((unsigned int)_pBuffer[2] & 0xc0) >> 6;
                nSfIndex = ((unsigned int)_pBuffer[2] & 0x3c) >> 2;
                nPrivateBit = ((unsigned int)_pBuffer[2] & 0x02) >> 1;
                nChannelConfiguration = ((((unsigned int)_pBuffer[2] & 0x01) << 2) | (((unsigned int)_pBuffer[3] & 0xc0) >> 6));
                nOriginal = ((unsigned int)_pBuffer[3] & 0x20) >> 5;
                nHome = ((unsigned int)_pBuffer[3] & 0x10) >> 4;
                nCopyrightIdentificationBit = ((unsigned int)_pBuffer[3] & 0x08) >> 3;
                nCopyrigthIdentificationStart = (unsigned int)_pBuffer[3] & 0x04 >> 2;
                nAacFrameLength = (((((unsigned int)_pBuffer[3]) & 0x03) << 11) |
                                           (((unsigned int)_pBuffer[4] & 0xFF) << 3) |
                                           ((unsigned int)_pBuffer[5] & 0xE0) >> 5);
                nAdtsBufferFullness = (((unsigned int)_pBuffer[5] & 0x1f) << 6 | ((unsigned int)_pBuffer[6] & 0xfc) >> 2);
                nNoRawDataBlocksInFrame = ((unsigned int)_pBuffer[6] & 0x03);
                return true;
        } else {
                //Warn("wrong AAC ADTS header");
                return false;
        }
}

bool AdtsHeader::GetBuffer(OUT char* _pBuffer, OUT size_t* _pSize)
{
        _pBuffer[0] = ((nSyncWord >> 4) & 0xff);
        _pBuffer[1] = (((nSyncWord & 0xf) << 4) | ((nId & 0x1) << 3) | ((nLayer & 0x3) << 1) | (nProtectionAbsent & 0x1));
        _pBuffer[2] = (((nProfile & 0x3) << 6) | ((nSfIndex & 0xf) << 2) |
                       ((nPrivateBit & 0x1) << 1) | ((nChannelConfiguration >> 2) & 0x1));
        _pBuffer[3] = (((nChannelConfiguration & 0x3) << 6) | ((nOriginal & 0x1) << 5) | ((nHome & 0x1) << 4) |
                       ((nCopyrightIdentificationBit & 0x1) << 3) | ((nCopyrigthIdentificationStart & 0x1) << 2) |
                       ((nAacFrameLength >> 11) & 0x3));
        _pBuffer[4] = ((nAacFrameLength >> 3) & 0xff);
        _pBuffer[5] = (((nAacFrameLength & 0x7) << 5) | ((nAdtsBufferFullness >> 6) & 0x1f));
        _pBuffer[6] = (((nAdtsBufferFullness & 0x3f) << 2) | (nNoRawDataBlocksInFrame & 0x3));

        if (_pSize != nullptr) {
                *_pSize = 7;
        }
        return true;
}

void AdtsHeader::Dump()
{
        Verbose("===== Adts Header =====");
        Verbose("nSyncWord=0x%x", nSyncWord);
        Verbose("nId=0x%x", nId);
        Verbose("nLayer=0x%x", nLayer);
        Verbose("nProtectionAbsent=0x%x", nProtectionAbsent);
        Verbose("nProfile=0x%x", nProfile);
        Verbose("nSfIndex=0x%x", nSfIndex);
        Verbose("nPrivateBit=0x%x", nPrivateBit);
        Verbose("nChannelConfiguration=0x%x", nChannelConfiguration);
        Verbose("nOriginal=0x%x", nOriginal);
        Verbose("nHome=0x%x", nHome);
        Verbose("nCopyrightIdentificationBit=0x%x", nCopyrightIdentificationBit);
        Verbose("nCopyrigthIdentificationStart=0x%x", nCopyrigthIdentificationStart);
        Verbose("nAacFrameLength=0x%x", nAacFrameLength);
        Verbose("nAdtsBufferFullness=0x%x", nAdtsBufferFullness);
        Verbose("nNoRawDataBlocksInFrame=0x%x", nNoRawDataBlocksInFrame);
        Verbose("=======================");
}

//
// H264Nalu
//

H264Nalu::H264Nalu(const char* pBuffer, int nSize)
{
        payload_.resize(nSize);
        std::copy(pBuffer, pBuffer + nSize, payload_.begin());
}

char* H264Nalu::Data() const
{
        return const_cast<char*>(payload_.data());
}

int H264Nalu::Size() const
{
        return payload_.size();
}

int H264Nalu::Type() const
{
        if (payload_.size() == 0) {
                //Warn("h264 NALU data size=0");
                return -1;
        }
        return payload_[0] & 0xf;
}

//
// RtmpSender
//

////////////////////////////////////////////
//           FLV specification
////////////////////////////////////////////

//
// audio tag
//

// format of SoundData
#define SOUND_FORMAT_LINEAR_PCM_PE   0  // Linear PCM, platform endian
#define SOUND_FORMAT_ADPCM           1  // ADPCM
#define SOUND_FORMAT_MP3             2  // MP3
#define SOUND_FORMAT_LINEAR_PCM_LE   3  // Linear PCM, little endian
#define SOUND_FORMAT_NELLYMOSER_16K  4  // Nellymoser 16 kHz mono
#define SOUND_FORMAT_NELLYMOSER_8K   5  // Nellymose 8 kHz mono
#define SOUND_FORMAT_NELLYMOSER      6  // Nellymoser
#define SOUND_FORMAT_G711A           7  // G.711 A-law logarithmic PCM
#define SOUND_FORMAT_G711U           8  // G.711 mu-law logarithmic PCM
#define SOUND_FORMAT_RESERVED        9  // reserved
#define SOUND_FORMAT_AAC             10 // AAC
#define SOUND_FORMT_SPEEX            11 // Speex
#define SOUND_FORMAT_MP3_8K          14 // MP3 8 kHz
#define SOUND_FORMAT_DEV_SPEC        15 // Device-specific sound

// sampling rate. The following values are defined
#define SOUND_RATE_5_5K  0 // 5.5 kHz
#define SOUND_RATE_11K   1 // 11 kHz
#define SOUND_RATE_22K   2 // 22 kHz
#define SOUND_RATE_44K   3 // 44 kHz

inline int FLV_SOUND_RATE_VALUE(int sr)
{
        switch (sr) {
        case SOUND_RATE_5_5K: return 5500;
        case SOUND_RATE_11K:  return 11025;
        case SOUND_RATE_22K:  return 22050;
        case SOUND_RATE_44K:  return 44100;
        default: return 0;
        }
}

// size of each audio sample
#define SOUND_SIZE_8BIT   0 // 8-bit samples
#define SOUND_SIZE_16BIT  1 // 16-bit samples

// mono or stereo sound
#define SOUND_TYPE_MONO   0 // Mono sound
#define SOUND_TYPE_STEREO 1 // Stereo sound

inline int FLV_SOUND_TYPE_VALUE(int ch)
{
        switch (ch) {
        case SOUND_TYPE_MONO: return 1;
        case SOUND_TYPE_STEREO: return 2;
        default: return 0;
        }
}

// AAC specific field
#define SOUND_AAC_TYPE_SEQ_HEADER 0 // AAC sequence header
#define SOUND_AAC_TYPE_RAW        1 // AAC raw


//
// video tag
//

// video frame type
#define VIDEO_FRAME_TYPE_KEY_FRAME         1
#define VIDEO_FRAME_TYPE_INTER_FRAME       2
#define VIDEO_FRAME_TYPE_DISP_INTER_FRAME  3
#define VIDEO_FRAME_TYPE_GEN_KEY_FRAME     4
#define VIDEO_FRAME_TYPE_INFO_FRAME        5

// video codec ID
#define VIDEO_CODEC_H263            2
#define VIDEO_CODEC_SCREEN          3
#define VIDEO_CODEC_VP6             4
#define VIDEO_CODEC_VP6_WITH_ALPHA  5
#define VIDEO_CODEC_SCREEN_V2       6
#define VIDEO_CODEC_AVC             7

// AVC packet type
#define AVC_SEQ_HEADER  0
#define AVC_NALU        1
#define AVC_END         2


//
// script tag
//

// script data value
#define SCRIPT_TYPE_NUMBER          0
#define SCRIPT_TYPE_BOOLEAN         1
#define SCRIPT_TYPE_STRING          2
#define SCRIPT_TYPE_OBJECT          3
#define SCRIPT_TYPE_MOVIECLIP       4
#define SCRIPT_TYPE_NULL            5
#define SCRIPT_TYPE_UNDEFINED       6
#define SCRIPT_TYPE_REFERENCE       7
#define SCRIPT_TYPE_ECMA_ARRAY      8
#define SCRIPT_TYPE_OBJ_END_MARKER  9
#define SCRIPT_TYPE_STRICT_ARRAY    10
#define SCRIPT_TYPE_DATE            11
#define SCRIPT_TYPE_LONG_STRING     12


////////////////////////////////////////////
//      Audio Specific Configuration
////////////////////////////////////////////

//
// audio object types
//

#define ASC_OBJTYPE_NULL		0
#define ASC_OBJTYPE_AAC_MAIN		1
#define ASC_OBJTYPE_AAC_LC		2
#define ASC_OBJTYPE_AAC_SSR             3
#define ASC_OBJTYPE_AAC_LTP             4
#define ASC_OBJTYPE_SBR                 5
#define ASC_OBJTYPE_AAC_SCALABLE        6
#define ASC_OBJTYPE_TWINVQ              7
#define ASC_OBJTYPE_CELP                8
#define ASC_OBJTYPE_HXVC                9
#define ASC_OBJTYPE_RESERVED1           10
#define ASC_OBJTYPE_RESERVED2           11
#define ASC_OBJTYPE_TTSI                12
#define ASC_OBJTYPE_MAIN_SYNTHESIS      13
#define ASC_OBJTYPE_WAVETABLE_SYNTHESIS 14
#define ASC_OBJTYPE_GENERAL_MIDI        15
#define ASC_OBJTYPE_ALGORITHMIC_SYNTHESIS_AND_AUDIO_EFFECTS 16
#define ASC_OBJTYPE_ER_AAC_LC           17
#define ASC_OBJTYPE_RESERVED3           18
#define ASC_OBJTYPE_ER_AAC_LTP          19
#define ASC_OBJTYPE_ER_AAC_SCALABLE     20
#define ASC_OBJTYPE_ER_TWINVQ           21
#define ASC_OBJTYPE_ER_BSAC             22
#define ASC_OBJTYPE_ER_AAC_LD           23
#define ASC_OBJTYPE_ER_CELP             24
#define ASC_OBJTYPE_ER_HVXC             25
#define ASC_OBJTYPE_ER_HILN             26
#define ASC_OBJTYPE_ER_PARAMETRIC       27
#define ASC_OBJTYPE_SSC                 28
#define ASC_OBJTYPE_PS                  29
#define ASC_OBJTYPE_MPEG_SURROUND       30
#define ASC_OBJTYPE_ESCAPE_VALUE        31
#define ASC_OBJTYPE_LAYER_1             32
#define ASC_OBJTYPE_LAYER_2             33
#define ASC_OBJTYPE_LAYER_3             34
#define ASC_OBJTYPE_DST                 35
#define ASC_OBJTYPE_ALS                 36
#define ASC_OBJTYPE_SLS                 37
#define ASC_OBJTYPE_SLS_NON_CORE        38
#define ASC_OBJTYPE_ER_AAC_ELD          39
#define ASC_OBJTYPE_SMR_SIMPLE          40
#define ASC_OBJTYPE_SMR_MAIN            41
#define ASC_OBJTYPE_USAC_NO_SBR         42
#define ASC_OBJTYPE_SAOC                43
#define ASC_OBJTYPE_LD_MPEG_SURROUND    44
#define ASC_OBJTYPE_USAC                45

//
// sampling frequencies
//

#define ASC_SF_96000      0
#define ASC_SF_88200      1
#define ASC_SF_64000      2
#define ASC_SF_48000      3
#define ASC_SF_44100      4
#define ASC_SF_32000      5
#define ASC_SF_24000      6
#define ASC_SF_22050      7
#define ASC_SF_16000      8
#define ASC_SF_12000      9
#define ASC_SF_11025      10
#define ASC_SF_8000       11
#define ASC_SF_7350       12
#define ASC_SF_RESERVED_1 13
#define ASC_SF_RESERVED_2 14
#define ASC_SF_CUSTOM     15

inline int ASC_SF_VALUE(int sf)
{
        switch (sf) {
        case ASC_SF_96000: return 96000;
        case ASC_SF_88200: return 88200;
        case ASC_SF_64000: return 64000;
        case ASC_SF_48000: return 48000;
        case ASC_SF_44100: return 44100;
        case ASC_SF_32000: return 32000;
        case ASC_SF_24000: return 24000;
        case ASC_SF_22050: return 22050;
        case ASC_SF_16000: return 16000;
        case ASC_SF_12000: return 12000;
        case ASC_SF_11025: return 11025;
        case ASC_SF_8000:  return 8000;
        case ASC_SF_7350:  return 7350;
        case ASC_SF_RESERVED_1: return 0;
        case ASC_SF_RESERVED_2: return 0;
        case ASC_SF_CUSTOM: return 0;
        default: return 0;
        }
}

//
// channel configurations
//

#define ASC_CHAN_AOT_SPEC         0
#define ASC_CHAN_FC               1
#define ASC_CHAN_FLR              2
#define ASC_CHAN_FCLR             3
#define ASC_CHAN_FCLR_BC          4
#define ASC_CHAN_FCLR_BLR         5
#define ASC_CHAN_FCLR_BLR_LFE     6
#define ASC_CHAN_FCLR_SLR_BLR_LFE 7
#define ASC_CHAN_RESERVED         8

RtmpSender::RtmpSender(std::shared_ptr<XLogger> xl)
{
        xl_ = xl;
        bytesSent_.store(0);
}

void RtmpSender::closeRtmpReset()
{
        closeRtmp();
}

void RtmpSender::closeRtmp()
{
        if (pRtmp_ != nullptr) {
                RTMP_Close(pRtmp_);
                RTMP_Free(pRtmp_);
                pRtmp_ = nullptr;
        }
}

RtmpSender::~RtmpSender()
{
        closeRtmp();
}

int RtmpSender::Send(IN const std::string& url, IN const std::shared_ptr<MediaPacket>& _pPacket)
{
        if (pRtmp_ == nullptr && pFlvFile_ == nullptr) {
                if (!strncmp(url.c_str(), "rtmp://", 7)) {
                        XInfo("rtmp: init");
                        pRtmp_ = RTMP_Alloc();
                        RTMP_Init(pRtmp_);
                        pRtmp_->reqid = (char *)strdup(xl_->reqid_.c_str());
                } else {
                        XInfo("flv: create %s", url.c_str());
                        pFlvFile_ = FlvFile::Create(url);
                        if (pFlvFile_ == nullptr) {
                                return -1;
                        }
                        if (pFlvFile_->WriteHeader() < 0) {
                                return -1;
                        }
                        keepSpsPpsInNalus_ = true;
                        useAnnexbConcatNalus_ = true;
                        dontSendMetadata_ = true;
                }
        }

        // connect to the server for the first time to send data
        if (pRtmp_ != nullptr && RTMP_IsConnected(pRtmp_) == 0) {
                if (firstConnected && dontReconnect_) {
                    return 0;
                }

                if (firstConnected && bH264ConfigSent_) {
                    XError("reconnect should sent config")
                    return -1;
                }

                url_ = url;

                XInfo("rtmp: connecting to %s, dontReconnect %d", url_.c_str(), dontReconnect_);

                if (RTMP_SetupURL(pRtmp_, const_cast<char*>(url_.c_str())) == 0) {
                        XError("rtmp: setup URL");
                        closeRtmpReset();
                        return -1;
                }
                RTMP_EnableWrite(pRtmp_);
                if (RTMP_Connect(pRtmp_, nullptr) == 0) {
                        XError("rtmp: connect failed");
                        closeRtmpReset();
                        return -1;
                }
                if (RTMP_ConnectStream(pRtmp_, 0) == 0) {
                        XError("rtmp: connect stream failed");
                        closeRtmpReset();
                        return -1;
                }

                if (SendChunkSize(4096) < 0) {
                        XError("rtmp: chunk size not sent");
                        closeRtmpReset();
                        return -1;
                }
                firstConnected = true;
                XInfo("rtmp: connection is established");
        }

        if (!dontSendMetadata_) {
                SendStreamMetaInfo(*_pPacket);
        }

        // send RTMP supported data
        int nStatus = 0;
        switch(_pPacket->Codec()) {
        case CODEC_H264:
                nStatus = SendH264Packet(*_pPacket); break;
        case CODEC_AAC:
                nStatus = SendAacPacket(*_pPacket); break;
        case CODEC_MP3:
                nStatus = SendMp3Packet(*_pPacket); break;
        case CODEC_FLV_METADATA:
                nStatus = SendMetadataPacket(*_pPacket); break;
        default:
                if (_pPacket->Stream() == STREAM_AUDIO || _pPacket->Stream() == STREAM_VIDEO) {
                        XWarn("rtmp: stream=%d, codec=%d not supported", _pPacket->Stream(), _pPacket->Codec());
                }
                return 0; // do nothing
        }

        int64_t bytesSent = 0;
        if (pRtmp_ != nullptr) {
                bytesSent = pRtmp_->bytesSent;
                pRtmp_->bytesSent = 0;
        } else if (pFlvFile_ != nullptr) {
                bytesSent = pFlvFile_->bytesWritten;
                pFlvFile_->bytesWritten = 0;
        }

        bytesSent_.fetch_add(bytesSent);

        return nStatus;
}

int RtmpSender::SendChunkSize(IN size_t _nSize)
{
        std::vector<char> body(4);
        body[0] = _nSize >> 24;
        body[1] = _nSize >> 16;
        body[2] = _nSize >> 8;
        body[3] = _nSize & 0xff;

        pRtmp_->m_outChunkSize = _nSize;

        return SendPacket(RTMP_PACKET_TYPE_CHUNK_SIZE, body.data(), body.size(), 0, true);
}

// MP3

int RtmpSender::SendMp3Packet(IN const MediaPacket& _packet)
{
        char chHeader = GetAudioTagHeader(SOUND_FORMAT_MP3, _packet);
        return SendAudioPacket(chHeader, _packet.Data(), _packet.Size(), _packet.Pts());
}

// AAC

int RtmpSender::SendAacPacket(IN const MediaPacket& _packet)
{
        // send sequence header
        if (!bAacConfigSent_) {
                if (SendAacConfig(_packet) == 0) {
                        bAacConfigSent_ = true;
                }
        }

        // get aac adts header length
        int nAdtsHeaderLen = 0;
        AdtsHeader header;
        if (header.Parse(_packet.Data()) == true) {
                nAdtsHeaderLen = header.nProtectionAbsent == 1 ? 7 : 9;
        }

        // audio header
        char chHeader = GetAudioTagHeader(SOUND_FORMAT_AAC, SOUND_RATE_44K, SOUND_SIZE_16BIT, SOUND_TYPE_STEREO);
        return SendAudioPacket(chHeader, _packet.Data() + nAdtsHeaderLen, _packet.Size() - nAdtsHeaderLen, _packet.Pts());
}

int RtmpSender::SendAacConfig(IN const MediaPacket& _packet)
{
        std::vector<char> body(4);

        body[0] = GetAudioTagHeader(SOUND_FORMAT_AAC, SOUND_RATE_44K, SOUND_SIZE_16BIT, SOUND_TYPE_STEREO);
        body[1] = SOUND_AAC_TYPE_SEQ_HEADER;

        // spec config bytes
        unsigned int nProfile = 0;
        unsigned int nSfIndex = 0;
        unsigned int nChannel = 0;
        AdtsHeader adts;
        if (adts.Parse(_packet.Data()) == true) {
                nProfile = adts.nProfile + 1;
                nSfIndex = adts.nSfIndex;
                nChannel = adts.nChannelConfiguration;
        } else {
                XWarn("aac seq header: no adts header, use default configuration");
                nProfile = ASC_OBJTYPE_AAC_LC;
                nSfIndex = ASC_SF_44100;
                nChannel = ASC_CHAN_FLR;
        }

        unsigned int nAudioSpecConfig = 0;
        nAudioSpecConfig |= ((nProfile << 11) & 0xf800);
        nAudioSpecConfig |= ((nSfIndex << 7) & 0x0780);
        nAudioSpecConfig |= ((nChannel << 3) & 0x78);
        nAudioSpecConfig |= 0 & 0x7;
        body[2] = (nAudioSpecConfig & 0xff00) >> 8;
        body[3] = nAudioSpecConfig & 0xff;

        XInfo("aac: seq header format: profile=%d, sf=%d, channel=%d", nProfile, nSfIndex, nChannel);
        return SendPacket(RTMP_PACKET_TYPE_AUDIO, body.data(), body.size(), 0, true);
}

char RtmpSender::GetAudioTagHeader(IN char _nFormat, IN const MediaPacket& _packet)
{
        char chSoundType;
        if (_packet.Channels() > 1) {
                chSoundType = SOUND_TYPE_STEREO;
        } else {
                chSoundType = SOUND_TYPE_MONO;
        }

        char chSoundSize;
        switch (_nFormat) {
        case SOUND_FORMAT_MP3:
                chSoundSize = SOUND_SIZE_16BIT; break;
        case SOUND_FORMAT_G711A:
        case SOUND_FORMAT_G711U:
                chSoundSize = SOUND_SIZE_8BIT; break;
        default:
                chSoundSize = SOUND_SIZE_16BIT;
        }

        char chSoundRate;
        if (_packet.SampleRate() < 11000) {
                chSoundRate = SOUND_RATE_5_5K;
        } else if (_packet.SampleRate() < 22000) {
                chSoundRate = SOUND_RATE_11K;
        } else if (_packet.SampleRate() < 44000) {
                chSoundRate = SOUND_RATE_22K;
        } else {
                chSoundRate = SOUND_RATE_44K;
        }

        return GetAudioTagHeader(SOUND_FORMAT_MP3, chSoundRate, chSoundSize, chSoundType);
}

char RtmpSender::GetAudioTagHeader(IN char _nFormat, IN char _nRate, IN char _nSize, IN char _nType)
{
        return static_cast<char>((((_nFormat & 0xf) << 4) |
                                  ((_nRate & 0x3) << 2) |
                                  ((_nSize & 0x1) << 1) |
                                  (_nType & 0x1)));
}

bool RtmpSender::IsAac(char _chHeader)
{
        return (_chHeader == GetAudioTagHeader(SOUND_FORMAT_AAC, SOUND_RATE_44K, SOUND_SIZE_16BIT, SOUND_TYPE_STEREO));
}

// H264

int RtmpSender::SendH264Packet(IN const MediaPacket& _packet)
{
        // try to get multiple h264 frames in one packet
        std::vector<H264Nalu> nalus;
        GetH264Nalu(_packet, nalus);
        if (nalus.size() == 0) {
                XWarn("no h264 frames in the packet");
                _packet.Print();
                return 0; // drop this packet
        }

        //Verbose("SendH264Packet configSent=%d sps=%d pps=%d", bH264ConfigSent_, pSps_!=nullptr, pPps_!=nullptr);

        for (auto it = nalus.begin(); it != nalus.end();) {
                // send sequence header
                if (!bH264ConfigSent_ && pSps_ != nullptr && pPps_!= nullptr) {
                        if (SendH264Config(_packet) != 0) {
                                XWarn("h264 sequence header: not sent");
                                return 1; // send next time
                        }
                        bH264ConfigSent_ = true;
                }

                // send data frame
                switch (it->Type()) {
                case 1:
                case 5:
                        break;
                case 6:
                        it = nalus.erase(it);
                        continue;
                case 7:
                        if (keepSpsPpsInNalus_) {
                                break;
                        }
                        pSps_ = std::make_shared<H264Nalu>(it->Data(), it->Size());
                        it = nalus.erase(it);
                        continue;
                case 8:
                        if (keepSpsPpsInNalus_) {
                                break;
                        }
                        pPps_ = std::make_shared<H264Nalu>(it->Data(), it->Size());
                        it = nalus.erase(it);
                        continue;
                default:
                        // not a data packet, drop it
                        XDebug("h264: nalu type not handled, type=%d", it->Type());
                        it = nalus.erase(it);
                        continue;
                }
                it++;
        }
        return SendH264Nalus(nalus, _packet);
}

int RtmpSender::SendH264Nalus(IN const std::vector<H264Nalu>& _nalus, IN const MediaPacket& _packet)
{
        bool bIsKeyframe = false;
        std::vector<char> body;
        for (auto& nalu : _nalus) {
                if (nalu.Type() == 5) {
                        bIsKeyframe = true;
                }
                int i = body.size();
                body.resize(body.size() + nalu.Size() + 4);
                if (useAnnexbConcatNalus_) {
                        body[i++] = 0;
                        body[i++] = 0;
                        body[i++] = 0;
                        body[i++] = 1;
                } else {
                        body[i++] = nalu.Size() >> 24;
                        body[i++] = nalu.Size() >> 16;
                        body[i++] = nalu.Size() >> 8;
                        body[i++] = nalu.Size() & 0xff;
                }
                std::copy(nalu.Data(), nalu.Data() + nalu.Size(), &body[i]);
        }

        if (bIsKeyframe) {
                return SendH264Idr(body, _packet);
        } else {
                return SendH264NonIdr(body, _packet);
        }
}

int RtmpSender::SendH264Config(IN const MediaPacket& _packet)
{
        if (pSps_ == nullptr || pPps_ == nullptr) {
                XWarn("internal: sps or pps not found");
                return -1;
        }

        std::vector<char> body(pSps_->Size() + pPps_->Size() + 16);
        char *p;
        int i = 0;

        body[i++] = 0x17; // 1:keyframe  7:AVC
        body[i++] = 0x00; // AVC sequence header

        body[i++] = 0x00;
        body[i++] = 0x00;
        body[i++] = 0x00; // fill in 0;

        // AVCDecoderConfigurationRecord.
        p = pSps_->Data();
        body[i++] = 0x01; // configurationVersion
        body[i++] = p[1]; // AVCProfileIndication
        body[i++] = p[2]; // profile_compatibility
        body[i++] = p[3]; // AVCLevelIndication
        body[i++] = 0xff; // lengthSizeMinusOne

        // sps nums
        body[i++] = 0xE1; //&0x1f
        // sps data length
        body[i++] = pSps_->Size() >> 8;
        body[i++] = pSps_->Size() & 0xff;
        // sps data
        std::copy(pSps_->Data(), pSps_->Data() + pSps_->Size(), &body[i]);
        i += pSps_->Size();

        // pps nums
        body[i++] = 0x01; //&0x1f
        // pps data length
        body[i++] = pPps_->Size() >> 8;
        body[i++] = pPps_->Size() & 0xff;
        // sps data
        std::copy(pPps_->Data(), pPps_->Data() + pPps_->Size(), &body[i]);
        i += pPps_->Size();

        XInfo("h264: seq header: sps_size=%d, pps_size=%d", pSps_->Size(), pPps_->Size());
        return SendPacket(RTMP_PACKET_TYPE_VIDEO, body.data(), i, 0, true);
}

int RtmpSender::GetH264StartCodeLen(IN const char* _pData)
{
        for (int i = 0, n = 0; i < 4; i++) {
                if (_pData[i] == 0x00) {
                        n++;
                } else if (_pData[i] == 0x01) {
                        if (n >= 2 && n <= 3) {
                                return n + 1;
                        }
                        break;
                } else {
                        break;
                }
        }
        return 0;
}

int RtmpSender::GetH264Nalu(IN const MediaPacket& _packet, OUT std::vector<H264Nalu>& _nalus)
{
        const char *p = _packet.Data();
        // the packet does not contain any h264 delimiter
        if (GetH264StartCodeLen(p) == 0) {
                _nalus.push_back(H264Nalu(_packet.Data(), _packet.Size()));
                return 0;
        }

        int nStart = 0, nEnd = 0; // initial value
        for (; nEnd < _packet.Size(); nEnd++) {
                int nLen = GetH264StartCodeLen(&p[nEnd]);
                if (nLen > 0) {
                        if (nStart != 0) {
                                // nalu size = nEnd - nStart - n + 1
                                _nalus.push_back(H264Nalu(&p[nStart], nEnd - nStart));
                        }
                        nEnd += nLen;
                        nStart = nEnd;
                        continue;
                }
        }
        if (nEnd > nStart) {
                _nalus.push_back(H264Nalu(&p[nStart], nEnd - nStart));
        }
        return 0;
}

int RtmpSender::SendH264NonIdr(IN const std::vector<char>& _buffer, IN const MediaPacket& _packet)
{
        return SendH264Packet(_buffer.data(), _buffer.size(), _packet.Dts(), false, _packet.Pts() - _packet.Dts());
}

int RtmpSender::SendH264Idr(IN const std::vector<char>& _buffer, IN const MediaPacket& _packet)
{
        return SendH264Packet(_buffer.data(), _buffer.size(), _packet.Dts(), true, _packet.Pts() - _packet.Dts());
}

int RtmpSender::SendH264Packet(IN const char* _pData, IN size_t _nSize, IN size_t _nTimestamp,
                               IN bool _bIsKeyFrame, size_t _nCompositionTime)
{
        std::vector<char> body(_nSize + 5);
        int i = 0;

        if (_bIsKeyFrame) {
                body[i++] = 0x17; // 1:Iframe 7:AVC
        } else {
                body[i++] = 0x27; // 2:Pframe 7:AVC
        }
        body[i++] = 0x01; // AVC NALU

        // composition time adjustment
        body[i++] = _nCompositionTime >> 16;
        body[i++] = _nCompositionTime >> 8;
        body[i++] = _nCompositionTime & 0xff;

        // NALU data
        std::copy(_pData, _pData + _nSize, &body[i]);

        return SendPacket(RTMP_PACKET_TYPE_VIDEO, body.data(), body.size(), _nTimestamp);
}

// FLV METADATA

int RtmpSender::SendMetadataPacket(IN const MediaPacket& _packet)
{
        return SendPacket(RTMP_PACKET_TYPE_INFO, _packet.Data(), _packet.Size(),  _packet.Pts(), true);
}

int RtmpSender::SendStreamMetaInfo(IN const MediaPacket& _packet)
{
        if (_packet.Stream() != STREAM_AUDIO && _packet.Stream() != STREAM_VIDEO) {
                return 0;
        }
        if ((_packet.Stream() == STREAM_AUDIO && bAudioMetaSent_) ||
            (_packet.Stream() == STREAM_VIDEO && bVideoMetaSent_)) {
                return 0;
        }

        metadata_.PutPacketMetaInfo(_packet);

        FlvMeta meta(metadata_);
        meta.PutAmfString("");
        meta.PutByte(AMF_OBJECT_END);

        if (SendPacket(RTMP_PACKET_TYPE_INFO, meta.Data(), meta.Size(), 0, true) < 0) {
                XWarn("metadata information not sent, stream=%d, codec=%d", _packet.Stream(), _packet.Codec());
                return -1;
        } else {
                if (_packet.Stream() == STREAM_AUDIO) {
                        bAudioMetaSent_ = true;
                } else {
                        bVideoMetaSent_ = true;
                }
        }

        return 0;
}

// send RTMP packet

int RtmpSender::SendAudioPacket(IN const char _chHeader, IN const char* _pData, IN size_t _nSize, IN size_t _nTimestamp)
{
        std::vector<char> body;

        // aac use 1 more bit to tell diff from seqheader and raw
        if (IsAac(_chHeader)) {
                body.resize(_nSize + 2);
                body[1] = SOUND_AAC_TYPE_RAW;
                std::copy(_pData, _pData + _nSize, &body[2]);
        } else {
                body.resize(_nSize + 1);
                std::copy(_pData, _pData + _nSize, &body[1]);
        }
        body[0] = _chHeader;

        return SendPacket(RTMP_PACKET_TYPE_AUDIO, body.data(), body.size(), _nTimestamp);
}

int RtmpSender::SendPacket(IN unsigned int _nPacketType, IN const char* _pData, IN size_t _nSize, IN size_t _nTimestamp, bool dontDrop)
{
        return SendRawPacket(_nPacketType, RTMP_PACKET_SIZE_LARGE, _pData, _nSize, _nTimestamp, dontDrop);
}

int RtmpSender::SendRawPacket(IN unsigned int _nPacketType, IN int _nHeaderType,
                              IN const char* _pData, IN size_t _nSize, IN size_t _nTimestamp, bool dontDrop)
{
        RTMPPacket packet = {};
        RTMPPacket_Alloc(&packet, _nSize);
        RTMPPacket_Reset(&packet);

        packet.m_headerType = _nHeaderType;
        if (pRtmp_ != nullptr) {
                packet.m_nInfoField2 = pRtmp_->m_stream_id;
        }

        packet.m_packetType = _nPacketType;
        switch (_nPacketType) {
        case RTMP_PACKET_TYPE_AUDIO:
                packet.m_nChannel = CHANNEL_AUDIO;
                break;
        case RTMP_PACKET_TYPE_VIDEO:
                packet.m_nChannel = CHANNEL_VIDEO;
                break;                
        case RTMP_PACKET_TYPE_INFO:
                packet.m_nChannel = CHANNEL_META;
                break;
        case RTMP_PACKET_TYPE_CHUNK_SIZE:
                packet.m_nChannel = CHANNEL_CHUNK;
                packet.m_nInfoField2 = 0;
                break;
        default:
                XWarn("internal: rtmp packet type=%d not recognized", _nPacketType);
                return -1;
        }
        packet.m_nTimeStamp = _nTimestamp;

        // copy payload
        packet.m_nBodySize = _nSize;
        std::copy(_pData, _pData + _nSize, packet.m_body);

        int r = -1;
        // send to target server
        if (pRtmp_ != nullptr) {
                if (!RTMP_SendPacket(pRtmp_, &packet, 0)) {
                        goto out_free;
                }
        } else if (pFlvFile_ != nullptr) {
                if (pFlvFile_->WritePacket(&packet) < 0) {
                        goto out_free;
                }
        }
        r = 0;

out_free:
        RTMPPacket_Free(&packet);
        return r;
}

ssize_t RtmpSender::AccTimestamp(IN const size_t& _nNow, OUT size_t& _nBase, OUT size_t& _nSequence)
{
        if (static_cast<ssize_t>(_nNow) < 0) {
                XWarn("nagative pts=%lu", _nNow);
                return -1;
        }

        size_t nTimestamp;
        if (_nBase == 0) {
                _nBase = _nNow;
                nTimestamp = 0;
        } else {
                nTimestamp = _nNow - _nBase;
        }
        _nSequence++;

        return nTimestamp;
}

RtmpSink::RtmpSink(const std::string& url, std::shared_ptr<XLogger> xl): 
        muxedQ_(100),
        audiobufQ_(100),
        videobufQ_(100),
        resampler_(xl)

{
        url_ = url;
        bSenderExit_.store(false);
        rtmpSender_ = std::make_unique<RtmpSender>(xl);
        xl_ = xl;
}

static const char *packetStreamTypeString(int t) {
        switch (t) {
        case STREAM_VIDEO:
                return "video";
        case STREAM_AUDIO:
                return "audio";
        }
        return "";
}

void RtmpSink::OnStart() {
        auto snd = [&] {
                int senddelay = 0;
                bool firstPktSent = false;

                auto sendPacket = [&](IN const std::shared_ptr<MediaPacket>& send) -> int {
                        if (!firstPktSent) {
                                firstPktSent = true;
                                XInfo("senddelay %d", senddelay);
                        }
                        XDebug("RtmpSinkSend type=%s dts=%d v=%zu a=%zu", packetStreamTypeString(send->Stream()), 
                                int(send->Dts()), videobufQ_.Size(), audiobufQ_.Size());
                        rtmpSender_->SetDontReconnect(dont_reconnect);
                        return rtmpSender_->Send(url_, send);
                };

                auto popSendOne = [&]() -> int {
                        std::shared_ptr<MediaPacket> a, v, send;
                        auto hasV = videobufQ_.Peek(v);
                        auto hasA = audiobufQ_.Peek(a);
                        bool sendV = false;
                        if (hasV && hasA) {
                                auto vdts = v->Dts();
                                auto adts = a->Dts();
                                if (vdts < adts) {
                                        sendV = true;
                                }
                        } else if (hasV) {
                                sendV = true;
                        }

                        if (sendV) {
                                send = videobufQ_.Pop();
                        } else {
                                send = audiobufQ_.Pop();
                        }
                        
                        return sendPacket(send);
                };

                while (bSenderExit_.load() == false) {
                        auto vEncoder = std::make_unique<AvEncoder>(xl_);
                        auto aEncoder = std::make_unique<AvEncoder>(xl_);

                        vEncoder->nMaxRate_ = videoMaxRate;
                        vEncoder->nMinRate_ = videoMinRate;
                        vEncoder->gop_ = videoGop;
                        vEncoder->fps_ = videoFps;
                        vEncoder->Bitrate(videoKbps);

                        int vencdelay = 0;
                        bool firstVGot = false;

                        auto encoderHook = [&](IN const std::shared_ptr<MediaPacket>& _pPacket) -> int {
                                if (bSenderExit_.load() == true) {
                                        return -1;
                                }

                                //XDebug("RtmpSinkEncode type=%s dts=%lld v=%zu a=%zu", packetStreamTypeString(_pPacket->Stream()), 
                                //        int64_t(_pPacket->Dts()), videobufQ_.Size(), audiobufQ_.Size());

                                if (!firstVGot && _pPacket->Stream() == STREAM_VIDEO) {
                                        firstVGot = true;
                                        XInfo("vencdelay %d", vencdelay);
                                }

                                if (_pPacket->Stream() == STREAM_VIDEO) {
                                        videobufQ_.Push(_pPacket);
                                } else {
                                        audiobufQ_.Push(_pPacket);
                                }

                                if (videobufQ_.Size()+audiobufQ_.Size() > 8) {
                                        return popSendOne();
                                } else {
                                        return 0;
                                }
                        };

                        while (bSenderExit_.load() == false) {
                                std::shared_ptr<MediaFrame> pFrame;
                                if (muxedQ_.PopWithTimeout(pFrame, std::chrono::milliseconds(10)) == false) {
                                        continue;
                                }

                                XDebug("RtmpSinkFrame type=%s dts=%d", packetStreamTypeString(pFrame->Stream()), 
                                        int(pFrame->AvFrame()->pts));

                                int nStatus = 0;
                                if (pFrame->Stream() == STREAM_VIDEO) {
                                        nStatus = vEncoder->Encode(pFrame, encoderHook);
                                        vencdelay++;
                                } else if (pFrame->Stream() == STREAM_AUDIO) {
                                        DebugPCM("/tmp/rtc.en1.s16", pFrame->AvFrame()->data[0], pFrame->AvFrame()->linesize[0]);
                                        nStatus = aEncoder->Encode(pFrame, encoderHook);
                                }
                                senddelay++;

                                if (nStatus != 0 && !dont_reconnect) {
                                        videobufQ_.Clear();
                                        audiobufQ_.Clear();
                                        rtmpSender_ = std::make_unique<RtmpSender>(xl_);
                                        break;
                                }
                        }

                        usleep(1e4);
                }

                rtmpSender_ = nullptr;
        };

        senderThread_ = std::thread(snd);
}

void RtmpSink::OnFrame(const std::shared_ptr<muxer::MediaFrame>& pFrame) {
        //XDebug("RtmpSinkOnFrame type=%s dts=%d", packetStreamTypeString(pFrame->Stream()), 
        //        int(pFrame->AvFrame()->pts));

        if (pFrame->Stream() == STREAM_AUDIO) {
                resampler_.Resample(pFrame, [&](const std::shared_ptr<MediaFrame>& out) {
                        muxedQ_.TryPush(out);
                });
        } else {
                muxedQ_.TryPush(pFrame);
        }
}

void RtmpSink::OnStop() {
        bSenderExit_.store(true);
        if (senderThread_.joinable()) {
                senderThread_.join();
        }
}

//
// Output
//

Output::Output(IN const std::string& _name)
        :OptionMap(),
        name_(_name),
        muxedQ_(100)
{
        bSenderExit_.store(false);
}

Output::~Output()
{
        Stop();
}

std::string Output::Name()
{
        return name_;
}

void Output::Start(IN FrameSender* stream) {
        stream_ = stream;
        onFrame_ = [&](IN std::shared_ptr<MediaFrame>& pFrame) {
                stream_->SendFrame(pFrame);
                return true;
        };
        onStop_ = [&]() {};
}

void Output::Stop()
{
        onStop_();
}

bool Output::Push(IN std::shared_ptr<MediaFrame>& _pFrame)
{
        return onFrame_(_pFrame);
}

