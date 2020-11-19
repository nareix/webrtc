#include "msgpump.hpp"

#include <arpa/inet.h>  // ntohl() htonl()
#include <memory>       // std::addressof()
#include <cstdint>      // uint8_t, etc
#include <cstdio>

int MsgPump::WriteMessage(const std::string& type, const json& message) {
  std::lock_guard<std::mutex> lock(wlock_);

  Verbose("WriteCmd %s=%s", type.c_str(), message.dump().c_str());
  std::vector<uint8_t> msgBSON = json::to_bson(message);

  if (msgBSON.size() == 0) {
      return -1;
  }

  uint32_t len = htonl(type.size() + msgBSON.size() + 1);

  if (fwrite(&len, 1, sizeof(len), stdout) != sizeof(len)) {
    return -1;
  }
  if (fwrite(type.c_str(), 1, type.size(), stdout) != type.size()) {
    return -1;
  }
  if (fwrite("=", 1, 1, stdout) != 1) {
    return -1;
  }
  if (fwrite(std::addressof(msgBSON[0]), 1, msgBSON.size(), stdout) != msgBSON.size()) {
    return -1;
  }
  if (fflush(stdout) != 0) {
    return -1;
  }

  return 0;
}

int MsgPump::readMessage(std::string& type, json& message) {
  uint32_t len;

  int fr = fread(&len, 1, sizeof(len), stdin);
  if (fr != sizeof(len)) {
    return -1;
  }
  len = ntohl(len);
  char* data = (char*)malloc(len + 1);
  int r = 0;
  int eq = -1;

  fr = fread(data, 1, len, stdin);
  if (fr != (int)len) {
    goto out_free;
  }
  data[len] = 0;

  Verbose("ReadCmd %s", data);

  for (int i = 0; i < (int)len; i++) {
    if (data[i] == '=') {
      eq = i;
      break;
    }
  }

  if (eq == -1) {
    r = -1;
    goto out_free;
  }

  type = std::string(data, eq);
  message = json::from_bson(data + eq + 1, data + len, false, false);
  if (message.is_discarded()) {
      r = -1;
      goto out_free;
  }

out_free:
    free(data);
    return r;
}

void MsgPump::Request::WriteResponse(const json& res) {
    p_->WriteMessage("$res16-"+reqid_, res);
}

void MsgPump::Run() {
    for (;;) {
        std::string type;
        json message;
        int r = readMessage(type, message);
        if (r < 0)
            break;

        if (!strncmp(type.c_str(), "$req16-", 7)) {
            if (type.size() > 7+16) {
                auto reqid = type.substr(7, 16);
                auto type2 = type.substr(7+16);
                auto req = new rtc::RefCountedObject<MsgPump::Request>(this, reqid, type2, message);
                observer_->OnRequest(req);
            }
        } else {
            observer_->OnMessage(type, message);
        }
    }
}
