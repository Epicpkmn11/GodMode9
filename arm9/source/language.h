#pragma once

#include "common.h"

#define STRING(what, def) extern char *STR_##what;
#include "language.inl"
#undef STRING

bool SetLanguage(const char *jsonStr, int jsonStrLen);
bool GetLanguage(const char *data, char *languageName);
