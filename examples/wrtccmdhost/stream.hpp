#ifndef __STREAM_HPP__
#define __STREAM_HPP__

#include "packet.hpp"
#include "rtc_base/json.h"

class SinkObserver {
public:
    SinkObserver() {}
    virtual void OnFrame(const std::shared_ptr<muxer::MediaFrame>& frame) = 0;
    virtual void OnStart() {};
    virtual void OnStop() {};
    virtual void OnStatBytes(int64_t& bytes) {};
    virtual void SetRequestKeyFrame(bool req) { need_requestKeyFrame = req;}
    virtual bool GetRequestKeyFrame() { return need_requestKeyFrame; }
    virtual void SetSEIKey(std::string& key) { seikey_ = key;}
    virtual ~SinkObserver() = default;

private:
    std::string id_;
    std::string seikey_;
    bool need_requestKeyFrame;
};

class SinkAddRemover {
public:
    virtual bool AddSink(const std::string& id, SinkObserver *sink) = 0;
    virtual bool RemoveSink(const std::string& id) = 0;
    virtual ~SinkAddRemover() {}
};

class FrameSender {
public:
    virtual void SendFrame(const std::shared_ptr<muxer::MediaFrame>& frame) = 0;
    virtual ~FrameSender() {}
};

class Stream: public SinkAddRemover, public FrameSender {
public:
    Stream() : sinks_map_(), sinks_map_lock_() {}
    bool AddSink(const std::string& id, SinkObserver *sink);
    bool RemoveSink(const std::string& id);
    SinkObserver *FindSink(const std::string& id);
    void SendFrame(const std::shared_ptr<muxer::MediaFrame>& frame);
    std::map<std::string, SinkObserver*>& GetSinks() {return sinks_map_;}

private:
    std::map<std::string, SinkObserver*> sinks_map_;
    std::mutex sinks_map_lock_;
};

#endif
