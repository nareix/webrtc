#ifndef __OUTPUT_HPP__
#define __OUTPUT_HPP__

#include "common.hpp"
#include "stream.hpp"
#include "modules/video_coding/codecs/h264/h264_decoder_impl.h"

namespace muxer
{
        typedef const std::function<int(IN const std::shared_ptr<MediaPacket>&)> EncoderHandlerType;
        class AvEncoder
        {
        public:
                AvEncoder(std::shared_ptr<XLogger> xl);
                ~AvEncoder();
                int Encode(IN std::shared_ptr<MediaFrame>& pFrame, IN EncoderHandlerType& callback);
                void Bitrate(IN int nBitrate);
                std::vector<uint8_t> Extradata();
                int gop_ = 50;
                int nMinRate_ = 0;
                int nMaxRate_ = 0;
                int fps_ = 25;
        private:
                void Deinit();
                int Init(IN const std::shared_ptr<MediaFrame>& pFrame);
                int Preset(IN const std::shared_ptr<MediaFrame>& pFrame);
                int PresetAac(IN const std::shared_ptr<MediaFrame>& pFrame);
                int PresetH264(IN const std::shared_ptr<MediaFrame>& pFrame);
                int InitAudioResampling(IN const std::shared_ptr<MediaFrame>& pFrame);
                int ResampleAudio(IN const std::shared_ptr<MediaFrame>& pInFrame, OUT std::shared_ptr<MediaFrame>& pOutFrame);
                int EncodeAac(IN std::shared_ptr<MediaFrame>& pFrame, IN EncoderHandlerType& callback);
                int EncodeH264(IN std::shared_ptr<MediaFrame>& pFrame, IN EncoderHandlerType& callback);
        private:
                AVCodecContext* pAvEncoderContext_ = nullptr;
                bool bIsEncoderAvailable_ = false;
                std::vector<char> frameBuffer_;
                SwrContext* pSwr_ = nullptr; // for resampling
                std::shared_ptr<XLogger> xl_;

                static std::mutex lock_;
                int nBitrate_ = 0;
        public:
                bool useGlobalHeader = false;
        };

        class AvSender
        {
        public:
                virtual int Send(IN const std::string& url, IN const std::shared_ptr<MediaPacket>& pPacket) = 0;
                virtual ~AvSender() {}
        };

        //
        // FlvMeta
        //

        class FlvMeta
        {
        public:
                FlvMeta();
                void PutByte(IN uint8_t nValue);
                void PutBe16(IN uint16_t nValue);
                void PutBe24(IN uint32_t nValue);
                void PutBe32(IN uint32_t nValue);
                void PutBe64(IN uint64_t nValue);
                void PutAmfString(IN const char* pString);
                void PutAmfString(IN const std::string& string);
                void PutAmfDouble(IN double dValue);
                void PutPacketMetaInfo(IN const MediaPacket& packet);
                void PutDefaultMetaInfo();
        public:
                char* Data() const;
                int Size() const;
        private:
                std::vector<char> payload_;
        };

        //
        // AdtsHeader
        //

        class AdtsHeader
        {
        public:
                unsigned int nSyncWord = 0;
                unsigned int nId = 0;
                unsigned int nLayer = 0;
                unsigned int nProtectionAbsent = 0;
                unsigned int nProfile = 0;
                unsigned int nSfIndex = 0;
                unsigned int nPrivateBit = 0;
                unsigned int nChannelConfiguration = 0;
                unsigned int nOriginal = 0;
                unsigned int nHome = 0;

                unsigned int nCopyrightIdentificationBit = 0;
                unsigned int nCopyrigthIdentificationStart = 0;
                unsigned int nAacFrameLength = 0;
                unsigned int nAdtsBufferFullness = 0;

                unsigned int nNoRawDataBlocksInFrame = 0;
        public:
                bool Parse(IN const char* pBuffer);
                bool GetBuffer(OUT char* pBuffer, OUT size_t* pSize = nullptr);
                void Dump();
        };

        //
        // H264Nalu
        //

        class H264Nalu
        {
        public:
                H264Nalu(const char* pBuffer, int nSize);
                char* Data() const;
                int Size() const;
                int Type() const;
        private:
                std::vector<char> payload_;
        };

        class FlvFile {
        public:
                FlvFile(FILE *fp) : fp_(fp) {
                }

                ~FlvFile() {
                        fclose(fp_);
                }

