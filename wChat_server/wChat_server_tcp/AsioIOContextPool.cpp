#include "AsioIOContextPool.h"
AsioIOContextPool::AsioIOContextPool(std::size_t size) :_ioContexts(size),
_works(size), _nextIOContext(0) {
	for (std::size_t i = 0; i < size; ++i) {
		_works[i] = std::unique_ptr<Work>(new Work(_ioContexts[i].get_executor()));
	}

	//遍历多个ioContext，创建多个线程，每个线程内部启动ioContext
	for (std::size_t i = 0; i < _ioContexts.size(); ++i) {
		_threads.emplace_back([this, i]() {
			_ioContexts[i].run();
			});
	}
}

AsioIOContextPool::~AsioIOContextPool() {
	std::cout << "AsioIOContextPool destruct" << std::endl;
}

boost::asio::io_context& AsioIOContextPool::GetIOContext() {
	auto& Context = _ioContexts[_nextIOContext++];
	if (_nextIOContext == _ioContexts.size()) {
		_nextIOContext = 0;
	}
	return Context;
}

void AsioIOContextPool::Stop() {
	////因为仅仅执行work.reset并不能让iocontext从run的状态中退出
	////当iocontext已经绑定了读或写的监听事件后，还需要手动stop该服务。
	//for (auto& work : _works) {
	//	//把服务先停止
	//	work->get_io_context().stop();
	//	work.reset();
	//}

	//for (auto& t : _threads) {
	//	t.join();
	//}

	// 1. 主动 stop 所有 io_context，中断阻塞的异步操作
	for (auto& io : _ioContexts) {
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

