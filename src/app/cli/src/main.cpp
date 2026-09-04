#include "command_api.hpp"
#include "command_ipc.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using Json = nlohmann::json;

void usage(std::ostream &output)
{
  output << "Usage:\n"
         << "  notepp-cli --help\n"
         << "  notepp-cli --project ROOT [--json] app capabilities\n"
         << "  notepp-cli --project ROOT [--json] note list [--folder FOLDER]\n"
         << "  notepp-cli --project ROOT [--json] note get --note NOTE\n"
         << "  notepp-cli --project ROOT [--json] note create --folder FOLDER --name NAME [--content TEXT]\n"
         << "  notepp-cli --project ROOT [--json] note header list --note NOTE\n"
         << "  notepp-cli --project ROOT [--json] note header get --note NOTE --title TITLE\n"
         << "  notepp-cli --project ROOT [--json] note header create --note NOTE --title TITLE [--parent PARENT] [--level LEVEL]\n"
         << "  notepp-cli --project ROOT [--json] note line create --note NOTE [--heading HEADING] [--heading-occurrence N] --line LINE\n"
         << "  notepp-cli --project ROOT [--json] note color set --note NOTE --color '#RRGGBB'\n"
         << "  notepp-cli --project ROOT [--json] [note] variable get --note NOTE --name NAME\n"
         << "  notepp-cli --project ROOT [--json] [note] variable set --note NOTE --name NAME --value JSON\n"
         << "NOTE accepts a unique id or project-root-relative markdown path.\n"
         << "  notepp-cli --project ROOT [--json] execute JSON\n"
         << "  cat request.json | notepp-cli --project ROOT [--json] execute --stdin\n\n"
         << "Notepp must be running with the selected project open.\n"
         << "The complete protocol is documented at:\n"
         << "https://github.com/sauronc0de/notepp/blob/main/docs/command_api.md\n";
}

notepp::project::ProjectInfo project_info(const std::filesystem::path &root)
{
  notepp::project::ProjectInfo project;
  project.root = root;
  project.notes = root / "notes";
  project.assets = root / "assets";
  project.config = root / "config";
  project.projectFile = root / "notepp.project.json";
  std::ifstream input(project.projectFile);
  if(input)
  {
    try
    {
      const auto manifest = Json::parse(input);
      project.projectId = manifest.value("projectId", std::string{});
      project.schemaVersion = manifest.value("schemaVersion", 0);
    }
    catch(const std::exception &)
    { /* GUI owns project initialization. */
    }
  }
  project.workspace = notepp::project::get_appdata_dir() / "projects" / project.projectId;
  return project;
}

struct ParseResult
{
  Json request;
  int error = 0; // 0 = valid, 2 = usage, 4 = invalid JSON
  std::string message;
};