                int WriteHeader() {
                        static uint8_t hdr[] = {
                                'F','L','V',0x01,
                                0,
                                0,0,0,9,
                                0,0,0,0,
                        };
                        if (write(hdr, sizeof(hdr)) < 0) {
                                return -1;
                        }
                        return 0;
                }

                int WritePacket(RTMPPacket *p) {
                        uint8_t hdr[] = {
                                p->m_packetType,
                                (uint8_t)((p->m_nBodySize>>16)&0xff),
                                (uint8_t)((p->m_nBodySize>>8)&0xff),
                                (uint8_t)((p->m_nBodySize>>0)&0xff),
                                (uint8_t)((p->m_nTimeStamp>>16)&0xff),
                                (uint8_t)((p->m_nTimeStamp>>8)&0xff),
                                (uint8_t)((p->m_nTimeStamp>>0)&0xff),
                                (uint8_t)((p->m_nTimeStamp>>24)&0xff),
                                0,0,0,
                        };
                        uint32_t tagsize = p->m_nBodySize+11;
                        uint8_t tail[] = {
                                (uint8_t)((tagsize>>24)&0xff),
                                (uint8_t)((tagsize>>16)&0xff),
                                (uint8_t)((tagsize>>8)&0xff),
                                (uint8_t)((tagsize>>0)&0xff),
                        };
                        if (write(hdr, sizeof(hdr)) < 0) {
                                return -1;
                        }
                        if (write(p->m_body, p->m_nBodySize) < 0) {
                                return -1;
                        }
                        if (write(tail, sizeof(tail)) < 0) {
                                return -1;
                        }
                        if (fflush(fp_) != 0) {
                                return -1;
                        }
                        return 0;
                }

                static std::shared_ptr<FlvFile> Create(const std::string& path) {
                        FILE *fp = fopen(path.c_str(), "wb+");
                        if (fp == NULL) {
                                Error("flv: create %s failed", path.c_str());
                                return nullptr;
                        }
                        return std::make_shared<FlvFile>(fp);
                }

                int64_t bytesWritten = 0;

        private:
                int write(void *buf, size_t len) {
                        bytesWritten += len;
                        return fwrite(buf, 1, len, fp_) != len ? -1 : 0;
                }
                FILE *fp_;
        };

        //
        // RtmpSender
        //

        class RtmpSender final : public AvSender
        {
        public:
                class Observer
                {
                public:
		        virtual ~Observer() = default;
                public:
                        virtual void OnSenderStatus(std::string connectStatus) = 0;
                };
        public:
                RtmpSender(Observer* observer, std::shared_ptr<XLogger> xl);
                ~RtmpSender();
                virtual int Send(IN const std::string& url, IN const std::shared_ptr<MediaPacket>& pPacket);
                std::atomic<int64_t> bytesSent_;
                void SetDontReconnect(IN bool dontReconnect) {dontReconnect_ = dontReconnect;}
                void SetSeiKey(IN const std::string& key) {seiKey_ = key;}
        private:
                std::shared_ptr<XLogger> xl_ = nullptr;
                // send audio
                int SendMp3Packet(IN const MediaPacket& packet);
                int SendAacPacket(IN const MediaPacket& packet);
                int SendAacConfig(IN const MediaPacket& packet);
                char GetAudioTagHeader(IN char nFormat, IN char nRate, IN char nSize, IN char nType);
                char GetAudioTagHeader(IN char nFormat, IN const MediaPacket& packet);
                bool IsAac(char chHeader);

                // send video
                int SendH264Packet(IN const MediaPacket& packet);
                int SendH264Nalus(IN const std::vector<H264Nalu>& nalus);
                int SendH264Config(IN const MediaPacket& packet);
                int GetH264Nalu(IN const MediaPacket& packet, OUT std::vector<H264Nalu>& nalus);
                int SendH264Nalus(IN const std::vector<H264Nalu>& nalus, IN const MediaPacket& packet);
                int GetH264StartCodeLen(IN const char* pData);
                int SendH264NonIdr(IN const std::vector<char>& buffer, IN const MediaPacket& packet);
                int SendH264Idr(IN const std::vector<char>& buffer, IN const MediaPacket& packet);
                int SendH264Packet(IN const char* pData, IN size_t nSize, IN size_t nTimestamp,
                                   IN bool bIsKeyFrame, size_t nCompositionTime);

                // send meta
                int SendStreamMetaInfo(IN const MediaPacket& packet);
                int SendMetadataPacket(IN const MediaPacket& packet);

                // send chunksize
                int SendChunkSize(IN size_t nSize);

