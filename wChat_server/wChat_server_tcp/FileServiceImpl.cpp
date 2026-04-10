#include "FileServiceImpl.h"
#include "LogicSystem.h"
#include "RedisMgr.h"

FileServiceImpl::FileServiceImpl() {}

Status FileServiceImpl::NotifyUploadDone(ServerContext* context,
	const FileUploadDoneReq* request, FileUploadDoneRsp* reply) {

	std::string file_id = request->file_id();
	std::string file_path = request->file_path();
	std::string md5 = request->md5();

	std::cout << "gRPC NotifyUploadDone: file_id=" << file_id << std::endl;

	// Delegate to LogicSystem (runs on the gRPC thread, which is acceptable
	// since the work is lightweight: DB update + Redis + session send)
	LogicSystem::GetInstance()->HandleFileUploadDone(file_id, file_path, md5);

	reply->set_error(0);
	return Status::OK;
}

Status FileServiceImpl::RegisterDownloadAuth(ServerContext* context,
	const FileDownloadAuthReq* request, FileDownloadAuthRsp* reply) {

	// ChatServer calls this on FileServer to pre-register a download token.
	// In our current design, ChatServer writes the token directly to Redis
	// (shared Redis instance), so this RPC is a placeholder for future use
	// when FileServer has its own Redis.
	reply->set_error(0);
	return Status::OK;
}
