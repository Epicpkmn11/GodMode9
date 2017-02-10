#include "firm.h"
#include "aes.h"
#include "sha.h"
#include "nand.h"
#include "keydb.h"
#include "ff.h"

// 0 -> pre 9.5 / 1 -> 9.5 / 2 -> post 9.5
#define A9L_CRYPTO_TYPE(hdr) ((hdr->k9l[3] == 0xFF) ? 0 : (hdr->k9l[3] == '1') ? 1 : 2)

u32 ValidateFirmHeader(FirmHeader* header, u32 data_size) {
    u8 magic[] = { FIRM_MAGIC };
    if (memcmp(header->magic, magic, sizeof(magic)) != 0)
        return 1;
    
    u32 firm_size = sizeof(FirmHeader);
    for (u32 i = 0; i < 4; i++) {
        FirmSectionHeader* section = header->sections + i;
        if (!section->size) continue;
        if (section->offset < firm_size) return 1;
        firm_size = section->offset + section->size;
    }
    
    if ((firm_size > FIRM_MAX_SIZE) || (data_size && (firm_size > data_size)))
        return 1;
    
    return 0;
}

u32 ValidateFirmA9LHeader(FirmA9LHeader* header) {
    const u8 enckeyX0x15hash[0x20] = {
        0x0A, 0x85, 0x20, 0x14, 0x8F, 0x7E, 0xB7, 0x21, 0xBF, 0xC6, 0xC8, 0x82, 0xDF, 0x37, 0x06, 0x3C,
        0x0E, 0x05, 0x1D, 0x1E, 0xF3, 0x41, 0xE9, 0x80, 0x1E, 0xC9, 0x97, 0x82, 0xA0, 0x84, 0x43, 0x08
    };
    return sha_cmp(enckeyX0x15hash, header->keyX0x15, 0x10, SHA256_MODE);
}

FirmSectionHeader* FindFirmArm9Section(FirmHeader* firm) {
    for (u32 i = 0; i < 4; i++) {
        FirmSectionHeader* section = firm->sections + i;
        if (section->size && (section->type == 0))
            return section;
    }
    return NULL;
}

u32 GetArm9BinarySize(FirmA9LHeader* a9l) {
    char* size_ascii = a9l->size_ascii;
    u32 size = 0;
    for (u32 i = 0; (i < 8) && *(size_ascii + i); i++)
        size = (size * 10) + (*(size_ascii + i) - '0');
    return size;
}

u32 SetupSecretKey(u32 keynum) {
    const char* base[] = { INPUT_PATHS };
    // from: https://github.com/AuroraWright/SafeA9LHInstaller/blob/master/source/installer.c#L9-L17
    const u8 sectorHash[0x20] = {
        0x82, 0xF2, 0x73, 0x0D, 0x2C, 0x2D, 0xA3, 0xF3, 0x01, 0x65, 0xF9, 0x87, 0xFD, 0xCC, 0xAC, 0x5C,
        0xBA, 0xB2, 0x4B, 0x4E, 0x5F, 0x65, 0xC9, 0x81, 0xCD, 0x7B, 0xE6, 0xF4, 0x38, 0xE6, 0xD9, 0xD3
    };
    static u8 __attribute__((aligned(32))) sector[0x200];
    u8 hash[0x20];
    
    // safety check
    if (keynum >= 0x200/0x10) return 1;
    
    // secret sector already loaded?
    sha_quick(hash, sector, 0x200, SHA256_MODE);
    if (memcmp(hash, sectorHash, 0x20) == 0) {
        setup_aeskey(0x11, sector + (keynum*0x10));
        use_aeskey(0x11);
        return 0;
    }
    
    // search for valid secret sector in SysNAND / EmuNAND
    const u32 nand_src[] = { NAND_SYSNAND, NAND_EMUNAND };
    for (u32 i = 0; i < sizeof(nand_src) / sizeof(u32); i++) {
        ReadNandSectors(sector, 0x96, 1, 0x11, nand_src[i]);
        sha_quick(hash, sector, 0x200, SHA256_MODE);
        if (memcmp(hash, sectorHash, 0x20) != 0) continue;
        setup_aeskey(0x11, sector + (keynum*0x10));
        use_aeskey(0x11);
        return 0;
    }
    
    // no luck? try searching for a file
    for (u32 i = 0; i < (sizeof(base)/sizeof(char*)); i++) {
        char path[64];
        FIL fp;
        UINT btr;
        snprintf(path, 64, "%s/%s", base[i], SECTOR_NAME);
        if (f_open(&fp, path, FA_READ | FA_OPEN_EXISTING) != FR_OK) {
            snprintf(path, 64, "%s/%s", base[i], SECRET_NAME);
            if (f_open(&fp, path, FA_READ | FA_OPEN_EXISTING) != FR_OK) continue;
        }
        f_read(&fp, sector, 0x200, &btr);
        f_close(&fp);
        sha_quick(hash, sector, 0x200, SHA256_MODE);
        if (memcmp(hash, sectorHash, 0x20) != 0) continue;
        setup_aeskey(0x11, sector + (keynum*0x10));
        use_aeskey(0x11);
        return 0;
    }
    
    // try to load from key database
    if ((keynum < 2) && (LoadKeyFromFile(NULL, 0x11, 'N', (keynum == 0) ? "95" : "96")))
        return 0; // key found in keydb, done
    
    // out of options
    return 1;
}

