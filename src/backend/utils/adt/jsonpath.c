/*-------------------------------------------------------------------------
 *
 * jsonpath.c
 *
 * Copyright (c) 2017, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	src/backend/utils/adt/jsonpath.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "catalog/pg_type.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/json.h"
#include "utils/jsonpath.h"

typedef struct JsonPathExecContext
{
	List	   *vars;
	bool		lax;
} JsonPathExecContext;

static JsonPathExecResult recursiveExecute(JsonPathExecContext *cxt,
										   JsonPathItem *jsp, JsonbValue *jb,
										   List **found);

/*****************************INPUT/OUTPUT************************************/
static int
flattenJsonPathParseItem(StringInfo buf, JsonPathParseItem *item,
						 bool forbiddenRoot)
{
	int32	pos = buf->len - VARHDRSZ; /* position from begining of jsonpath data */
	int32	chld, next;

	check_stack_depth();

	appendStringInfoChar(buf, (char)(item->type));
	alignStringInfoInt(buf);

	next = (item->next) ? buf->len : 0;
	appendBinaryStringInfo(buf, (char*)&next /* fake value */, sizeof(next));

	switch(item->type)
	{
		case jpiString:
		case jpiVariable:
			/* scalars aren't checked during grammar parse */
			if (item->next != NULL)
				elog(ERROR,"Scalar could not be a part of path");
		case jpiKey:
			appendBinaryStringInfo(buf, (char*)&item->string.len, sizeof(item->string.len));
			appendBinaryStringInfo(buf, item->string.val, item->string.len);
			appendStringInfoChar(buf, '\0');
			break;
		case jpiNumeric:
			if (item->next != NULL)
				elog(ERROR,"Scalar could not be a part of path");
			appendBinaryStringInfo(buf, (char*)item->numeric, VARSIZE(item->numeric));
			break;
		case jpiBool:
			if (item->next != NULL)
				elog(ERROR,"Scalar could not be a part of path");
			appendBinaryStringInfo(buf, (char*)&item->boolean, sizeof(item->boolean));
			break;
		case jpiAnd:
		case jpiOr:
		case jpiEqual:
		case jpiNotEqual:
		case jpiLess:
		case jpiGreater:
		case jpiLessOrEqual:
		case jpiGreaterOrEqual:
		case jpiAdd:
		case jpiSub:
		case jpiMul:
		case jpiDiv:
		case jpiMod:
			{
				int32	left, right;

				forbiddenRoot = true;
				left = buf->len;
				appendBinaryStringInfo(buf, (char*)&left /* fake value */, sizeof(left));
				right = buf->len;
				appendBinaryStringInfo(buf, (char*)&right /* fake value */, sizeof(right));

				chld = flattenJsonPathParseItem(buf, item->args.left, true);
				*(int32*)(buf->data + left) = chld;
				chld = flattenJsonPathParseItem(buf, item->args.right, true);
				*(int32*)(buf->data + right) = chld;
			}
			break;
		case jpiFilter:
		case jpiIsUnknown:
		case jpiNot:
		case jpiPlus:
		case jpiMinus:
		case jpiExists:
			{
				int32 arg;

				arg = buf->len;
				appendBinaryStringInfo(buf, (char*)&arg /* fake value */, sizeof(arg));

				forbiddenRoot = true;
				chld = flattenJsonPathParseItem(buf, item->arg, true);
				*(int32*)(buf->data + arg) = chld;
			}
			break;
		case jpiNull:
			if (item->next != NULL)
				elog(ERROR,"Scalar could not be a part of path");
			break;
		case jpiRoot:
			if (forbiddenRoot)
				elog(ERROR,"Root is not allowed in expression");
		case jpiAnyArray:
		case jpiAnyKey:
		case jpiCurrent:
			break;
		case jpiIndexArray:
			appendBinaryStringInfo(buf,
								   (char*)&item->array.nelems,
								   sizeof(item->array.nelems));
			appendBinaryStringInfo(buf,
								   (char*)item->array.elems,
								   item->array.nelems * sizeof(item->array.elems[0]));
			break;
		case jpiAny:
			appendBinaryStringInfo(buf,
								   (char*)&item->anybounds.first,
								   sizeof(item->anybounds.first));
			appendBinaryStringInfo(buf,
								   (char*)&item->anybounds.last,
								   sizeof(item->anybounds.last));
			break;
		default:
			elog(ERROR, "1Unknown type: %d", item->type);
	}

	if (item->next)
		*(int32*)(buf->data + next) =
			flattenJsonPathParseItem(buf, item->next, forbiddenRoot);

	return  pos;
}

Datum
jsonpath_in(PG_FUNCTION_ARGS)
{
	char				*in = PG_GETARG_CSTRING(0);
	int32				len = strlen(in);
	JsonPathParseItem	*jsonpath = parsejsonpath(in, len);
	JsonPath				*res;
	StringInfoData		buf;

	initStringInfo(&buf);
	enlargeStringInfo(&buf, 4 * len /* estimation */);

	appendStringInfoSpaces(&buf, VARHDRSZ);

	if (jsonpath != NULL)
	{
		flattenJsonPathParseItem(&buf, jsonpath, false);

		res = (JsonPath*)buf.data;
		SET_VARSIZE(res, buf.len);

		PG_RETURN_JSONPATH(res);
	}

	PG_RETURN_NULL();
}

