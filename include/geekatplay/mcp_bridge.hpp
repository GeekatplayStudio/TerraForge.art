#pragma once

#include <string>

namespace Geekatplay {

class MCPBridge {
public:
    // Dispatches and responds to JSON-RPC Model Context Protocol requests
    static std::string HandleJSONRPC(const std::string& requestJson);
};

} // namespace Geekatplay
