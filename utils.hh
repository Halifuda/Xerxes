#pragma once
#ifndef XERXES_UTILS_HH
#define XERXES_UTILS_HH

#include <iostream>

namespace xerxes {
enum LogLevel { NONE = 0, INFO = 1, TEMP = 2, DEBUG = 3, WARN = 4, ERROR = 5 };

class Logger {
private:
  LogLevel glb_level;
  LogLevel cur_level;
  std::ostream *stream;

  Logger() : glb_level(NONE), cur_level(NONE), stream(&std::cout) {}

public:
  template <typename T> Logger &operator<<(const T &t) {
    if (cur_level <= glb_level)
      *stream << t;
    return *this;
  }

  Logger &operator<<(std::ostream &(*f)(std::ostream &)) {
    if (cur_level <= glb_level)
      *stream << f;
    return *this;
  }

  Logger &operator<<(LogLevel l) {
    cur_level = l;
    return *this;
  }
  static Logger &get_or_set(bool set = false, std::ostream &os = std::cout,
                            LogLevel level = NONE) {
    static Logger logger = Logger{};
    if (set) {
      logger.stream = &os;
      logger.glb_level = level;
    }
    return logger;
  }
  static void set(std::ostream &os, LogLevel level = NONE) {
    get_or_set(true, os, level);
  }
  static Logger &info() { return get_or_set() << INFO; }
  static Logger &temp() { return get_or_set() << TEMP; }
  static Logger &debug() { return get_or_set() << DEBUG; }
  static Logger &warn() { return get_or_set() << WARN; }
  static Logger &error() { return get_or_set() << ERROR; }
};

#ifndef PANIC
#define PANIC(msg) __panic((msg), __FILE__, __LINE__)
#endif
#ifndef ASSERT
#define ASSERT(cond, msg) __assert((cond), (msg), __FILE__, __LINE__)
#endif

inline void __panic(const std::string &msg, const std::string &file, int line) {
  Logger::info() << "Panic at " << file << ":" << line << " : " << msg
                 << std::endl;
  std::exit(-1);
}

inline void __assert(bool cond, const std::string &msg, const std::string &file,
                     int line) {
  if (!cond)
    __panic(msg, file, line);
}
} // namespace xerxes

#endif // XERXES_UTILS_HH
