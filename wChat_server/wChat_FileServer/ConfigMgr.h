#pragma once
#include "core.h"
struct SectionInfo {
	SectionInfo() {}
	~SectionInfo() {
		_section_datas.clear();
	}

	SectionInfo(const SectionInfo& src) {
		_section_datas = src._section_datas;
	}

	SectionInfo& operator = (const SectionInfo& src) {
		if (&src == this) {
			return *this;
		}

		this->_section_datas = src._section_datas;
		return *this;
	}

	std::map<std::string, std::string> _section_datas;
	std::string  operator[](const std::string& key) {
		if (_section_datas.find(key) == _section_datas.end()) {
			return "";
		}
		// �����������һЩ�߽���  
		return _section_datas[key];
	}

	std::string GetValue(const std::string& key) {
		if (_section_datas.find(key) == _section_datas.end()) {
			return "";
		}
		// �����������һЩ�߽���  
		return _section_datas[key];
	}
};
class ConfigMgr
{
public:
	~ConfigMgr() {
		_config_map.clear();
	}
	SectionInfo operator[](const std::string& section) {
		if (_config_map.find(section) == _config_map.end()) {
			return SectionInfo();
		}
		return _config_map[section];
	}


	ConfigMgr& operator=(const ConfigMgr& src) {
		if (&src == this) {
			return *this;
		}

		this->_config_map = src._config_map;
	};

	ConfigMgr(const ConfigMgr& src) {
		this->_config_map = src._config_map;
	}

	static ConfigMgr& Inst() {
		static ConfigMgr cfg_mgr;
		return cfg_mgr;
	}

	// 在首次调用 Inst() 之前设置，用于指定自定义配置文件路径（支持相对路径或绝对路径）。
	// 若未设置，则回退到可执行文件所在目录下的 "config.ini"（保持原有行为）。
	static void SetConfigPath(const std::string& path) {
		_s_config_path = path;
	}

	std::string GetValue(const std::string& section, const std::string& key);
private:
	ConfigMgr();
	// �洢section��key-value�Ե�map
	std::map<std::string, SectionInfo> _config_map;
	// 由 SetConfigPath 写入；ConfigMgr 构造时读取此路径
	static std::string _s_config_path;
};

