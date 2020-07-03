/*-------------------------------------------------------------------------
 *
 * nodeFuzzyClustering.c
 *	  Support routines for fuzzy clustering.
 *
 * Portions Copyright (c) 2020, PostgreSQLf UCAB Development Group
 * Portions Copyright (c) 2020, Regents of the Universidad Catolica Andres Bello
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/nodeFuzzyClustering.c
 *
 *-------------------------------------------------------------------------
 */
/*
 * INTERFACE ROUTINES
 *		ExecInitFuzzyClustering     creates and initializes a fuzzyclustering node.
 *		ExecFuzzyClustering         return generated tuples.
 *		ExecEndFuzzyClustering      releases any storage allocated.
 *		ExecReScanFuzzyClustering   rescans.
 */
#include "postgres.h"

#include "access/relscan.h"
#include "executor/executor.h"
#include "executor/execdebug.h"
#include "executor/nodeFuzzyClustering.h"
#include "utils/rel.h"
#include "../include/utils/tuplestore.h"

#include <math.h>

#include "miscadmin.h" // necesario para usar work_mem



float calcularPertenencia(int j, int dimensionGrupo, float datos[dimensionGrupo], int cantGrupos, TupleTableSlot *slot, FuzzyClusteringState *node){
	float membership = 0;
	float sum = 0;
	float distance = 0;
	float distanceaux = 0;
	
	for (int i = 0; i < dimensionGrupo; i++){
		distance += pow ( datos[i] - node->centros[j * cantGrupos + i], 2);
	}
	
	distance = sqrt(distance);
	
	for (int k = 0; k < cantGrupos; k++){
		distanceaux = 0;
		
		for (int i = 0; i < dimensionGrupo; i++){
			distanceaux += pow ( datos[i] - node->centros[k * cantGrupos + i], 2);
		}
		distanceaux = sqrt(distanceaux);
		
		sum += pow( distance / distanceaux , 2/(node->fuzziness-1) );
	}
	
	
	if (sum > 0){
		membership = 1 / ( sum );
	}
	else{
		return 1;
	}
	
	//membership = 1 / ( sum );
	
	return membership;
}

float calcularCentro (int i, int j, int totalTuplas, TupleTableSlot *slot, FuzzyClusteringState *node){

	float sum1, sum2 = 0;
	int aux = 0;
	/*
	for (int aux = 0; aux < totalTuplas; aux++){
		sum1 += pow ( node->pertenencia[aux][i] , node->fuzziness) * datos[aux][j];
		sum2 += pow ( node->pertenencia[aux][i] , node->fuzziness);
	}*/

	slot = ExecProcNode(outerPlanState(node));

	do{

		if (slot == NULL)
			elog(ERROR, "error al obtener tupla");

		if (slot->tts_isempty == FALSE){
			
			slot->tts_tuple = ExecMaterializeSlot(slot);
			slot_getallattrs(slot);

			sum1 += pow ( node->pertenencia[aux * totalTuplas + i] , node->fuzziness) * DatumGetFloat4(slot->tts_values[j]);
			//if(node->calcular == false)
			//	elog(ERROR, "sum1 %f: %f , %f * %f pow: %f", sum1, node->pertenencia[aux * totalTuplas + i], node->fuzziness, DatumGetFloat4(slot->tts_values[j]), pow ( node->pertenencia[aux * totalTuplas + i] , node->fuzziness));
			sum2 += pow ( node->pertenencia[aux * totalTuplas + i] , node->fuzziness);

			aux++;
		}else
			break;

		slot = ExecProcNode(outerPlanState(node));

	} while (!slot->tts_isempty);
//if(node->calcular == false)
//	elog(ERROR, "sums %f / %f", sum1, sum2);
	return (sum1/sum2);

}


/* -------------------
 * ExecFuzzyClustering
 * -------------------
 */