                // send data
                int SendAudioPacket(IN const char chHeader, IN const char* pData, IN size_t nSize, IN size_t nTimestamp);

                int SendPacket(IN unsigned int nPacketType, IN const char* pData, IN size_t nSize,
                               IN size_t nTimestamp, bool dontDrop = false);

                // final call to send data
                int SendRawPacket(IN unsigned int nPacketType, IN int nHeaderType,
                                  IN const char* pData, IN size_t nSize, IN size_t nTimestamp, bool dontDrop);

                // calculate timestamp
                ssize_t AccTimestamp(IN const size_t& nNow, OUT size_t& nBase, OUT size_t& nSequence);

                void closeRtmp();
                void closeRtmpReset();

        private:
                // channel
                const int CHANNEL_CHUNK = 0x2;
                const int CHANNEL_VIDEO = 0x4;
                const int CHANNEL_META  = 0x5;
                const int CHANNEL_AUDIO = 0x6;

                // packet count
                size_t nVideoSequence_ = 0;
                size_t nVideoTimestamp_ = 0;
                size_t nAudioSequence_ = 0;
                size_t nAudioTimestamp_ = 0;
                size_t nDataSequence_ = 0;
                size_t nDataTimestamp_ = 0;

                // sequence header
                bool bH264ConfigSent_ = false;
                bool bAacConfigSent_ = false;

                // metadata
                FlvMeta metadata_;
                bool bVideoMetaSent_ = false;
                bool bAudioMetaSent_ = false;

                // URL target rtmp server
                std::string url_;

                // SEIQueue key
                std::string seiKey_;

                // RTMP object from RTMP dump
                RTMP* pRtmp_ = nullptr;
                std::shared_ptr<FlvFile> pFlvFile_ = nullptr;
                bool useAnnexbConcatNalus_ = false;
                bool keepSpsPpsInNalus_ = false;
                bool dontSendMetadata_ = false;
                bool dontReconnect_ = false;
                bool firstConnected = false;

                // sequence header
                std::shared_ptr<H264Nalu> pSps_ = nullptr;
                std::shared_ptr<H264Nalu> pPps_ = nullptr;

                // Observer
                Observer* observer_ = nullptr;
        };

        class RtmpSink: public SinkObserver, public RtmpSender::Observer {
        public:
                class Observer
                {
                public:
                        virtual ~Observer() = default;
                        virtual void OnRtmpSinkStatus(std::string id, std::string connectStatus) = 0;
                };
        public:
                RtmpSink(Observer* observer, const std::string id, const std::string& url, std::shared_ptr<XLogger> xl);

                void OnStart();
                void OnFrame(const std::shared_ptr<muxer::MediaFrame>& frame);
                void OnStop();
                void OnStatBytes(int64_t& bytes) {
                        bytes = rtmpSender_->bytesSent_.exchange(0);
                }
                void SetSeiKey(const std::string &key) {
                        rtmpSender_->SetSeiKey(key);
                }

                // Implement for RtmpSender::Observer
                void OnSenderStatus(std::string connectStatus);

                int videoKbps = 1000;
                int videoGop = 50;
                int videoFps = 25;
                int videoMinRate = 0;
                int videoMaxRate = 0;
                bool dont_reconnect = false;
                std::shared_ptr<XLogger> xl_ = nullptr;

        private:
                std::string id_;
                std::shared_ptr<RtmpSender> rtmpSender_;
                std::string url_;
                std::thread senderThread_;
                std::atomic<bool> bSenderExit_;
                SharedQueue<std::shared_ptr<MediaFrame>> muxedQ_;
                SharedQueue<std::shared_ptr<MediaPacket>> audiobufQ_;
                SharedQueue<std::shared_ptr<MediaPacket>> videobufQ_;
                AudioResampler resampler_;
                Observer* observer_ = nullptr;
        };

        class Output : public OptionMap
        {
        public:
                Output(IN const std::string& name);
                ~Output();
                std::string Name();
                void Start(IN FrameSender* stream);
                void Stop();
                bool Push(IN std::shared_ptr<MediaFrame>& pFrame);

        private:
                FrameSender* stream_;
                std::string name_;
                std::thread sender_;
                std::atomic<bool> bSenderExit_;
                SharedQueue<std::shared_ptr<MediaFrame>> muxedQ_;
                std::function<void ()> onStop_;
                std::function<bool (IN std::shared_ptr<MediaFrame>& pFrame)> onFrame_;
        };
}

#endif