static void
printOperation(StringInfo buf, JsonPathItemType type)
{
	switch(type)
	{
		case jpiAnd:
			appendBinaryStringInfo(buf, " && ", 4); break;
		case jpiOr:
			appendBinaryStringInfo(buf, " || ", 4); break;
		case jpiEqual:
			appendBinaryStringInfo(buf, " == ", 4); break;
		case jpiNotEqual:
			appendBinaryStringInfo(buf, " <> ", 4); break;
		case jpiLess:
			appendBinaryStringInfo(buf, " < ", 3); break;
		case jpiGreater:
			appendBinaryStringInfo(buf, " > ", 3); break;
		case jpiLessOrEqual:
			appendBinaryStringInfo(buf, " <= ", 4); break;
		case jpiGreaterOrEqual:
			appendBinaryStringInfo(buf, " >= ", 4); break;
		case jpiAdd:
			appendBinaryStringInfo(buf, " + ", 3); break;
		case jpiSub:
			appendBinaryStringInfo(buf, " - ", 3); break;
		case jpiMul:
			appendBinaryStringInfo(buf, " * ", 3); break;
		case jpiDiv:
			appendBinaryStringInfo(buf, " / ", 3); break;
		case jpiMod:
			appendBinaryStringInfo(buf, " % ", 3); break;
		default:
			elog(ERROR, "2Unknown type: %d", type);
	}
}

static int
operationPriority(JsonPathItemType op)
{
	switch (op)
	{
		case jpiOr:
			return 0;
		case jpiAnd:
			return 1;
		case jpiEqual:
		case jpiNotEqual:
		case jpiLess:
		case jpiGreater:
		case jpiLessOrEqual:
		case jpiGreaterOrEqual:
			return 2;
		case jpiAdd:
		case jpiSub:
			return 3;
		case jpiMul:
		case jpiDiv:
		case jpiMod:
			return 4;
		default:
			return 5;
	}
}

static void
printJsonPathItem(StringInfo buf, JsonPathItem *v, bool inKey, bool printBracketes)
{
	JsonPathItem	elem;
	int				i;

	check_stack_depth();

	switch(v->type)
	{
		case jpiNull:
			appendStringInfoString(buf, "null");
			break;
		case jpiKey:
			if (inKey)
				appendStringInfoChar(buf, '.');
			escape_json(buf, jspGetString(v, NULL));
			break;
		case jpiString:
			escape_json(buf, jspGetString(v, NULL));
			break;
		case jpiVariable:
			appendStringInfoChar(buf, '$');
			escape_json(buf, jspGetString(v, NULL));
			break;
		case jpiNumeric:
			appendStringInfoString(buf,
								   DatumGetCString(DirectFunctionCall1(numeric_out,
								   PointerGetDatum(jspGetNumeric(v)))));
			break;
		case jpiBool:
			if (jspGetBool(v))
				appendBinaryStringInfo(buf, "true", 4);
			else
				appendBinaryStringInfo(buf, "false", 5);
			break;
		case jpiAnd:
		case jpiOr:
		case jpiEqual:
		case jpiNotEqual:
		case jpiLess:
		case jpiGreater:
		case jpiLessOrEqual:
		case jpiGreaterOrEqual:
		case jpiAdd:
		case jpiSub:
		case jpiMul:
		case jpiDiv:
		case jpiMod:
			if (printBracketes)
				appendStringInfoChar(buf, '(');
			jspGetLeftArg(v, &elem);
			printJsonPathItem(buf, &elem, false,
							  operationPriority(elem.type) <=
							  operationPriority(v->type));
			printOperation(buf, v->type);
			jspGetRightArg(v, &elem);
			printJsonPathItem(buf, &elem, false,
							  operationPriority(elem.type) <=
							  operationPriority(v->type));
			if (printBracketes)
				appendStringInfoChar(buf, ')');
			break;
		case jpiFilter:
			appendBinaryStringInfo(buf, "?(", 2);
			jspGetArg(v, &elem);
			printJsonPathItem(buf, &elem, false, false);
			appendStringInfoChar(buf, ')');
			break;
		case jpiNot:
			appendBinaryStringInfo(buf, "!(", 2);
			jspGetArg(v, &elem);
			printJsonPathItem(buf, &elem, false, false);
			appendStringInfoChar(buf, ')');
			break;
		case jpiIsUnknown:
			appendStringInfoChar(buf, '(');
			jspGetArg(v, &elem);
			printJsonPathItem(buf, &elem, false, false);
			appendBinaryStringInfo(buf, ") is unknown", 12);
			break;
		case jpiExists:
			appendBinaryStringInfo(buf,"exists (", 8);
			jspGetArg(v, &elem);
			printJsonPathItem(buf, &elem, false, false);
			appendStringInfoChar(buf, ')');
			break;
		case jpiCurrent:
			Assert(!inKey);
			appendStringInfoChar(buf, '@');
			break;
		case jpiRoot:
			Assert(!inKey);
			appendStringInfoChar(buf, '$');
			break;
		case jpiAnyArray:
			if (inKey)
				appendStringInfoChar(buf, '.');
			appendBinaryStringInfo(buf, "[*]", 3);
			break;
		case jpiAnyKey:
			if (inKey)
				appendStringInfoChar(buf, '.');
			appendStringInfoChar(buf, '*');
			break;
		case jpiIndexArray:
			if (inKey)
				appendStringInfoChar(buf, '.');
			appendStringInfoChar(buf, '[');
			for(i = 0; i< v->array.nelems; i++)
			{
				if (i)
					appendStringInfoChar(buf, ',');
				appendStringInfo(buf, "%d", v->array.elems[i]);
			}
			appendStringInfoChar(buf, ']');
			break;
		case jpiAny:
			if (inKey)
				appendStringInfoChar(buf, '.');

			if (v->anybounds.first == 0 &&
					v->anybounds.last == PG_UINT32_MAX)
				appendBinaryStringInfo(buf, "**", 2);
			else if (v->anybounds.first == 0)
				appendStringInfo(buf, "**{,%u}", v->anybounds.last);
			else if (v->anybounds.last == PG_UINT32_MAX)
				appendStringInfo(buf, "**{%u,}", v->anybounds.first);
			else if (v->anybounds.first == v->anybounds.last)
				appendStringInfo(buf, "**{%u}", v->anybounds.first);
			else
				appendStringInfo(buf, "**{%u,%u}", v->anybounds.first,
												   v->anybounds.last);
			break;
		default:
			elog(ERROR, "Unknown JsonPathItem type: %d", v->type);
	}

	if (jspGetNext(v, &elem))
		printJsonPathItem(buf, &elem, true, true);
}

