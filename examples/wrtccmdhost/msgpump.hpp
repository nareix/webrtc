#ifndef __MSGPUMP_HPP__
#define __MSGPUMP_HPP__

#include <cstdio>
#include <string>
#include "rtc_base/scoped_ref_ptr.h"
#include "rtc_base/refcount.h"
#include "common.hpp"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

class MsgPump {
public:
    class Request: public rtc::RefCountInterface {
    public:
        Request(
            MsgPump* p, const std::string& reqid,
            const std::string& type, const json& body
        ) : p_(p), reqid_(reqid), type(type), body(body) {}
        void WriteResponse(const json& res);

    private:
        MsgPump* p_;
        std::string reqid_;

    public:
        std::string type;
        json body;
    };

    class Observer {
    public:
        virtual void OnRequest(rtc::scoped_refptr<Request> req) = 0;
        virtual void OnMessage(const std::string& type, const json& message) = 0;
        virtual ~Observer() {}
    };

public:
    MsgPump(Observer* observer) : observer_(observer) {}

    int WriteMessage(const std::string& type, const json& message);
    void Run();

private:
    std::mutex wlock_;
    Observer* observer_;

    int readMessage(std::string& type, json& message);
};

#endif