u32 DecryptA9LHeader(FirmA9LHeader* header) {
    u32 type = A9L_CRYPTO_TYPE(header);
    
    if (SetupSecretKey(0) != 0) return 1;
    aes_decrypt(header->keyX0x15, header->keyX0x15, 1, AES_CNT_ECB_DECRYPT_MODE);
    if (type) {
        if (SetupSecretKey((type == 1) ? 0 : 1) != 0) return 1;
        aes_decrypt(header->keyX0x16, header->keyX0x16, 1, AES_CNT_ECB_DECRYPT_MODE);
    }
    
    return 0;
}

u32 SetupArm9BinaryCrypto(FirmA9LHeader* header) {
    u32 type = A9L_CRYPTO_TYPE(header);
    
    if (type == 0) {
        u8 __attribute__((aligned(32))) keyX0x15[0x10];
        memcpy(keyX0x15, header->keyX0x15, 0x10);
        if (SetupSecretKey(0) != 0) return 1;
        aes_decrypt(keyX0x15, keyX0x15, 1, AES_CNT_ECB_DECRYPT_MODE);
        setup_aeskeyX(0x15, keyX0x15);
        setup_aeskeyY(0x15, header->keyY0x150x16);
        use_aeskey(0x15);
    } else {
        u8 __attribute__((aligned(32))) keyX0x16[0x10];
        memcpy(keyX0x16, header->keyX0x16, 0x10);
        if (SetupSecretKey((type == 1) ? 0 : 1) != 0) return 1;
        aes_decrypt(keyX0x16, keyX0x16, 1, AES_CNT_ECB_DECRYPT_MODE);
        setup_aeskeyX(0x16, keyX0x16);
        setup_aeskeyY(0x16, header->keyY0x150x16);
        use_aeskey(0x16);
    }
    
    return 0;
}

u32 DecryptArm9Binary(u8* data, u32 offset, u32 size, FirmA9LHeader* a9l) {
    // offset == offset inside ARM9 binary
    // ARM9 binary begins 0x800 byte after the ARM9 loader header
    
    // only process actual ARM9 binary
    u32 size_bin = GetArm9BinarySize(a9l);
    if (offset >= size_bin) return 0;
    else if (size >= size_bin - offset)
        size = size_bin - offset;
    
    // decrypt data
    if (SetupArm9BinaryCrypto(a9l) != 0) return 1;
    ctr_decrypt_byte(data, data, size, offset, AES_CNT_CTRNAND_MODE, a9l->ctr);
    
    return 0;
}

u32 DecryptFirm(u8* data, u32 offset, u32 size, FirmHeader* firm, FirmA9LHeader* a9l) {
    // ARM9 binary size / offset
    FirmSectionHeader* arm9s = FindFirmArm9Section(firm);
    u32 offset_arm9bin = arm9s->offset + ARM9BIN_OFFSET;
    u32 size_arm9bin = GetArm9BinarySize(a9l);
    
    // sanity checks
    if (!size_arm9bin || (size_arm9bin + ARM9BIN_OFFSET > arm9s->size))
        return 1; // bad header / data
    
    // check if ARM9 binary in data
    if ((offset_arm9bin >= offset + size) ||
        (offset >= offset_arm9bin + size_arm9bin))
        return 0; // section not in data
    
    // determine data / offset / size
    u8* data_i = data;
    u32 offset_i = 0;
    u32 size_i = size_arm9bin;
    if (offset_arm9bin < offset)
        offset_i = offset - offset_arm9bin;
    else data_i = data + (offset_arm9bin - offset);
    size_i = size_arm9bin - offset_i;
    if (size_i > size - (data_i - data))
        size_i = size - (data_i - data);
    
    return DecryptArm9Binary(data_i, offset_i, size_i, a9l);
}

u32 DecryptFirmSequential(u8* data, u32 offset, u32 size) {
    // warning: this will only work for sequential processing
    // unexpected results otherwise
    static FirmHeader firm = { 0 };
    static FirmA9LHeader a9l = { 0 };
    static FirmHeader* firmptr = NULL;
    static FirmA9LHeader* a9lptr = NULL;
    static FirmSectionHeader* arm9s = NULL;
    
    // fetch firm header from data
    if ((offset == 0) && (size >= sizeof(FirmHeader))) {
        memcpy(&firm, data, sizeof(FirmHeader));
        firmptr = (ValidateFirmHeader(&firm, 0) == 0) ? &firm : NULL;
        arm9s = (firmptr) ? FindFirmArm9Section(firmptr) : NULL;
        a9lptr = NULL;
    }
    
    // safety check, firm header pointer
    if (!firmptr) return 1;
    
    // fetch ARM9 loader header from data
    if (arm9s && !a9lptr && (offset <= arm9s->offset) &&
        ((offset + size) >= arm9s->offset + sizeof(FirmA9LHeader))) {
        memcpy(&a9l, data + arm9s->offset - offset, sizeof(FirmA9LHeader));
        a9lptr = (ValidateFirmA9LHeader(&a9l) == 0) ? &a9l : NULL;
    }
    
    return (a9lptr) ? DecryptFirm(data, offset, size, firmptr, a9lptr) : 0;
}