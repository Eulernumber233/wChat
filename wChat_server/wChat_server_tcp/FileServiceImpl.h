#pragma once
#include "core.h"
#include "message.grpc.pb.h"
#include "message.pb.h"

using grpc::ServerContext;
using grpc::Status;
using message::FileService;
using message::FileUploadDoneReq;
using message::FileUploadDoneRsp;
using message::FileDownloadAuthReq;
using message::FileDownloadAuthRsp;

class FileServiceImpl final : public FileService::Service {
public:
	FileServiceImpl();

	Status NotifyUploadDone(ServerContext* context,
		const FileUploadDoneReq* request, FileUploadDoneRsp* reply) override;

	Status RegisterDownloadAuth(ServerContext* context,
		const FileDownloadAuthReq* request, FileDownloadAuthRsp* reply) override;
};