Datum
jsonpath_out(PG_FUNCTION_ARGS)
{
	JsonPath			*in = PG_GETARG_JSONPATH(0);
	StringInfoData	buf;
	JsonPathItem		v;

	initStringInfo(&buf);
	enlargeStringInfo(&buf, VARSIZE(in) /* estimation */);

	jspInit(&v, in);
	printJsonPathItem(&buf, &v, false, true);

	PG_RETURN_CSTRING(buf.data);
}

/********************Support functions for JsonPath****************************/

#define read_byte(v, b, p) do {			\
	(v) = *(uint8*)((b) + (p));			\
	(p) += 1;							\
} while(0)								\

#define read_int32(v, b, p) do {		\
	(v) = *(uint32*)((b) + (p));		\
	(p) += sizeof(int32);				\
} while(0)								\

#define read_int32_n(v, b, p, n) do {	\
	(v) = (int32*)((b) + (p));			\
	(p) += sizeof(int32) * (n);			\
} while(0)								\

void
jspInit(JsonPathItem *v, JsonPath *js)
{
	jspInitByBuffer(v, VARDATA(js), 0);
}

void
jspInitByBuffer(JsonPathItem *v, char *base, int32 pos)
{
	v->base = base;

	read_byte(v->type, base, pos);

	switch(INTALIGN(pos) - pos)
	{
		case 3: pos++;
		case 2: pos++;
		case 1: pos++;
		default: break;
	}

	read_int32(v->nextPos, base, pos);

	switch(v->type)
	{
		case jpiNull:
		case jpiRoot:
		case jpiCurrent:
		case jpiAnyArray:
		case jpiAnyKey:
			break;
		case jpiKey:
		case jpiString:
		case jpiVariable:
			read_int32(v->value.datalen, base, pos);
			/* follow next */
		case jpiNumeric:
		case jpiBool:
			v->value.data = base + pos;
			break;
		case jpiAnd:
		case jpiOr:
		case jpiAdd:
		case jpiSub:
		case jpiMul:
		case jpiDiv:
		case jpiMod:
		case jpiEqual:
		case jpiNotEqual:
		case jpiLess:
		case jpiGreater:
		case jpiLessOrEqual:
		case jpiGreaterOrEqual:
			read_int32(v->args.left, base, pos);
			read_int32(v->args.right, base, pos);
			break;
		case jpiNot:
		case jpiExists:
		case jpiIsUnknown:
		case jpiMinus:
		case jpiPlus:
		case jpiFilter:
			read_int32(v->arg, base, pos);
			break;
		case jpiIndexArray:
			read_int32(v->array.nelems, base, pos);
			read_int32_n(v->array.elems, base, pos, v->array.nelems);
			break;
		case jpiAny:
			read_int32(v->anybounds.first, base, pos);
			read_int32(v->anybounds.last, base, pos);
			break;
		default:
			elog(ERROR, "3Unknown type: %d", v->type);
	}
}

void
jspGetArg(JsonPathItem *v, JsonPathItem *a)
{
	Assert(
		v->type == jpiFilter ||
		v->type == jpiNot ||
		v->type == jpiIsUnknown ||
		v->type == jpiExists ||
		v->type == jpiPlus ||
		v->type == jpiMinus
	);

	jspInitByBuffer(a, v->base, v->arg);
}

bool
jspGetNext(JsonPathItem *v, JsonPathItem *a)
{
	if (v->nextPos > 0)
	{
		Assert(
			v->type == jpiKey ||
			v->type == jpiAny ||
			v->type == jpiAnyArray ||
			v->type == jpiAnyKey ||
			v->type == jpiIndexArray ||
			v->type == jpiFilter ||
			v->type == jpiCurrent ||
			v->type == jpiExists ||
			v->type == jpiRoot
		);

		if (a)
			jspInitByBuffer(a, v->base, v->nextPos);
		return true;
	}

	return false;
}

void
jspGetLeftArg(JsonPathItem *v, JsonPathItem *a)
{
	Assert(
		v->type == jpiAnd ||
		v->type == jpiOr ||
		v->type == jpiEqual ||
		v->type == jpiNotEqual ||
		v->type == jpiLess ||
		v->type == jpiGreater ||
		v->type == jpiLessOrEqual ||
		v->type == jpiGreaterOrEqual ||
		v->type == jpiAdd ||
		v->type == jpiSub ||
		v->type == jpiMul ||
		v->type == jpiDiv ||
		v->type == jpiMod
	);

	jspInitByBuffer(a, v->base, v->args.left);
}

void
jspGetRightArg(JsonPathItem *v, JsonPathItem *a)
{
	Assert(
		v->type == jpiAnd ||
		v->type == jpiOr ||
		v->type == jpiEqual ||
		v->type == jpiNotEqual ||
		v->type == jpiLess ||
		v->type == jpiGreater ||
		v->type == jpiLessOrEqual ||
		v->type == jpiGreaterOrEqual ||
		v->type == jpiAdd ||
		v->type == jpiSub ||
		v->type == jpiMul ||
		v->type == jpiDiv ||
		v->type == jpiMod
	);

	jspInitByBuffer(a, v->base, v->args.right);
}

bool
jspGetBool(JsonPathItem *v)
{
	Assert(v->type == jpiBool);

	return (bool)*v->value.data;
}

Numeric
jspGetNumeric(JsonPathItem *v)
{
	Assert(v->type == jpiNumeric);

	return (Numeric)v->value.data;
}

