/*-----------------------------------------------------------------------------
 *   Copyright 2019 Fei Li<if16888@foxmail.com>
 * 
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 *-----------------------------------------------------------------------------
 */
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