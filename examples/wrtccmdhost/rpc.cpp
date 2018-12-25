#include "rpc.h"
#include "common.hpp"

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>

class RpcImpl final : public Rpc::Service {
public:
    grpc::Status NewPC(::grpc::ServerContext* context, const NewPCReq* request, NewPCRes* response) {
        return grpc::Status::OK;
    }
    grpc::Status CreateOffer(::grpc::ServerContext* context, const OfferAnswerReq* request, OfferAnswerRes* response) {
        return grpc::Status::OK;
    }
    grpc::Status CreateAnswer(::grpc::ServerContext* context, const OfferAnswerReq* request, OfferAnswerRes* response) {
        return grpc::Status::OK;
    }
    grpc::Status SetLocalDesc(::grpc::ServerContext* context, const SetDescReq* request, SetDescRes* response) {
        return grpc::Status::OK;
    }
    grpc::Status SetRemoteDesc(::grpc::ServerContext* context, const SetDescReq* request, SetDescRes* response) {
        return grpc::Status::OK;
    }
    grpc::Status AddTracks(::grpc::ServerContext* context, const AddTracksReq* request, Empty* response) {
        return grpc::Status::OK;
    }
    grpc::Status NewUrlTracks(::grpc::ServerContext* context, const NewUrlTracksReq* request, NewUrlTracksRes* response) {
        return grpc::Status::OK;
    }
    grpc::Status NewTrackSrcs(::grpc::ServerContext* context, const Empty* request, NewTrackSrcsRes* response) {
        return grpc::Status::OK;
    }
    grpc::Status TrackSrcSendPacket(::grpc::ServerContext* context, const TrackSrcSendPacketReq* request, Empty* response) {
        return grpc::Status::OK;
    }
    grpc::Status OnTrackPacket(::grpc::ServerContext* context, const OnTrackPacketMsg* request, Empty* response) {
        Info("OnTrackPacket %zu", request->pkt().buf().size());
        return grpc::Status::OK;
    }
};

void CmdhostRpc::Run() {
    auto ch = grpc::CreateChannel(cliAddr, grpc::InsecureChannelCredentials());
    cliStub = Rpc::NewStub(ch);

    grpc::ClientContext context;
    OnTrackPacketMsg msg;
    msg.set_pcid(10);
    msg.mutable_pkt()->set_buf(std::string("\x01\x02\x00", 3));
    Empty res;
    auto status = cliStub->OnTrackPacket(&context, msg, &res);
    Info("status code=%d msg=%s", status.error_code(), status.error_message().c_str());

    grpc::ServerBuilder builder;
    builder.AddListeningPort(srvAddr, grpc::InsecureServerCredentials());
    builder.RegisterService(new RpcImpl());
    auto server = builder.BuildAndStart();
    server->Wait();
}