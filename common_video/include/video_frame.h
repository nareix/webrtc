/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef COMMON_VIDEO_INCLUDE_VIDEO_FRAME_H_
#define COMMON_VIDEO_INCLUDE_VIDEO_FRAME_H_

// TODO(nisse): This header file should eventually be deleted. The
// EncodedImage class stays in this file until we have figured out how
// to refactor and clean up related interfaces, at which point it
// should be moved to somewhere under api/.

#include "common_types.h"  // NOLINT(build/include)
#include "typedefs.h"  // NOLINT(build/include)


#include "rtc_base/bytebuffer.h"

namespace webrtc {

// TODO(pbos): Rename EncodedFrame and reformat this class' members.
class EncodedImage {
 public:
  static const size_t kBufferPaddingBytesH264;

  // Some decoders require encoded image buffers to be padded with a small
  // number of additional bytes (due to over-reading byte readers).
  static size_t GetBufferPaddingBytes(VideoCodecType codec_type);

  EncodedImage();
  EncodedImage(uint8_t* buffer, size_t length, size_t size);

  void SetEncodeTime(int64_t encode_start_ms, int64_t encode_finish_ms) const;
  void SetRawPkt(bool flag) { _rawpkt = flag; }
  bool RawPkt() const {return _rawpkt;}

  // TODO(kthelgason): get rid of this struct as it only has a single member
  // remaining.
  struct AdaptReason {
    AdaptReason() : bw_resolutions_disabled(-1) {}
    int bw_resolutions_disabled;  // Number of resolutions that are not sent
                                  // due to bandwidth for this frame.
                                  // Or -1 if information is not provided.
  };
  uint32_t _encodedWidth = 0;
  uint32_t _encodedHeight = 0;
  uint32_t _timeStamp = 0;
  // NTP time of the capture time in local timebase in milliseconds.
  int64_t ntp_time_ms_ = 0;
  int64_t capture_time_ms_ = 0;
  FrameType _frameType = kVideoFrameDelta;
  uint8_t* _buffer;
  size_t _length;
  size_t _size;
  VideoRotation rotation_ = kVideoRotation_0;
  mutable VideoContentType content_type_ = VideoContentType::UNSPECIFIED;
  bool _completeFrame = false;
  AdaptReason adapt_reason_;
  int qp_ = -1;  // Quantizer value.
  bool _rawpkt = false;

  // When an application indicates non-zero values here, it is taken as an
  // indication that all future frames will be constrained with those limits
  // until the application indicates a change again.
  PlayoutDelay playout_delay_ = {-1, -1};

  // Timing information should be updatable on const instances.
  mutable struct Timing {
    uint8_t flags = TimingFrameFlags::kInvalid;
    int64_t encode_start_ms = 0;
    int64_t encode_finish_ms = 0;
    int64_t packetization_finish_ms = 0;
    int64_t pacer_exit_ms = 0;
    int64_t network_timestamp_ms = 0;
    int64_t network2_timestamp_ms = 0;
    int64_t receive_start_ms = 0;
    int64_t receive_finish_ms = 0;
  } timing_;

  void Marshall(rtc::ByteBufferWriter& mb) const {
    // type 
    mb.WriteUInt8(1);
    mb.WriteUInt32(1);
    mb.WriteUInt8(1); // video

    // img data
    mb.WriteUInt8(10);
    mb.WriteUInt32(_length);
    mb.WriteBytes((const char*)_buffer, _length);

    // img info
    {
      rtc::ByteBufferWriter b(rtc::ByteBuffer::ByteOrder::ORDER_NETWORK);
      b.WriteUInt32(_encodedWidth);
      b.WriteUInt32(_encodedHeight);
      b.WriteUInt32(_timeStamp);
      b.WriteUInt64(ntp_time_ms_);
      b.WriteUInt64(capture_time_ms_);
      b.WriteUInt32(_frameType);
      b.WriteUInt8(timing_.flags);
      b.WriteUInt32(qp_);
      b.WriteUInt32(timing_.encode_start_ms);
      b.WriteUInt32(timing_.encode_finish_ms);
      b.WriteUInt32(playout_delay_.min_ms);
      b.WriteUInt32(playout_delay_.max_ms);
      b.WriteUInt8(_completeFrame != 0);

      mb.WriteUInt8(11);
      mb.WriteUInt32(b.Length());
      mb.WriteBytes(b.Data(), b.Length());
    }
  }

  bool Unmarshall(uint8_t type, rtc::ByteBufferReader& b) {
    uint8_t v8;
    uint32_t v32;

    switch (type) {
    case 10: // img data
      _buffer = (uint8_t*)b.Data();
      _length = b.Length();
      _size = b.Length();
      return true;

    case 11: // img info
      b.ReadUInt32(&_encodedWidth);
      b.ReadUInt32(&_encodedHeight);
      b.ReadUInt32(&_timeStamp);
      b.ReadUInt64((uint64_t *)&ntp_time_ms_);
      b.ReadUInt64((uint64_t *)&capture_time_ms_);
      if (b.ReadUInt32(&v32)) {
        _frameType = (webrtc::FrameType)v32;
      }
      b.ReadUInt8(&timing_.flags);
      if (b.ReadUInt32(&v32)) {
        qp_ = v32;
      }
      if (b.ReadUInt32(&v32)) {
        timing_.encode_start_ms = v32;
      }
      if (b.ReadUInt32(&v32)) {
        timing_.encode_finish_ms = v32;
      }
      if (b.ReadUInt32(&v32)) {
        playout_delay_.min_ms = v32;
      }
      if (b.ReadUInt32(&v32)) {
        playout_delay_.max_ms = v32;
      }
      if (b.ReadUInt8(&v8)) {
        _completeFrame = v8 != 0;
      }
      return true;
    }

    return false;
  }
};

}  // namespace webrtc

#endif  // COMMON_VIDEO_INCLUDE_VIDEO_FRAME_H_
