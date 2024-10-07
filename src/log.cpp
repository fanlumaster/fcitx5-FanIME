#include "log.h"
#include <iostream>
#include <ctime>

Log::Log(const std::string &filename) : log_file(filename, std::ios_base::app) {
  if (!log_file.is_open()) {
    std::cerr << "Error: Could not open log file!" << std::endl;
  }
}

Log::~Log() {
  if (log_file.is_open()) {
    log_file.close();
  }
}

void Log::info(const std::string &message) { write(INFO, message); }

void Log::warning(const std::string &message) { write(WARNING, message); }

void Log::error(const std::string &message) { write(ERROR, message); }

void Log::write(Level level, const std::string &message) {
  std::lock_guard<std::mutex> lock(log_mutex); // 保证线程安全
  if (log_file.is_open()) {
    log_file << getTimestamp() << " [" << levelToString(level) << "] " << message << std::endl;
  }
}

std::string Log::getTimestamp() {
  std::time_t now = std::time(0);
  char buffer[80];
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return std::string(buffer);
}

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
