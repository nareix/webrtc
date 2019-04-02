/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_AUDIO_DEVICE_INCLUDE_FAKE_AUDIO_DEVICE2_H_
#define WEBRTC_MODULES_AUDIO_DEVICE_INCLUDE_FAKE_AUDIO_DEVICE2_H_

#include "rtc_base/logging.h"
#include "rtc_base/timeutils.h"
#include "modules/audio_device/include/audio_device.h"
#include "rtc_base/criticalsection.h"
#include "rtc_base/platform_thread.h"
#include "modules/audio_device/audio_device_buffer.h"
#include "system_wrappers/include/sleep.h"

namespace webrtc {

const uint32_t N_REC_SAMPLES_PER_SEC = 48000;
const uint32_t N_PLAY_SAMPLES_PER_SEC = 48000;

const uint32_t N_REC_CHANNELS = 1;   // default is mono recording
const uint32_t N_PLAY_CHANNELS = 2;  // default is stereo playout
const uint32_t N_DEVICE_CHANNELS = 64;

const int kBufferSizeMs = 10;

const uint32_t ENGINE_REC_BUF_SIZE_IN_SAMPLES =
    N_REC_SAMPLES_PER_SEC * kBufferSizeMs / 1000;
const uint32_t ENGINE_PLAY_BUF_SIZE_IN_SAMPLES =
    N_PLAY_SAMPLES_PER_SEC * kBufferSizeMs / 1000;

const int N_BLOCKS_IO = 2;
const int N_BUFFERS_IN = 2;   // Must be at least N_BLOCKS_IO.
const int N_BUFFERS_OUT = 3;  // Must be at least N_BLOCKS_IO.

const uint32_t TIMER_PERIOD_MS = 2 * 10 * N_BLOCKS_IO * 1000000;

const uint32_t REC_BUF_SIZE_IN_SAMPLES =
    ENGINE_REC_BUF_SIZE_IN_SAMPLES * N_DEVICE_CHANNELS * N_BUFFERS_IN;
const uint32_t PLAY_BUF_SIZE_IN_SAMPLES =
    ENGINE_PLAY_BUF_SIZE_IN_SAMPLES * N_PLAY_CHANNELS * N_BUFFERS_OUT;

const int kGetMicVolumeIntervalMs = 1000;

class FakeAudioDeviceModule2 : public AudioDeviceModule {
 public:
  FakeAudioDeviceModule2() : 
    recording_(false), playing_(false), _audioDeviceBuffer(),
    capture_worker_thread_(nullptr), render_worker_thread_(nullptr)
  {
    _audioDeviceBuffer.SetRecordingSampleRate(N_REC_SAMPLES_PER_SEC);
    _audioDeviceBuffer.SetPlayoutSampleRate(N_PLAY_SAMPLES_PER_SEC);
    _audioDeviceBuffer.SetRecordingChannels(N_REC_CHANNELS);
    _audioDeviceBuffer.SetPlayoutChannels(N_PLAY_CHANNELS);
  }
  virtual ~FakeAudioDeviceModule2() {}

 private:
  rtc::CriticalSection _critSect;

  bool recording_;
  bool playing_;
  AudioDeviceBuffer _audioDeviceBuffer;
  std::unique_ptr<rtc::PlatformThread> capture_worker_thread_;
  std::unique_ptr<rtc::PlatformThread> render_worker_thread_;
  int64_t lastRenderTime_ = 0;

  bool RenderWorkerThread() {
    if (lastRenderTime_ == 0) {
      lastRenderTime_ = rtc::TimeNanos();
    }

    int8_t playBuffer[4 * ENGINE_PLAY_BUF_SIZE_IN_SAMPLES];
    // Ask for new PCM data to be played out using the AudioDeviceBuffer.
    uint32_t nSamples = _audioDeviceBuffer.RequestPlayoutData(ENGINE_PLAY_BUF_SIZE_IN_SAMPLES);
    nSamples = _audioDeviceBuffer.GetPlayoutData(playBuffer);

    lastRenderTime_ += int64_t((double)nSamples / (double)N_REC_SAMPLES_PER_SEC * 1e9);

    int64_t currentTime = rtc::TimeNanos();
    //LOG(LS_VERBOSE) << "currentTime=" << double(currentTime)/1e6 << " ms";

    int64_t delta = lastRenderTime_ - currentTime;
    if (delta > 0) {
      SleepNs(delta);
    }
    return true;
  }
  static bool RunRender(void* ptrThis) {
    return static_cast<FakeAudioDeviceModule2*>(ptrThis)->RenderWorkerThread();
  }

