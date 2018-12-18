#ifndef RPC_H_
#define RPC_H_

#include <string>

#include "rpc.grpc.pb.h"

class CmdhostRpc {
public:

    CmdhostRpc(std::string cliAddr, std::string srvAddr) : cliAddr(cliAddr), srvAddr(srvAddr) {
    }

    void Run();

private:
    std::string cliAddr, srvAddr;
    std::unique_ptr<Rpc::Stub> cliStub;
};

#endif