#include "language.h"
#include "cJSON.h"

#define STRING(what, def) char *STR_##what = NULL;
#include "language.inl"
#undef STRING

static char *getString(const cJSON *json, const char *key, const char *fallback) {
	cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);

	if (item && item->valuestring)
		return strdup(item->valuestring);
	else
		return strdup(fallback);
}

bool SetLanguage(const char *jsonStr, int jsonStrLen) {
	// Free strings if loaded
	#define STRING(what, def) if (STR_##what) free(STR_##what);
	#include "language.inl"
	#undef STRING

	cJSON *json = cJSON_ParseWithLength(jsonStr, jsonStrLen);
	if (!json) {
		// Reset to defaults
		#define STRING(what, def) STR_##what = strdup(def);
		#include "language.inl"
		#undef STRING

		return false;
	}

	// Load all the strings from the JSON
	#define STRING(what, def) STR_##what = getString(json, ""#what, def);
	#include "language.inl"
	#undef STRING

	cJSON_Delete(json);

	return true;
}

bool GetLanguage(const char *data, char *languageName) {
	// Not parsing as JSON since A) it's faster and B) this is used on just the
	// 'header' of the file in type checking. For that reason the "GM9_LANGUAGE"
	// *must* be in the first 0x2C0 bytes of the file.

	const char *str = strstr(data, "\"GM9_LANGUAGE\"");

	// If we were given a languageName pointer, copy the name into it
	if (str && languageName) {
		const char *start = strchr(str, ':');
		if (start) {
			start = strchr(start, '"');
			if (start) {
				start++;
				const char *end = strchr(start, '"');
				if (end) {
					int strLen = end - start;

					memcpy(languageName, start, strLen);
					languageName[strLen] = '\0';
				} else {
					strcpy(languageName, start);
				}
			}
		}
	}

	return str != NULL;
}
