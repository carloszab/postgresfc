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
#include <math.h>

#include "miscadmin.h" // necesario para usar work_mem


float calcularPertenencia (int j, int dimension, float datos[dimension], int cantGrupos, ClusteringState *node, float centros[cantGrupos][dimension]){
	double sum = 0;
	double distance = 0;
	double distanceaux = 0;

	/*
	Para calcular la pertenencia del dato j al centro i (las letras pueden diferir)

	primero se calcula la distancia del dato j al centro i.
	nota: si los datos (y en consecuencia los centros) tienen más de un atributo (dimensión), 
	el proceso se repite por cada uno de estos y al final se suman todas las distancias, 
	para luego aplicar la raíz cuadrada y obtener la distancia real.
	*/

	for (int i = 0; i < dimension; i++){
		distance += pow ( datos[i] - centros[j][i], 2);
	}

	distance = sqrt(distance);

	/*
	luego: 
	*/

	for (int k = 0; k < cantGrupos; k++){
		distanceaux = 0;
		
		/*
		se calcula la distancia (distanceaux) del dato j al cada uno de los centros
		*/

		for (int i = 0; i < dimension; i++){
			distanceaux += pow ( datos[i] - centros[k][i], 2);
		}
		distanceaux = sqrt(distanceaux);
		
		/*
		cada vez que se calcula la distancia del dato a un centro se realiza la división
		distance/distanceaux y luego se eleva a 2/(m-1)

		este proceso se realiza con cada uno de los centros y el resultado se va acumulando
		*/

		sum += pow( distance / distanceaux , 2/(node->fuzziness-1) );
	}

	/*
	una vez que esté listo el acumulado (sum), se divide 1 / sum, siempre y cuando la suma
	sea mayor a cero, debido a que:
	1) la división entre cero está indeterminada
	2) no se aceptan pertenencias negativas, el rango va de 0 a 1, 
	si la suma es igual o menor a cero se retorna 1, lo que representa que 
	la pertenencia del dato j al centro i es total.
	*/

	if (sum > 0){
		return (1 / sum);
	}
	else{
		return 1;
	}

}


float calcularCentro (int i, int j, int totalTuplas, ClusteringState *node, int cantGrupos, float pertenencia[totalTuplas][cantGrupos]){
	
	double sum1 = 0;
	double sum2 = 0;
	double potencia = 0;

	TupleTableSlot *slot;
	slot = ExecProcNode(outerPlanState(node));

	for(int aux = 0; !slot->tts_isempty; aux++){

		if (slot == NULL)
			elog(ERROR, "error al obtener tupla");

		slot->tts_tuple = ExecMaterializeSlot(slot);
		slot_getallattrs(slot);

		/*
		para calcular el centro de un grupo se sigue el siguiente procedimiento

		primero se realizan dos sumas por separado:
		*en la primera la pertenencia de un dato al grupo se eleva a la m (valor del difusor),
		y el resultado se multiplica con ese dato.
		*en la segunda suma solamente se calcula la potencia del dato a la m

		este procedimiento se realiza para cada dato y el resultado de cada operación se 
		va acumulando en su respectiva suma.

		*/
		potencia = pow ( pertenencia[aux][i] , node->fuzziness);

		sum1 += potencia * DatumGetFloat4(slot->tts_values[j]);
		sum2 += potencia;

		slot = ExecProcNode(outerPlanState(node));
	}

	/*
	por último, se realiza una división entre las dos sumas, obteniendo como resultado 
	el nuevo centro del grupo

	*/

	return (sum1/sum2);
}


/* -------------------
 * ExecClustering
 * -------------------
 */

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
	int columnas = slot->tts_nvalid;
	int dimension = (columnas - cantGrupos)/2;
	int nvalid = dimension + cantGrupos;


	if(node->calcular == true){

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

		float centros[cantGrupos][dimension];
		for(int i = 0; i < cantGrupos; i++){
			for(int j = 0; j < dimension; j++){
				centros[i][j] = calcularCentro(i, j, totalTuplas, node, cantGrupos, pertenencia);
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
					pertenencia[i][j] = calcularPertenencia(j, dimension, datos, cantGrupos, node, centros);
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
					centros[i][j] = calcularCentro(i, j, totalTuplas, node, cantGrupos, pertenencia);

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

			for (int i = dimension; i < nvalid; i++){
				slot->tts_values[i] = Float4GetDatum(pertenencia[z][i - dimension]);
				slot->tts_isnull[i] = FALSE;
			}
			if (z < cantGrupos){
				for (int i = nvalid; i < columnas; i++){
					slot->tts_values[i] = Float4GetDatum(centros[z][i - nvalid]);
					slot->tts_isnull[i] = FALSE;
				}
			}
			tuplestore_putvalues(node->tss, slot->tts_tupleDescriptor, slot->tts_values, slot->tts_isnull);

			slot = ExecProcNode(outerPlanState(node));
			slot->tts_tuple = ExecMaterializeSlot(slot);
			slot_getallattrs(slot);
		}

	}

	tuplestore_gettupleslot(node->tss, true, true, slot);
	node->calcular = false;

	
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
	/*
	 * create state structure
	 */
	fcstate = makeNode(ClusteringState);
	fcstate->ps.plan = (Plan *) node;
	fcstate->ps.state = estate;
	
	fcstate->total_tuplas = 0;
	fcstate->calcular = true;
	fcstate->pertenencia = NULL;
	fcstate->centros = NULL;

	fcstate->cant_grupos = node->cant_grupos;
	fcstate->fuzziness = node->fuzziness/100;
	fcstate->error = node->error/1000000;

	fcstate->tss = tuplestore_begin_heap(true, true, work_mem);

	tuplestore_set_eflags(fcstate->tss, eflags);

    /*
	 * create expression context
	 */
	ExecAssignExprContext(estate, &fcstate->ps);

    /*
	 * tuple table initialization
	 */
	ExecInitResultTupleSlot(estate, &fcstate->ps);

    /*
	 * initialize child expressions
	 */

	fcstate->ps.targetlist = (List *)
		ExecInitExpr((Expr *) node->plan.targetlist,
					 (PlanState *) fcstate);
	fcstate->ps.qual = (List *)
		ExecInitExpr((Expr *) node->plan.qual,
					 (PlanState *) fcstate);

    /*
	 * initialize child nodes
	 */
	outerPlanState(fcstate) = ExecInitNode(outerPlan(node), estate, eflags);

    /*
	 * Initialize result tuple type and projection info.
	 */
	ExecAssignResultTypeFromTL(&fcstate->ps);
	ExecAssignProjectionInfo(&fcstate->ps, NULL);

    fcstate->ps.ps_TupFromTlist = false;
    

    return fcstate;
}

/* ----------------------
 * ExecEndClustering
 * ----------------------
 */
void
ExecEndClustering(ClusteringState *node)
{
	
    ExecFreeExprContext(&node->ps);
	
    ExecEndNode(outerPlanState(node));

}

