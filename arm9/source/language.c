#define STRING(what, def) const char *STR_##what = def;
#include "language.ja.inl"
#undef STRING

// TODO read from file