TupleTableSlot *
ExecFuzzyClustering(FuzzyClusteringState *node)
{

	TupleTableSlot *slot;

	
/*
	fcstate->tupla_actual = 0; 	
	fcstate->total_tuplas = 0;
	fcstate->calcular = true;
	fcstate->pertenencia = NULL;
	fcstate->centros = NULL;
	fcstate->cant_grupos = node->cant_grupos;
	fcstate->fuzziness = node->fuzziness;
	fcstate->error = node->error;
*/
	
	slot = ExecProcNode(outerPlanState(node));
	slot->tts_tuple = ExecMaterializeSlot(slot);
	slot_getallattrs(slot);

	if(node->calcular == TRUE){

		float truncNum = 0;
		int nvalid = slot->tts_nvalid;
		time_t t;   
		/* Intializes random number generator */
		srand((unsigned) time(&t));


		/**
		 * lo primero que se realiza es contar
		 * el numero de tuplas que van a ser procesadas
		 */

		do{

			if (slot == NULL)
				elog(ERROR, "error al obtener tupla");

			if (slot->tts_isempty == FALSE)
				node->total_tuplas++;
			else
				break;

			slot = ExecProcNode(outerPlanState(node));

		} while (!slot->tts_isempty);

		/**
		 * una vez que se tiene el numero total de tuplas
		 * se inicializa la matriz de pertenencia
		 * con numeros aleatorios
		 */

		int totalTuplas = node->total_tuplas;
		int cantGrupos = (int) node->cant_grupos;
		node->pertenencia = palloc(sizeof *node->pertenencia * totalTuplas * cantGrupos);
		if(node->pertenencia==NULL){
			elog(ERROR, "palloc() failed: insufficient memory");
		}

		for (int i = 0; i < totalTuplas; i++){
			float aux = 0;
			for (int j = 0; j < cantGrupos; j++){
				if (j != cantGrupos-1){
					node->pertenencia[i * totalTuplas + j] = ((float)rand()/(float)(RAND_MAX))  * (1-aux);
					aux += node->pertenencia[i * totalTuplas + j];
				} else {
					node->pertenencia[i * totalTuplas + j] = 1-aux;
				}
			}
		}


		/** una vez se ha inicializado la matriz de pertenencias
		 * se inicializa la matriz de centros de los clusters
		 */

		int dimensionGrupo = nvalid - cantGrupos;
		node->centros = palloc(sizeof *node->pertenencia * cantGrupos * dimensionGrupo);
		if(node->centros==NULL){
			elog(ERROR, "palloc() failed: insufficient memory");
		}

		for (int i = 0; i < cantGrupos; i++){
			for (int j = 0; j < dimensionGrupo; j++){
				/*truncNum = calcularCentro(i, j, totalTuplas, slot, node);
				truncNum = trunc(1000*truncNum)/1000;
				node->centros[i * cantGrupos + j] = truncNum;*/
				node->centros[i * cantGrupos + j] = calcularCentro(i, j, totalTuplas, slot, node);
			}
		}

		/**
		 * Teniendo las dos matrices inicializadas (pertenencia y centros), 
		 * comienza el ciclo de calculos que terminara cuando el error sea
		 * menor al especificado por el usuario
		 */
		float aux = 0;
		float diff = 0;
		float diffaux = 0;
		float datos[dimensionGrupo];	//arreglo para guardar temporalmente los datos de la tupla actual para calcular la pertenencia

		do{
			/**
			 * actualizar matriz de pertenencia
			 */
			for (int i = 0; i < totalTuplas; i++){

				slot = ExecProcNode(outerPlanState(node));
				slot->tts_tuple = ExecMaterializeSlot(slot);
				slot_getallattrs(slot);

				for (int  k = 0; k < dimensionGrupo; k++){
					datos[k] = DatumGetFloat4(slot->tts_values[k]);
				}

				for (int j = 0; j < cantGrupos; j++){
					/*truncNum = calcularPertenencia(j, dimensionGrupo, datos, cantGrupos, slot, node);
					truncNum = trunc(1000*truncNum)/1000;
					node->pertenencia[i * totalTuplas + j] = truncNum;*/
					node->pertenencia[i * totalTuplas + j] = calcularPertenencia(j, dimensionGrupo, datos, cantGrupos, slot, node);
				}
			}
			slot = ExecProcNode(outerPlanState(node));

			/**
			 * actualizar la matriz de centros
			 */

			aux = 0;
			diff = 0;

			for (int i = 0; i < cantGrupos; i++){
				diffaux = 0;
				for (int j = 0; j < dimensionGrupo; j++){
					aux = node->centros[i * cantGrupos + j];
					/*truncNum = calcularCentro(i, j, totalTuplas, slot, node);
					truncNum = trunc(1000*truncNum)/1000;
					node->centros[i * cantGrupos + j] = truncNum;*/
					node->centros[i * cantGrupos + j] = calcularCentro(i, j, totalTuplas, slot, node);

					diffaux += pow ( node->centros[i * cantGrupos + j] - aux, 2);
				}
				diffaux = fabs(diffaux);
				diffaux = sqrt(diffaux);

				if ( diffaux > diff ){
					diff = diffaux;
				}
			}
			
		} while (/*fabs(diff) > node->error*/ false);
		

	slot = ExecProcNode(outerPlanState(node));
	node->calcular = false;
			
	}

	slot->tts_tuple = ExecMaterializeSlot(slot);
	slot_getallattrs(slot);

	//char *palabra;

	char str[64];
	char str2[64];
	char str3[64];
	sprintf(str, "(%f, %f)", node->centros[0 * (int) node->cant_grupos + 0], node->centros[0 * (int) node->cant_grupos + 1]);

	//elog(ERROR, "str %s / ctrs %f , %f", str, node->centros[0 * (int) node->cant_grupos + 0], node->centros[0 * (int) node->cant_grupos + 1]);
	strcpy(slot->tts_tupleDescriptor->attrs[2]->attname.data, str);

	sprintf(str2, "(%f, %f)", node->centros[1 * (int) node->cant_grupos + 0], node->centros[1 * (int) node->cant_grupos + 1]);
	strcpy(slot->tts_tupleDescriptor->attrs[3]->attname.data, str2);

	sprintf(str3, "(%f, %f)", node->centros[2 * (int) node->cant_grupos + 0], node->centros[2 * (int) node->cant_grupos + 1]);
	strcpy(slot->tts_tupleDescriptor->attrs[4]->attname.data, str3);
	//strcpy(slot->tts_tupleDescriptor->attrs[0]->attname.data, "texto");
	

	//elog(ERROR, "str: %s", slot->tts_tupleDescriptor->attrs[2]->attname.data);

	
		//elog(ERROR, "centr %f %f", node->centros[0 * (int) node->cant_grupos + 0], node->centros[0 * (int) node->cant_grupos + 1]);

	if (node->tupla_actual == 0){
		slot->tts_values[0] = Float4GetDatumFast(node->centros[0 * (int) node->cant_grupos + 0]);
		slot->tts_values[1] = Float4GetDatumFast(node->centros[0 * (int) node->cant_grupos + 1]);
	}

	if (node->tupla_actual == 1){
		slot->tts_values[0] = Float4GetDatumFast(node->centros[1 * (int) node->cant_grupos + 0]);
		slot->tts_values[1] = Float4GetDatumFast(node->centros[1 * (int) node->cant_grupos + 1]);
	}

	if (node->tupla_actual == 2){
		slot->tts_values[0] = Float4GetDatumFast(node->centros[2 * (int) node->cant_grupos + 0]);
		slot->tts_values[1] = Float4GetDatumFast(node->centros[2 * (int) node->cant_grupos + 1]);
	}

	int cantGrupos = (int) node->cant_grupos;
	int dimensionDatos = slot->tts_nvalid - cantGrupos;

	for (int i = dimensionDatos; i < slot->tts_nvalid; i++){
		slot->tts_values[i] = Float4GetDatumFast(node->pertenencia[node->tupla_actual * node->total_tuplas + (i - dimensionDatos)]);
		slot->tts_isnull[i] = FALSE;
	}

	node->tupla_actual++;
	
		//slot->tts_values[3] = Float4GetDatumFast(datof);
		//elog(ERROR, "val: %f / %d", DatumGetFloat4(slot->tts_values[1]), node->tupla_actual);

	return slot;

}


