#ifndef _ENT_COMM_H_
#define _ENT_COMM_H_

#include "ent_types.h"

#ifdef ENT_MODULE_BEING_COMPILED
#ifdef WIN32
#define ENT_PUBLIC __declspec(dllexport)
#else
#define ENT_PUBLIC
#endif
#else
#if defined(WIN32) && !defined(LINKING_LIBENT)
#define ENT_PUBLIC __declspec(dllimport)
#else
#define ENT_PUBLIC extern
#endif
#endif

#endif