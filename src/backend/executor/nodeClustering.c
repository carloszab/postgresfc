/*-------------------------------------------------------------------------
 *
 * nodeClustering.c
 *	  Support routines for fuzzy clustering.
 *
 * Portions Copyright (c) 2020, PostgreSQLf UCAB Development Group
 * Portions Copyright (c) 2020, Regents of the Universidad Catolica Andres Bello
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/nodeClustering.c
 *
 *-------------------------------------------------------------------------
 */
/*
 * INTERFACE ROUTINES
 *		ExecInitClustering     creates and initializes a clustering node.
 *		ExecClustering         return generated tuples.
 *		ExecEndClustering      releases any storage allocated.
 *		ExecReScanClustering   rescans.
 */
#include "postgres.h"

#include "access/relscan.h"
#include "executor/executor.h"
#include "executor/execdebug.h"
#include "executor/nodeClustering.h"
#include "utils/rel.h"
#include "../include/utils/tuplestore.h"
//#include "backend/utils/sort/tuplestore.c"
/*
src/include/utils/tuplestore.h
src/backend/utils/sort/tuplestore.c
*/
#include <math.h>

#include "miscadmin.h" // necesario para usar work_mem

float calcularPertenencia2(int j, int dimension, float datos[dimension], int cantGrupos, ClusteringState *node, float centros[cantGrupos][dimension]){
	float sum = 0;
	float distance = 0;
	float distanceaux = 0;

	for (int i = 0; i < dimension; i++){
		distance += pow ( datos[i] - centros[j][i], 2);
	}

	distance = sqrt(distance);

	for (int k = 0; k < cantGrupos; k++){
		distanceaux = 0;
		
		for (int i = 0; i < dimension; i++){
			distanceaux += pow ( datos[i] - centros[k][i], 2);
		}
		distanceaux = sqrt(distanceaux);
		
		sum += pow( distance / distanceaux , 2/(node->fuzziness-1) );
	}

	if (sum > 0){
		return (1 / sum);
	}
	else{
		return 1;
	}

}

float calcularPertenencia(int j, int dimensionGrupo, float datos[dimensionGrupo], int cantGrupos, TupleTableSlot *slot, ClusteringState *node){
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

float calcularCentro2 (int i, int j, int totalTuplas, ClusteringState *node, int cantGrupos, float pertenencia[totalTuplas][cantGrupos]){
	
	float sum1 = 0;
	float sum2 = 0;

	TupleTableSlot *slot;
	slot = ExecProcNode(outerPlanState(node));

	for(int aux = 0; !slot->tts_isempty; aux++){

		if (slot == NULL)
			elog(ERROR, "error al obtener tupla");

		slot->tts_tuple = ExecMaterializeSlot(slot);
		slot_getallattrs(slot);

		sum1 += pow ( pertenencia[aux][i] , node->fuzziness) * DatumGetFloat4(slot->tts_values[j]);
		sum2 += pow ( pertenencia[aux][i] , node->fuzziness);

		slot = ExecProcNode(outerPlanState(node));
	}

	return (sum1/sum2);
}

float calcularCentro (int i, int j, int totalTuplas, TupleTableSlot *slot, ClusteringState *node){

	float sum1 = 0;
	float sum2 = 0;
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
 * ExecClustering
 * -------------------
 */
TupleTableSlot *
ExecClusteringno(ClusteringState *node)
{
	
	TupleTableSlot *slot;
	slot = ExecProcNode(outerPlanState(node));

	if (TupIsNull(slot))
			return NULL;

	slot->tts_tuple = ExecMaterializeSlot(slot);
	slot_getallattrs(slot);

	if(node->tupla_actual == 0){

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

			slot = ExecProcNode(outerPlanState(node));

		} while (!TupIsNull(slot));

		

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
					//node->pertenencia[i * totalTuplas + j] = ((float)rand()/(float)(RAND_MAX))  * (1-aux);
					node->pertenencia[i * totalTuplas + j] = 0.5;
					aux += node->pertenencia[i * totalTuplas + j];
				} else {
					//node->pertenencia[i * totalTuplas + j] = 1-aux;
					node->pertenencia[i * totalTuplas + j] = 0;
				}
			}
		}
				elog(ERROR, "random pertenencia 0 1 2 %f %f %f", node->pertenencia[0 * totalTuplas + 0], node->pertenencia[0 * totalTuplas + 1], node->pertenencia[0 * totalTuplas + 2]);

		

		/** una vez se ha inicializado la matriz de pertenencias
		 * se inicializa la matriz de centros de los clusters
		 */

		int dimensionGrupo = nvalid - cantGrupos;
		node->centros = palloc(sizeof *node->centros * cantGrupos * dimensionGrupo);
		if(node->centros==NULL){
			elog(ERROR, "palloc() failed: insufficient memory");
		}

		for (int i = 0; i < cantGrupos; i++){
			for (int j = 0; j < dimensionGrupo; j++){
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
					node->centros[i * cantGrupos + j] = calcularCentro(i, j, totalTuplas, slot, node);

					diffaux += pow ( node->centros[i * cantGrupos + j] - aux, 2);
				}
				diffaux = fabs(diffaux);
				diffaux = sqrt(diffaux);

				if ( diffaux > diff ){
					diff = diffaux;
				}
			}
			
		} while (fabs(diff) > node->error);

		slot = ExecProcNode(outerPlanState(node));
		slot->tts_tuple = ExecMaterializeSlot(slot);
		slot_getallattrs(slot);
