#ifndef __COMMON_HPP__
#define __COMMON_HPP__

#include <string>
#include <memory>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <cstring>
#include <functional>
#include <thread>
#include <stdexcept>
#include <ctime>
#include <chrono>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <sys/time.h>
#include <unistd.h>

#if __cplusplus == 201402L // C++14
using std::make_unique ;
#else // C++11
namespace std {
template < typename T, typename... CONSTRUCTOR_ARGS >
std::unique_ptr<T> make_unique( CONSTRUCTOR_ARGS&&... constructor_args )
{ return std::unique_ptr<T>( new T( std::forward<CONSTRUCTOR_ARGS>(constructor_args)... ) ); }
}
#endif // __cplusplus == 201402L

#include "fstream"
#include "sstream"
extern std::ofstream outfile;

extern "C"
{
#include "libavutil/opt.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/imgutils.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
}

extern "C"
{
#include "rtmp.h"
#include "rtmp_sys.h"
#include "amf.h"
}

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

#ifndef INOUT
#define INOUT
#endif

#define LogFormat(level, levelstr, reqid, fmt, arg...)                              \
        do {                                                            \
                if ((unsigned int)(level) <= muxer::global::nLogLevel) { \
                        struct timeval tv;                              \
                        char timeFmt[32];                               \
                        char reqstr[256] = {};                          \
                        if (reqid != NULL)                              \
                                snprintf(reqstr, sizeof(reqstr), "[%s]", (char *)reqid); \
                        gettimeofday(&tv, nullptr);                     \
                        strftime(timeFmt, sizeof(timeFmt), "%Y/%m/%d %H:%M:%S", localtime(&tv.tv_sec)); \
                        fprintf(stderr, "%s.%06lu %s[%s] %s:%d: " fmt "\n",     \
                                timeFmt, (unsigned long)(tv.tv_usec / 1000), reqstr, levelstr, __FILE__, __LINE__, ##arg); \
                }                                                       \
        } while(0)

#define XLogFormat(level, levelstr, xl, fmt, arg...)                              \
        { \
                const char *reqid = "";         \
                if (xl) reqid = xl->reqid_.c_str(); \
                LogFormat(level, levelstr, reqid, fmt, ##arg); \
        }

#define XError(fmt, arg...) XLogFormat(2, "ERROR", xl_, fmt, ##arg)
#define XWarn(fmt, arg...) XLogFormat(3, "WARN", xl_, fmt, ##arg)
#define XInfo(fmt, arg...) XLogFormat(4, "INFO", xl_, fmt, ##arg)
#define XDebug(fmt, arg...) XLogFormat(5, "DEBUG", xl_, fmt, ##arg)

#define Fatal(fmt, arg...)                                              \
        do {                                                            \
                LogFormat(1, "FATAL", NULL, fmt, ##arg);                          \
                LogFormat(1, "FATAL", NULL, "fatal error, will exit");            \
                exit(1);                                                \
        } while(0)

#define Error(fmt, arg...)                              \
        do {                                            \
                LogFormat(2, "ERROR", NULL, fmt, ##arg);          \
        } while(0)

#define Warn(fmt, arg...)                               \
        do {                                            \
                LogFormat(3, "WARN", NULL, fmt, ##arg);          \
        } while(0)

#define Info(fmt, arg...)                               \
        do {                                            \
                LogFormat(4, "INFO", NULL, fmt, ##arg);          \
        } while(0)

#define InfoR(fmt, reqid, arg...)                               \
        do {                                            \
                LogFormat(4, "INFO", reqid, fmt, ##arg);          \
        } while(0)

#define Debug(fmt, arg...)                              \
        do {                                            \
                LogFormat(5, "DEBUG", NULL, fmt, ##arg);          \
        } while(0)

#define DebugR(fmt, reqid, arg...)                              \
        do {                                            \
                LogFormat(5, "DEBUG", reqid, fmt, ##arg);          \
        } while(0)

#define Verbose(fmt, arg...)                            \
        do {                                            \
                LogFormat(6, "VERBOSE", NULL, fmt, ##arg);          \
        } while(0)

namespace muxer
{
        namespace global
        {
                extern unsigned int nLogLevel;

                inline void PrintMem(const void* _pAddr, unsigned long _nBytes)
                {
                        if (_pAddr == nullptr || _nBytes == 0) {
                                return;
                        }

                        const int nBytesPerLine = 16;
                        unsigned char* p = reinterpret_cast<unsigned char*>(const_cast<void*>(_pAddr));
                        std::string line;
                        char value[6];
                        unsigned int i = 0;

                        Verbose("========== memory <%p+%lu> ==========", _pAddr, _nBytes);
                        for (; i < _nBytes; ++i) {
                                if (i % nBytesPerLine == 0) {
                                        line = "";
                                }

                                // "0xAB \0" => 6 bytes
                                snprintf(value, 6, "0x%02x ", p[i]);
                                line += value;

                                if (((i + 1) % nBytesPerLine) == 0) {
                                        Verbose("<%p>: %s", p + i - nBytesPerLine + 1, line.c_str());
                                }
                        }

                        // print rest bytes
                        if (_nBytes % nBytesPerLine != 0) {
                                Verbose("<%p>: %s", p + i - (_nBytes % nBytesPerLine), line.c_str());
                        }
                        Verbose("========== end of <%p+%lu> ==========", _pAddr, _nBytes);
                }
        }

        typedef enum
        {
                STREAM_VIDEO = AVMEDIA_TYPE_VIDEO,
                STREAM_AUDIO = AVMEDIA_TYPE_AUDIO,
                STREAM_DATA = AVMEDIA_TYPE_DATA,
                STREAM_RAWPACKET = 0x1653,
        } StreamType;

        typedef enum
        {
                // video
                CODEC_H264 = AV_CODEC_ID_H264,
                CODEC_VC1  = AV_CODEC_ID_VC1,

                // Audio
                CODEC_MP3  = AV_CODEC_ID_MP3,
                CODEC_AAC  = AV_CODEC_ID_AAC,
                CODEC_WAV1 = AV_CODEC_ID_WMAV1,
                CODEC_WAV2 = AV_CODEC_ID_WMAV2,

                // others
                CODEC_FLV_METADATA = AV_CODEC_ID_FFMETADATA,
                CODEC_UNKNOWN = AV_CODEC_ID_NONE
        } CodecType;

        //
        // option map
        //

        namespace options {
                const std::string width  = "w";
                const std::string height = "h";
                const std::string x      = "x";
                const std::string y      = "y";
                const std::string z      = "z";
                const std::string hidden = "hidden";
                const std::string muted  = "muted";
                const std::string vbitrate = "vb";
                const std::string abitrate = "ab";
                const std::string bgcolor  = "bg";
                const std::string stretchMode  = "stretchMode";
                const std::string stretchAspectFill  = "aspectFill";
                const std::string stretchAspectFit = "aspectFit";
                const std::string stretchScaleToFit  = "scaleToFit";
        }

        class OptionMap
        {
        public:
                virtual bool GetOption(IN const std::string& key, OUT std::string& value);
                virtual bool GetOption(IN const std::string& key, OUT int& value);
                virtual bool GetOption(IN const std::string& key);
                virtual void SetOption(IN const std::string& flag);
                virtual void SetOption(IN const std::string& key, IN const std::string& value);
                virtual void SetOption(IN const std::string& key, IN int val);
                virtual void DelOption(IN const std::string& key);
                virtual void GetOptions(IN const OptionMap& opts);
                virtual ~OptionMap() {}
        protected:
                std::unordered_map<std::string, std::string> params_;
                std::mutex paramsLck_;

                std::unordered_map<std::string, int> intparams_;
                std::mutex intparamsLck_;
        };

        typedef OptionMap Option;
}

#include "util.hpp"
#include "packet.hpp"
#include "media.hpp"

#endif
