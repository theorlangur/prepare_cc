#include "log.h"
#include <iostream>
#include <ostream>

static Log g_GlobalLogLevel = Log::Error;
static std::ostream g_Null(0);

void setGlobalLogLevel(Log l) { g_GlobalLogLevel = l;}
Log getGlobalLogLevel() {return g_GlobalLogLevel;}

std::ostream& log(Log l)
{
    if (l <= getGlobalLogLevel())
      return std::cout;
    else
      return g_Null;
}