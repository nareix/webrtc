#ifndef __MSGPUMP_HPP__
#define __MSGPUMP_HPP__

#include <stdio.h>
#include <string>
#include "rtc_base/json.h"
#include "rtc_base/scoped_ref_ptr.h"
#include "rtc_base/refcount.h"
#include "common.hpp"

class MsgPump {
public:
    class Request: public rtc::RefCountInterface {
    public:
        Request(
            MsgPump* p, const std::string& reqid, 
            const std::string& type, const Json::Value& body
        ) : p_(p), reqid_(reqid), type(type), body(body) {}
        void WriteResponse(const Json::Value& res);

    private:
        MsgPump* p_;
        std::string reqid_;

    public:
        std::string type;
        Json::Value body;
    };

    class Observer {
    public:
        virtual void OnRequest(rtc::scoped_refptr<Request> req) = 0;
        virtual void OnMessage(const std::string& type, const Json::Value& message) = 0;
        virtual ~Observer() {}
    };

public:
    MsgPump(Observer* observer) : observer_(observer) {}

    int WriteMessage(const std::string& type, const Json::Value& message);
    void Run();

private:
    std::mutex wlock_;
    Observer* observer_;

    int readMessage(std::string& type, Json::Value& message);
};

#endif