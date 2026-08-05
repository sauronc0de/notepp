#pragma once

#include "note_project.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace notepp::command_api
{
struct Response
{
  bool ok = false;
  nlohmann::json body;

  std::string serialize() const;
};

struct VariableResult
{
  bool success = false;
  nlohmann::json value;
  std::string error;
};

struct VariableAdapter
{
  std::function<VariableResult(const std::filesystem::path &, std::string_view, std::string_view)> get;
  std::function<VariableResult(const std::filesystem::path &, std::string &, std::string_view, const nlohmann::json &)> set;
};

// Headless implementation of the command protocol. All file operations are
// performed synchronously by the caller (the GUI invokes this at a frame
// boundary, while the CLI invokes it through IPC).
class Api
{
public:
  explicit Api(project::ProjectInfo project);
  Response execute(const nlohmann::json &request);
  nlohmann::json capabilities() const;
  void set_variable_adapter(VariableAdapter adapter);

  static constexpr std::string_view protocol_version = "1";

private:
  project::ProjectInfo project_;
  VariableAdapter variable_adapter_;
};

std::string endpoint_for_project(const project::ProjectInfo &project);
} // namespace notepp::command_api
