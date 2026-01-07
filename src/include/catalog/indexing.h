/*-------------------------------------------------------------------------
 *
 * indexing.h
 *	  This file provides some definitions to support indexing
 *	  on system catalogs
 *
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/catalog/indexing.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef INDEXING_H
#define INDEXING_H

#include "access/htup.h"
#include "nodes/execnodes.h"
#include "utils/relcache.h"

/*
 * The state object used by CatalogOpenIndexes and friends is actually the
 * same as the executor's ResultRelInfo, but we give it another type name
 * to decouple callers from that fact.
 */
typedef struct ResultRelInfo *CatalogIndexState;

/*
 * Cap the maximum amount of bytes allocated for multi-inserts with system
 * catalogs, limiting the number of slots used.
 */
#define MAX_CATALOG_MULTI_INSERT_BYTES 65535

/*
 * indexing.c prototypes
 */
extern CatalogIndexState CatalogOpenIndexes(Relation heapRel);
extern void CatalogCloseIndexes(CatalogIndexState indstate);
extern void CatalogTupleInsert(Relation heapRel, HeapTuple tup);
extern void CatalogTupleInsertWithInfo(Relation heapRel, HeapTuple tup,
									   CatalogIndexState indstate, bool yb_shared_insert);
extern void CatalogTuplesMultiInsertWithInfo(Relation heapRel,
											 TupleTableSlot **slot,
											 int ntuples,
<<<<<<< HEAD
											 CatalogIndexState indstate);
extern void CatalogTupleUpdate(Relation heapRel, const ItemPointerData *otid,
=======
											 CatalogIndexState indstate,
											 bool yb_shared_insert);
extern void CatalogTupleUpdate(Relation heapRel, ItemPointer otid,
>>>>>>> 939dce21892 (yb changes)
							   HeapTuple tup);
extern void CatalogTupleUpdateWithInfo(Relation heapRel,
									   const ItemPointerData *otid, HeapTuple tup,
									   CatalogIndexState indstate);
<<<<<<< HEAD
extern void CatalogTupleDelete(Relation heapRel, const ItemPointerData *tid);
=======
extern void CatalogTupleDelete(Relation heapRel, HeapTuple tup);

extern void YBCatalogTupleInsert(Relation heapRel, HeapTuple tup, bool yb_shared_insert);
>>>>>>> 939dce21892 (yb changes)

#endif							/* INDEXING_H */
