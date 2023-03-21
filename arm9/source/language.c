#include "language.h"
#include "fsdrive.h"
#include "fsutil.h"
#include "support.h"
#include "ui.h"

#define STRING(what, def) const char *STR_##what = NULL;
#include "language.inl"
#undef STRING

static const char **translation_ptrs[] = {
	#define STRING(what, def) &STR_##what,
	#include "language.inl"
	#undef STRING
};

static const char *translation_fallbacks[] = {
	#define STRING(what, def) def,
	#include "language.inl"
	#undef STRING
};

static u8 *translation_data = NULL;

typedef struct {
	char name[32];
	char path[256];
} Language;

bool SetLanguage(const void *translation, u32 translation_size) {
	u32 str_count;
	u8* ptr = NULL;

	if ((ptr = GetLanguage(translation, translation_size, NULL, &str_count, NULL))) {
		// character data
		if (memcmp(ptr, "SDAT", 4) == 0) {
			u32 section_size;
			memcpy(&section_size, ptr + 4, sizeof(u32));

			if (translation_data) free(translation_data);
			translation_data = malloc(section_size);
			if (!translation_data) goto fallback;

			memcpy(translation_data, ptr + 8, section_size);

			ptr += 8 + section_size;
		} else goto fallback;

		// character map
		if (memcmp(ptr, "SMAP", 4) == 0) {
			u32 section_size;
			memcpy(&section_size, ptr + 4, sizeof(u32));

			u16 *string_map = (u16*)((u32)ptr + 8);

			// Load all the strings
			for (u32 i = 0; i < countof(translation_ptrs); i++) {
				if (i < str_count) {
					*translation_ptrs[i] = (char*)(translation_data + string_map[i]);
				} else {
					*translation_ptrs[i] = translation_fallbacks[i];
				}
			}

			ptr += 8 + section_size;
		} else goto fallback;

		return true;
	}

fallback:
	if (translation_data) {
		free(translation_data);
		translation_data = NULL;
	}

	for (u32 i = 0; i < countof(translation_ptrs); i++) {
		*translation_ptrs[i] = translation_fallbacks[i];
	}

	return false;
}

u8 *GetLanguage(const void *riff, const u32 riff_size, u32 *version, u32 *count, char *language_name) {
	u8 *ptr = (u8*) riff;
	u32 riff_version = 0;
	u32 riff_count = 0;
	char riff_lang_name[32] = "";

	// check header magic, then skip over
	if (!ptr || memcmp(ptr, "RIFF", 4) != 0) return NULL;

	// ensure enough space is allocated
	u32 data_size;
	memcpy(&data_size, ptr + 4, sizeof(u32));
	if (data_size > riff_size) return NULL;

	ptr += 8;

	// check for and load META section
	if (memcmp(ptr, "META", 4) == 0) {
		u32 section_size;
		memcpy(&section_size, ptr + 4, sizeof(u32));
		if (section_size != 40) return NULL;

		memcpy(&riff_version, ptr + 8, sizeof(u32));
		memcpy(&riff_count, ptr + 12, sizeof(u32));
		memcpy(riff_lang_name, ptr + 16, 31);
		if (riff_version != TRANSLATION_VER || riff_count > countof(translation_ptrs)) return NULL;

		ptr += 8 + section_size;
	} else return NULL;

	// all good
	if (version) *version = riff_version;
	if (count) *count = riff_count;
	if (language_name) strcpy(language_name, riff_lang_name);
	return ptr;
}

int compLanguage(const void *e1, const void *e2) {
	const Language *entry2 = (const Language *) e2;
	const Language *entry1 = (const Language *) e1;
	return strncasecmp(entry1->name, entry2->name, 32);
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
			size_t fsize = FileGetSize(langDir->entry[i].path);
			FileGetData(langDir->entry[i].path, header, 0x2C0, 0);
			if (GetLanguage(header, fsize, NULL, NULL, langs[langCount].name)) {
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