/*
		char str[9];
		char str2[64];

		for (int i = cantGrupos; i <= slot->tts_nvalid; i++){
			sprintf(str2, "(");
			for (int j = 0; j < dimensionGrupo; j++){

				sprintf(str, "%f", node->centros[(i-cantGrupos) * node->cant_grupos + j]);
				strcat(str2, str);
				if(j != dimensionGrupo-1)
					strcat(str2, ",");
			}
			strcat(str2, ")");
			
			strcpy(slot->tts_tupleDescriptor->attrs[i-1]->attname.data, str2);
		}
*/
		
		

	}
	
/*
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
*/

	int dimensionDatos = slot->tts_nvalid - node->cant_grupos;
/*
	for (int i = dimensionDatos; i < slot->tts_nvalid; i++){
		slot->tts_values[i] = Float4GetDatumFast(node->pertenencia[node->tupla_actual * node->total_tuplas + (i - dimensionDatos)]);
		slot->tts_isnull[i] = FALSE;
	}
*/

	/*sirve
		char *palabra = DatumGetCString(slot->tts_values[2]);
		slot->tts_values[4] = CStringGetDatum(palabra);
	*/

	/*sirve
		char *palabra = DatumGetCString(slot->tts_values[2]);
		sprintf(palabra, "chao");
		slot->tts_values[4] = CStringGetDatum(palabra);

		en navicat aparece hao
	*/
	
	/*sirve
		char *palabra = DatumGetCString(slot->tts_values[2]);
		strcat(palabra, "chao");
		slot->tts_values[4] = CStringGetDatum(palabra);

		en navicat a la palabra no se le agrega el chao
	*/
	char word[250];
	char *palabra;
	char *palabra2;
	char *palabra3;
	//char *palabra = DatumGetCString(slot->tts_values[2]);
	palabra = palloc(250);
	palabra2 = palloc(250);
	palabra3 = palloc(250);
	//sprintf(palabra, "numero (%f)", node->centros[node->tupla_actual * (int) node->cant_grupos + 0]);
	//char *palabra2 = "hola32";
	//sprintf(palabra, palabra2);
	float fl = 25.3;
	sprintf(palabra, "c %f (%d)\0", node->pertenencia[node->tupla_actual * node->total_tuplas + (2 - dimensionDatos)], node->tupla_actual);
	sprintf(palabra2, "c %f %d\0", node->pertenencia[node->tupla_actual * node->total_tuplas + (3 - dimensionDatos)], node->tupla_actual);
	sprintf(palabra3, "c %f %d\0", node->pertenencia[node->tupla_actual * node->total_tuplas + (4 - dimensionDatos)], node->tupla_actual);

	char outstring[node->cant_grupos][250];

	//outstring[0] = "hola";
	//outstring[1] = "chao";

	sprintf(outstring[0], "c %f ", node->pertenencia[node->tupla_actual * node->total_tuplas + (2 - dimensionDatos)]);
	sprintf(outstring[1], "c %f", node->pertenencia[node->tupla_actual * node->total_tuplas + (3 - dimensionDatos)]);
	sprintf(outstring[2], "c %f", node->pertenencia[node->tupla_actual * node->total_tuplas + (4 - dimensionDatos)]);

	for (int i = dimensionDatos; i < slot->tts_nvalid; i++){
		//slot->tts_values[i] = Float4GetDatumFast(node->pertenencia[node->tupla_actual * node->total_tuplas + (i - dimensionDatos)]);
		sprintf(outstring[i - dimensionDatos], "c %f ", node->pertenencia[node->tupla_actual * node->total_tuplas + (i - dimensionDatos)]);
	}

	if (node->tupla_actual == 0){
		char str[10];
		for (int i = node->cant_grupos; i <= slot->tts_nvalid; i++){
			strcat(outstring[i - node->cant_grupos], "(");
			for (int j = 0; j < dimensionDatos; j++){

				sprintf(str, "%f", node->centros[(i-node->cant_grupos) * node->cant_grupos + j]);
				strcat(outstring[i - node->cant_grupos], str);
				if(j != dimensionDatos-1)
					strcat(outstring[i - node->cant_grupos], ",");
			}
			strcat(outstring[i - node->cant_grupos], ")");
		}
	}

	//elog(ERROR, "los strings son: %s, %s, %s", outstring[0], outstring[1], outstring[2]);
	/*
	slot->tts_values[2] = CStringGetDatum(outstring[0]);
	slot->tts_isnull[2] = FALSE;
	slot->tts_values[3] = CStringGetDatum(outstring[1]);
	slot->tts_isnull[3] = FALSE;
	slot->tts_values[4] = CStringGetDatum(outstring[2]);
	slot->tts_isnull[4] = FALSE;
	*/
	for (int i = dimensionDatos; i < slot->tts_nvalid; i++){
		slot->tts_values[i] = CStringGetDatum(outstring[i - dimensionDatos]);
		slot->tts_isnull[i] = FALSE;
	}

	node->tupla_actual++;

	TupleDesc	tupdesc;
	tupdesc = slot->tts_tupleDescriptor;
	HeapTuple	tuple;
	tuple = heap_form_tuple(tupdesc, slot->tts_values, slot->tts_isnull);