char*
jspGetString(JsonPathItem *v, int32 *len)
{
	Assert(
		v->type == jpiKey ||
		v->type == jpiString ||
		v->type == jpiVariable
	);

	if (len)
		*len = v->value.datalen;
	return v->value.data;
}

/********************Execute functions for JsonPath***************************/

static void
computeJsonPathVariable(JsonPathItem *variable, List *vars, JsonbValue *value)
{
	ListCell			*cell;
	JsonPathVariable	*var = NULL;
	bool				isNull;
	Datum				computedValue;
	char				*varName;
	int					varNameLength;

	Assert(variable->type == jpiVariable);
	varName = jspGetString(variable, &varNameLength);

	foreach(cell, vars)
	{
		var = (JsonPathVariable*)lfirst(cell);

		if (strncmp(varName, VARDATA_ANY(var->varName),
				Min(varNameLength, VARSIZE_ANY_EXHDR(var->varName))) == 0)
			break;

		var = NULL;
	}

	if (var == NULL)
		elog(ERROR, "could not find '%s' passed variable",
					pnstrdup(varName, varNameLength));

	computedValue = var->cb(var->cb_arg, &isNull);

	if (isNull)
	{
		value->type = jbvNull;
		return;
	}

	switch(var->typid)
	{
		case BOOLOID:
			value->type = jbvBool;
			value->val.boolean = DatumGetBool(computedValue);
			break;
		case NUMERICOID:
			value->type = jbvNumeric;
			value->val.numeric = DatumGetNumeric(computedValue);
			break;
		case TEXTOID:
		case VARCHAROID:
			value->type = jbvString;
			value->val.string.val = VARDATA_ANY(computedValue);
			value->val.string.len = VARSIZE_ANY_EXHDR(computedValue);
			break;
		case JSONBOID:
			{
				Jsonb	   *jb = DatumGetJsonb(computedValue);

				if (JB_ROOT_IS_SCALAR(jb))
					JsonbExtractScalar(&jb->root, value);
				else
				{
					value->type = jbvBinary;
					value->val.binary.data = &jb->root;
					value->val.binary.len = VARSIZE_ANY_EXHDR(jb);
				}
			}
			break;
		default:
			elog(ERROR, "only bool, numeric and text types could be casted to supported jsonpath types");
	}
}

static void
computeJsonPathItem(JsonPathExecContext *cxt, JsonPathItem *item, JsonbValue *value)
{
	switch(item->type)
	{
		case jpiNull:
			value->type = jbvNull;
			break;
		case jpiBool:
			value->type = jbvBool;
			value->val.boolean = jspGetBool(item);
			break;
		case jpiNumeric:
			value->type = jbvNumeric;
			value->val.numeric = jspGetNumeric(item);
			break;
		case jpiString:
			value->type = jbvString;
			value->val.string.val = jspGetString(item, &value->val.string.len);
			break;
		case jpiVariable:
			computeJsonPathVariable(item, cxt->vars, value);
			break;
		default:
			elog(ERROR, "Wrong type");
	}
}


#define jbvScalar jbvBinary
static int
JsonbType(JsonbValue *jb)
{
	int type = jb->type;

	if (jb->type == jbvBinary)
	{
		JsonbContainer	*jbc = jb->val.binary.data;

		if (jbc->header & JB_FSCALAR)
			type = jbvScalar;
		else if (jbc->header & JB_FOBJECT)
			type = jbvObject;
		else if (jbc->header & JB_FARRAY)
			type = jbvArray;
		else
			elog(ERROR, "Unknown container type: 0x%08x", jbc->header);
	}

	return type;
}

static int
compareNumeric(Numeric a, Numeric b)
{
	return	DatumGetInt32(
				DirectFunctionCall2(
					numeric_cmp,
					PointerGetDatum(a),
					PointerGetDatum(b)
				)
			);
}

static bool
checkScalarEquality(JsonbValue *jb1, JsonbValue *jb2)
{
	switch (jb1->type)
	{
		case jbvNull:
			return true;
		case jbvString:
			return (jb1->val.string.len == jb2->val.string.len &&
					memcmp(jb2->val.string.val, jb1->val.string.val,
						   jb1->val.string.len) == 0);
		case jbvBool:
			return (jb2->val.boolean == jb1->val.boolean);
		case jbvNumeric:
			return (compareNumeric(jb1->val.numeric, jb2->val.numeric) == 0);
		default:
			elog(ERROR,"Wrong state");
			return false;
	}
}

static JsonPathExecResult
checkEquality(JsonbValue *jb1, JsonbValue *jb2, bool not)
{
	bool	eq;

	if (jb1->type != jb2->type)
	{
		if (jb1->type == jbvNull || jb2->type == jbvNull)
			return jperNotFound;

		return jperError;
	}

	if (jb1->type == jbvBinary)
		return jperError;
	/*
		eq = compareJsonbContainers(jb1->val.binary.data,
									jb2->val.binary.data) == 0;
	*/
	else
		eq = checkScalarEquality(jb1, jb2);

	return !!not ^ !!eq ? jperOk : jperNotFound;
}

