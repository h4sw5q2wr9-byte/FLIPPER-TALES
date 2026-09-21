#include "ft_storage.h"

#include <furi.h>
#include <storage/storage.h>

/* APP_DATA_PATH puts this under /ext/apps_data/flipper_tales, which the
 * firmware creates for us and which survives an app update. */
#define SAVE_PATH APP_DATA_PATH("save.bin")

bool ft_storage_save(const FtSaveData* data) {
    uint8_t bytes[FT_SAVE_MAX_BYTES];
    const uint8_t len = ft_save_encode(data, bytes, sizeof(bytes));
    if(len == 0u) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;

    if(storage_file_open(file, SAVE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = storage_file_write(file, bytes, len) == len;
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    return ok;
}

bool ft_storage_load(FtSaveData* data) {
    uint8_t bytes[FT_SAVE_MAX_BYTES];

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    uint16_t got = 0;

    if(storage_file_open(file, SAVE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        got = storage_file_read(file, bytes, sizeof(bytes));
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    if(got == 0u) return false;

    /* A short, truncated or corrupt file is refused here, so the caller gets
     * a clean new game rather than a half-restored one. */
    return ft_save_decode(bytes, (uint8_t)got, data);
}

bool ft_storage_erase(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const bool gone = storage_simply_remove(storage, SAVE_PATH);
    furi_record_close(RECORD_STORAGE);

    return gone;
}
