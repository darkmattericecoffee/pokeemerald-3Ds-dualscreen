/*
 * Whole files to the SD card from a thread of their own, so the game's
 * frame never waits on the card: the plane editor's edit files, its index
 * and log, the online votes and review marks (3ds_card_writer.c).
 *
 * A queued write replaces the file whole (written beside it and renamed
 * over, as before) or appends to it. Anything about to read one of these
 * files calls CtrCardWriter_Flush first, which returns at once when
 * nothing waits; the shutdown flushes too.
 */
#ifndef CTR_CARD_WRITER_H
#define CTR_CARD_WRITER_H

#include <stdbool.h>
#include <stdint.h>

/* Queues `size` bytes for `path`; the data is copied. Written here and now
 * when the thread cannot run or the queue is full. False when it failed. */
bool CtrCardWriter_Queue(const char *path, const void *data, uint32_t size, bool append);
/* Waits until every queued write is on the card. */
void CtrCardWriter_Flush(void);
bool CtrCardWriter_Pending(void);

#endif