static JsonPathExecResult
makeCompare(int32 op, JsonbValue *jb1, JsonbValue *jb2)
{
	int			cmp;
	bool		res;

	if (jb1->type != jb2->type)
	{
		if (jb1->type == jbvNull || jb2->type == jbvNull)
			return jperNotFound;

		return jperError;
	}

	switch (jb1->type)
	{
		case jbvNumeric:
			cmp = compareNumeric(jb1->val.numeric, jb2->val.numeric);
			break;
		/*
		case jbvString:
			cmp = varstr_cmp(jb1->val.string.val, jb1->val.string.len,
							 jb2->val.string.val, jb2->val.string.len,
							 collationId);
			break;
		*/
		default:
			return jperError;
	}

	switch (op)
	{
		case jpiEqual:
			res = (cmp == 0);
			break;
		case jpiNotEqual:
			res = (cmp != 0);
			break;
		case jpiLess:
			res = (cmp < 0);
			break;
		case jpiGreater:
			res = (cmp > 0);
			break;
		case jpiLessOrEqual:
			res =  (cmp <= 0);
			break;
		case jpiGreaterOrEqual:
			res = (cmp >= 0);
			break;
		default:
			elog(ERROR, "Unknown operation");
			return jperError;
	}

	return res ? jperOk : jperNotFound;
}

static JsonPathExecResult
executeExpr(JsonPathExecContext *cxt, JsonPathItem *jsp, JsonbValue *jb)
{
	JsonPathExecResult res;
	JsonPathItem elem;
	List	   *lseq = NIL;
	List	   *rseq = NIL;
	ListCell   *llc;
	ListCell   *rlc;
	bool		error = false;
	bool		found = false;

	jspGetLeftArg(jsp, &elem);
	res = recursiveExecute(cxt, &elem, jb, &lseq);
	if (res != jperOk)
		return res;

	jspGetRightArg(jsp, &elem);
	res = recursiveExecute(cxt, &elem, jb, &rseq);
	if (res != jperOk)
		return res;

	foreach(llc, lseq)
	{
		JsonbValue *lval = lfirst(llc);

		foreach(rlc, rseq)
		{
			JsonbValue *rval = lfirst(rlc);

			switch (jsp->type)
			{
				case jpiEqual:
					res = checkEquality(lval, rval, false);
					break;
				case jpiNotEqual:
					res = checkEquality(lval, rval, true);
					break;
				case jpiLess:
				case jpiGreater:
				case jpiLessOrEqual:
				case jpiGreaterOrEqual:
					res = makeCompare(jsp->type, lval, rval);
					break;
				default:
					elog(ERROR, "Unknown operation");
			}

			if (res == jperOk)
			{
				if (cxt->lax)
					return jperOk;

				found = true;
			}
			else if (res == jperError)
			{
				if (!cxt->lax)
					return jperError;

				error = true;
			}
		}
	}

	if (found) /* possible only in strict mode */
		return jperOk;

	if (error) /* possible only in non-strict mode */
		return jperError;

	return jperNotFound;
}

static JsonPathExecResult
executeArithmExpr(JsonPathExecContext *cxt, JsonPathItem *jsp, JsonbValue *jb,
				  List **found)
{
	JsonPathExecResult jper;
	JsonPathItem elem;
	List	   *lseq = NIL;
	List	   *rseq = NIL;
	JsonbValue *lval;
	JsonbValue *rval;
	JsonbValue	lvalbuf;
	JsonbValue	rvalbuf;
	Datum		ldatum;
	Datum		rdatum;
	Datum		res;

	jspGetLeftArg(jsp, &elem);
	jper = recursiveExecute(cxt, &elem, jb, &lseq);
	if (jper == jperOk)
	{
		jspGetRightArg(jsp, &elem);
		jper = recursiveExecute(cxt, &elem, jb, &rseq);
	}

	if (jper != jperOk || list_length(lseq) != 1 || list_length(rseq) != 1)
		return jperError; /* ERRCODE_SINGLETON_JSON_ITEM_REQUIRED; */

	lval = linitial(lseq);
	rval = linitial(rseq);

	if (JsonbType(lval) == jbvScalar)
		lval = JsonbExtractScalar(lval->val.binary.data, &lvalbuf);

	if (lval->type != jbvNumeric)
		return jperError; /* ERRCODE_SINGLETON_JSON_ITEM_REQUIRED; */

	if (JsonbType(rval) == jbvScalar)
		rval = JsonbExtractScalar(rval->val.binary.data, &rvalbuf);

	if (rval->type != jbvNumeric)
		return jperError; /* ERRCODE_SINGLETON_JSON_ITEM_REQUIRED; */

	if (!found)
		return jperOk;

	ldatum = NumericGetDatum(lval->val.numeric);
	rdatum = NumericGetDatum(rval->val.numeric);

	switch (jsp->type)
	{
		case jpiAdd:
			res = DirectFunctionCall2(numeric_add, ldatum, rdatum);
			break;
		case jpiSub:
			res = DirectFunctionCall2(numeric_sub, ldatum, rdatum);
			break;
		case jpiMul:
			res = DirectFunctionCall2(numeric_mul, ldatum, rdatum);
			break;
		case jpiDiv:
			res = DirectFunctionCall2(numeric_div, ldatum, rdatum);
			break;
		case jpiMod:
			res = DirectFunctionCall2(numeric_mod, ldatum, rdatum);
			break;
		default:
			elog(ERROR, "unknown jsonpath arithmetic operation %d", jsp->type);
	}

	lval = palloc(sizeof(*lval));
	lval->type = jbvNumeric;
	lval->val.numeric = DatumGetNumeric(res);

	*found = lappend(*found, lval);

	return jperOk;
}

static JsonbValue*
copyJsonbValue(JsonbValue *src)
{
	JsonbValue	*dst = palloc(sizeof(*dst));

	*dst = *src;

	return dst;
}

