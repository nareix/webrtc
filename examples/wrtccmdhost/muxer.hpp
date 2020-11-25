#ifndef __MUXER_HPP__
#define __MUXER_HPP__

#include "common.hpp"
#include "input.hpp"
#include "output.hpp"

namespace muxer
{
        class VideoMuxer
        {
        public:
                VideoMuxer(IN int nW, IN int nH);
                ~VideoMuxer();
                int Mux(IN std::vector<std::shared_ptr<MediaFrame>>& frames, OUT std::shared_ptr<MediaFrame>&);
                void BgColor(int nRGB);
        private:
                bool isBgColor(IN std::shared_ptr<MediaFrame>& pFrame);

                int nCanvasW_ = 0, nCanvasH_ = 0;
                int nBackground_ = 0x000000; // black
				
                bool _bEnterMuxMode = false;
                int _nFrameCount = 0, _nFrameCountThreshold = 50;
        };

        class AudioMixer
        {
        public:
                AudioMixer();
                ~AudioMixer();
                int Mix(IN const std::vector<std::shared_ptr<MediaFrame>>& frames, OUT std::shared_ptr<MediaFrame>&);
        private:
                void SimpleMix(IN const std::shared_ptr<MediaFrame>& pFrom, OUT std::shared_ptr<MediaFrame>& pTo);
        };

        class AvMuxer : public OptionMap
        {
        public:
                AvMuxer(std::shared_ptr<XLogger> xl, IN int nWidth, IN int nHeight);
                ~AvMuxer();

                int AddOutput(IN const std::string& name, IN FrameSender* stream);
                int ModOutputOption(IN const std::string& name, IN const std::string& key, IN const std::string& val = "");
                int ModOutputOption(IN const std::string& name, IN const std::string& key, IN int nVal);
                int DelOutputOption(IN const std::string& name, IN const std::string& key);
                int RemoveOutput(IN const std::string& name);

                int AddInput(IN const std::string& name, IN SinkAddRemover *stream);
                int ModInputOption(IN const std::string& name, IN const std::string& key, IN const std::string& val = "");
                int ModInputOption(IN const std::string& name, IN const std::string& key, IN int nVal);
                int DelInputOption(IN const std::string& name, IN const std::string& key);
                int RemoveInput(IN const std::string& name);

                int Start();
                int PrintInputs();
        public:
                std::shared_ptr<Input> FindInput(IN const std::string& name);
                std::shared_ptr<Output> FindOutput(IN const std::string& name);
                void FeedOutputs(IN std::shared_ptr<MediaFrame>& pFrame, IN double pts);

                void SetInputKey(IN const std::string &id, IN const std::string &key);
                std::string GetInputKey(IN const std::string &id);
                void RemoveInputKey(IN const std::string &id);

                std::atomic<bool> audioOnly_;
                int fps_ = 25;

        private:
                std::shared_ptr<XLogger> xl_ = nullptr;
                SharedQueue<std::shared_ptr<Input>> inputs_;
                SharedQueue<std::shared_ptr<Output>> outputs_;

                std::map<std::string, std::string> inputKeys_;

                std::thread videoMuxerThread_;
                std::thread audioMuxerThread_;

                VideoMuxer videoMuxer_;
                AudioMixer audioMixer_;

                // internal clock to generate pts
                std::mutex clockLck_;
                std::mutex inputKeyLck_;

                double tstart_ = 0;
        };
}

#endif
