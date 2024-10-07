#ifndef LOG_H
#define LOG_H

#include <string>
#include <fstream>
#include <mutex>

class Log {
public:
  enum Level { INFO, WARNING, ERROR };

  Log(const std::string &filename);

  ~Log();

  void info(const std::string &message);
  void warning(const std::string &message);
  void error(const std::string &message);

private:
  std::ofstream log_file; // 日志文件输出流
  std::mutex log_mutex;   // 保证日志写入线程安全

  std::string getTimestamp();

  std::string levelToString(Level level);

  void write(Level level, const std::string &message);
};

#endif // LOG_H
