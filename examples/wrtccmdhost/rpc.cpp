#include "rpc.h"

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;

void CmdhostRpc::Run() {
    auto ch = grpc::CreateChannel(cliAddr, grpc::InsecureChannelCredentials());
    cliStub = Rpc::NewStub(ch);

    //ClientContext context;
    //OnTrackPacketMsg msg;
    //cliStub->OnTrackPacket(&context);
}