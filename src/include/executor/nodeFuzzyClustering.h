/*-------------------------------------------------------------------------
 *
 * nodeFuzzyClustering.h
 *	 
 *
 *
 * Portions Copyright (c) 2020, PostgreSQLf UCAB Development Group
 * Portions Copyright (c) 2020, Regents of the Universidad Católica Andrés Bello
 *
 * src/include/executor/nodeFuzzyClustering.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEFUZZYCLUSTERING_H
#define NODEFUZZYCLUSTERING_H

#include "nodes/execnodes.h"

extern FuzzyClusteringState *ExecInitFuzzyClustering(FuzzyClustering *node, EState *estate, int eflags);
extern TupleTableSlot *ExecFuzzyClustering(FuzzyClusteringState *node);
extern void ExecEndFuzzyClustering(FuzzyClusteringState *node);
/*extern void ExecReScanFuzzyClustering(FuzzyClusteringState *node);*/

#endif   /* NODEFUZZYCLUSTERING_H */
