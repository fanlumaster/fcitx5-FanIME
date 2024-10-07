#ifndef LOG_H
#define LOG_H

#include <string>
#include <fstream>
#include <mutex>

class Log {
public:
  // 日志级别
  enum Level { INFO, WARNING, ERROR };

  // 构造函数，指定日志文件
  Log(const std::string &filename);

  // 析构函数，关闭文件
  ~Log();

  // 打印信息日志
  void info(const std::string &message);

  // 打印警告日志
  void warning(const std::string &message);

  // 打印错误日志
  void error(const std::string &message);

private:
  std::ofstream log_file; // 日志文件输出流
  std::mutex log_mutex;   // 保证日志写入线程安全

  // 获取当前时间的时间戳
  std::string getTimestamp();

  // 将日志级别转换为字符串
  std::string levelToString(Level level);

  // 写入日志
  void write(Level level, const std::string &message);
};

#endif // LOG_H