static JsonPathExecResult
recursiveAny(JsonPathExecContext *cxt, JsonPathItem *jsp, JsonbValue *jb,
			 List **found, uint32 level, uint32 first, uint32 last)
{
	JsonPathExecResult	res = jperNotFound;
	JsonbIterator		*it;
	int32				r;
	JsonbValue			v;

	check_stack_depth();

	if (level > last)
		return res;

	it = JsonbIteratorInit(jb->val.binary.data);

	while((r = JsonbIteratorNext(&it, &v, true)) != WJB_DONE)
	{
		if (r == WJB_KEY)
		{
			r = JsonbIteratorNext(&it, &v, true);
			Assert(r == WJB_VALUE);
		}

		if (r == WJB_VALUE || r == WJB_ELEM)
		{

			if (level >= first)
			{
				/* check expression */
				if (jsp)
				{
					res = recursiveExecute(cxt, jsp, &v, found);
					if (res == jperOk && !found)
						break;
				}
				else
				{
					res = jperOk;
					if (!found)
						break;
					*found = lappend(*found, copyJsonbValue(&v));
				}
			}

			if (level < last && v.type == jbvBinary)
			{
				res = recursiveAny(cxt, jsp, &v, found, level + 1, first, last);

				if (res == jperOk && found == NULL)
					break;
			}
		}
	}

	return res;
}

static JsonPathExecResult
recursiveExecute(JsonPathExecContext *cxt, JsonPathItem *jsp, JsonbValue *jb,
				 List **found)
{
	JsonPathItem		elem;
	JsonPathExecResult	res = jperNotFound;

	check_stack_depth();

	switch(jsp->type) {
		case jpiAnd:
			jspGetLeftArg(jsp, &elem);
			res = recursiveExecute(cxt, &elem, jb, NULL);
			if (res != jperNotFound)
			{
				JsonPathExecResult res2;

				jspGetRightArg(jsp, &elem);
				res2 = recursiveExecute(cxt, &elem, jb, NULL);

				res = res2 == jperOk ? res : res2;
			}
			break;
		case jpiOr:
			jspGetLeftArg(jsp, &elem);
			res = recursiveExecute(cxt, &elem, jb, NULL);
			if (res != jperOk)
			{
				JsonPathExecResult res2;

				jspGetRightArg(jsp, &elem);
				res2 = recursiveExecute(cxt, &elem, jb, NULL);

				res = res2 == jperNotFound ? res : res2;
			}
			break;
		case jpiNot:
			jspGetArg(jsp, &elem);
			switch((res = recursiveExecute(cxt, &elem, jb, NULL)))
			{
				case jperOk:
					res = jperNotFound;
					break;
				case jperNotFound:
					res = jperOk;
					break;
				default:
					break;
			}
			break;
		case jpiIsUnknown:
			jspGetArg(jsp, &elem);
			res = recursiveExecute(cxt, &elem, jb, NULL);
			res = res == jperError ? jperOk : jperNotFound;
			break;
		case jpiKey:
			if (JsonbType(jb) == jbvObject)
			{
				JsonbValue	*v, key;

				key.type = jbvString;
				key.val.string.val = jspGetString(jsp, &key.val.string.len);

				v = findJsonbValueFromContainer(jb->val.binary.data, JB_FOBJECT, &key);

				if (v != NULL)
				{
					if (jspGetNext(jsp, &elem))
					{
						res = recursiveExecute(cxt, &elem, v, found);
						pfree(v);
					}
					else
					{
						res = jperOk;
						if (found)
							*found = lappend(*found, v);
						else
							pfree(v);
					}
				}
			}
			break;
		case jpiCurrent:
			if (!jspGetNext(jsp, &elem))
			{
				res = jperOk;
				if (found)
				{
					JsonbValue *v;

					if (JsonbType(jb) == jbvScalar)
						v = JsonbExtractScalar(jb->val.binary.data,
											   palloc(sizeof(*v)));
					else
						v = copyJsonbValue(jb); /* FIXME */

					*found = lappend(*found, v);
				}
			}
			else if (JsonbType(jb) == jbvScalar)
			{
				JsonbValue	v;

				JsonbExtractScalar(jb->val.binary.data, &v);

				res = recursiveExecute(cxt, &elem, &v, found);
			}
			else
			{
				res = recursiveExecute(cxt, &elem, jb, found);
			}
			break;
		case jpiAnyArray:
			if (JsonbType(jb) == jbvArray)
			{
				JsonbIterator	*it;
				int32			r;
				JsonbValue		v;
				bool			hasNext;

				hasNext = jspGetNext(jsp, &elem);
				it = JsonbIteratorInit(jb->val.binary.data);

				while((r = JsonbIteratorNext(&it, &v, true)) != WJB_DONE)
				{
					if (r == WJB_ELEM)
					{
						if (hasNext == true)
						{
							res = recursiveExecute(cxt, &elem, &v, found);

							if (res == jperError)
								break;

							if (res == jperOk && found == NULL)
								break;
						}
						else
						{
							res = jperOk;

							if (found == NULL)
								break;

							*found = lappend(*found, copyJsonbValue(&v));
						}
					}
				}
			}
			break;

		case jpiIndexArray:
			if (JsonbType(jb) == jbvArray)
			{
				JsonbValue		*v;
				bool			hasNext;
				int				i;

				hasNext = jspGetNext(jsp, &elem);

				for(i=0; i<jsp->array.nelems; i++)
				{
					v = getIthJsonbValueFromContainer(jb->val.binary.data,
													  jsp->array.elems[i]);

					if (v == NULL)
						continue;

					if (hasNext == true)
					{
						res = recursiveExecute(cxt, &elem, v, found);

						if (res == jperError || found == NULL)
							break;

						if (res == jperOk && found == NULL)
								break;
					}
					else
					{
						res = jperOk;

						if (found == NULL)
							break;

						*found = lappend(*found, v);
					}
				}
			}
			break;
		case jpiAnyKey:
			if (JsonbType(jb) == jbvObject)
			{
				JsonbIterator	*it;
				int32			r;
				JsonbValue		v;
				bool			hasNext;

				hasNext = jspGetNext(jsp, &elem);
				it = JsonbIteratorInit(jb->val.binary.data);

				while((r = JsonbIteratorNext(&it, &v, true)) != WJB_DONE)
				{
					if (r == WJB_VALUE)
					{
						if (hasNext == true)
						{
							res = recursiveExecute(cxt, &elem, &v, found);

							if (res == jperError)
								break;

							if (res == jperOk && found == NULL)
								break;
						}
						else
						{
							res = jperOk;

							if (found == NULL)
								break;

							*found = lappend(*found, copyJsonbValue(&v));
						}
					}
				}
			}
			break;
		case jpiEqual:
		case jpiNotEqual:
		case jpiLess:
		case jpiGreater:
		case jpiLessOrEqual:
		case jpiGreaterOrEqual:
			res = executeExpr(cxt, jsp, jb);
			break;
		case jpiAdd:
		case jpiSub:
		case jpiMul:
		case jpiDiv:
		case jpiMod:
			res = executeArithmExpr(cxt, jsp, jb, found);
			break;
		case jpiRoot:
			if (jspGetNext(jsp, &elem))
			{
				res = recursiveExecute(cxt, &elem, jb, found);
			}
			else
			{
				res = jperOk;
				if (found)
					*found = lappend(*found, copyJsonbValue(jb));
			}

			break;
		case jpiFilter:
			jspGetArg(jsp, &elem);
			res = recursiveExecute(cxt, &elem, jb, NULL);
			if (res != jperOk)
				res = jperNotFound;
			else if (jspGetNext(jsp, &elem))
				res = recursiveExecute(cxt, &elem, jb, found);
			else if (found)
				*found = lappend(*found, copyJsonbValue(jb));
			break;
		case jpiAny:
		{
			bool hasNext = jspGetNext(jsp, &elem);

			/* first try without any intermediate steps */
			if (jsp->anybounds.first == 0)
			{
				if (hasNext)
				{
					res = recursiveExecute(cxt, &elem, jb, found);
					if (res == jperOk && !found)
						break;
				}
				else
				{
					res = jperOk;
					if (!found)
						break;
					*found = lappend(*found, copyJsonbValue(jb));
				}
			}

			if (jb->type == jbvBinary)
				res = recursiveAny(cxt, hasNext ? &elem : NULL, jb, found,
								   1,
								   jsp->anybounds.first,
								   jsp->anybounds.last);
			break;
		}
		case jpiExists:
			jspGetArg(jsp, &elem);
			res = recursiveExecute(cxt, &elem, jb, NULL);
			break;
		case jpiNull:
		case jpiBool:
		case jpiNumeric:
		case jpiString:
		case jpiVariable:
			res = jperOk;
			if (found)
			{
				JsonbValue *jbv = palloc(sizeof(*jbv));
				computeJsonPathItem(cxt, jsp, jbv);
				*found = lappend(*found, jbv);
			}
			break;
		default:
			elog(ERROR,"Wrong state: %d", jsp->type);
	}

	return res;
}

