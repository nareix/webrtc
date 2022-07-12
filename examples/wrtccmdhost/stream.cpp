#include "stream.hpp"

bool Stream::AddSink(const std::string& id, SinkObserver *sink) {
    std::lock_guard<std::mutex> lock(sinks_map_lock_);
    auto it = sinks_map_.find(id);
    if (it != sinks_map_.end()) {
        return false;
    }
    sinks_map_[id] = sink;
    sink->OnStart();
    return true;
}

bool Stream::RemoveSink(const std::string& id) {
    std::lock_guard<std::mutex> lock(sinks_map_lock_);
    auto it = sinks_map_.find(id);
    if (it == sinks_map_.end()) {
        return false;
    }
    auto sink = it->second;
    sink->OnStop();
    sinks_map_.erase(it);
    return true;
}

SinkObserver *Stream::FindSink(const std::string& id) {
    std::lock_guard<std::mutex> lock(sinks_map_lock_);
    auto it = sinks_map_.find(id);
    if (it == sinks_map_.end()) {
        return NULL;
    }
    return it->second;
}

void Stream::SendFrame(const std::shared_ptr<muxer::MediaFrame>& frame) {
    std::lock_guard<std::mutex> lock(sinks_map_lock_);
    if (frame->Stream() == muxer::STREAM_VIDEO && frame->AvFrame()->width!=0 && frame->AvFrame()->height!=0){
        lastVideo_ = frame;
    }
    for (auto it = sinks_map_.begin(); it != sinks_map_.end(); it++) {
        auto sink = it->second;
        sink->OnFrame(frame);
    }
}
