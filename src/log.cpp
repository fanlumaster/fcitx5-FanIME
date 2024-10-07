#include "log.h"
#include <iostream>
#include <ctime>

// 构造函数，打开日志文件
Log::Log(const std::string &filename) : log_file(filename, std::ios_base::app) {
  if (!log_file.is_open()) {
    std::cerr << "Error: Could not open log file!" << std::endl;
  }
}

// 析构函数，关闭日志文件
Log::~Log() {
  if (log_file.is_open()) {
    log_file.close();
  }
}

// 打印信息日志
void Log::info(const std::string &message) { write(INFO, message); }

// 打印警告日志
void Log::warning(const std::string &message) { write(WARNING, message); }

// 打印错误日志
void Log::error(const std::string &message) { write(ERROR, message); }

// 写入日志（私有方法）
void Log::write(Level level, const std::string &message) {
  std::lock_guard<std::mutex> lock(log_mutex); // 保证线程安全
  if (log_file.is_open()) {
    log_file << getTimestamp() << " [" << levelToString(level) << "] " << message << std::endl;
  }
}

// 获取当前时间的时间戳（私有方法）
std::string Log::getTimestamp() {
  std::time_t now = std::time(0);
  char buffer[80];
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return std::string(buffer);
}

// 将日志级别转换为字符串（私有方法）
std::string Log::levelToString(Level level) {
  switch (level) {
  case INFO:
    return "INFO";
  case WARNING:
    return "WARNING";
  case ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}
