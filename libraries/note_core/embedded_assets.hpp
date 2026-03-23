#pragma once

#include <cstddef>
#include <string_view>

namespace NoteppEmbeddedAssets
{
struct AssetSpan
{
  const unsigned char *data = nullptr;
  size_t size = 0;

  constexpr explicit operator bool() const noexcept
  {
    return data != nullptr;
  }
};

AssetSpan find_asset(std::string_view path);
bool has_asset(std::string_view path);
} // namespace NoteppEmbeddedAssets
