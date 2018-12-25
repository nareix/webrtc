#ifndef __TEST_HPP__
#define __TEST_HPP__

#include "input.hpp"
#include "output.hpp"
#include "packet.hpp"
#include "common.hpp"

class Tests {
public:
    static std::shared_ptr<muxer::MediaFrame> getVideoFrame(int w, int h, int ts_ms) {
        std::shared_ptr<muxer::MediaFrame> frame = std::make_shared<muxer::MediaFrame>();
        frame->Stream(muxer::STREAM_VIDEO);
        frame->Codec(muxer::CODEC_H264);
        frame->AvFrame()->format = AV_PIX_FMT_YUV420P;
        frame->AvFrame()->height = w;
        frame->AvFrame()->width = h;
        frame->AvFrame()->pts = ts_ms;
        av_frame_get_buffer(frame->AvFrame(), 32);
        return frame;
    }

    static void TestAACDecode() {
        auto decoder = new muxer::AvDecoder(nullptr);
        auto resampler = new muxer::AudioResampler(nullptr);
        resampler->frameSize = muxer::AudioResampler::SAMPLE_RATE/100; // 10ms

        auto resampleCb = [&](const std::shared_ptr<muxer::MediaFrame>& out) {
            Info("resample out %d", out->AvFrame()->linesize[0]);
        };

        auto decodeCb = [&](const std::shared_ptr<muxer::MediaFrame>& frame) -> int {
            resampler->Resample(frame, resampleCb);
            return 0;
        };

        uint8_t extradata_[] = {17,144,};
        uint8_t data_[] = {33,33,69,0,20,80,1,70,255,241,10,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,90,93,233,162,20,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,188,};
        auto extradata = std::vector<uint8_t>(extradata_, extradata_+sizeof(extradata_));
        auto data = std::vector<uint8_t>(data_, data_+sizeof(data_));

        auto pkt = std::make_unique<muxer::MediaPacket>(muxer::STREAM_AUDIO, muxer::CODEC_AAC, extradata, data);
        int r = decoder->Decode(pkt, decodeCb);
        Info("decode %d", r);
    }

    static void TestAACEncode() {
        auto encoder = new muxer::AvEncoder(nullptr);
        encoder->useGlobalHeader = true;

        auto frame = std::make_shared<muxer::MediaFrame>();

        frame->Stream(muxer::STREAM_AUDIO);
        frame->Codec(muxer::CODEC_AAC);
        frame->AvFrame()->format = AV_SAMPLE_FMT_S16;
        frame->AvFrame()->channel_layout = AV_CH_LAYOUT_MONO;
        frame->AvFrame()->sample_rate = 44100;
        frame->AvFrame()->channels = 1;
        frame->AvFrame()->nb_samples = 1024;
        frame->AvFrame()->pts = 0;
        av_frame_get_buffer(frame->AvFrame(), 0);

        auto encodeCb = [&](IN const std::shared_ptr<muxer::MediaPacket>& pkt) -> int {
            Info("encodeCb");
            return 0;
        };

        int r = encoder->Encode(frame, encodeCb);
        Info("encode %d", r);
    }

    static void TestH264ResChangeEncodeFlv() {
        auto s = new muxer::RtmpSink("/tmp/t.flv", std::make_shared<XLogger>());
        s->OnStart();

        int ts_ms = 0;

        for (int i = 0; i < 40; i++) {
            s->OnFrame(getVideoFrame(400, 400, ts_ms));
            ts_ms += 40;
            usleep(40*1000);
        }
        for (int i = 0; i < 40; i++) {
            s->OnFrame(getVideoFrame(200, 200, ts_ms));
            ts_ms += 40;
            usleep(40*1000);
        }
    }

    static void Run() {
        TestAACEncode();
    }
};

#endif