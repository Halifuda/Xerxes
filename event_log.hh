#pragma once
#ifndef XERXES_EVENT_LOG_HH
#define XERXES_EVENT_LOG_HH

#include "def.hh"
#include <string>
#include <vector>
#include <fstream>
#include <cassert>

namespace xerxes {

class TimedEventLog {
public:
    TimedEventLog(const std::string &name, const std::vector<std::string> &cols)
        : name_(name), cols_(cols) {}

    void log(Tick timestamp, const std::vector<double> &values) {
        assert(values.size() == cols_.size());
        rows_.push_back({timestamp, values});
    }

    void write_csv(std::ostream &os) const {
        os << "timestamp";
        for (auto &c : cols_) os << "," << c;
        os << std::endl;
        for (auto &row : rows_) {
            os << row.first;
            for (auto v : row.second) os << "," << v;
            os << std::endl;
        }
    }

    const std::string &name() const { return name_; }
    bool empty() const { return rows_.empty(); }
    const auto &rows() const { return rows_; }

private:
    std::string name_;
    std::vector<std::string> cols_;
    std::vector<std::pair<Tick, std::vector<double>>> rows_;
};

} // namespace xerxes
#endif
