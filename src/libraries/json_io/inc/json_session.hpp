#include <mutex>
#include <map>
#include <string>
#include <memory>
#include "json_io.hpp"

class FileLockRegistry
{
public:
  static std::mutex &getMutex(const std::string &path)
  {
    static std::mutex globalMutex;
    static std::map<std::string, std::shared_ptr<std::mutex>> mutexMap;

    std::lock_guard<std::mutex> lock(globalMutex);
    auto &mtxPtr = mutexMap[path];
    if(!mtxPtr)
      mtxPtr = std::make_shared<std::mutex>();
    return *mtxPtr;
  }
};

template <typename T>
class JsonSession
{
public:
  T data;
  std::string path;

  JsonSession(const std::string &file_path)
      : path(file_path), lock(FileLockRegistry::getMutex(file_path))
  {
    data = JsonIO::loadJsonFile<T>(file_path);
  }

  ~JsonSession()
  {
    JsonIO::overwrite(path, data);
  }

  T *operator->() { return &data; }
  T &operator*() { return data; }

private:
  std::unique_lock<std::mutex> lock;
};
