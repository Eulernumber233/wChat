#pragma once
#include "core.h"
#include "Singleton.h"

class FileStorage : public Singleton<FileStorage> {
	friend class Singleton<FileStorage>;
public:
	~FileStorage();

	// Initialize storage root path from config
	void Init(const std::string& storage_path);

	// Create the target file on disk (with date-based subdirectory)
	// Returns the relative path e.g. "2026/04/09/a1b2c3d4.jpg"
	std::string PrepareFile(const std::string& file_id, const std::string& file_name);

	// Write a chunk to the file at the given offset
	bool WriteChunk(const std::string& relative_path, int64_t offset, const char* data, size_t length);

	// Read a chunk from the file at the given offset
	// Returns actual bytes read
	size_t ReadChunk(const std::string& relative_path, int64_t offset, char* buffer, size_t max_length);

	// Get the full absolute path from a relative path
	std::string GetFullPath(const std::string& relative_path);

	// Get file size on disk
	int64_t GetFileSize(const std::string& relative_path);

private:
	FileStorage();
	std::string _storage_path;   // root path e.g. "D:\wChat_files"
};