ParseResult parse_friendly(const std::vector<std::string> &tokens)
{
  ParseResult result;
  if(tokens.size() < 2U)
  {
    result.error = 2;
    result.message = "missing command";
    return result;
  }
  const std::string &group = tokens[0];
  std::size_t pos = 1;
  Json args = Json::object();
  std::string command;
  if(group == "app" && pos < tokens.size() && tokens[pos] == "capabilities")
  {
    ++pos;
    command = "app.capabilities";
  }
  else if(group == "note")
  {
    if(pos >= tokens.size())
    {
      result.error = 2;
      result.message = "missing note command";
      return result;
    }
    const std::string action = tokens[pos++];
    if(action == "list")
      command = "note.list";
    else if(action == "get")
      command = "note.get";
    else if(action == "create")
      command = "note.create";
    else if(action == "header")
    {
      if(pos >= tokens.size())
      {
        result.error = 2;
        result.message = "missing header command";
        return result;
      }
      const std::string header_action = tokens[pos++];
      if(header_action == "list")
        command = "note.header.list";
      else if(header_action == "get")
        command = "note.header.get";
      else if(header_action == "create")
        command = "note.header.create";
      else
      {
        result.error = 2;
        result.message = "unknown header command";
        return result;
      }
    }
    else if(action == "line")
    {
      if(pos >= tokens.size() || tokens[pos++] != "create")
      {
        result.error = 2;
        result.message = "expected line create";
        return result;
      }
      command = "note.line.create";
    }
    else if(action == "color")
    {
      if(pos >= tokens.size() || tokens[pos++] != "set")
      {
        result.error = 2;
        result.message = "expected color set";
        return result;
      }
      command = "note.color.set";
    }
    else if(action == "variable")
    {
      if(pos >= tokens.size() || (tokens[pos] != "get" && tokens[pos] != "set"))
      {
        result.error = 2;
        result.message = "expected variable get or set";
        return result;
      }
      command = tokens[pos++] == "set" ? "note.variable.set" : "note.variable.get";
    }
    else
    {
      result.error = 2;
      result.message = "unknown note command";
      return result;
    }

    while(pos < tokens.size())
    {
      const std::string key = tokens[pos++];
      if(key == "--folder" || key == "--note" || key == "--name" || key == "--content" ||
         key == "--parent" || key == "--level" || key == "--title" || key == "--value" ||
         key == "--color" || key == "--heading" || key == "--heading-occurrence" || key == "--line")
      {
        if(pos >= tokens.size() || tokens[pos].rfind("--", 0) == 0)
        {
          result.error = 2;
          result.message = "missing value for " + key;
          return result;
        }
        const std::string value = tokens[pos++];
        if(key == "--note")
        {
          const std::filesystem::path selector(value);
          if(selector.has_extension() || value.find('/') != std::string::npos ||
             value.find('\\') != std::string::npos)
            args["path"] = selector.generic_string();
          else
            args["id"] = value;
        }
        else if(key == "--folder")
          args["folder"] = value;
        else if(key == "--name" || key == "--title")
        {
          if(command == "note.variable.get" || command == "note.variable.set")
            args["name"] = value;
          else
            args["title"] = value;
        }
        else if(key == "--content")
          args["content"] = value;
        else if(key == "--parent")
          args["parent"] = value;
        else if(key == "--heading")
          args["heading"] = value;
        else if(key == "--line")
          args["line"] = value;
        else if(key == "--heading-occurrence")
        {
          try
          {
            std::size_t consumed = 0;
            const int occurrence = std::stoi(value, &consumed);
            if(consumed != value.size() || occurrence < 0)
              throw std::invalid_argument("invalid occurrence");
            args["heading_occurrence"] = occurrence;
          }
          catch(const std::exception &)
          {
            result.error = 2;
            result.message = "invalid heading occurrence";
            return result;
          }
        }
        else if(key == "--level")
        {
          try
          {
            args["level"] = std::stoi(value);
          }
          catch(const std::exception &)
          {
            result.error = 2;
            result.message = "invalid level";
            return result;
          }
        }
        else if(key == "--value")
        {
          try
          {
            args["value"] = Json::parse(value);
          }
          catch(const std::exception &)
          {
            result.error = 4;
            result.message = "invalid JSON value";
            return result;
          }
        }
        else
        {
          if(value.size() != 7U || value.front() != '#' ||
             value.substr(1).find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
          {
            result.error = 2;
            result.message = "color must be #RRGGBB";
            return result;
          }
          try
          {
            const int color = std::stoi(value.substr(1), nullptr, 16);
            args["r"] = (color >> 16) & 255;
            args["g"] = (color >> 8) & 255;
            args["b"] = color & 255;
          }
          catch(const std::exception &)
          {
            result.error = 2;
            result.message = "invalid color";
            return result;
          }
        }
      }
      else
      {
        result.error = 2;
        result.message = "unknown option: " + key;
        return result;
      }
    }
  }
  else if(group == "variable")
  {
    if(pos >= tokens.size() || (tokens[pos] != "get" && tokens[pos] != "set"))
    {
      result.error = 2;
      result.message = "expected variable get or set";
      return result;
    }
    const bool set = tokens[pos++] == "set";
    command = set ? "note.variable.set" : "note.variable.get";
    while(pos < tokens.size())
    {
      const std::string key = tokens[pos++];
      if(key != "--note" && key != "--name" && key != "--value")
      {
        result.error = 2;
        result.message = "unknown option: " + key;
        return result;
      }
      if(pos >= tokens.size())
      {
        result.error = 2;
        result.message = "missing value for " + key;
        return result;
      }
      const std::string value = tokens[pos++];
      if(key == "--note")
      {
        const std::filesystem::path selector(value);
        if(selector.has_extension() || value.find('/') != std::string::npos ||
           value.find('\\') != std::string::npos)
          args["path"] = selector.generic_string();
        else
          args["id"] = value;
      }
      else if(key == "--name")
        args["name"] = value;
      else
      {
        try
        {
          args["value"] = Json::parse(value);
        }
        catch(const std::exception &)
        {
          result.error = 4;
          result.message = "invalid JSON value";
          return result;
        }
      }
    }
  }
  else
  {
    result.error = 2;
    result.message = "unknown command group";
    return result;
  }
  if(pos != tokens.size())
  {
    result.error = 2;
    result.message = "unexpected argument: " + tokens[pos];
    return result;
  }
  result.request = Json{{"id", "cli"}, {"command", command}, {"args", std::move(args)}};
  return result;
}

void print_note_summary(std::ostream &output, const Json &note, std::string_view prefix = "- ")
{
  const std::string title = note.value("title", note.value("name", std::string{"(untitled)"}));
  output << prefix << title;
  if(note.contains("id") && note.at("id").is_string()) output << " [" << note.at("id").get<std::string>() << "]";
  if(note.contains("path") && note.at("path").is_string()) output << " (" << note.at("path").get<std::string>() << ")";
  output << '\n';
}

void print_human_response(std::ostream &output, const std::string &command, const Json &envelope)
{
  const bool success = envelope.value("success", envelope.value("ok", false));
  if(!success)
  {
    const Json error = envelope.value("error", Json::object());
    output << "Error";
    if(error.is_object() && error.contains("code") && error.at("code").is_string())
      output << " [" << error.at("code").get<std::string>() << "]";
    if(error.is_object() && error.contains("message") && error.at("message").is_string())
      output << ": " << error.at("message").get<std::string>();
    else if(error.is_string())
      output << ": " << error.get<std::string>();
    else
      output << ": " << error.dump();
    output << '\n';
    return;
  }

  const Json result = envelope.value("result", Json{});
  if(command == "note.list" && result.is_object() && result.contains("notes") && result.at("notes").is_array())
  {
    const auto &notes = result.at("notes");
    output << "Notes (" << notes.size() << "):\n";
    for(const auto &note : notes) print_note_summary(output, note);
    return;
  }
  if(result.is_object() && result.contains("headers") && result.at("headers").is_array())
  {
    output << "Headers:\n";
    for(const auto &header : result.at("headers"))
    {
      output << "- " << header.value("title", std::string{"(untitled)"});
      if(header.contains("level")) output << " (level " << header.at("level") << ")";
      output << '\n';
    }
    if(result.contains("note") && result.at("note").is_object())
    {
      output << "Note: ";
      print_note_summary(output, result.at("note"), "");
    }
    return;
  }
  if(result.is_object() && result.contains("note") && result.at("note").is_object())
  {
    print_note_summary(output, result.at("note"), "");
    if(result.contains("content") && result.at("content").is_string())
    {
      const std::string content = result.at("content").get<std::string>();
      output << content;
      if(content.empty() || content.back() != '\n') output << '\n';
    }
    return;
  }
  if(result.is_object() && result.contains("header") && result.at("header").is_object())
  {
    const auto &header = result.at("header");
    output << header.value("title", std::string{"(untitled)"});
    if(header.contains("level")) output << " (level " << header.at("level") << ")";
    if(header.contains("offset")) output << " at offset " << header.at("offset");
    output << '\n';
    return;
  }
  if(result.is_object() && result.contains("name") && result.contains("value"))
  {
    output << result.value("name", std::string{"(unnamed)"}) << " = " << result.at("value").dump() << '\n';
    return;
  }
  if(command == "app.capabilities" && result.is_object() && result.contains("commands"))
  {
    output << "Available commands:\n";
    for(const auto &entry : result.at("commands"))
      if(entry.is_object()) output << "- " << entry.value("name", std::string{"(unnamed)"}) << '\n';
    return;
  }
  output << result.dump(2) << '\n';
}

ParseResult parse_request(const std::vector<std::string> &tokens)
{
  if(tokens.empty()) return {Json{}, 2, "missing command"};
  if(tokens[0] != "execute") return parse_friendly(tokens);
  if(tokens.size() != 2U) return {Json{}, 2, "execute expects JSON or --stdin"};
  std::string text;
  if(tokens[1] == "--stdin")
  {
    std::ostringstream input;
    input << std::cin.rdbuf();
    text = input.str();
  }
  else
    text = tokens[1];
  try
  {
    Json request = Json::parse(text);
    if(!request.is_object() || (!request.contains("command") && !request.contains("method")))
      return {Json{}, 5, "protocol request requires command or method"};
    if(!request.contains("id")) request["id"] = "cli";
    return {std::move(request), 0, {}};
  }
  catch(const std::exception &error)
  {
    return {Json{}, 4, error.what()};
  }
}
} // namespace

