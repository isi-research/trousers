
/*
 * Licensed Materials - Property of IBM
 *
 * trousers - An open source TCG Software Stack
 *
 * (C) Copyright International Business Machines Corp. 2004
 *
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "trousers/tss.h"
#include "spi_internal_types.h"
#include "spi_utils.h"
#include "capabilities.h"
#include "memmgr.h"
#include "tsplog.h"
#include "obj.h"

/* caller needs to lock memtable lock */
struct memTable *
getTable(TSS_HCONTEXT tspContext)
{
	struct memTable *tmp;

	for (tmp = SpiMemoryTable; tmp; tmp = tmp->nextTable)
		if (tmp->tspContext == tspContext)
			return tmp;

	return NULL;
}

/* caller needs to lock memtable lock and be sure the context mem slot for
 * @tspContext exists before calling.
 */
void
addEntry(TSS_HCONTEXT tspContext, struct memEntry *new)
{
	struct memTable *tmp = getTable(tspContext);
	struct memEntry *tmp_entry = tmp->entries;

	if (tmp->entries == NULL) {
		tmp->entries = new;
		return;
	}

	/* else tack @new onto the end */
	for (; tmp_entry; tmp_entry = tmp_entry->nextEntry)
		if (tmp_entry->nextEntry == NULL) {
			tmp_entry->nextEntry = new;
			break;
		}
}

/* caller needs to lock memtable lock */
void
addTable(struct memTable *new)
{
	struct memTable *tmp = SpiMemoryTable;

	/* base case, this is the first table */
	if (SpiMemoryTable == NULL) {
		SpiMemoryTable = new;
		return;
	}

	/* else add @new onto the end */
	for (; tmp; tmp = tmp->nextTable)
		if (tmp->nextTable == NULL) {
			tmp->nextTable = new;
			break;
		}
}

/* caller needs to lock memtable lock */
TSS_RESULT
freeTable(TSS_HCONTEXT tspContext)
{
	struct memTable *prev = NULL, *index = NULL, *next = NULL;
	struct memEntry *entry = NULL, *entry_next = NULL;

	for(index = SpiMemoryTable; index; index = index->nextTable) {
		next = index->nextTable;
		if (index->tspContext == tspContext) {
			for (entry = index->entries; entry; entry = entry_next) {
				/* this needs to be set before we do free(entry) */
				entry_next = entry->nextEntry;
				free(entry->memPointer);
				free(entry);
			}

			if (prev != NULL)
				prev->nextTable = next;
			else
				SpiMemoryTable = NULL;

			free(index);
			break;
		}
		prev = index;
	}

	return TSS_SUCCESS;
}

TSS_RESULT
freeEntry(struct memTable *table, void *pointer)
{
	struct memEntry *index = NULL;
	struct memEntry *prev = NULL;
	struct memEntry *toKill = NULL;

	for (index = table->entries; index; prev = index, index = index->nextEntry) {
		if (index->memPointer == pointer) {
			toKill = index;
			if (prev == NULL)
				table->entries = toKill->nextEntry;
			else
				prev->nextEntry = toKill->nextEntry;

			free(pointer);
			free(toKill);
			return TSS_SUCCESS;

		}

	}
	LogError1("Internal error: pointer to allocated memory not found.");
	return TSPERR(TSS_E_INTERNAL_ERROR);
}

/*
 * calloc_tspi will be called by functions outside of this file. All locking
 * is done here.
 */
void *
calloc_tspi(TSS_HCONTEXT tspContext, UINT32 howMuch)
{
	struct memTable *table = NULL;
	struct memEntry *newEntry = NULL;

	pthread_mutex_lock(&memtable_lock);

	table = getTable(tspContext);
	if (table == NULL) {
		/* no table has yet been created to hold the memory allocations of
		 * this context, so we need to create one
		 */
		table = calloc(1, sizeof(struct memTable));
		if (table == NULL) {
			LogError("malloc of %d bytes failed.", sizeof(struct memTable));
			pthread_mutex_unlock(&memtable_lock);
			return NULL;
		}
		table->tspContext = tspContext;
		addTable(table);
	}

	newEntry = calloc(1, sizeof(struct memEntry));
	if (newEntry == NULL) {
		LogError("malloc of %d bytes failed.", sizeof(struct memEntry));
		pthread_mutex_unlock(&memtable_lock);
		return NULL;
	}

	newEntry->memPointer = calloc(1, howMuch);
	if (newEntry->memPointer == NULL) {
		LogError("malloc of %d bytes failed.", howMuch);
		pthread_mutex_unlock(&memtable_lock);
		return NULL;
	}

	/* this call must happen inside the lock or else another thread could
	 * remove the context mem slot, causing a segfault
	 */
	addEntry(tspContext, newEntry);

	pthread_mutex_unlock(&memtable_lock);

	return newEntry->memPointer;
}

/*
 * free_tspi will be called by functions outside of this file. All locking
 * is done here.
 */
TSS_RESULT
free_tspi(TSS_HCONTEXT tspContext, void *memPointer)
{
	struct memTable *index;
	TSS_RESULT result;

	pthread_mutex_lock(&memtable_lock);

	if (memPointer == NULL) {
		result = freeTable(tspContext);
		pthread_mutex_unlock(&memtable_lock);
		return result;
	}

	if ((index = getTable(tspContext)) == NULL) {
		pthread_mutex_unlock(&memtable_lock);
		return TSPERR(TSS_E_INVALID_HANDLE);
	}

	/* just free one entry */
	result = freeEntry(index, memPointer);

	pthread_mutex_unlock(&memtable_lock);

	return result;
}
