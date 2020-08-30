#ifndef  _ENT_TYPES_H_
#define  _ENT_TYPES_H_

typedef int MSG_ID_T;

typedef void *DB_HANDLE;

#ifdef WIN32
#define ENT_FILE_SEP "\\"
#define ENT_FILE_SEP_C '\\'
#else
#define ENT_FILE_SEP "/"
#define ENT_FILE_SEP_C '/'
#endif

#ifdef __linux__
typedef int BOOL;
#define TRUE  1
#define FALSE 0
typedef unsigned long long UINT64;
typedef unsigned long      DWORD;
#define MAX_PATH PATH_MAX
#endif

#endif