#pragma once

#include "isaac_seed_seeker/core.hpp"

#include <string_view>

namespace isaac_seed_seeker {

struct BuiltinProfileInfo {
    std::string_view id;
    std::string_view game_version;
    std::string_view game_build;
    std::string_view source_collectible_table_sha256;
    std::string_view source_trinket_pool_sha256;
    std::string_view collectible_semantic_sha256;
    std::string_view trinket_semantic_sha256;
};

const BuiltinProfileInfo& builtin_j460_profile_info() noexcept;
ProfileTables builtin_j460_profile();

}  // namespace isaac_seed_seeker