return ExecStoreTuple(tuple, slot, InvalidBuffer, true);
}

TupleTableSlot *
ExecClustering(ClusteringState *node)
{

	TupleTableSlot *slot;
	slot = ExecProcNode(outerPlanState(node));

	if (TupIsNull(slot))
			return NULL;

	slot->tts_tuple = ExecMaterializeSlot(slot);
	slot_getallattrs(slot);

	int totalTuplas = node->total_tuplas;
	int cantGrupos = node->cant_grupos;
	int nvalid = slot->tts_nvalid;
	int dimension = nvalid - cantGrupos;


	if(node->tupla_actual == 0){

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

			slot = ExecProcNode(outerPlanState(node));
		} while (!TupIsNull(slot));

		totalTuplas = node->total_tuplas;

		/**
		 * una vez que se tiene el numero total de tuplas
		 * se inicializa la matriz de pertenencia
		 * con numeros aleatorios
		 */
/*
		node->pertenencia =	(float *)palloc(sizeof(float) * totalTuplas * cantGrupos);
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
*/
		float pertenencia[totalTuplas][cantGrupos];
		for(int i = 0; i < totalTuplas; i++){
			float aux = 0;
			for(int j = 0; j < cantGrupos; j++){
				if (j != cantGrupos-1){
					pertenencia[i][j] = ((float)rand()/(float)(RAND_MAX))  * (1-aux);
					aux += pertenencia[i][j];
				}else{
					pertenencia[i][j] = 1-aux;
				}
			}
		}
		

		/** una vez se ha inicializado la matriz de pertenencias
		 * se inicializa la matriz de centros de los clusters
		 */
/*
		node->centros = (float *)palloc(sizeof(float) * cantGrupos * dimension);
		if(node->centros==NULL){
			elog(ERROR, "palloc() failed: insufficient memory");
		}

		for (int i = 0; i < cantGrupos; i++){
			for (int j = 0; j < dimension; j++){
				node->centros[i * cantGrupos + j] = calcularCentro(i, j, totalTuplas, slot, node);
			}
		}
*/
		float centros[cantGrupos][dimension];
		for(int i = 0; i < cantGrupos; i++){
			float aux = 0;
			for(int j = 0; j < dimension; j++){
				centros[i][j] = calcularCentro2(i, j, totalTuplas, node, cantGrupos, pertenencia);
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
		float datos[dimension];	//arreglo para guardar temporalmente los datos de la tupla actual para calcular la pertenencia

		do{
			/**
			 * actualizar matriz de pertenencia
			 */
			for (int i = 0; i < totalTuplas; i++){

				slot = ExecProcNode(outerPlanState(node));
				slot->tts_tuple = ExecMaterializeSlot(slot);
				slot_getallattrs(slot);

				for (int  k = 0; k < dimension; k++){
					datos[k] = DatumGetFloat4(slot->tts_values[k]);
				}

				for (int j = 0; j < cantGrupos; j++){
					pertenencia[i][j] = calcularPertenencia2(j, dimension, datos, cantGrupos, node, centros);

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
				for (int j = 0; j < dimension; j++){
					aux = centros[i][j];
					centros[i][j] = calcularCentro2(i, j, totalTuplas, node, cantGrupos, pertenencia);

					diffaux += pow ( centros[i][j] - aux, 2);
				}
				diffaux = sqrt(diffaux);

				if ( diffaux > diff ){
					diff = diffaux;
				}
			}

		} while (fabs(diff) > node->error);


		slot = ExecProcNode(outerPlanState(node));
		slot->tts_tuple = ExecMaterializeSlot(slot);
		slot_getallattrs(slot);

		for(int z = 0; z < totalTuplas; z++){

			char outstring[cantGrupos][250];

			for (int i = dimension; i < nvalid; i++){
				sprintf(outstring[i - dimension], "c %.3f ", pertenencia[z][i - dimension]);
			}

			if (node->tupla_actual == 0){
				char str[10];
				for (int i = 0; i < cantGrupos; i++){
					strcat(outstring[i], "(");
					for (int j = 0; j < dimension; j++){

						sprintf(str, "%.3f", centros[i][j]);
						strcat(outstring[i], str);
						if(j != dimension-1)
							strcat(outstring[i], ", ");
					}
					strcat(outstring[i], ")");
				}
				node->tupla_actual++;
			}

			for (int i = dimension; i < nvalid; i++){
				slot->tts_values[i] = CStringGetDatum(outstring[i - dimension]);
				slot->tts_isnull[i] = FALSE;
			}
			//tuplestore_puttupleslot(node->tss, slot);
			tuplestore_putvalues(node->tss, slot->tts_tupleDescriptor, slot->tts_values, slot->tts_isnull);

			slot = ExecProcNode(outerPlanState(node));
			slot->tts_tuple = ExecMaterializeSlot(slot);
			slot_getallattrs(slot);
		}
		node->tupla_actual--;

	}

	tuplestore_gettupleslot(node->tss, true, true, slot);
	node->tupla_actual++;
	
	return slot;
}


/* -----------------------
 * ExecInitClustering
 * -----------------------
 */
ClusteringState *
ExecInitClustering(Clustering *node, EState *estate, int eflags)
{
	ClusteringState *fcstate;
	//elog(ERROR, "aqui");
	/*
	 * create state structure
	 */
	fcstate = makeNode(ClusteringState);
	fcstate->ss.ps.plan = (Plan *) node;
	fcstate->ss.ps.state = estate;
	fcstate->tupla_actual = 0; 	
	fcstate->total_tuplas = 0;
	fcstate->calcular = true;
	fcstate->pertenencia = NULL;
	fcstate->centros = NULL;
	fcstate->cant_grupos = node->cant_grupos;
	fcstate->fuzziness = (float) node->fuzziness/100;
	fcstate->error = (float) node->error/1000000;
	fcstate->tss = tuplestore_begin_heap(true, true, work_mem);

	tuplestore_set_eflags(fcstate->tss, eflags);

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
 * ExecEndClustering
 * ----------------------
 */
void
ExecEndClustering(ClusteringState *node)
{
	
    ExecFreeExprContext(&node->ss.ps);

    /* clean up tuple table */
	ExecClearTuple(node->ss.ss_ScanTupleSlot);

    ExecEndNode(outerPlanState(node));

}

