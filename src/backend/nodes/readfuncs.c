/*-------------------------------------------------------------------------
 *
 * readfuncs.c
 *	  Reader functions for Postgres tree nodes.
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/nodes/readfuncs.c
 *
 * NOTES
 *	  Parse location fields are written out by outfuncs.c, but only for
 *	  debugging use.  When reading a location field, we normally discard
 *	  the stored value and set the location field to -1 (ie, "unknown").
 *	  This is because nodes coming from a stored rule should not be thought
 *	  to have a known location in the current query's text.
 *
 *	  However, if restore_location_fields is true, we do restore location
 *	  fields from the string.  This is currently intended only for use by the
 *	  debug_write_read_parse_plan_trees test code, which doesn't want to cause
 *	  any change in the node contents.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "miscadmin.h"
#include "nodes/bitmapset.h"
#include "nodes/readfuncs.h"

/*
 * Macros to simplify reading of different kinds of fields.  Use these
 * wherever possible to reduce the chance for silly typos.  Note that these
 * hard-wire conventions about the names of the local variables in a Read
 * routine.
 */

/* Macros for declaring appropriate local variables */

/* A few guys need only local_node */
#define READ_LOCALS_NO_FIELDS(nodeTypeName) \
	nodeTypeName *local_node = makeNode(nodeTypeName)

/* And a few guys need only the pg_strtok support fields */
#define READ_TEMP_LOCALS()	\
	const char *token;		\
	int			length

/* ... but most need both */
#define READ_LOCALS(nodeTypeName)			\
	READ_LOCALS_NO_FIELDS(nodeTypeName);	\
	READ_TEMP_LOCALS()

/* Read an integer field (anything written as ":fldname %d") */
#define READ_INT_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = atoi(token)

/* Read an unsigned integer field (anything written as ":fldname %u") */
#define READ_UINT_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = atoui(token)

/* Read a signed integer field (anything written using INT64_FORMAT) */
#define READ_INT64_FIELD(fldname) \
	token = pg_strtok(&length); /* skip :fldname */ \
	token = pg_strtok(&length); /* get field value */ \
	local_node->fldname = strtoi64(token, NULL, 10)

/* Read an unsigned integer field (anything written using UINT64_FORMAT) */
#define READ_UINT64_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = strtou64(token, NULL, 10)

/* Read a long integer field (anything written as ":fldname %ld") */
#define READ_LONG_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = atol(token)

/* Read an OID field (don't hard-wire assumption that OID is same as uint) */
#define READ_OID_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = atooid(token)

/* Read a char field (ie, one ascii character) */
#define READ_CHAR_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	/* avoid overhead of calling debackslash() for one char */ \
	local_node->fldname = (length == 0) ? '\0' : (token[0] == '\\' ? token[1] : token[0])

/* Read an enumerated-type field that was written as an integer code */
#define READ_ENUM_FIELD(fldname, enumtype) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = (enumtype) atoi(token)

/* Read a float field */
#define READ_FLOAT_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = atof(token)

/* Read a boolean field */
#define READ_BOOL_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = strtobool(token)

/* Read a character-string field */
#define READ_STRING_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = nullable_string(token, length)

/* Read a parse location field (and possibly throw away the value) */
#ifdef DEBUG_NODE_TESTS_ENABLED
#define READ_LOCATION_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	local_node->fldname = restore_location_fields ? atoi(token) : -1
#else
#define READ_LOCATION_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	token = pg_strtok(&length);		/* get field value */ \
	(void) token;				/* in case not used elsewhere */ \
	local_node->fldname = -1	/* set field to "unknown" */
#endif

/* Read a Node field */
#define READ_NODE_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	(void) token;				/* in case not used elsewhere */ \
	local_node->fldname = nodeRead(NULL, 0)

/* Read a bitmapset field */
#define READ_BITMAPSET_FIELD(fldname) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	(void) token;				/* in case not used elsewhere */ \
	local_node->fldname = _readBitmapset()

/* Read an attribute number array */
#define READ_ATTRNUMBER_ARRAY(fldname, len) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	local_node->fldname = readAttrNumberCols(len)

/* Read an oid array */
#define READ_OID_ARRAY(fldname, len) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	local_node->fldname = readOidCols(len)

/* Read an int array */
#define READ_INT_ARRAY(fldname, len) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	local_node->fldname = readIntCols(len)

/* Read a bool array */
#define READ_BOOL_ARRAY(fldname, len) \
	token = pg_strtok(&length);		/* skip :fldname */ \
	local_node->fldname = readBoolCols(len)

/* Routine exit */
#define READ_DONE() \
	return local_node


/*
 * NOTE: use atoi() to read values written with %d, or atoui() to read
 * values written with %u in outfuncs.c.  An exception is OID values,
 * for which use atooid().  (As of 7.1, outfuncs.c writes OIDs as %u,
 * but this will probably change in the future.)
 */
#define atoui(x)  ((unsigned int) strtoul((x), NULL, 10))

#define strtobool(x)  ((*(x) == 't') ? true : false)

static char *
nullable_string(const char *token, int length)
{
	/* outToken emits <> for NULL, and pg_strtok makes that an empty string */
	if (length == 0)
		return NULL;
	/* outToken emits "" for empty string */
	if (length == 2 && token[0] == '"' && token[1] == '"')
		return pstrdup("");
	/* otherwise, we must remove protective backslashes added by outToken */
	return debackslash(token, length);
}


/*
 * _readBitmapset
 *
 * Note: this code is used in contexts where we know that a Bitmapset
 * is expected.  There is equivalent code in nodeRead() that can read a
 * Bitmapset when we come across one in other contexts.
 */
static Bitmapset *
_readBitmapset(void)
{
	Bitmapset  *result = NULL;

	READ_TEMP_LOCALS();

	token = pg_strtok(&length);
	if (token == NULL)
		elog(ERROR, "incomplete Bitmapset structure");
	if (length != 1 || token[0] != '(')
		elog(ERROR, "unrecognized token: \"%.*s\"", length, token);

	token = pg_strtok(&length);
	if (token == NULL)
		elog(ERROR, "incomplete Bitmapset structure");
	if (length != 1 || token[0] != 'b')
		elog(ERROR, "unrecognized token: \"%.*s\"", length, token);

	for (;;)
	{
		int			val;
		char	   *endptr;

		token = pg_strtok(&length);
		if (token == NULL)
			elog(ERROR, "unterminated Bitmapset structure");
		if (length == 1 && token[0] == ')')
			break;
		val = (int) strtol(token, &endptr, 10);
		if (endptr != token + length)
			elog(ERROR, "unrecognized integer: \"%.*s\"", length, token);
		result = bms_add_member(result, val);
	}

	return result;
}

/*
 * We export this function for use by extensions that define extensible nodes.
 * That's somewhat historical, though, because calling nodeRead() will work.
 */
Bitmapset *
readBitmapset(void)
{
	return _readBitmapset();
}

#include "readfuncs.funcs.c"


/*
 * Support functions for nodes with custom_read_write attribute or
 * special_read_write attribute
 */

static Const *
_readConst(void)
{
	READ_LOCALS(Const);

	READ_OID_FIELD(consttype);
	READ_INT_FIELD(consttypmod);
	READ_OID_FIELD(constcollid);
	READ_INT_FIELD(constlen);
	READ_BOOL_FIELD(constbyval);
	READ_BOOL_FIELD(constisnull);
	READ_LOCATION_FIELD(location);

	token = pg_strtok(&length); /* skip :constvalue */
	if (local_node->constisnull)
		token = pg_strtok(&length); /* skip "<>" */
	else
		local_node->constvalue = readDatum(local_node->constbyval);

	READ_DONE();
}

<<<<<<< HEAD
=======
/*
 * _readParam
 */
