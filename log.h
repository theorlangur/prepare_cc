#ifndef LOG_H_
#define LOG_H_

#include <ostream>

enum class Log
{
    Error,
    Warning,
    Info,
    Debug
};

std::ostream& log(Log l);
inline std::ostream& lErr() {return log(Log::Error);}
inline std::ostream& lWarn() {return log(Log::Warning);}
inline std::ostream& lInfo() {return log(Log::Info);}
inline std::ostream& lDbg() {return log(Log::Debug);}

void setGlobalLogLevel(Log l);
Log getGlobalLogLevel();

#endif