int main(int argc, char **argv)
{
  if(argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h"))
  {
    usage(std::cout);
    return 0;
  }
  if(argc < 4 || std::string(argv[1]) != "--project")
  {
    usage(std::cerr);
    return 2;
  }
  const auto project = project_info(std::filesystem::path(argv[2]));
  std::vector<std::string> tokens;
  bool json_mode = false;
  for(int i = 3; i < argc; ++i)
  {
    if(std::string(argv[i]) == "--json")
      json_mode = true;
    else
      tokens.emplace_back(argv[i]);
  }
  const ParseResult parsed = parse_request(tokens);
  if(parsed.error != 0)
  {
    if(!parsed.message.empty()) std::cerr << parsed.message << '\n';
    return parsed.error == 5 ? 5 : parsed.error;
  }
  std::string error;
  const std::string response = notepp::command_ipc::Client::request(
      notepp::command_api::endpoint_for_project(project), parsed.request.dump(), &error);
  if(response.empty())
  {
    std::cerr << "Notepp is not running: " << error << '\n';
    return 3;
  }
  try
  {
    const auto envelope = Json::parse(response);
    if(!envelope.contains("success") && !envelope.contains("ok"))
    {
      std::cerr << "protocol mismatch\n";
      return 5;
    }
    const bool success = envelope.value("success", envelope.value("ok", false));
    if(json_mode)
      std::cout << response << '\n';
    else
      print_human_response(std::cout,
                           parsed.request.value("command", parsed.request.value("method", std::string{})),
                           envelope);
    return success ? 0 : 1;
  }
  catch(const std::exception &error2)
  {
    std::cerr << "invalid response from Notepp: " << error2.what() << '\n';
    return 5;
  }
}