static Param *
_readParam(void)
{
	READ_LOCALS(Param);

	READ_ENUM_FIELD(paramkind, ParamKind);
	READ_INT_FIELD(paramid);
	READ_OID_FIELD(paramtype);
	READ_INT_FIELD(paramtypmod);
	READ_OID_FIELD(paramcollid);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readAggref
 */
static Aggref *
_readAggref(void)
{
	READ_LOCALS(Aggref);

	READ_OID_FIELD(aggfnoid);
	READ_OID_FIELD(aggtype);
	READ_OID_FIELD(aggcollid);
	READ_OID_FIELD(inputcollid);
	READ_OID_FIELD(aggtranstype);
	READ_NODE_FIELD(aggargtypes);
	READ_NODE_FIELD(aggdirectargs);
	READ_NODE_FIELD(args);
	READ_NODE_FIELD(aggorder);
	READ_NODE_FIELD(aggdistinct);
	READ_NODE_FIELD(aggfilter);
	READ_BOOL_FIELD(aggstar);
	READ_BOOL_FIELD(aggvariadic);
	READ_CHAR_FIELD(aggkind);
	READ_UINT_FIELD(agglevelsup);
	READ_ENUM_FIELD(aggsplit, AggSplit);
	READ_INT_FIELD(aggno);
	READ_INT_FIELD(aggtransno);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readGroupingFunc
 */
static GroupingFunc *
_readGroupingFunc(void)
{
	READ_LOCALS(GroupingFunc);

	READ_NODE_FIELD(args);
	READ_NODE_FIELD(refs);
	READ_NODE_FIELD(cols);
	READ_UINT_FIELD(agglevelsup);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readWindowFunc
 */
static WindowFunc *
_readWindowFunc(void)
{
	READ_LOCALS(WindowFunc);

	READ_OID_FIELD(winfnoid);
	READ_OID_FIELD(wintype);
	READ_OID_FIELD(wincollid);
	READ_OID_FIELD(inputcollid);
	READ_NODE_FIELD(args);
	READ_NODE_FIELD(aggfilter);
	READ_UINT_FIELD(winref);
	READ_BOOL_FIELD(winstar);
	READ_BOOL_FIELD(winagg);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readSubscriptingRef
 */
static SubscriptingRef *
_readSubscriptingRef(void)
{
	READ_LOCALS(SubscriptingRef);

	READ_OID_FIELD(refcontainertype);
	READ_OID_FIELD(refelemtype);
	READ_OID_FIELD(refrestype);
	READ_INT_FIELD(reftypmod);
	READ_OID_FIELD(refcollid);
	READ_NODE_FIELD(refupperindexpr);
	READ_NODE_FIELD(reflowerindexpr);
	READ_NODE_FIELD(refexpr);
	READ_NODE_FIELD(refassgnexpr);

	READ_DONE();
}

/*
 * _readFuncExpr
 */
static FuncExpr *
_readFuncExpr(void)
{
	READ_LOCALS(FuncExpr);

	READ_OID_FIELD(funcid);
	READ_OID_FIELD(funcresulttype);
	READ_BOOL_FIELD(funcretset);
	READ_BOOL_FIELD(funcvariadic);
	READ_ENUM_FIELD(funcformat, CoercionForm);
	READ_OID_FIELD(funccollid);
	READ_OID_FIELD(inputcollid);
	READ_NODE_FIELD(args);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readNamedArgExpr
 */
static NamedArgExpr *
_readNamedArgExpr(void)
{
	READ_LOCALS(NamedArgExpr);

	READ_NODE_FIELD(arg);
	READ_STRING_FIELD(name);
	READ_INT_FIELD(argnumber);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readOpExpr
 */
static OpExpr *
_readOpExpr(void)
{
	READ_LOCALS(OpExpr);

	READ_OID_FIELD(opno);
	READ_OID_FIELD(opfuncid);
	READ_OID_FIELD(opresulttype);
	READ_BOOL_FIELD(opretset);
	READ_OID_FIELD(opcollid);
	READ_OID_FIELD(inputcollid);
	READ_NODE_FIELD(args);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readDistinctExpr
 */
static DistinctExpr *
_readDistinctExpr(void)
{
	READ_LOCALS(DistinctExpr);

	READ_OID_FIELD(opno);
	READ_OID_FIELD(opfuncid);
	READ_OID_FIELD(opresulttype);
	READ_BOOL_FIELD(opretset);
	READ_OID_FIELD(opcollid);
	READ_OID_FIELD(inputcollid);
	READ_NODE_FIELD(args);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readNullIfExpr
 */
static NullIfExpr *
_readNullIfExpr(void)
{
	READ_LOCALS(NullIfExpr);

	READ_OID_FIELD(opno);
	READ_OID_FIELD(opfuncid);
	READ_OID_FIELD(opresulttype);
	READ_BOOL_FIELD(opretset);
	READ_OID_FIELD(opcollid);
	READ_OID_FIELD(inputcollid);
	READ_NODE_FIELD(args);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readScalarArrayOpExpr
 */
static ScalarArrayOpExpr *
_readScalarArrayOpExpr()
{
	READ_LOCALS(ScalarArrayOpExpr);

	READ_OID_FIELD(opno);
	READ_OID_FIELD(opfuncid);
	if (GetYbExpressionVersion() != 11)
	{
		READ_OID_FIELD(hashfuncid);
		READ_OID_FIELD(negfuncid);
	}
	READ_BOOL_FIELD(useOr);
	READ_OID_FIELD(inputcollid);
	READ_NODE_FIELD(args);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

/*
 * _readBoolExpr
 */
>>>>>>> bc662ba7050
static BoolExpr *
_readBoolExpr(void)
{
	READ_LOCALS(BoolExpr);

	/* do-it-yourself enum representation */
	token = pg_strtok(&length); /* skip :boolop */
	token = pg_strtok(&length); /* get field value */
	if (length == 3 && strncmp(token, "and", 3) == 0)
		local_node->boolop = AND_EXPR;
	else if (length == 2 && strncmp(token, "or", 2) == 0)
		local_node->boolop = OR_EXPR;
	else if (length == 3 && strncmp(token, "not", 3) == 0)
		local_node->boolop = NOT_EXPR;
	else
		elog(ERROR, "unrecognized boolop \"%.*s\"", length, token);

	READ_NODE_FIELD(args);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

static A_Const *
_readA_Const(void)
{
	READ_LOCALS(A_Const);

	/* We expect either NULL or :val here */
	token = pg_strtok(&length);
	if (length == 4 && strncmp(token, "NULL", 4) == 0)
		local_node->isnull = true;
	else
	{
		union ValUnion *tmp = nodeRead(NULL, 0);

		/* To forestall valgrind complaints, copy only the valid data */
		switch (nodeTag(tmp))
		{
			case T_Integer:
				memcpy(&local_node->val, tmp, sizeof(Integer));
				break;
			case T_Float:
				memcpy(&local_node->val, tmp, sizeof(Float));
				break;
			case T_Boolean:
				memcpy(&local_node->val, tmp, sizeof(Boolean));
				break;
			case T_String:
				memcpy(&local_node->val, tmp, sizeof(String));
				break;
			case T_BitString:
				memcpy(&local_node->val, tmp, sizeof(BitString));
				break;
			default:
				elog(ERROR, "unrecognized node type: %d",
					 (int) nodeTag(tmp));
				break;
		}
	}

	READ_LOCATION_FIELD(location);

	READ_DONE();
}

static RangeTblEntry *
_readRangeTblEntry(void)
{
	READ_LOCALS(RangeTblEntry);

	READ_NODE_FIELD(alias);
	READ_NODE_FIELD(eref);
	READ_ENUM_FIELD(rtekind, RTEKind);

	switch (local_node->rtekind)
	{
		case RTE_RELATION:
			READ_OID_FIELD(relid);
			READ_BOOL_FIELD(inh);
			READ_CHAR_FIELD(relkind);
			READ_INT_FIELD(rellockmode);
			READ_UINT_FIELD(perminfoindex);
			READ_NODE_FIELD(tablesample);
			break;
		case RTE_SUBQUERY:
			READ_NODE_FIELD(subquery);
			READ_BOOL_FIELD(security_barrier);
			/* we re-use these RELATION fields, too: */
			READ_OID_FIELD(relid);
			READ_BOOL_FIELD(inh);
			READ_CHAR_FIELD(relkind);
			READ_INT_FIELD(rellockmode);
			READ_UINT_FIELD(perminfoindex);
			break;
		case RTE_JOIN:
			READ_ENUM_FIELD(jointype, JoinType);
			READ_INT_FIELD(joinmergedcols);
			READ_NODE_FIELD(joinaliasvars);
			READ_NODE_FIELD(joinleftcols);
			READ_NODE_FIELD(joinrightcols);
			READ_NODE_FIELD(join_using_alias);
			break;
		case RTE_FUNCTION:
			READ_NODE_FIELD(functions);
			READ_BOOL_FIELD(funcordinality);
			break;
		case RTE_TABLEFUNC:
			READ_NODE_FIELD(tablefunc);
			/* The RTE must have a copy of the column type info, if any */
			if (local_node->tablefunc)
			{
				TableFunc  *tf = local_node->tablefunc;

				local_node->coltypes = tf->coltypes;
				local_node->coltypmods = tf->coltypmods;
				local_node->colcollations = tf->colcollations;
			}
			break;
		case RTE_VALUES:
			READ_NODE_FIELD(values_lists);
			READ_NODE_FIELD(coltypes);
			READ_NODE_FIELD(coltypmods);
			READ_NODE_FIELD(colcollations);
			break;
		case RTE_CTE:
			READ_STRING_FIELD(ctename);
			READ_UINT_FIELD(ctelevelsup);
			READ_BOOL_FIELD(self_reference);
			READ_NODE_FIELD(coltypes);
			READ_NODE_FIELD(coltypmods);
			READ_NODE_FIELD(colcollations);
			break;
		case RTE_NAMEDTUPLESTORE:
			READ_STRING_FIELD(enrname);
			READ_FLOAT_FIELD(enrtuples);
			READ_NODE_FIELD(coltypes);
			READ_NODE_FIELD(coltypmods);
			READ_NODE_FIELD(colcollations);
			/* we re-use these RELATION fields, too: */
			READ_OID_FIELD(relid);
			break;
		case RTE_GRAPH_TABLE:
			READ_NODE_FIELD(graph_pattern);
			READ_NODE_FIELD(graph_table_columns);
			/* we re-use these RELATION fields, too: */
			READ_OID_FIELD(relid);
			READ_CHAR_FIELD(relkind);
			READ_INT_FIELD(rellockmode);
			READ_UINT_FIELD(perminfoindex);
			break;
		case RTE_RESULT:
			/* no extra fields */
			break;
		case RTE_GROUP:
			READ_NODE_FIELD(groupexprs);
			break;
		default:
			elog(ERROR, "unrecognized RTE kind: %d",
				 (int) local_node->rtekind);
			break;
	}

	READ_BOOL_FIELD(lateral);
	READ_BOOL_FIELD(inFromCl);
	READ_NODE_FIELD(securityQuals);

	READ_DONE();
}

static A_Expr *
_readA_Expr(void)
{
	READ_LOCALS(A_Expr);

	token = pg_strtok(&length);

	if (length == 3 && strncmp(token, "ANY", 3) == 0)
	{
		local_node->kind = AEXPR_OP_ANY;
		READ_NODE_FIELD(name);
	}
	else if (length == 3 && strncmp(token, "ALL", 3) == 0)
	{
		local_node->kind = AEXPR_OP_ALL;
		READ_NODE_FIELD(name);
	}
	else if (length == 8 && strncmp(token, "DISTINCT", 8) == 0)
	{
		local_node->kind = AEXPR_DISTINCT;
		READ_NODE_FIELD(name);
	}
	else if (length == 12 && strncmp(token, "NOT_DISTINCT", 12) == 0)
	{
		local_node->kind = AEXPR_NOT_DISTINCT;
		READ_NODE_FIELD(name);
	}
	else if (length == 6 && strncmp(token, "NULLIF", 6) == 0)
	{
		local_node->kind = AEXPR_NULLIF;
		READ_NODE_FIELD(name);
	}
	else if (length == 2 && strncmp(token, "IN", 2) == 0)
	{
		local_node->kind = AEXPR_IN;
		READ_NODE_FIELD(name);
	}
	else if (length == 4 && strncmp(token, "LIKE", 4) == 0)
	{
		local_node->kind = AEXPR_LIKE;
		READ_NODE_FIELD(name);
	}
	else if (length == 5 && strncmp(token, "ILIKE", 5) == 0)
	{
		local_node->kind = AEXPR_ILIKE;
		READ_NODE_FIELD(name);
	}
	else if (length == 7 && strncmp(token, "SIMILAR", 7) == 0)
	{
		local_node->kind = AEXPR_SIMILAR;
		READ_NODE_FIELD(name);
	}
	else if (length == 7 && strncmp(token, "BETWEEN", 7) == 0)
	{
		local_node->kind = AEXPR_BETWEEN;
		READ_NODE_FIELD(name);
	}
	else if (length == 11 && strncmp(token, "NOT_BETWEEN", 11) == 0)
	{
		local_node->kind = AEXPR_NOT_BETWEEN;
		READ_NODE_FIELD(name);
	}
	else if (length == 11 && strncmp(token, "BETWEEN_SYM", 11) == 0)
	{
		local_node->kind = AEXPR_BETWEEN_SYM;
		READ_NODE_FIELD(name);
	}
	else if (length == 15 && strncmp(token, "NOT_BETWEEN_SYM", 15) == 0)
	{
		local_node->kind = AEXPR_NOT_BETWEEN_SYM;
		READ_NODE_FIELD(name);
	}
	else if (length == 5 && strncmp(token, ":name", 5) == 0)
	{
		local_node->kind = AEXPR_OP;
		local_node->name = nodeRead(NULL, 0);
	}
	else
		elog(ERROR, "unrecognized A_Expr kind: \"%.*s\"", length, token);

	READ_NODE_FIELD(lexpr);
	READ_NODE_FIELD(rexpr);
	READ_LOCATION_FIELD(rexpr_list_start);
	READ_LOCATION_FIELD(rexpr_list_end);
	READ_LOCATION_FIELD(location);

	READ_DONE();
}

<<<<<<< HEAD
=======
/*
 *	Stuff from plannodes.h.
 */

/*
 * _readPlannedStmt
 */
static PlannedStmt *
_readPlannedStmt(void)
{
	READ_LOCALS(PlannedStmt);

	READ_ENUM_FIELD(commandType, CmdType);
	READ_UINT64_FIELD(queryId);
	READ_BOOL_FIELD(hasReturning);
	READ_BOOL_FIELD(hasModifyingCTE);
	READ_BOOL_FIELD(canSetTag);
	READ_BOOL_FIELD(transientPlan);
	READ_BOOL_FIELD(dependsOnRole);
	READ_BOOL_FIELD(parallelModeNeeded);
	READ_INT_FIELD(jitFlags);
	READ_NODE_FIELD(planTree);
	READ_NODE_FIELD(rtable);
	READ_NODE_FIELD(resultRelations);
	READ_NODE_FIELD(appendRelations);
	READ_NODE_FIELD(subplans);
	READ_BITMAPSET_FIELD(rewindPlanIDs);
	READ_NODE_FIELD(rowMarks);
	READ_NODE_FIELD(relationOids);
	READ_NODE_FIELD(invalItems);
	READ_NODE_FIELD(paramExecTypes);
	READ_NODE_FIELD(utilityStmt);
	READ_LOCATION_FIELD(stmt_location);
	READ_INT_FIELD(yb_num_referenced_relations);
	READ_INT_FIELD(stmt_len);
	READ_UINT64_FIELD(ybPlanId);

	READ_DONE();
}

/*
 * ReadCommonPlan
 *	Assign the basic stuff of all nodes that inherit from Plan
 */
static void
ReadCommonPlan(Plan *local_node)
{
	READ_TEMP_LOCALS();

	READ_FLOAT_FIELD(startup_cost);
	READ_FLOAT_FIELD(total_cost);
	READ_FLOAT_FIELD(plan_rows);
	READ_INT_FIELD(plan_width);
	READ_BOOL_FIELD(parallel_aware);
	READ_BOOL_FIELD(parallel_safe);
	READ_BOOL_FIELD(async_capable);
	READ_INT_FIELD(plan_node_id);
	READ_NODE_FIELD(targetlist);
	READ_NODE_FIELD(qual);
	READ_NODE_FIELD(lefttree);
	READ_NODE_FIELD(righttree);
	READ_NODE_FIELD(initPlan);
	READ_BITMAPSET_FIELD(extParam);
	READ_BITMAPSET_FIELD(allParam);
	READ_STRING_FIELD(ybHintAlias);
	READ_UINT_FIELD(ybUniqueId);
	READ_STRING_FIELD(ybInheritedHintAlias);
	READ_BOOL_FIELD(ybIsHinted);
	READ_BOOL_FIELD(ybHasHintedUid);
}

/*
 * _readPlan
 */
static Plan *
_readPlan(void)
{
	READ_LOCALS_NO_FIELDS(Plan);

	ReadCommonPlan(local_node);

	READ_DONE();
}

/*
 * _readResult
 */
static Result *
_readResult(void)
{
	READ_LOCALS(Result);

	ReadCommonPlan(&local_node->plan);

	READ_NODE_FIELD(resconstantqual);

	READ_DONE();
}

/*
 * _readProjectSet
 */
static ProjectSet *
_readProjectSet(void)
{
	READ_LOCALS_NO_FIELDS(ProjectSet);

	ReadCommonPlan(&local_node->plan);

	READ_DONE();
}

/*
 * _readModifyTable
 */
static ModifyTable *
_readModifyTable(void)
{
	READ_LOCALS(ModifyTable);

	ReadCommonPlan(&local_node->plan);

	READ_ENUM_FIELD(operation, CmdType);
	READ_BOOL_FIELD(canSetTag);
	READ_UINT_FIELD(nominalRelation);
	READ_UINT_FIELD(rootRelation);
	READ_BOOL_FIELD(partColsUpdated);
	READ_NODE_FIELD(resultRelations);
	READ_NODE_FIELD(updateColnosLists);
	READ_NODE_FIELD(withCheckOptionLists);
	READ_NODE_FIELD(returningLists);
	READ_NODE_FIELD(fdwPrivLists);
	READ_BITMAPSET_FIELD(fdwDirectModifyPlans);
	READ_NODE_FIELD(rowMarks);
	READ_INT_FIELD(epqParam);
	READ_ENUM_FIELD(onConflictAction, OnConflictAction);
	READ_NODE_FIELD(arbiterIndexes);
	READ_NODE_FIELD(onConflictSet);
	READ_NODE_FIELD(onConflictCols);
	READ_NODE_FIELD(onConflictWhere);
	READ_UINT_FIELD(exclRelRTI);
	READ_NODE_FIELD(exclRelTlist);
	READ_NODE_FIELD(mergeActionLists);

	READ_NODE_FIELD(ybPushdownTlist);
	READ_NODE_FIELD(ybReturningColumns);
	READ_NODE_FIELD(ybColumnRefs);
	READ_NODE_FIELD(yb_skip_entities);
	READ_NODE_FIELD(yb_update_affected_entities);
	READ_BOOL_FIELD(no_row_trigger);

	READ_DONE();
}

/*
 * _readAppend
 */
static Append *
_readAppend(void)
{
	READ_LOCALS(Append);

	ReadCommonPlan(&local_node->plan);

	READ_BITMAPSET_FIELD(apprelids);
	READ_NODE_FIELD(appendplans);
	READ_INT_FIELD(nasyncplans);
	READ_INT_FIELD(first_partial_plan);
	READ_NODE_FIELD(part_prune_info);

	READ_DONE();
}

/*
 * _readMergeAppend
 */
static MergeAppend *
_readMergeAppend(void)
{
	READ_LOCALS(MergeAppend);

	ReadCommonPlan(&local_node->plan);

	READ_BITMAPSET_FIELD(apprelids);
	READ_NODE_FIELD(mergeplans);
	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(sortColIdx, local_node->numCols);
	READ_OID_ARRAY(sortOperators, local_node->numCols);
	READ_OID_ARRAY(collations, local_node->numCols);
	READ_BOOL_ARRAY(nullsFirst, local_node->numCols);
	READ_NODE_FIELD(part_prune_info);

	READ_DONE();
}

/*
 * _readRecursiveUnion
 */
static RecursiveUnion *
_readRecursiveUnion(void)
{
	READ_LOCALS(RecursiveUnion);

	ReadCommonPlan(&local_node->plan);

	READ_INT_FIELD(wtParam);
	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(dupColIdx, local_node->numCols);
	READ_OID_ARRAY(dupOperators, local_node->numCols);
	READ_OID_ARRAY(dupCollations, local_node->numCols);
	READ_LONG_FIELD(numGroups);

	READ_DONE();
}

/*
 * _readBitmapAnd
 */
static BitmapAnd *
_readBitmapAnd(void)
{
	READ_LOCALS(BitmapAnd);

	ReadCommonPlan(&local_node->plan);

	READ_NODE_FIELD(bitmapplans);

	READ_DONE();
}

/*
 * _readBitmapOr
 */
static BitmapOr *
_readBitmapOr(void)
{
	READ_LOCALS(BitmapOr);

	ReadCommonPlan(&local_node->plan);

	READ_BOOL_FIELD(isshared);
	READ_NODE_FIELD(bitmapplans);

	READ_DONE();
}

/*
 * ReadCommonScan
 *	Assign the basic stuff of all nodes that inherit from Scan
 */
static void
ReadCommonScan(Scan *local_node)
{
	READ_TEMP_LOCALS();

	ReadCommonPlan(&local_node->plan);

	READ_UINT_FIELD(scanrelid);

	/* YB */
	READ_STRING_FIELD(ybScannedObjectName);
}

/*
 * _readScan
 */
static Scan *
_readScan(void)
{
	READ_LOCALS_NO_FIELDS(Scan);

	ReadCommonScan(local_node);

	READ_DONE();
}

/*
 * _readSeqScan
 */
static SeqScan *
_readSeqScan(void)
{
	READ_LOCALS_NO_FIELDS(SeqScan);

	ReadCommonScan(&local_node->scan);

	READ_DONE();
}

/*
 * _readYbSeqScan
 */
static YbSeqScan *
_readYbSeqScan(void)
{
	READ_LOCALS(YbSeqScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(yb_pushdown.quals);
	READ_NODE_FIELD(yb_pushdown.colrefs);

	READ_DONE();
}

/*
 * _readSampleScan
 */
static SampleScan *
_readSampleScan(void)
{
	READ_LOCALS(SampleScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(tablesample);

	READ_DONE();
}

/*
 * _readIndexScan
 */
static IndexScan *
_readIndexScan(void)
{
	READ_LOCALS(IndexScan);

	ReadCommonScan(&local_node->scan);

	READ_OID_FIELD(indexid);
	READ_NODE_FIELD(indexqual);
	READ_NODE_FIELD(indexqualorig);
	READ_NODE_FIELD(indexorderby);
	READ_NODE_FIELD(indexorderbyorig);
	READ_NODE_FIELD(indexorderbyops);
	READ_NODE_FIELD(indextlist);
	READ_ENUM_FIELD(indexorderdir, ScanDirection);
	READ_NODE_FIELD(yb_idx_pushdown.quals);
	READ_NODE_FIELD(yb_idx_pushdown.colrefs);
	READ_NODE_FIELD(yb_rel_pushdown.quals);
	READ_NODE_FIELD(yb_rel_pushdown.colrefs);
	READ_INT_FIELD(yb_distinct_prefixlen);
	READ_ENUM_FIELD(yb_lock_mechanism, YbLockMechanism);

	READ_DONE();
}

/*
 * _readIndexOnlyScan
 */
static IndexOnlyScan *
_readIndexOnlyScan(void)
{
	READ_LOCALS(IndexOnlyScan);

	ReadCommonScan(&local_node->scan);

	READ_OID_FIELD(indexid);
	READ_NODE_FIELD(indexqual);
	READ_NODE_FIELD(recheckqual);
	READ_NODE_FIELD(indexorderby);
	READ_NODE_FIELD(indextlist);
	READ_ENUM_FIELD(indexorderdir, ScanDirection);
	READ_NODE_FIELD(yb_pushdown.quals);
	READ_NODE_FIELD(yb_pushdown.colrefs);
	READ_INT_FIELD(yb_distinct_prefixlen);
	READ_INT_FIELD(yb_num_decoded_pk_cols);

	READ_DONE();
}

/*
 * _readBitmapIndexScan
 */
static BitmapIndexScan *
_readBitmapIndexScan(void)
{
	READ_LOCALS(BitmapIndexScan);

	ReadCommonScan(&local_node->scan);

	READ_OID_FIELD(indexid);
	READ_BOOL_FIELD(isshared);
	READ_NODE_FIELD(indexqual);
	READ_NODE_FIELD(indexqualorig);

	READ_DONE();
}

/*
 * _readYbBitmapIndexScan
 */
static YbBitmapIndexScan *
_readYbBitmapIndexScan(void)
{
	READ_LOCALS(YbBitmapIndexScan);

	ReadCommonScan(&local_node->scan);

	READ_OID_FIELD(indexid);
	READ_BOOL_FIELD(isshared);
	READ_NODE_FIELD(indexqual);
	READ_NODE_FIELD(indexqualorig);

	READ_NODE_FIELD(indextlist);

	READ_NODE_FIELD(yb_idx_pushdown.quals);
	READ_NODE_FIELD(yb_idx_pushdown.colrefs);

	READ_DONE();
}

/*
 * _readBitmapHeapScan
 */
static BitmapHeapScan *
_readBitmapHeapScan(void)
{
	READ_LOCALS(BitmapHeapScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(bitmapqualorig);

	READ_DONE();
}

/*
 * _readBitmapHeapScan
 */
static YbBitmapTableScan *
_readYbBitmapTableScan(void)
{
	READ_LOCALS(YbBitmapTableScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(rel_pushdown.quals);
	READ_NODE_FIELD(rel_pushdown.colrefs);

	READ_NODE_FIELD(recheck_pushdown.quals);
	READ_NODE_FIELD(recheck_pushdown.colrefs);
	READ_NODE_FIELD(recheck_local_quals);

	READ_NODE_FIELD(fallback_pushdown.quals);
	READ_NODE_FIELD(fallback_pushdown.colrefs);
	READ_NODE_FIELD(fallback_local_quals);

	READ_DONE();
}

/*
 * _readTidScan
 */
static TidScan *
_readTidScan(void)
{
	READ_LOCALS(TidScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(yb_rel_pushdown.quals);
	READ_NODE_FIELD(yb_rel_pushdown.colrefs);
	READ_NODE_FIELD(tidquals);

	READ_DONE();
}

/*
 * _readTidRangeScan
 */
static TidRangeScan *
_readTidRangeScan(void)
{
	READ_LOCALS(TidRangeScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(tidrangequals);

	READ_DONE();
}

/*
 * _readSubqueryScan
 */
static SubqueryScan *
_readSubqueryScan(void)
{
	READ_LOCALS(SubqueryScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(subplan);
	READ_ENUM_FIELD(scanstatus, SubqueryScanStatus);

	READ_DONE();
}

/*
 * _readFunctionScan
 */
static FunctionScan *
_readFunctionScan(void)
{
	READ_LOCALS(FunctionScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(functions);
	READ_BOOL_FIELD(funcordinality);

	READ_DONE();
}

/*
 * _readValuesScan
 */
static ValuesScan *
_readValuesScan(void)
{
	READ_LOCALS(ValuesScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(values_lists);

	READ_DONE();
}

/*
 * _readTableFuncScan
 */
static TableFuncScan *
_readTableFuncScan(void)
{
	READ_LOCALS(TableFuncScan);

	ReadCommonScan(&local_node->scan);

	READ_NODE_FIELD(tablefunc);

	READ_DONE();
}

/*
 * _readCteScan
 */
static CteScan *
_readCteScan(void)
{
	READ_LOCALS(CteScan);

	ReadCommonScan(&local_node->scan);

	READ_INT_FIELD(ctePlanId);
	READ_INT_FIELD(cteParam);

	READ_DONE();
}

/*
 * _readNamedTuplestoreScan
 */
static NamedTuplestoreScan *
_readNamedTuplestoreScan(void)
{
	READ_LOCALS(NamedTuplestoreScan);

	ReadCommonScan(&local_node->scan);

	READ_STRING_FIELD(enrname);

	READ_DONE();
}

/*
 * _readWorkTableScan
 */
static WorkTableScan *
_readWorkTableScan(void)
{
	READ_LOCALS(WorkTableScan);

	ReadCommonScan(&local_node->scan);

	READ_INT_FIELD(wtParam);

	READ_DONE();
}

/*
 * _readForeignScan
 */
static ForeignScan *
_readForeignScan(void)
{
	READ_LOCALS(ForeignScan);

	ReadCommonScan(&local_node->scan);

	READ_ENUM_FIELD(operation, CmdType);
	READ_UINT_FIELD(resultRelation);
	READ_OID_FIELD(fs_server);
	READ_NODE_FIELD(fdw_exprs);
	READ_NODE_FIELD(fdw_private);
	READ_NODE_FIELD(fdw_scan_tlist);
	READ_NODE_FIELD(fdw_recheck_quals);
	READ_BITMAPSET_FIELD(fs_relids);
	READ_BOOL_FIELD(fsSystemCol);

	READ_DONE();
}

/*
 * _readCustomScan
 */
static CustomScan *
_readCustomScan(void)
{
	READ_LOCALS(CustomScan);
	char	   *custom_name;
	const CustomScanMethods *methods;

	ReadCommonScan(&local_node->scan);

	READ_UINT_FIELD(flags);
	READ_NODE_FIELD(custom_plans);
	READ_NODE_FIELD(custom_exprs);
	READ_NODE_FIELD(custom_private);
	READ_NODE_FIELD(custom_scan_tlist);
	READ_BITMAPSET_FIELD(custom_relids);

	/* Lookup CustomScanMethods by CustomName */
	token = pg_strtok(&length); /* skip methods: */
	token = pg_strtok(&length); /* CustomName */
	custom_name = nullable_string(token, length);
	methods = GetCustomScanMethods(custom_name, false);
	local_node->methods = methods;

	READ_DONE();
}

/*
 * ReadCommonJoin
 *	Assign the basic stuff of all nodes that inherit from Join
 */
static void
ReadCommonJoin(Join *local_node)
{
	READ_TEMP_LOCALS();

	ReadCommonPlan(&local_node->plan);

	READ_ENUM_FIELD(jointype, JoinType);
	READ_BOOL_FIELD(inner_unique);
	READ_NODE_FIELD(joinqual);
}

/*
 * _readJoin
 */
static Join *
_readJoin(void)
{
	READ_LOCALS_NO_FIELDS(Join);

	ReadCommonJoin(local_node);

	READ_DONE();
}

/*
 * _readNestLoop
 */
static NestLoop *
_readNestLoop(void)
{
	READ_LOCALS(NestLoop);

	ReadCommonJoin(&local_node->join);

	READ_NODE_FIELD(nestParams);

	READ_DONE();
}

/*
 * _readYbBatchedNestLoop
 */
static YbBatchedNestLoop *
_readYbBatchedNestLoop(void)
{
	READ_LOCALS(YbBatchedNestLoop);

	ReadCommonJoin(&local_node->nl.join);

	READ_NODE_FIELD(nl.nestParams);
	READ_INT_FIELD(num_hashClauseInfos);
	local_node->hashClauseInfos =
		palloc0(local_node->num_hashClauseInfos * sizeof(YbBNLHashClauseInfo));

	/* Ignore :hashOps */
	pg_strtok(&length);
	for (int i = 0; i < local_node->num_hashClauseInfos; i++)
	{
		token = pg_strtok(&length);
		local_node->hashClauseInfos[i].hashOp = atoi(token);
	}

	/* Ignore :innerHashAttNos */
	pg_strtok(&length);
	for (int i = 0; i < local_node->num_hashClauseInfos; i++)
	{
		token = pg_strtok(&length);
		local_node->hashClauseInfos[i].innerHashAttNo = atoi(token);
	}

	/* Ignore :outerParamExprs */
	pg_strtok(&length);
	for (int i = 0; i < local_node->num_hashClauseInfos; i++)
		local_node->hashClauseInfos[i].outerParamExpr = nodeRead(NULL, 0);

	/* Ignore :orig_expr */
	pg_strtok(&length);
	for (int i = 0; i < local_node->num_hashClauseInfos; i++)
		local_node->hashClauseInfos[i].orig_expr = nodeRead(NULL, 0);

	READ_INT_FIELD(numSortCols);
	READ_ATTRNUMBER_ARRAY(sortColIdx, local_node->numSortCols);
	READ_OID_ARRAY(sortOperators, local_node->numSortCols);
	READ_OID_ARRAY(collations, local_node->numSortCols);
	READ_BOOL_ARRAY(nullsFirst, local_node->numSortCols);
	READ_DONE();
}

/*
 * _readMergeJoin
 */
static MergeJoin *
_readMergeJoin(void)
{
	int			numCols;

	READ_LOCALS(MergeJoin);

	ReadCommonJoin(&local_node->join);

	READ_BOOL_FIELD(skip_mark_restore);
	READ_NODE_FIELD(mergeclauses);

	numCols = list_length(local_node->mergeclauses);

	READ_OID_ARRAY(mergeFamilies, numCols);
	READ_OID_ARRAY(mergeCollations, numCols);
	READ_INT_ARRAY(mergeStrategies, numCols);
	READ_BOOL_ARRAY(mergeNullsFirst, numCols);

	READ_DONE();
}

/*
 * _readHashJoin
 */
static HashJoin *
_readHashJoin(void)
{
	READ_LOCALS(HashJoin);

	ReadCommonJoin(&local_node->join);

	READ_NODE_FIELD(hashclauses);
	READ_NODE_FIELD(hashoperators);
	READ_NODE_FIELD(hashcollations);
	READ_NODE_FIELD(hashkeys);

	READ_DONE();
}

/*
 * _readMaterial
 */
static Material *
_readMaterial(void)
{
	READ_LOCALS_NO_FIELDS(Material);

	ReadCommonPlan(&local_node->plan);

	READ_DONE();
}

/*
 * _readMemoize
 */
static Memoize *
_readMemoize(void)
{
	READ_LOCALS(Memoize);

	ReadCommonPlan(&local_node->plan);

	READ_INT_FIELD(numKeys);
	READ_OID_ARRAY(hashOperators, local_node->numKeys);
	READ_OID_ARRAY(collations, local_node->numKeys);
	READ_NODE_FIELD(param_exprs);
	READ_BOOL_FIELD(singlerow);
	READ_BOOL_FIELD(binary_mode);
	READ_UINT_FIELD(est_entries);
	READ_BITMAPSET_FIELD(keyparamids);

	READ_DONE();
}

/*
 * ReadCommonSort
 *	Assign the basic stuff of all nodes that inherit from Sort
 */
static void
ReadCommonSort(Sort *local_node)
{
	READ_TEMP_LOCALS();

	ReadCommonPlan(&local_node->plan);

	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(sortColIdx, local_node->numCols);
	READ_OID_ARRAY(sortOperators, local_node->numCols);
	READ_OID_ARRAY(collations, local_node->numCols);
	READ_BOOL_ARRAY(nullsFirst, local_node->numCols);
}

/*
 * _readSort
 */
static Sort *
_readSort(void)
{
	READ_LOCALS_NO_FIELDS(Sort);

	ReadCommonSort(local_node);

	READ_DONE();
}

/*
 * _readIncrementalSort
 */
static IncrementalSort *
_readIncrementalSort(void)
{
	READ_LOCALS(IncrementalSort);

	ReadCommonSort(&local_node->sort);

	READ_INT_FIELD(nPresortedCols);

	READ_DONE();
}

/*
 * _readGroup
 */
static Group *
_readGroup(void)
{
	READ_LOCALS(Group);

	ReadCommonPlan(&local_node->plan);

	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(grpColIdx, local_node->numCols);
	READ_OID_ARRAY(grpOperators, local_node->numCols);
	READ_OID_ARRAY(grpCollations, local_node->numCols);

	READ_DONE();
}

/*
 * _readAgg
 */
static Agg *
_readAgg(void)
{
	READ_LOCALS(Agg);

	ReadCommonPlan(&local_node->plan);

	READ_ENUM_FIELD(aggstrategy, AggStrategy);
	READ_ENUM_FIELD(aggsplit, AggSplit);
	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(grpColIdx, local_node->numCols);
	READ_OID_ARRAY(grpOperators, local_node->numCols);
	READ_OID_ARRAY(grpCollations, local_node->numCols);
	READ_LONG_FIELD(numGroups);
	READ_UINT64_FIELD(transitionSpace);
	READ_BITMAPSET_FIELD(aggParams);
	READ_NODE_FIELD(groupingSets);
	READ_NODE_FIELD(chain);

	READ_DONE();
}

/*
 * _readWindowAgg
 */
static WindowAgg *
_readWindowAgg(void)
{
	READ_LOCALS(WindowAgg);

	ReadCommonPlan(&local_node->plan);

	READ_UINT_FIELD(winref);
	READ_INT_FIELD(partNumCols);
	READ_ATTRNUMBER_ARRAY(partColIdx, local_node->partNumCols);
	READ_OID_ARRAY(partOperators, local_node->partNumCols);
	READ_OID_ARRAY(partCollations, local_node->partNumCols);
	READ_INT_FIELD(ordNumCols);
	READ_ATTRNUMBER_ARRAY(ordColIdx, local_node->ordNumCols);
	READ_OID_ARRAY(ordOperators, local_node->ordNumCols);
	READ_OID_ARRAY(ordCollations, local_node->ordNumCols);
	READ_INT_FIELD(frameOptions);
	READ_NODE_FIELD(startOffset);
	READ_NODE_FIELD(endOffset);
	READ_NODE_FIELD(runCondition);
	READ_NODE_FIELD(runConditionOrig);
	READ_OID_FIELD(startInRangeFunc);
	READ_OID_FIELD(endInRangeFunc);
	READ_OID_FIELD(inRangeColl);
	READ_BOOL_FIELD(inRangeAsc);
	READ_BOOL_FIELD(inRangeNullsFirst);
	READ_BOOL_FIELD(topWindow);

	READ_DONE();
}

/*
 * _readUnique
 */
static Unique *
_readUnique(void)
{
	READ_LOCALS(Unique);

	ReadCommonPlan(&local_node->plan);

	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(uniqColIdx, local_node->numCols);
	READ_OID_ARRAY(uniqOperators, local_node->numCols);
	READ_OID_ARRAY(uniqCollations, local_node->numCols);

	READ_DONE();
}

/*
 * _readGather
 */
static Gather *
_readGather(void)
{
	READ_LOCALS(Gather);

	ReadCommonPlan(&local_node->plan);

	READ_INT_FIELD(num_workers);
	READ_INT_FIELD(rescan_param);
	READ_BOOL_FIELD(single_copy);
	READ_BOOL_FIELD(invisible);
	READ_BITMAPSET_FIELD(initParam);

	READ_DONE();
}

/*
 * _readGatherMerge
 */
static GatherMerge *
_readGatherMerge(void)
{
	READ_LOCALS(GatherMerge);

	ReadCommonPlan(&local_node->plan);

	READ_INT_FIELD(num_workers);
	READ_INT_FIELD(rescan_param);
	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(sortColIdx, local_node->numCols);
	READ_OID_ARRAY(sortOperators, local_node->numCols);
	READ_OID_ARRAY(collations, local_node->numCols);
	READ_BOOL_ARRAY(nullsFirst, local_node->numCols);
	READ_BITMAPSET_FIELD(initParam);

	READ_DONE();
}

/*
 * _readHash
 */
static Hash *
_readHash(void)
{
	READ_LOCALS(Hash);

	ReadCommonPlan(&local_node->plan);

	READ_NODE_FIELD(hashkeys);
	READ_OID_FIELD(skewTable);
	READ_INT_FIELD(skewColumn);
	READ_BOOL_FIELD(skewInherit);
	READ_FLOAT_FIELD(rows_total);

	/* YB */
	READ_STRING_FIELD(ybSkewTableName);

	READ_DONE();
}

/*
 * _readSetOp
 */
static SetOp *
_readSetOp(void)
{
	READ_LOCALS(SetOp);

	ReadCommonPlan(&local_node->plan);

	READ_ENUM_FIELD(cmd, SetOpCmd);
	READ_ENUM_FIELD(strategy, SetOpStrategy);
	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(dupColIdx, local_node->numCols);
	READ_OID_ARRAY(dupOperators, local_node->numCols);
	READ_OID_ARRAY(dupCollations, local_node->numCols);
	READ_INT_FIELD(flagColIdx);
	READ_INT_FIELD(firstFlag);
	READ_LONG_FIELD(numGroups);

	READ_DONE();
}

/*
 * _readLockRows
 */
static LockRows *
_readLockRows(void)
{
	READ_LOCALS(LockRows);

	ReadCommonPlan(&local_node->plan);

	READ_NODE_FIELD(rowMarks);
	READ_INT_FIELD(epqParam);

	READ_DONE();
}

/*
 * _readLimit
 */
static Limit *
_readLimit(void)
{
	READ_LOCALS(Limit);

	ReadCommonPlan(&local_node->plan);

	READ_NODE_FIELD(limitOffset);
	READ_NODE_FIELD(limitCount);
	READ_ENUM_FIELD(limitOption, LimitOption);
	READ_INT_FIELD(uniqNumCols);
	READ_ATTRNUMBER_ARRAY(uniqColIdx, local_node->uniqNumCols);
	READ_OID_ARRAY(uniqOperators, local_node->uniqNumCols);
	READ_OID_ARRAY(uniqCollations, local_node->uniqNumCols);

	READ_DONE();
}

/*
 * _readNestLoopParam
 */
static NestLoopParam *
_readNestLoopParam(void)
{
	READ_LOCALS(NestLoopParam);

	READ_INT_FIELD(paramno);
	READ_NODE_FIELD(paramval);
	READ_INT_FIELD(yb_batch_size);

	READ_DONE();
}

/*
 * _readPlanRowMark
 */
static PlanRowMark *
_readPlanRowMark(void)
{
	READ_LOCALS(PlanRowMark);

	READ_UINT_FIELD(rti);
	READ_UINT_FIELD(prti);
	READ_UINT_FIELD(rowmarkId);
	READ_ENUM_FIELD(markType, RowMarkType);
	READ_INT_FIELD(allMarkTypes);
	READ_ENUM_FIELD(strength, LockClauseStrength);
	READ_ENUM_FIELD(waitPolicy, LockWaitPolicy);
	READ_BOOL_FIELD(isParent);

	READ_DONE();
}

static PartitionPruneInfo *
_readPartitionPruneInfo(void)
{
	READ_LOCALS(PartitionPruneInfo);

	READ_NODE_FIELD(prune_infos);
	READ_BITMAPSET_FIELD(other_subplans);

	READ_DONE();
}

static PartitionedRelPruneInfo *
_readPartitionedRelPruneInfo(void)
{
	READ_LOCALS(PartitionedRelPruneInfo);

	READ_UINT_FIELD(rtindex);
	READ_BITMAPSET_FIELD(present_parts);
	READ_INT_FIELD(nparts);
	READ_INT_ARRAY(subplan_map, local_node->nparts);
	READ_INT_ARRAY(subpart_map, local_node->nparts);
	READ_OID_ARRAY(relid_map, local_node->nparts);
	READ_NODE_FIELD(initial_pruning_steps);
	READ_NODE_FIELD(exec_pruning_steps);
	READ_BITMAPSET_FIELD(execparamids);

	READ_DONE();
}

static PartitionPruneStepOp *
_readPartitionPruneStepOp(void)
{
	READ_LOCALS(PartitionPruneStepOp);

	READ_INT_FIELD(step.step_id);
	READ_INT_FIELD(opstrategy);
	READ_NODE_FIELD(exprs);
	READ_NODE_FIELD(cmpfns);
	READ_BITMAPSET_FIELD(nullkeys);

	READ_DONE();
}

static YbPartitionPruneStepFuncOp *
_readYbPartitionPruneStepFuncOp(void)
{
	READ_LOCALS(YbPartitionPruneStepFuncOp);

	READ_INT_FIELD(step.step_id);
	READ_NODE_FIELD(exprs);

	READ_DONE();
}

static PartitionPruneStepCombine *
_readPartitionPruneStepCombine(void)
{
	READ_LOCALS(PartitionPruneStepCombine);

	READ_INT_FIELD(step.step_id);
	READ_ENUM_FIELD(combineOp, PartitionPruneCombineOp);
	READ_NODE_FIELD(source_stepids);

	READ_DONE();
}

/*
 * _readPlanInvalItem
 */
static PlanInvalItem *
_readPlanInvalItem(void)
{
	READ_LOCALS(PlanInvalItem);

	READ_INT_FIELD(cacheId);
	READ_UINT_FIELD(hashValue);

	READ_DONE();
}

/*
 * _readSubPlan
 */
static SubPlan *
_readSubPlan(void)
{
	READ_LOCALS(SubPlan);

	READ_ENUM_FIELD(subLinkType, SubLinkType);
	READ_NODE_FIELD(testexpr);
	READ_NODE_FIELD(paramIds);
	READ_INT_FIELD(plan_id);
	READ_STRING_FIELD(plan_name);
	READ_OID_FIELD(firstColType);
	READ_INT_FIELD(firstColTypmod);
	READ_OID_FIELD(firstColCollation);
	READ_BOOL_FIELD(useHashTable);
	READ_BOOL_FIELD(unknownEqFalse);
	READ_BOOL_FIELD(parallel_safe);
	READ_NODE_FIELD(setParam);
	READ_NODE_FIELD(parParam);
	READ_NODE_FIELD(args);
	READ_FLOAT_FIELD(startup_cost);
	READ_FLOAT_FIELD(per_call_cost);

	READ_DONE();
}

/*
 * _readAlternativeSubPlan
 */
static AlternativeSubPlan *
_readAlternativeSubPlan(void)
{
	READ_LOCALS(AlternativeSubPlan);

	READ_NODE_FIELD(subplans);

	READ_DONE();
}

/*
 * _readExtensibleNode
 */
>>>>>>> bc662ba7050
static ExtensibleNode *
_readExtensibleNode(void)
{
	const ExtensibleNodeMethods *methods;
	ExtensibleNode *local_node;
	const char *extnodename;

	READ_TEMP_LOCALS();

	token = pg_strtok(&length); /* skip :extnodename */
	token = pg_strtok(&length); /* get extnodename */

	extnodename = nullable_string(token, length);
	if (!extnodename)
		elog(ERROR, "extnodename has to be supplied");
	methods = GetExtensibleNodeMethods(extnodename, false);

	local_node = (ExtensibleNode *) newNode(methods->node_size,
											T_ExtensibleNode);
	local_node->extnodename = extnodename;

	/* deserialize the private fields */
	methods->nodeRead(local_node);

	READ_DONE();
}


/*
 * _readYbExprParamDesc
 */
static YbExprColrefDesc *
_readYbExprColrefDesc(void)
{
	READ_LOCALS(YbExprColrefDesc);

	READ_INT_FIELD(attno);
	READ_OID_FIELD(typid);
	READ_INT_FIELD(typmod);
	READ_OID_FIELD(collid);

	READ_DONE();
}

static YbSkippableEntities *
_readYbSkippableEntities(void)
{
	READ_LOCALS(YbSkippableEntities);

	READ_NODE_FIELD(index_list);
	READ_NODE_FIELD(referencing_fkey_list);
	READ_NODE_FIELD(referenced_fkey_list);

	READ_DONE();
}

static YbUpdateAffectedEntities *
_readYbUpdateAffectedEntities(void)
{
	int			nfields;
	int			nentities;

	READ_LOCALS(YbUpdateAffectedEntities);

	READ_INT_FIELD(matrix.nrows);
	READ_INT_FIELD(matrix.ncols);

	nfields = local_node->matrix.nrows;
	nentities = local_node->matrix.ncols;

	local_node->entity_list = palloc0(nentities * sizeof(struct YbUpdateEntity));
	for (int i = 0; i < nentities; i++)
	{
		READ_OID_FIELD(entity_list[i].oid);
		READ_ENUM_FIELD(entity_list[i].etype, YbSkippableEntityType);
	}

	local_node->col_info_list = palloc0(nfields * sizeof(struct YbUpdateColInfo));
	for (int i = 0; i < nfields; i++)
	{
		READ_INT_FIELD(col_info_list[i].attnum);
		READ_NODE_FIELD(col_info_list[i].entity_refs);
	}

	READ_BITMAPSET_FIELD(matrix.data);

	READ_DONE();
}

static YbMergeScanInfo *
_readYbMergeScanInfo(void)
{
	READ_LOCALS(YbMergeScanInfo);

	READ_NODE_FIELD(saop_cols);
	READ_NODE_FIELD(sort_cols);

	READ_DONE();
}

static YbMergeScanSaopColInfo *
_readYbMergeScanSaopColInfo(void)
{
	READ_LOCALS(YbMergeScanSaopColInfo);

	READ_NODE_FIELD(saop);
	READ_INT_FIELD(indexcol);
	READ_INT_FIELD(num_elems);

	READ_DONE();
}

static YbSortInfo *
_readYbSortInfo(void)
{
	READ_LOCALS(YbSortInfo);

	READ_INT_FIELD(numCols);
	READ_ATTRNUMBER_ARRAY(sortColIdx, local_node->numCols);
	READ_OID_ARRAY(sortOperators, local_node->numCols);
	READ_OID_ARRAY(collations, local_node->numCols);
	READ_BOOL_ARRAY(nullsFirst, local_node->numCols);

	READ_DONE();
}

/*
 * parseNodeString
 *
 * Given a character string representing a node tree, parseNodeString creates
 * the internal node structure.
 *
 * The string to be read must already have been loaded into pg_strtok().
 */
Node *
parseNodeString(void)
{
	READ_TEMP_LOCALS();

	/* Guard against stack overflow due to overly complex expressions */
	check_stack_depth();

	token = pg_strtok(&length);

#define MATCH(tokname, namelen) \
	(length == namelen && memcmp(token, tokname, namelen) == 0)

<<<<<<< HEAD
#include "readfuncs.switch.c"
=======
	if (MATCH("QUERY", 5))
		return_value = _readQuery();
	else if (MATCH("WITHCHECKOPTION", 15))
		return_value = _readWithCheckOption();
	else if (MATCH("SORTGROUPCLAUSE", 15))
		return_value = _readSortGroupClause();
	else if (MATCH("GROUPINGSET", 11))
		return_value = _readGroupingSet();
	else if (MATCH("WINDOWCLAUSE", 12))
		return_value = _readWindowClause();
	else if (MATCH("ROWMARKCLAUSE", 13))
		return_value = _readRowMarkClause();
	else if (MATCH("CTESEARCHCLAUSE", 15))
		return_value = _readCTESearchClause();
	else if (MATCH("CTECYCLECLAUSE", 14))
		return_value = _readCTECycleClause();
	else if (MATCH("COMMONTABLEEXPR", 15))
		return_value = _readCommonTableExpr();
	else if (MATCH("MERGEWHENCLAUSE", 15))
		return_value = _readMergeWhenClause();
	else if (MATCH("MERGEACTION", 11))
		return_value = _readMergeAction();
	else if (MATCH("SETOPERATIONSTMT", 16))
		return_value = _readSetOperationStmt();
	else if (MATCH("ALIAS", 5))
		return_value = _readAlias();
	else if (MATCH("RANGEVAR", 8))
		return_value = _readRangeVar();
	else if (MATCH("INTOCLAUSE", 10))
		return_value = _readIntoClause();
	else if (MATCH("TABLEFUNC", 9))
		return_value = _readTableFunc();
	else if (MATCH("VAR", 3))
		return_value = _readVar();
	else if (MATCH("CONST", 5))
		return_value = _readConst();
	else if (MATCH("PARAM", 5))
		return_value = _readParam();
	else if (MATCH("AGGREF", 6))
		return_value = _readAggref();
	else if (MATCH("GROUPINGFUNC", 12))
		return_value = _readGroupingFunc();
	else if (MATCH("WINDOWFUNC", 10))
		return_value = _readWindowFunc();
	else if (MATCH("SUBSCRIPTINGREF", 15))
		return_value = _readSubscriptingRef();
	else if (MATCH("FUNCEXPR", 8))
		return_value = _readFuncExpr();
	else if (MATCH("NAMEDARGEXPR", 12))
		return_value = _readNamedArgExpr();
	else if (MATCH("OPEXPR", 6))
		return_value = _readOpExpr();
	else if (MATCH("DISTINCTEXPR", 12))
		return_value = _readDistinctExpr();
	else if (MATCH("NULLIFEXPR", 10))
		return_value = _readNullIfExpr();
	else if (MATCH("SCALARARRAYOPEXPR", 17))
		return_value = _readScalarArrayOpExpr();
	else if (MATCH("BOOLEXPR", 8))
		return_value = _readBoolExpr();
	else if (MATCH("SUBLINK", 7))
		return_value = _readSubLink();
	else if (MATCH("FIELDSELECT", 11))
		return_value = _readFieldSelect();
	else if (MATCH("FIELDSTORE", 10))
		return_value = _readFieldStore();
	else if (MATCH("RELABELTYPE", 11))
		return_value = _readRelabelType();
	else if (MATCH("COERCEVIAIO", 11))
		return_value = _readCoerceViaIO();
	else if (MATCH("ARRAYCOERCEEXPR", 15))
		return_value = _readArrayCoerceExpr();
	else if (MATCH("CONVERTROWTYPEEXPR", 18))
		return_value = _readConvertRowtypeExpr();
	else if (MATCH("COLLATEEXPR", 11))
		return_value = _readCollateExpr();
	else if (MATCH("CASEEXPR", 8))
		return_value = _readCaseExpr();
	else if (MATCH("CASEWHEN", 8))
		return_value = _readCaseWhen();
	else if (MATCH("CASETESTEXPR", 12))
		return_value = _readCaseTestExpr();
	else if (MATCH("ARRAYEXPR", 9))
		return_value = _readArrayExpr();
	else if (MATCH("ROWEXPR", 7))
		return_value = _readRowExpr();
	else if (MATCH("ROWCOMPAREEXPR", 14))
		return_value = _readRowCompareExpr();
	else if (MATCH("COALESCEEXPR", 12))
		return_value = _readCoalesceExpr();
	else if (MATCH("MINMAXEXPR", 10))
		return_value = _readMinMaxExpr();
	else if (MATCH("SQLVALUEFUNCTION", 16))
		return_value = _readSQLValueFunction();
	else if (MATCH("XMLEXPR", 7))
		return_value = _readXmlExpr();
	else if (MATCH("NULLTEST", 8))
		return_value = _readNullTest();
	else if (MATCH("BOOLEANTEST", 11))
		return_value = _readBooleanTest();
	else if (MATCH("COERCETODOMAIN", 14))
		return_value = _readCoerceToDomain();
	else if (MATCH("COERCETODOMAINVALUE", 19))
		return_value = _readCoerceToDomainValue();
	else if (MATCH("SETTODEFAULT", 12))
		return_value = _readSetToDefault();
	else if (MATCH("CURRENTOFEXPR", 13))
		return_value = _readCurrentOfExpr();
	else if (MATCH("NEXTVALUEEXPR", 13))
		return_value = _readNextValueExpr();
	else if (MATCH("INFERENCEELEM", 13))
		return_value = _readInferenceElem();
	else if (MATCH("TARGETENTRY", 11))
		return_value = _readTargetEntry();
	else if (MATCH("RANGETBLREF", 11))
		return_value = _readRangeTblRef();
	else if (MATCH("JOINEXPR", 8))
		return_value = _readJoinExpr();
	else if (MATCH("FROMEXPR", 8))
		return_value = _readFromExpr();
	else if (MATCH("ONCONFLICTEXPR", 14))
		return_value = _readOnConflictExpr();
	else if (MATCH("APPENDRELINFO", 13))
		return_value = _readAppendRelInfo();
	else if (MATCH("RANGETBLENTRY", 13))
		return_value = _readRangeTblEntry();
	else if (MATCH("RANGETBLFUNCTION", 16))
		return_value = _readRangeTblFunction();
	else if (MATCH("TABLESAMPLECLAUSE", 17))
		return_value = _readTableSampleClause();
	else if (MATCH("NOTIFYSTMT", 10))
		return_value = _readNotifyStmt();
	else if (MATCH("DEFELEM", 7))
		return_value = _readDefElem();
	else if (MATCH("DECLARECURSORSTMT", 17))
		return_value = _readDeclareCursorStmt();
	else if (MATCH("PLANNEDSTMT", 11))
		return_value = _readPlannedStmt();
	else if (MATCH("PLAN", 4))
		return_value = _readPlan();
	else if (MATCH("RESULT", 6))
		return_value = _readResult();
	else if (MATCH("PROJECTSET", 10))
		return_value = _readProjectSet();
	else if (MATCH("MODIFYTABLE", 11))
		return_value = _readModifyTable();
	else if (MATCH("APPEND", 6))
		return_value = _readAppend();
	else if (MATCH("MERGEAPPEND", 11))
		return_value = _readMergeAppend();
	else if (MATCH("RECURSIVEUNION", 14))
		return_value = _readRecursiveUnion();
	else if (MATCH("BITMAPAND", 9))
		return_value = _readBitmapAnd();
	else if (MATCH("BITMAPOR", 8))
		return_value = _readBitmapOr();
	else if (MATCH("SCAN", 4))
		return_value = _readScan();
	else if (MATCH("SEQSCAN", 7))
		return_value = _readSeqScan();
	else if (MATCH("YBSEQSCAN", 9))
		return_value = _readYbSeqScan();
	else if (MATCH("SAMPLESCAN", 10))
		return_value = _readSampleScan();
	else if (MATCH("INDEXSCAN", 9))
		return_value = _readIndexScan();
	else if (MATCH("INDEXONLYSCAN", 13))
		return_value = _readIndexOnlyScan();
	else if (MATCH("BITMAPINDEXSCAN", 15))
		return_value = _readBitmapIndexScan();
	else if (MATCH("YBBITMAPINDEXSCAN", 17))
		return_value = _readYbBitmapIndexScan();
	else if (MATCH("BITMAPHEAPSCAN", 14))
		return_value = _readBitmapHeapScan();
	else if (MATCH("YBBITMAPTABLESCAN", 17))
		return_value = _readYbBitmapTableScan();
	else if (MATCH("TIDSCAN", 7))
		return_value = _readTidScan();
	else if (MATCH("TIDRANGESCAN", 12))
		return_value = _readTidRangeScan();
	else if (MATCH("SUBQUERYSCAN", 12))
		return_value = _readSubqueryScan();
	else if (MATCH("FUNCTIONSCAN", 12))
		return_value = _readFunctionScan();
	else if (MATCH("VALUESSCAN", 10))
		return_value = _readValuesScan();
	else if (MATCH("TABLEFUNCSCAN", 13))
		return_value = _readTableFuncScan();
	else if (MATCH("CTESCAN", 7))
		return_value = _readCteScan();
	else if (MATCH("NAMEDTUPLESTORESCAN", 19))
		return_value = _readNamedTuplestoreScan();
	else if (MATCH("WORKTABLESCAN", 13))
		return_value = _readWorkTableScan();
	else if (MATCH("FOREIGNSCAN", 11))
		return_value = _readForeignScan();
	else if (MATCH("CUSTOMSCAN", 10))
		return_value = _readCustomScan();
	else if (MATCH("JOIN", 4))
		return_value = _readJoin();
	else if (MATCH("NESTLOOP", 8))
		return_value = _readNestLoop();
	else if (MATCH("YBBATCHEDNESTLOOP", 17))
		return_value = _readYbBatchedNestLoop();
	else if (MATCH("MERGEJOIN", 9))
		return_value = _readMergeJoin();
	else if (MATCH("HASHJOIN", 8))
		return_value = _readHashJoin();
	else if (MATCH("MATERIAL", 8))
		return_value = _readMaterial();
	else if (MATCH("MEMOIZE", 7))
		return_value = _readMemoize();
	else if (MATCH("SORT", 4))
		return_value = _readSort();
	else if (MATCH("INCREMENTALSORT", 15))
		return_value = _readIncrementalSort();
	else if (MATCH("GROUP", 5))
		return_value = _readGroup();
	else if (MATCH("AGG", 3))
		return_value = _readAgg();
	else if (MATCH("WINDOWAGG", 9))
		return_value = _readWindowAgg();
	else if (MATCH("UNIQUE", 6))
		return_value = _readUnique();
	else if (MATCH("GATHER", 6))
		return_value = _readGather();
	else if (MATCH("GATHERMERGE", 11))
		return_value = _readGatherMerge();
	else if (MATCH("HASH", 4))
		return_value = _readHash();
	else if (MATCH("SETOP", 5))
		return_value = _readSetOp();
	else if (MATCH("LOCKROWS", 8))
		return_value = _readLockRows();
	else if (MATCH("LIMIT", 5))
		return_value = _readLimit();
	else if (MATCH("NESTLOOPPARAM", 13))
		return_value = _readNestLoopParam();
	else if (MATCH("PLANROWMARK", 11))
		return_value = _readPlanRowMark();
	else if (MATCH("PARTITIONPRUNEINFO", 18))
		return_value = _readPartitionPruneInfo();
	else if (MATCH("PARTITIONEDRELPRUNEINFO", 23))
		return_value = _readPartitionedRelPruneInfo();
	else if (MATCH("PARTITIONPRUNESTEPOP", 20))
		return_value = _readPartitionPruneStepOp();
	else if (MATCH("PARTITIONPRUNESTEPCOMBINE", 25))
		return_value = _readPartitionPruneStepCombine();
	else if (MATCH("PLANINVALITEM", 13))
		return_value = _readPlanInvalItem();
	else if (MATCH("SUBPLAN", 7))
		return_value = _readSubPlan();
	else if (MATCH("ALTERNATIVESUBPLAN", 18))
		return_value = _readAlternativeSubPlan();
	else if (MATCH("EXTENSIBLENODE", 14))
		return_value = _readExtensibleNode();
	else if (MATCH("PARTITIONBOUNDSPEC", 18))
		return_value = _readPartitionBoundSpec();
	else if (MATCH("PARTITIONRANGEDATUM", 19))
		return_value = _readPartitionRangeDatum();
	else if (MATCH("PARTITIONPRUNESTEPFUNCOP", 24))
		return_value = _readYbPartitionPruneStepFuncOp();
	else if (MATCH("YBEXPRCOLREFDESC", 16))
		return_value = _readYbExprColrefDesc();
	else if (MATCH("YBSKIPPABLEENTITIES", 19))
		return_value = _readYbSkippableEntities();
	else if (MATCH("YBUPDATEAFFECTEDENTITIES", 24))
		return_value = _readYbUpdateAffectedEntities();
	else if (MATCH("YBMERGESCANINFO", 15))
		return_value = _readYbMergeScanInfo();
	else if (MATCH("YBMERGESCANSAOPCOLINFO", 22))
		return_value = _readYbMergeScanSaopColInfo();
	else if (MATCH("YBSORTINFO", 10))
		return_value = _readYbSortInfo();
	else
	{
		elog(ERROR, "badly formatted node string \"%.32s\"...", token);
		return_value = NULL;	/* keep compiler quiet */
	}
>>>>>>> bc662ba7050

	elog(ERROR, "badly formatted node string \"%.32s\"...", token);
	return NULL;				/* keep compiler quiet */
}


/*
 * readDatum
 *
 * Given a string representation of a constant, recreate the appropriate
 * Datum.  The string representation embeds length info, but not byValue,
 * so we must be told that.
 */
Datum
readDatum(bool typbyval)
{
	Size		length;
	int			tokenLength;
	const char *token;
	Datum		res;
	char	   *s;

	/*
	 * read the actual length of the value
	 */
	token = pg_strtok(&tokenLength);
	length = atoui(token);

	token = pg_strtok(&tokenLength);	/* read the '[' */
	if (token == NULL || token[0] != '[')
		elog(ERROR, "expected \"[\" to start datum, but got \"%s\"; length = %zu",
			 token ? token : "[NULL]", length);

	if (typbyval)
	{
		if (length > (Size) sizeof(Datum))
			elog(ERROR, "byval datum but length = %zu", length);
		res = (Datum) 0;
		s = (char *) (&res);
		for (Size i = 0; i < (Size) sizeof(Datum); i++)
		{
			token = pg_strtok(&tokenLength);
			s[i] = (char) atoi(token);
		}
	}
	else if (length <= 0)
		res = (Datum) 0;
	else
	{
		s = (char *) palloc(length);
		for (Size i = 0; i < length; i++)
		{
			token = pg_strtok(&tokenLength);
			s[i] = (char) atoi(token);
		}
		res = PointerGetDatum(s);
	}

	token = pg_strtok(&tokenLength);	/* read the ']' */
	if (token == NULL || token[0] != ']')
		elog(ERROR, "expected \"]\" to end datum, but got \"%s\"; length = %zu",
			 token ? token : "[NULL]", length);

	return res;
}

/*
 * common implementation for scalar-array-reading functions
 *
 * The data format is either "<>" for a NULL pointer (in which case numCols
 * is ignored) or "(item item item)" where the number of items must equal
 * numCols.  The convfunc must be okay with stopping at whitespace or a
 * right parenthesis, since pg_strtok won't null-terminate the token.
 */
#define READ_SCALAR_ARRAY(fnname, datatype, convfunc) \
datatype * \
fnname(int numCols) \
{ \
	datatype   *vals; \
	READ_TEMP_LOCALS(); \
	token = pg_strtok(&length); \
	if (token == NULL) \
		elog(ERROR, "incomplete scalar array"); \
	if (length == 0) \
		return NULL;			/* it was "<>", so return NULL pointer */ \
	if (length != 1 || token[0] != '(') \
		elog(ERROR, "unrecognized token: \"%.*s\"", length, token); \
	vals = (datatype *) palloc(numCols * sizeof(datatype)); \
	for (int i = 0; i < numCols; i++) \
	{ \
		token = pg_strtok(&length); \
		if (token == NULL || token[0] == ')') \
			elog(ERROR, "incomplete scalar array"); \
		vals[i] = convfunc(token); \
	} \
	token = pg_strtok(&length); \
	if (token == NULL || length != 1 || token[0] != ')') \
		elog(ERROR, "incomplete scalar array"); \
	return vals; \
}

/*
 * Note: these functions are exported in nodes.h for possible use by
 * extensions, so don't mess too much with their names or API.
 */
READ_SCALAR_ARRAY(readAttrNumberCols, int16, atoi)
READ_SCALAR_ARRAY(readOidCols, Oid, atooid)
/* outfuncs.c has writeIndexCols, but we don't yet need that here */
/* READ_SCALAR_ARRAY(readIndexCols, Index, atoui) */
READ_SCALAR_ARRAY(readIntCols, int, atoi)
READ_SCALAR_ARRAY(readBoolCols, bool, strtobool)