JsonPathExecResult
executeJsonPath(JsonPath *path, List *vars, Jsonb *json, List **foundJson)
{
	JsonPathExecContext cxt;
	JsonPathItem	jsp;
	JsonbValue		jbv;

	cxt.vars = vars;
	cxt.lax = false; /* FIXME */

	jbv.type = jbvBinary;
	jbv.val.binary.data = &json->root;
	jbv.val.binary.len = VARSIZE_ANY_EXHDR(json);

	jspInit(&jsp, path);

	return recursiveExecute(&cxt, &jsp, &jbv, foundJson);
}

/********************Example functions for JsonPath***************************/

static Datum
returnDATUM(void *arg, bool *isNull)
{
	*isNull = false;
	return	PointerGetDatum(arg);
}

static Datum
returnNULL(void *arg, bool *isNull)
{
	*isNull = true;
	return Int32GetDatum(0);
}

static List*
makePassingVars(Jsonb *jb)
{
	JsonbValue		v;
	JsonbIterator	*it;
	int32			r;
	List			*vars = NIL;

	it = JsonbIteratorInit(&jb->root);

	r =  JsonbIteratorNext(&it, &v, true);

	if (r != WJB_BEGIN_OBJECT)
		elog(ERROR, "passing variable json is not a object");

	while((r = JsonbIteratorNext(&it, &v, true)) != WJB_DONE)
	{
		if (r == WJB_KEY)
		{
			JsonPathVariable	*jpv = palloc0(sizeof(*jpv));

			jpv->varName = cstring_to_text_with_len(v.val.string.val,
													v.val.string.len);

			JsonbIteratorNext(&it, &v, true);

			jpv->cb = returnDATUM;

			switch(v.type)
			{
				case jbvBool:
					jpv->typid = BOOLOID;
					jpv->cb_arg = DatumGetPointer(BoolGetDatum(v.val.boolean));
					break;
				case jbvNull:
					jpv->cb = returnNULL;
					break;
				case jbvString:
					jpv->typid = TEXTOID;
					jpv->cb_arg = cstring_to_text_with_len(v.val.string.val,
														   v.val.string.len);
					break;
				case jbvNumeric:
					jpv->typid = NUMERICOID;
					jpv->cb_arg = v.val.numeric;
					break;
				case jbvBinary:
					jpv->typid = JSONBOID;
					jpv->cb_arg = JsonbValueToJsonb(&v);
					break;
				default:
					elog(ERROR, "unsupported type in passing variable json");
			}

			vars = lappend(vars, jpv);
		}
	}

	return vars;
}

static Datum
__jsonpath_exists(PG_FUNCTION_ARGS)
{
	Jsonb				*jb = PG_GETARG_JSONB(0);
	JsonPath			*jp = PG_GETARG_JSONPATH(1);
	JsonPathExecResult	res;
	List				*vars = NIL;

	if (PG_NARGS() == 3)
		vars = makePassingVars(PG_GETARG_JSONB(2));

	res = executeJsonPath(jp, vars, jb, NULL);

	PG_FREE_IF_COPY(jb, 0);
	PG_FREE_IF_COPY(jp, 1);

	if (res == jperError)
		elog(ERROR, "Something wrong");

	PG_RETURN_BOOL(res == jperOk);
}

