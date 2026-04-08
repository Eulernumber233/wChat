#include "AsioIOContextPool.h"
AsioIOContextPool::AsioIOContextPool(std::size_t size) :_IOContexts(size),
_works(size), _nextIOContext(0) {
	for (std::size_t i = 0; i < size; ++i) {
		_works[i] = std::unique_ptr<Work>(new Work(_IOContexts[i].get_executor()));
	}

	//遍历多个IOContext，创建多个线程，每个线程内部启动IOContext
	for (std::size_t i = 0; i < _IOContexts.size(); ++i) {
		_threads.emplace_back([this, i]() {
			_IOContexts[i].run();
			});
	}
}

AsioIOContextPool::~AsioIOContextPool() {
	Stop();
	std::cout << "AsioIOContextPool destruct" << std::endl;
}

boost::asio::io_context& AsioIOContextPool::GetIOContext() {
	auto& service = _IOContexts[_nextIOContext++];
	if (_nextIOContext == _IOContexts.size()) {
		_nextIOContext = 0;
	}
	return service;
}

void AsioIOContextPool::Stop() {

	// 1. 主动 stop 所有 io_context，中断阻塞的异步操作
	for (auto& io : _IOContexts) {
		io.stop();
	}

	// 2. 重置 work_guard，释放“保持运行”的逻辑（可选，若需彻底清理）
	for (auto& work : _works) {
		work.reset();
	}

	// 3. 等待所有线程join，确保线程安全收尾
	for (auto& t : _threads) {
		if (t.joinable()) {
			t.join();
		}
	}
}
