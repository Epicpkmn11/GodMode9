#pragma once

#define STRING(what, def) extern const char *STR_##what;
#include "language.ja.inl"
#undef STRING