Datum
_jsonpath_exists2(PG_FUNCTION_ARGS)
{
	return __jsonpath_exists(fcinfo);
}

Datum
_jsonpath_exists3(PG_FUNCTION_ARGS)
{
	return __jsonpath_exists(fcinfo);
}

static Datum
__jsonpath_object(PG_FUNCTION_ARGS)
{
	FuncCallContext	*funcctx;
	List			*found = NIL;
	JsonbValue		*v;
	ListCell		*c;

	if (SRF_IS_FIRSTCALL())
	{
		JsonPath			*jp = PG_GETARG_JSONPATH(1);
		Jsonb				*jb;
		JsonPathExecResult	res;
		MemoryContext		oldcontext;
		List				*vars = NIL;

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		jb = PG_GETARG_JSONB_COPY(0);
		if (PG_NARGS() == 3)
			vars = makePassingVars(PG_GETARG_JSONB(2));

		res = executeJsonPath(jp, vars, jb, &found);

		if (res == jperError)
			elog(ERROR, "Something wrong");

		PG_FREE_IF_COPY(jp, 1);

		funcctx->user_fctx = found;

		MemoryContextSwitchTo(oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP();
	found = funcctx->user_fctx;

	c = list_head(found);

	if (c == NULL)
		SRF_RETURN_DONE(funcctx);

	v = lfirst(c);
	funcctx->user_fctx = list_delete_first(found);

	SRF_RETURN_NEXT(funcctx, JsonbGetDatum(JsonbValueToJsonb(v)));
}


Datum
_jsonpath_object2(PG_FUNCTION_ARGS)
{
	return __jsonpath_object(fcinfo);
}

Datum
_jsonpath_object3(PG_FUNCTION_ARGS)
{
	return __jsonpath_object(fcinfo);
}

bool
JsonbPathExists(Jsonb *jb, JsonPath *jp, List *vars)
{
	JsonPathExecResult res = executeJsonPath(jp, vars, jb, NULL);

	if (res == jperError)
		ereport(ERROR,
				(errcode(ERRCODE_NO_JSON_ITEM), /* FIXME */
				 errmsg("JSON path error")));

	return res == jperOk;
}

Jsonb *
JsonbPathQuery(Jsonb *jb, JsonPath *jp, JsonWrapper wrapper,
			   bool *empty, List *vars)
{
	JsonbValue *first;
	Jsonb	   *res = NULL;
	bool		wrap;
	List	   *found = NIL;
	JsonPathExecResult jper = executeJsonPath(jp, vars, jb, &found);

	if (jper == jperError)
		ereport(ERROR,
				(errcode(ERRCODE_NO_JSON_ITEM), /* FIXME */
				 errmsg("JSON path error")));

	first = found ? linitial(found) : NULL;

	if (!first)
		wrap = false;
	else if (wrapper == JSW_NONE)
		wrap = false;
	else if (wrapper == JSW_UNCONDITIONAL)
		wrap = true;
	else if (wrapper == JSW_CONDITIONAL)
		wrap = list_length(found) > 1 ||
			   IsAJsonbScalar(first) ||
			   (first->type == jbvBinary &&
				JsonContainerIsScalar(first->val.binary.data));
	else
	{
		elog(ERROR, "unrecognized json wrapper %d", wrapper);
		wrap = false;
	}

	if (wrap)
	{
		JsonbParseState *ps = NULL;
		JsonbValue *arr;
		ListCell   *lc;

		pushJsonbValue(&ps, WJB_BEGIN_ARRAY, NULL);

		foreach(lc, found)
		{
			JsonbValue *jbv = (JsonbValue *) lfirst(lc);

			if (jbv->type == jbvBinary &&
				JsonContainerIsScalar(jbv->val.binary.data))
				JsonbExtractScalar(jbv->val.binary.data, jbv);

			pushJsonbValue(&ps, WJB_ELEM, jbv);
		}

		arr = pushJsonbValue(&ps, WJB_END_ARRAY, NULL);
		res = JsonbValueToJsonb(arr);
	}
	else
	{
		if (list_length(found) > 1)
			ereport(ERROR,
					(errcode(ERRCODE_MORE_THAN_ONE_JSON_ITEM),
					 errmsg("more than one SQL/JSON item")));
		else if (first)
			res = JsonbValueToJsonb(first);
		else
		{
			*empty = true;
			res = NULL;
		}
	}

	return res;
}

Jsonb *
JsonbPathValue(Jsonb *jb, JsonPath *jp, bool *empty, List *vars)
{
	JsonbValue *res;
	List	   *found = NIL;
	JsonPathExecResult jper = executeJsonPath(jp, vars, jb, &found);

	if (jper == jperError)
		ereport(ERROR,
				(errcode(ERRCODE_NO_JSON_ITEM), /* FIXME */
				 errmsg("JSON path error")));

	*empty = !found;

	if (*empty)
		return NULL;

	if (list_length(found) > 1)
		ereport(ERROR,
				(errcode(ERRCODE_MORE_THAN_ONE_JSON_ITEM),
				 errmsg("more than one SQL/JSON item")));

	res = linitial(found);

	if (res->type == jbvBinary &&
		JsonContainerIsScalar(res->val.binary.data))
		JsonbExtractScalar(res->val.binary.data, res);

	if (!IsAJsonbScalar(res))
		ereport(ERROR,
				(errcode(ERRCODE_JSON_SCALAR_REQUIRED),
				 errmsg("SQL/JSON scalar required")));

	if (res->type == jbvNull)
		return NULL;

	return JsonbValueToJsonb(res);
}
