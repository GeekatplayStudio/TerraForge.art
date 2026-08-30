#pragma once

#include <string>
#include <vector>

namespace Geekatplay {

class WALJournal {
public:
    explicit WALJournal(const std::string& journalPath);

    void LogMutation(const std::string& action, const std::string& payload);
    void Truncate();
    std::vector<std::pair<std::string, std::string>> Replay() const;

private:
    std::string m_path;
};

} // namespace Geekatplay
