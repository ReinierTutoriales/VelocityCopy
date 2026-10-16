#pragma once
#include "velocitycopy/shell_request.hpp"
#include <shlobj.h>

namespace velocitycopy::shell {
HRESULT read_sources(IDataObject* data, std::vector<std::filesystem::path>& paths);
HRESULT clipboard_sources_from_data(IDataObject* object, ShellRequest& request);
}
