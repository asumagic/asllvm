#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
// Include order matters apparently (probably an AS bug: as_memory.h seems to be required by as_array.h)
// clang-format off
#include <as_memory.h>
#include <as_callfunc.h>
#include <as_scriptfunction.h>
#include <as_scriptobject.h>
#include <as_typeinfo.h>
#include <as_objecttype.h>
#include <as_scriptengine.h>
// clang-format on
#pragma GCC diagnostic pop
