#include "../include/geekatplay/persistence.hpp"
#include <fstream>
#include <sstream>

namespace Geekatplay {

WALJournal::WALJournal(const std::string& journalPath) : m_path(journalPath) {}

void WALJournal::LogMutation(const std::string& action, const std::string& payload) {
    std::ofstream ofs(m_path, std::ios::app);
    if (ofs.is_open()) {
        ofs << action << "|" << payload << "\n";
    }
}

void WALJournal::Truncate() {
    std::ofstream ofs(m_path, std::ios::trunc);
}

std::vector<std::pair<std::string, std::string>> WALJournal::Replay() const {
    std::vector<std::pair<std::string, std::string>> entries;
    std::ifstream ifs(m_path);
    if (!ifs.is_open()) return entries;

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        auto pos = line.find('|');
        if (pos != std::string::npos) {
            std::string action = line.substr(0, pos);
            std::string payload = line.substr(pos + 1);
            entries.emplace_back(action, payload);
        }
    }
    return entries;
}

} // namespace Geekatplay