/* -----------------------
 * ExecInitFuzzyClustering
 * -----------------------
 */
FuzzyClusteringState *
ExecInitFuzzyClustering(FuzzyClustering *node, EState *estate, int eflags)
{
	FuzzyClusteringState *fcstate;

	/*
	 * create state structure
	 */
	fcstate = makeNode(FuzzyClusteringState);
	fcstate->ss.ps.plan = (Plan *) node;
	fcstate->ss.ps.state = estate;
	fcstate->tupla_actual = 0; 	
	fcstate->total_tuplas = 0;
	fcstate->calcular = true;
	fcstate->pertenencia = NULL;
	fcstate->centros = NULL;
	fcstate->cant_grupos = node->cant_grupos;
	fcstate->fuzziness = node->fuzziness;
	fcstate->error = node->error;
	fcstate->tuplestorestate = NULL;

    /*
	 * create expression context
	 */
	ExecAssignExprContext(estate, &fcstate->ss.ps);

    /*
	 * tuple table initialization
	 */
	ExecInitScanTupleSlot(estate, &fcstate->ss);
	ExecInitResultTupleSlot(estate, &fcstate->ss.ps);

    /*
	 * initialize child expressions
	 */
	fcstate->ss.ps.targetlist = (List *)
		ExecInitExpr((Expr *) node->plan.targetlist,
					 (PlanState *) fcstate);
	fcstate->ss.ps.qual = (List *)
		ExecInitExpr((Expr *) node->plan.qual,
					 (PlanState *) fcstate);

    /*
	 * initialize child nodes
	 */
	outerPlanState(fcstate) = ExecInitNode(outerPlan(node), estate, eflags);

    /*
	 * Initialize result tuple type and projection info.
	 */
	ExecAssignResultTypeFromTL(&fcstate->ss.ps);
	ExecAssignProjectionInfo(&fcstate->ss.ps, NULL);

    fcstate->ss.ps.ps_TupFromTlist = false;
    

    return fcstate;
}

/* ----------------------
 * ExecEndFuzzyClustering
 * ----------------------
 */
void
ExecEndFuzzyClustering(FuzzyClusteringState *node)
{
	
    ExecFreeExprContext(&node->ss.ps);

    /* clean up tuple table */
	ExecClearTuple(node->ss.ss_ScanTupleSlot);

    ExecEndNode(outerPlanState(node));

}

