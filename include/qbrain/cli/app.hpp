#pragma once
#include <string>
#include <vector>

namespace qbrain::cli {

int run(int argc, char** argv);
std::string resolve_brain_id(const std::vector<std::string>& args);

}  // namespace qbrain::cli