  bool CaptureWorkerThread() {
    int16_t recordBuffer[ENGINE_REC_BUF_SIZE_IN_SAMPLES * N_REC_CHANNELS];
    usleep(kBufferSizeMs * 1000);
    _audioDeviceBuffer.SetRecordedBuffer((int8_t*)&recordBuffer, ENGINE_REC_BUF_SIZE_IN_SAMPLES);
    _audioDeviceBuffer.DeliverRecordedData();
    return true;
  }
  static bool RunCapture(void* ptrThis) {
    return static_cast<FakeAudioDeviceModule2*>(ptrThis)->CaptureWorkerThread();
  }

  virtual int32_t RegisterAudioCallback(AudioTransport* audioCallback) {
    rtc::CritScope lock(&_critSect);
    return _audioDeviceBuffer.RegisterAudioCallback(audioCallback);
  }
  virtual int32_t Init() { return 0; }
  virtual int32_t InitSpeaker() { return 0; }
  virtual int32_t SetPlayoutDevice(uint16_t index) { return 0; }
  virtual int32_t SetPlayoutDevice(WindowsDeviceType device) { return 0; }
  virtual int32_t SetStereoPlayout(bool enable) { return 0; }
  virtual int32_t StopPlayout() {
    rtc::CritScope lock(&_critSect);
    if (!playing_)
      return 0;
    render_worker_thread_->Stop();
    render_worker_thread_.reset();
    _audioDeviceBuffer.StopPlayout();
    playing_ = false;
    return 0;
  }
  virtual int32_t InitMicrophone() { return 0; }
  virtual int32_t SetRecordingDevice(uint16_t index) { return 0; }
  virtual int32_t SetRecordingDevice(WindowsDeviceType device) { return 0; }
  virtual int32_t SetStereoRecording(bool enable) { return 0; }
  virtual int32_t SetAGC(bool enable) { return 0; }
  virtual int32_t StopRecording() {
    rtc::CritScope lock(&_critSect);
    if (!recording_)
      return 0;
    capture_worker_thread_->Stop();
    capture_worker_thread_.reset();
    _audioDeviceBuffer.StopRecording();
    recording_ = false;
    return 0;
  }

  // If the subclass doesn't override the ProcessThread implementation,
  // we'll fall back on an implementation that doesn't eat too much CPU.
  virtual int64_t TimeUntilNextProcess() {
    if (turn_off_module_callbacks_)
      return 7 * 24 * 60 * 60 * 1000;  // call me next week.
    uses_default_module_implementation_ = true;
    return 10;
  }

  virtual void Process() {
    turn_off_module_callbacks_ = uses_default_module_implementation_;
  }

  virtual int32_t Terminate() { return 0; }

