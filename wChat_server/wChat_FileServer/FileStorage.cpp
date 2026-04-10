#include "FileStorage.h"
#include <chrono>
#include <iomanip>
#include <sstream>

FileStorage::FileStorage() {}

FileStorage::~FileStorage() {}

void FileStorage::Init(const std::string& storage_path) {
	_storage_path = storage_path;
	// Ensure root directory exists
	boost::filesystem::create_directories(_storage_path);
	std::cout << "FileStorage initialized at: " << _storage_path << std::endl;
}

std::string FileStorage::PrepareFile(const std::string& file_id, const std::string& file_name) {
	// Extract extension from original file name
	std::string ext;
	auto dot_pos = file_name.rfind('.');
	if (dot_pos != std::string::npos) {
		ext = file_name.substr(dot_pos); // includes the dot, e.g. ".jpg"
	}

	// Build date-based subdirectory: YYYY/MM/DD
	auto now = std::chrono::system_clock::now();
	auto time_t_now = std::chrono::system_clock::to_time_t(now);
	std::tm tm_now;
	localtime_s(&tm_now, &time_t_now);

	std::ostringstream date_path;
	date_path << std::setfill('0')
		<< (1900 + tm_now.tm_year) << "/"
		<< std::setw(2) << (1 + tm_now.tm_mon) << "/"
		<< std::setw(2) << tm_now.tm_mday;

	std::string relative_dir = date_path.str();
	std::string relative_path = relative_dir + "/" + file_id + ext;

	// Create directory on disk (file itself is created on first WriteChunk)
	boost::filesystem::path full_dir = boost::filesystem::path(_storage_path) / relative_dir;
	boost::filesystem::create_directories(full_dir);

	return relative_path;
}

bool FileStorage::WriteChunk(const std::string& relative_path, int64_t offset, const char* data, size_t length) {
	std::string full = GetFullPath(relative_path);
	std::fstream fs(full, std::ios::in | std::ios::out | std::ios::binary);
	if (!fs.is_open()) {
		// File doesn't exist yet, create it
		std::ofstream create(full, std::ios::binary);
		create.close();
		fs.open(full, std::ios::in | std::ios::out | std::ios::binary);
		if (!fs.is_open()) {
			std::cerr << "FileStorage::WriteChunk failed to open: " << full << std::endl;
			return false;
		}
	}
	fs.seekp(offset);
	fs.write(data, length);
	bool ok = fs.good();
	fs.close();
	return ok;
}

size_t FileStorage::ReadChunk(const std::string& relative_path, int64_t offset, char* buffer, size_t max_length) {
	std::string full = GetFullPath(relative_path);
	std::ifstream fs(full, std::ios::binary);
	if (!fs.is_open()) {
		std::cerr << "FileStorage::ReadChunk failed to open: " << full << std::endl;
		return 0;
	}
	fs.seekg(offset);
	fs.read(buffer, max_length);
	size_t bytes_read = static_cast<size_t>(fs.gcount());
	fs.close();
	return bytes_read;
}

std::string FileStorage::GetFullPath(const std::string& relative_path) {
	return (boost::filesystem::path(_storage_path) / relative_path).string();
}

int64_t FileStorage::GetFileSize(const std::string& relative_path) {
	std::string full = GetFullPath(relative_path);
	try {
		return static_cast<int64_t>(boost::filesystem::file_size(full));
	}
	catch (...) {
		return -1;
	}
}
