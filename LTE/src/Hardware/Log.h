#ifndef LOG_H
#define  LOG_H

#define ERROR 1
#define LOG 2
#define NOTIFY 3
#define DEBUG 4

void Log(char level,const char* format, ...);

#endif 