  virtual int32_t ActiveAudioLayer(AudioLayer* audioLayer) const { return 0; }
  virtual ErrorCode LastError() const { return kAdmErrNone; }
  virtual bool Initialized() const { return true; }
  virtual int16_t PlayoutDevices() { return 0; }
  virtual int16_t RecordingDevices() { return 0; }
  virtual int32_t PlayoutDeviceName(uint16_t index,
                            char name[kAdmMaxDeviceNameSize],
                            char guid[kAdmMaxGuidSize]) {
    return 0;
  }
  virtual int32_t RecordingDeviceName(uint16_t index,
                              char name[kAdmMaxDeviceNameSize],
                              char guid[kAdmMaxGuidSize]) {
    return 0;
  }
  virtual int32_t PlayoutIsAvailable(bool* available) { return 0; }
  virtual int32_t InitPlayout() { return 0; }
  virtual bool PlayoutIsInitialized() const { return true; }
  virtual int32_t RecordingIsAvailable(bool* available) { return 0; }
  virtual int32_t InitRecording() {
    _audioDeviceBuffer.SetRecordingSampleRate(N_REC_SAMPLES_PER_SEC);
    _audioDeviceBuffer.SetRecordingChannels(N_REC_CHANNELS);
    return 0;
  }
  virtual bool RecordingIsInitialized() const { return true; }
  virtual int32_t StartPlayout() {
    rtc::CritScope lock(&_critSect);
    if (playing_)
      return 0;
    _audioDeviceBuffer.StartPlayout();
    render_worker_thread_.reset(
        new rtc::PlatformThread(RunRender, this, "RenderWorkerThread"));
    render_worker_thread_->Start();
    render_worker_thread_->SetPriority(rtc::kRealtimePriority);
    playing_ = true;
    return 0; 
  }
  virtual bool Playing() const { 
    rtc::CritScope lock(&_critSect);
    return playing_;
  }
  virtual int32_t StartRecording() {
    rtc::CritScope lock(&_critSect);
    if (recording_)
      return 0;
    _audioDeviceBuffer.StartRecording();
    capture_worker_thread_.reset(
        new rtc::PlatformThread(RunCapture, this, "CaptureWorkerThread"));
    capture_worker_thread_->Start();
    capture_worker_thread_->SetPriority(rtc::kRealtimePriority);
    recording_ = true;
    return 0; 
  }
  virtual bool Recording() const {
    rtc::CritScope lock(&_critSect);
    return recording_;
  }
  virtual bool AGC() const { return true; }
  virtual bool SpeakerIsInitialized() const { return true; }
  virtual bool MicrophoneIsInitialized() const { return true; }
  virtual int32_t SpeakerVolumeIsAvailable(bool* available) { return 0; }
  virtual int32_t SetSpeakerVolume(uint32_t volume) { return 0; }
  virtual int32_t SpeakerVolume(uint32_t* volume) const { return 0; }
  virtual int32_t MaxSpeakerVolume(uint32_t* maxVolume) const { return 0; }
  virtual int32_t MinSpeakerVolume(uint32_t* minVolume) const { return 0; }
  virtual int32_t MicrophoneVolumeIsAvailable(bool* available) { return 0; }
  virtual int32_t SetMicrophoneVolume(uint32_t volume) { return 0; }
  virtual int32_t MicrophoneVolume(uint32_t* volume) const { return 0; }
  virtual int32_t MaxMicrophoneVolume(uint32_t* maxVolume) const { return 0; }
  virtual int32_t MinMicrophoneVolume(uint32_t* minVolume) const { return 0; }
  virtual int32_t SpeakerMuteIsAvailable(bool* available) { return 0; }
  virtual int32_t SetSpeakerMute(bool enable) { return 0; }
  virtual int32_t SpeakerMute(bool* enabled) const { return 0; }
  virtual int32_t MicrophoneMuteIsAvailable(bool* available) { return 0; }
  virtual int32_t SetMicrophoneMute(bool enable) { return 0; }
  virtual int32_t MicrophoneMute(bool* enabled) const { return 0; }
  virtual int32_t StereoPlayoutIsAvailable(bool* available) const {
    *available = false;
    return 0;
  }
  virtual int32_t StereoPlayout(bool* enabled) const { return 0; }
  virtual int32_t StereoRecordingIsAvailable(bool* available) const {
    *available = false;
    return 0;
  }
  virtual int32_t StereoRecording(bool* enabled) const { return 0; }
  virtual int32_t SetRecordingChannel(const ChannelType channel) { return 0; }
  virtual int32_t RecordingChannel(ChannelType* channel) const { return 0; }
  virtual int32_t PlayoutDelay(uint16_t* delayMS) const {
    *delayMS = 0;
    return 0;
  }
  virtual int32_t RecordingDelay(uint16_t* delayMS) const { return 0; }
  virtual int32_t SetRecordingSampleRate(const uint32_t samplesPerSec) {
    return 0;
  }
  virtual int32_t RecordingSampleRate(uint32_t* samplesPerSec) const {
    return 0;
  }
  virtual int32_t SetPlayoutSampleRate(const uint32_t samplesPerSec) {
    return 0;
  }
  virtual int32_t PlayoutSampleRate(uint32_t* samplesPerSec) const { return 0; }
  virtual int32_t SetLoudspeakerStatus(bool enable) { return 0; }
  virtual int32_t GetLoudspeakerStatus(bool* enabled) const { return 0; }
  virtual bool BuiltInAECIsAvailable() const { return false; }
  virtual int32_t EnableBuiltInAEC(bool enable) { return -1; }
  virtual bool BuiltInAGCIsAvailable() const { return false; }
  virtual int32_t EnableBuiltInAGC(bool enable) { return -1; }
  virtual bool BuiltInNSIsAvailable() const { return false; }
  virtual int32_t EnableBuiltInNS(bool enable) { return -1; }

#if defined(WEBRTC_IOS)
  virtual int GetPlayoutAudioParameters(AudioParameters* params) const {
    return -1;
  }
  virtual int GetRecordAudioParameters(AudioParameters* params) const {
    return -1;
  }
#endif  // WEBRTC_IOS

 private:
  bool uses_default_module_implementation_ = false;
  bool turn_off_module_callbacks_ = false;
};

}  // namespace webrtc

#endif  // WEBRTC_MODULES_AUDIO_DEVICE_INCLUDE_FAKE_AUDIO_DEVICE_H_
