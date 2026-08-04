#include "project_settings.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>

namespace notepp::project_settings
{
namespace
{
using Json = nlohmann::json;

struct DocumentResult
{
  bool success = false;
  Json document = Json::object();
  Settings settings;
  atomic_file::Snapshot snapshot;
  bool needs_save = false;
  std::string message;
};

DocumentResult read_document(const std::filesystem::path &path)
{
  DocumentResult result;
  const auto loaded = atomic_file::read_text(path);
  if(!loaded)
  {
    result.message = loaded.message;
    return result;
  }
  result.snapshot = loaded.snapshot;
  if(loaded.snapshot.existed)
  {
    result.document = Json::parse(loaded.snapshot.content, nullptr, false);
    if(result.document.is_discarded() || !result.document.is_object())
    {
      result.message = "Project settings must be a JSON object: " + path.generic_string();
      return result;
    }
  }
  else
  {
    result.needs_save = true;
  }

  const auto schema = result.document.find("schemaVersion");
  if(schema != result.document.end() &&
     (schema->is_number_integer() || schema->is_number_unsigned()))
  {
    try
    {
      if(schema->is_number_unsigned())
      {
        const auto value = schema->get<std::uint64_t>();
        if(value > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        {
          result.message = "Project settings schema version is outside the supported range";
          return result;
        }
        result.settings.schema_version = static_cast<int>(value);
      }
      else
      {
        const auto value = schema->get<std::int64_t>();
        if(value < 0 || value > std::numeric_limits<int>::max())
        {
          result.message = "Project settings schema version is outside the supported range";
          return result;
        }
        result.settings.schema_version = static_cast<int>(value);
      }
    }
    catch(const std::exception &error)
    {
      result.message = std::string("Invalid project settings schema version: ") + error.what();
      return result;
    }
  }
  else
  {
    result.needs_save = true;
  }
  if(result.settings.schema_version > current_schema_version)
  {
    result.message = "Project settings schema is newer than this Notepp version: " +
                     std::to_string(result.settings.schema_version);
    return result;
  }
  if(result.settings.schema_version != current_schema_version)
  {
    result.settings.schema_version = current_schema_version;
    result.needs_save = true;
  }

  if(const auto language = result.document.find("language");
     language != result.document.end() && language->is_string())
    result.settings.language = language->get<std::string>();
  else
    result.needs_save = true;

  if(const auto enabled = result.document.find("gitSyncEnabled");
     enabled != result.document.end() && enabled->is_boolean())
  {
    result.settings.git_sync_enabled = enabled->get<bool>();
    result.settings.has_git_sync_enabled = true;
  }
  else
  {
    result.needs_save = true;
  }

  result.document["schemaVersion"] = current_schema_version;
  result.document["language"] = result.settings.language;
  result.document["gitSyncEnabled"] = result.settings.git_sync_enabled;
  result.document.erase("lastGitSync");
  // Git status is transient and remains in the application settings store;
  // persisting it here would dirty a Git-managed project after every sync.
  result.success = true;
  return result;
}

std::string serialize(const Json &document)
{
  std::string content = document.dump(2);
  content.push_back('\n');
  return content;
}
} // namespace

Store::Store(std::filesystem::path config_file) : config_file_(std::move(config_file)) {}

LoadResult Store::load(bool persist_defaults) noexcept
{
  try
  {
    auto document = read_document(config_file_);
    if(!document.success) return {false, document.settings, document.snapshot.existed, document.message};
    const bool existed = document.snapshot.existed;
    if(document.needs_save && persist_defaults)
    {
      const auto saved = atomic_file::save_text(config_file_, serialize(document.document),
                                                &document.snapshot);
      if(!saved) return {false, document.settings, existed, saved.message};
      document.snapshot = saved.new_snapshot;
    }
    observed_snapshot_ = document.snapshot;
    observed_settings_ = document.settings;
    return {true, document.settings, existed, {}};
  }
  catch(const std::exception &error)
  {
    return {false, {}, false, error.what()};
  }
}

UpdateResult Store::update(const std::optional<std::string> &language,
                           const std::optional<bool> &git_sync_enabled) noexcept
{
  try
  {
    auto document = read_document(config_file_);
    if(!document.success) return {false, document.settings, document.message};
    if(language) document.settings.language = *language;
    if(git_sync_enabled)
    {
      document.settings.git_sync_enabled = *git_sync_enabled;
      document.settings.has_git_sync_enabled = true;
    }
    document.document["schemaVersion"] = current_schema_version;
    document.document["language"] = document.settings.language;
    document.document["gitSyncEnabled"] = document.settings.git_sync_enabled;
    const auto saved = atomic_file::save_text(config_file_, serialize(document.document),
                                              &document.snapshot);
    if(!saved) return {false, document.settings, saved.message};
    observed_snapshot_ = saved.new_snapshot;
    observed_settings_ = document.settings;
    return {true, document.settings, {}};
  }
  catch(const std::exception &error)
  {
    return {false, {}, error.what()};
  }
}

UpdateResult Store::set_language(const std::string &language) noexcept
{
  return update(language, std::nullopt);
}

UpdateResult Store::set_git_sync_enabled(bool enabled) noexcept
{
  return update(std::nullopt, enabled);
}

PollResult Store::poll() noexcept
{
  const auto current = atomic_file::read_text(config_file_);
  if(!current) return {false, false, observed_settings_, current.message};
  if(observed_snapshot_ && current.snapshot == *observed_snapshot_)
    return {true, false, observed_settings_, {}};
  const auto loaded = load();
  if(!loaded) return {false, false, loaded.settings, loaded.message};
  return {true, true, loaded.settings, {}};
}
} // namespace notepp::project_settings
