#pragma once

#include <filesystem>

namespace ld {

std::filesystem::path resourceFolder();
void setResourceFolder(std::filesystem::path folder);

}
