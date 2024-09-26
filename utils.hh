#pragma once
#ifndef XERXES_UTILS_HH
#define XERXES_UTILS_HH

#include <iostream>

namespace xerxes {
enum XerxesLogLevel {
  NONE = 0,
  INFO = 1,
  TEMP = 2,
  DEBUG = 3,
  WARNING = 4,
  ERROR = 5
};

inline XerxesLogLevel str_to_log_level(const std::string &str) {
  if (str == "INFO")
    return INFO;
  if (str == "TEMP")
    return TEMP;
  if (str == "DEBUG")
    return DEBUG;
  if (str == "WARNING")
    return WARNING;
  if (str == "ERROR")
    return ERROR;
  return NONE;
}

class XerxesLogger {
private:
  XerxesLogLevel glb_level;
  XerxesLogLevel cur_level;
  std::ostream *stream;

  XerxesLogger() : glb_level(NONE), cur_level(NONE), stream(&std::cout) {}

public:
  template <typename T> XerxesLogger &operator<<(const T &t) {
    if (cur_level <= glb_level)
      *stream << t;
    return *this;
  }

  XerxesLogger &operator<<(std::ostream &(*f)(std::ostream &)) {
    if (cur_level <= glb_level)
      *stream << f;
    return *this;
  }

  XerxesLogger &operator<<(XerxesLogLevel l) {
    cur_level = l;
    return *this;
  }
  static XerxesLogger &get_or_set(bool set = false,
                                  std::ostream &os = std::cout,
                                  XerxesLogLevel level = NONE) {
    static XerxesLogger logger = XerxesLogger{};
    if (set) {
      logger.stream = &os;
      logger.glb_level = level;
    }
    return logger;
  }
  static void set(std::ostream &os, XerxesLogLevel level = NONE) {
    get_or_set(true, os, level);
  }
  static XerxesLogger &info() { return get_or_set() << INFO; }
  static XerxesLogger &temp() { return get_or_set() << TEMP; }
  static XerxesLogger &debug() { return get_or_set() << DEBUG; }
  static XerxesLogger &warning() { return get_or_set() << WARNING; }
  static XerxesLogger &error() { return get_or_set() << ERROR; }
};

#ifndef PANIC
#define PANIC(msg) __panic((msg), __FILE__, __LINE__)
#endif
#ifndef ASSERT
#define ASSERT(cond, msg) __assert((cond), (msg), __FILE__, __LINE__)
#endif

inline void __panic(const std::string &msg, const std::string &file, int line) {
  std::cerr << "Panic at " << file << ":" << line << " : " << msg << std::endl;
  XerxesLogger::info() << "Panic at " << file << ":" << line << " : " << msg
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
