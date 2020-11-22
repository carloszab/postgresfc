CREATE OR REPLACE FUNCTION "doClustering"(varchar, varchar, int4, int4, float8, float8)
  RETURNS "pg_catalog"."void" AS $BODY$
	
	DECLARE
	
	origen alias for $1;
	destino alias for $2;
	atributos alias for $3;
	cantidad alias for $4;
	fuzziness alias for $5;
	error alias for $6;

	i int;

	
BEGIN
	-- Routine body goes here...

	i := 1;
	WHILE i <= cantidad LOOP
	
		EXECUTE 'ALTER TABLE '
		||	quote_ident(origen) 
		||	' ADD CLUSTER'
		||  i
		||	' float4';
	
	i := i + 1;
	END LOOP;
	
	i := 1;
	WHILE i <= atributos LOOP
	
		EXECUTE 'ALTER TABLE '
		||	quote_ident(origen) 
		||	' ADD ATRIBUTO'
		||  i
		||	' float4';
	
	i := i + 1;
	END LOOP;

	
	EXECUTE 'CREATE TABLE '
	||	quote_ident(destino)
	|| ' AS SELECT * from '
	||	quote_ident(origen)
	||	' CLUSTERING '
	||	quote_literal(cantidad)
	||	', '
	||	quote_literal(fuzziness)
	||	', '
	||	quote_literal(error)
	USING destino, origen, cantidad, fuzziness, error;
	
	
	i := 1;
	WHILE i <= cantidad LOOP
	
		EXECUTE 'ALTER TABLE '
		||	quote_ident(origen) 
		||	' DROP COLUMN CLUSTER'
		||  i;
	
	i := i + 1;
	END LOOP;
	
	i := 1;
	WHILE i <= atributos LOOP
	
		EXECUTE 'ALTER TABLE '
		||	quote_ident(origen) 
		||	' DROP COLUMN ATRIBUTO'
		||  i;
	
	i := i + 1;
	END LOOP;
	
	
END
$BODY$
  LANGUAGE plpgsql VOLATILE
  COST 100