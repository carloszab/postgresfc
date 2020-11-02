/*-------------------------------------------------------------------------
 *
 * nodeClustering.h
 *	 
 *
 *
 * Portions Copyright (c) 2020, PostgreSQLf UCAB Development Group
 * Portions Copyright (c) 2020, Regents of the Universidad Católica Andrés Bello
 *
 * src/include/executor/nodeClustering.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODECLUSTERING_H
#define NODECLUSTERING_H

#include "nodes/execnodes.h"

extern ClusteringState *ExecInitClustering(Clustering *node, EState *estate, int eflags);
extern TupleTableSlot *ExecClustering(ClusteringState *node);
extern void ExecEndClustering(ClusteringState *node);

#endif   /* NODECLUSTERING_H */
