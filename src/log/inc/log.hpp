#pragma once
#include "logger.hpp"

namespace engine
{
extern Logger logger;
}

#if ENABLE_LOG
#define LOG_INFO(...) ::engine::logger.log(INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) ::engine::logger.log(DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) ::engine::logger.log(ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) ::engine::logger.log(WARNING, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_TRANSITION(...) ::engine::logger.log(ED_TRANSITION, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ACTIVITY(...) ::engine::logger.log(ED_ACTIVITY, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_EVENT(...) ::engine::logger.log(ED_EVENT, __FILE__, __LINE__, __VA_ARGS__)
#define ASSERT(expr, message)                               \
  do                                                        \
  {                                                         \
    if(!(expr))                                             \
    {                                                       \
      LOG_ERROR("Condition assert: ", #expr, " ", message); \
    }                                                       \
  } while(0)
#else
#define LOG_INFO(...)
#define LOG_DEBUG(...)
#define LOG_ERROR(...)
#define LOG_WARNING(...)
#define LOG_TRANSITION(...)
#define LOG_ACTIVITY(...)
#define LOG_EVENT(...)
#define ASSERT(expr, message) \
  do                          \
  {                           \
    (void)sizeof(expr);       \
  } while(0)
#endif
