#include "language.h"
#include "cJSON.h"
#include "fsdrive.h"
#include "fsutil.h"
#include "support.h"
#include "ui.h"

#define STRING(what, def) char *STR_##what = NULL;
#include "language.inl"
#undef STRING

typedef struct {
	char name[256];
	char path[256];
} Language;

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

int compLanguage(const void *e1, const void *e2) {
	const Language *entry2 = (const Language *) e2;
	const Language *entry1 = (const Language *) e1;
	return strncasecmp(entry1->name, entry2->name, 256);
}

bool LanguageMenu(char *result, const char *title) {
	DirStruct *langDir = (DirStruct *)malloc(sizeof(DirStruct));
	if (!langDir) return false;

	char path[256];
	if (!GetSupportDir(path, LANGUAGES_DIR)) return false;
	GetDirContents(langDir, path);

	char *header = (char *)malloc(0x2C0);
	Language *langs = (Language *)malloc(langDir->n_entries * sizeof(Language));
	int langCount = 0;

	// Find all valid files and get their language names
	for (u32 i = 0; i < langDir->n_entries; i++) {
		if (langDir->entry[i].type == T_FILE) {
			FileGetData(langDir->entry[i].path, header, 0x2C0, 0);
			if (GetLanguage(header, langs[langCount].name)) {
				memcpy(langs[langCount].path, langDir->entry[i].path, 256);
				langCount++;
			}
		}
	}

	free(langDir);
	free(header);

	qsort(langs, langCount, sizeof(Language), compLanguage);

	// Make an array of just the names for the select promt
	const char *langNames[langCount];
	for (int i = 0; i < langCount; i++) {
		langNames[i] = langs[i].name;
	}

	u32 selected = ShowSelectPrompt(langCount, langNames, "%s", title);
	if (selected > 0 && result) {
		memcpy(result, langs[selected - 1].path, 256);
	}

	return selected > 0;
}
