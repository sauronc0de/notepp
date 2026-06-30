#pragma once

#include <cxxabi.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

#include <boost/hana.hpp>
#include <nlohmann/json.hpp>

namespace hana = boost::hana;

namespace JsonIO
{
inline std::string demangle(const char *name)
{
#if defined(__GNUG__)
  int status = 0;
  char *demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
  std::string result = (status == 0 && demangled) ? demangled : name;
  std::free(demangled);
  return result;
#else
  return name;
#endif
}

template <typename T>
inline std::string type_name()
{
  return demangle(typeid(T).name());
}

// Generic serialization using Boost.Hana
template <typename T>
void to_json(nlohmann::json &j, T const &obj)
{
  hana::for_each(hana::keys(obj), [&](auto key) {
    auto name = hana::to<const char *>(key);
    j[name] = hana::at_key(obj, key);
  });
}

template <typename T>
void from_json(nlohmann::json const &j, T &obj)
{
  T def{}; // default-constructed value

  hana::for_each(hana::keys(obj), [&](auto key) {
    auto name = hana::to<const char *>(key);
    using ValueT = std::decay_t<decltype(hana::at_key(obj, key))>;

    if(j.contains(name))
    {
      hana::at_key(obj, key) = j.at(name).template get<ValueT>();
    }
    else
    {
      // Automatically use the default value for missing fields
      hana::at_key(obj, key) = hana::at_key(def, key);
    }
  });
}

template <typename T>
T loadJsonFile(std::string const &path)
{
  std::ifstream file(path);
  if(!file.is_open())
  {
    throw std::runtime_error("Failed to open: " + path);
  }

  nlohmann::json j;
  file >> j;

  return j.get<T>();
}

template <typename T>
void saveJsonFile(std::string const &path, T const &data)
{
  std::ofstream file(path);
  if(!file.is_open())
  {
    throw std::runtime_error("Failed to open for writing: " + path);
  }

  nlohmann::json j = data; // uses ADL to call to_json()
  file << j.dump(4);       // pretty print (4 spaces indentation)
}

template <typename T>
std::vector<std::pair<std::string, T>> loadAllFromDir(std::string const &dirPath)
{
  std::vector<std::pair<std::string, T>> out;

  for(auto const &entry : std::filesystem::directory_iterator(dirPath))
  {
    if(!entry.is_regular_file())
      continue;

    auto path = entry.path().string();
    try
    {
      T obj = loadJsonFile<T>(path);
      out.emplace_back(path, std::move(obj));
    }
    catch(std::exception const &e)
    {
      // std::cerr << "❌ Failed to load " << path << ": " << e.what() << std::endl;
    }
  }
  return out;
}

template <typename T>
void print_struct(T const &obj)
{
  std::cout << "{ ";
  bool first = true;
  hana::for_each(hana::keys(obj), [&](auto key) {
    if(!first)
      std::cout << ", ";
    first = false;

    auto name = hana::to<const char *>(key);
    std::cout << name << ": " << hana::at_key(obj, key);
  });
  std::cout << " }" << std::endl;
}

template <typename T>
bool overwrite(std::string filePath, T json_struct)
{
  // Overwrite the file
  std::ofstream outFile(filePath);
  if(!outFile)
  {
    std::cerr << "❌ Failed to open file for writing: " << filePath << "\n";
    return 1;
  }

  nlohmann::json j = json_struct;
  outFile << j.dump(2) << "\n";

  // std::cout << "✅ Overwritten file with updated AAA at: " << filePath << "\n";

  return 0;
}

//---------------------------------------------------------
// Append one event to a JSON log file
//---------------------------------------------------------
template <typename T>
void append_event(const T &evt, const std::string &path)
{
  // Type name (demangled)
  std::string type_name = JsonIO::type_name<T>();

  nlohmann::json j_evt;
  to_json(j_evt, evt);

  nlohmann::json j_entry = {
      {"type", type_name},
      {"data", j_evt}};

  // Load old log
  nlohmann::json j_log;
  std::ifstream in(path);
  if(in.good())
    in >> j_log;
  if(!j_log.is_array())
    j_log = nlohmann::json::array();

  j_log.push_back(j_entry);
  std::ofstream(path) << j_log.dump(2);
}

//---------------------------------------------------------
// Read all logged events generically
//---------------------------------------------------------
template <typename Callback>
void read_events(const std::string &path, Callback on_event)
{
  std::ifstream in(path);
  if(!in.good())
    throw std::runtime_error("Cannot open JSON log: " + path);

  nlohmann::json j_log;
  in >> j_log;

  if(!j_log.is_array())
    throw std::runtime_error("Invalid log format");

  for(auto &entry : j_log)
  {
    std::string type = entry.at("type").get<std::string>();
    on_event(type, entry); // pass full entry
  }
}

//* Macros to define JSON-serializable structs (Only structs!)
#define JSON_IO_STRUCT(NAME, ...)                           \
  struct NAME                                               \
  {                                                         \
    BOOST_HANA_DEFINE_STRUCT(NAME, __VA_ARGS__);            \
  };                                                        \
  inline void to_json(nlohmann::json &j, NAME const &obj)   \
  {                                                         \
    JsonIO::to_json(j, obj);                                \
  }                                                         \
  inline void from_json(nlohmann::json const &j, NAME &obj) \
  {                                                         \
    JsonIO::from_json(j, obj);                              \
  }

//* Macro to define JSON-serializable fields to be used inside a class (Only classes!)
#define JSON_IO_FIELDS(NAME, ...)                           \
public: /* Public is a must to allow Hana access to it */   \
  BOOST_HANA_DEFINE_STRUCT(NAME, __VA_ARGS__);              \
                                                            \
  friend void to_json(nlohmann::json &j, const NAME &obj)   \
  {                                                         \
    JsonIO::to_json(j, obj);                                \
  }                                                         \
                                                            \
  friend void from_json(const nlohmann::json &j, NAME &obj) \
  {                                                         \
    JsonIO::from_json(j, obj);                              \
  }

} // namespace JsonIO