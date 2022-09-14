protobuf=$grpc/third_party/protobuf
protoc=$protobuf/inst/bin/protoc
$protoc --cpp_out=. -I. rpc.proto
$protoc --cpp_out=. -I. rpc.proto --grpc_out=. --plugin=protoc-gen-grpc=$grpc/inst/bin/grpc_cpp_plugin
$protoc --cpp_out=. -I. rpc.proto --go_out=plugins=grpc:go