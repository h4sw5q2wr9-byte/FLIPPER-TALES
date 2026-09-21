/* Where the save lives. The only file that talks to Storage.
 *
 * ft_save.c owns the *layout*; this owns the SD card. Keeping them apart is
 * what lets the format be tested on a host with no card in sight. */
#ifndef FT_STORAGE_H
#define FT_STORAGE_H

#include "../core/ft_save.h"

/* All three are best-effort: a missing card, a full card or a corrupt file
 * means the run carries on unsaved rather than the app dying. */
bool ft_storage_save(const FtSaveData* data);
bool ft_storage_load(FtSaveData* data);
/* Delete the save. Used only behind the New game confirmation. */
bool ft_storage_erase(void);

#endif /* FT_STORAGE_H */
