#ifndef __TEST_HPP__
#define __TEST_HPP__

#include "output.hpp"
#include "packet.hpp"

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
        TestH264ResChangeEncodeFlv();
    }
};

#endif