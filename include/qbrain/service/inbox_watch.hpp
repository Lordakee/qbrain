#pragma once
#include "qbrain/core/brain.hpp"
#include <filesystem>

namespace qbrain::service {

std::filesystem::path inbox_dir();
int watch_inbox_once(Brain& brain);

}  // namespace qbrain::service
