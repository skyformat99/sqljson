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

/*****************************INPUT/OUTPUT************************************/
static int
flattenJsonPathParseItem(StringInfo buf, JsonPathParseItem *item)
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
		case jpiKey:
		case jpiString:
		case jpiVariable:
			appendBinaryStringInfo(buf, (char*)&item->string.len, sizeof(item->string.len));
			appendBinaryStringInfo(buf, item->string.val, item->string.len);
			appendStringInfoChar(buf, '\0');
			break;
		case jpiNumeric:
			appendBinaryStringInfo(buf, (char*)item->numeric, VARSIZE(item->numeric));
			break;
		case jpiBool:
			appendBinaryStringInfo(buf, (char*)&item->boolean, sizeof(item->boolean));
			break;
		case jpiAnd:
		case jpiOr:
			{
				int32	left, right;

				left = buf->len;
				appendBinaryStringInfo(buf, (char*)&left /* fake value */, sizeof(left));
				right = buf->len;
				appendBinaryStringInfo(buf, (char*)&right /* fake value */, sizeof(right));

				chld = flattenJsonPathParseItem(buf, item->args.left);
				*(int32*)(buf->data + left) = chld;
				chld = flattenJsonPathParseItem(buf, item->args.right);
				*(int32*)(buf->data + right) = chld;
			}
			break;
		case jpiEqual:
		case jpiLess:
		case jpiGreater:
		case jpiLessOrEqual:
		case jpiGreaterOrEqual:
		case jpiNot:
		case jpiExpression:
			{
				int32 arg;

				arg = buf->len;
				appendBinaryStringInfo(buf, (char*)&arg /* fake value */, sizeof(arg));

				chld = flattenJsonPathParseItem(buf, item->arg);
				*(int32*)(buf->data + arg) = chld;
			}
			break;
		case jpiAnyArray:
		case jpiAnyKey:
		case jpiCurrent:
		case jpiRoot:
		case jpiNull:
			break;
		case jpiIndexArray:
			appendBinaryStringInfo(buf,
								   (char*)&item->array.nelems,
								   sizeof(item->array.nelems));
			appendBinaryStringInfo(buf,
								   (char*)item->array.elems,
								   item->array.nelems * sizeof(item->array.elems[0]));
			break;
		default:
			elog(ERROR, "1Unknown type: %d", item->type);
	}

	if (item->next)
		*(int32*)(buf->data + next) = flattenJsonPathParseItem(buf, item->next);

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
		flattenJsonPathParseItem(&buf, jsonpath);

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
			appendBinaryStringInfo(buf, " = ", 3); break;
		case jpiLess:
			appendBinaryStringInfo(buf, " < ", 3); break;
		case jpiGreater:
			appendBinaryStringInfo(buf, " > ", 3); break;
		case jpiLessOrEqual:
			appendBinaryStringInfo(buf, " <= ", 4); break;
		case jpiGreaterOrEqual:
			appendBinaryStringInfo(buf, " >= ", 4); break;
		default:
			elog(ERROR, "2Unknown type: %d", type);
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
			appendStringInfoChar(buf, '(');
			jspGetLeftArg(v, &elem);
			printJsonPathItem(buf, &elem, false, true);
			printOperation(buf, v->type);
			jspGetRightArg(v, &elem);
			printJsonPathItem(buf, &elem, false, true);
			appendStringInfoChar(buf, ')');
			break;
		case jpiExpression:
			appendBinaryStringInfo(buf, "?(", 2);
			jspGetArg(v, &elem);
			printJsonPathItem(buf, &elem, false, false);
			appendStringInfoChar(buf, ')');
			break;
		case jpiEqual:
		case jpiLess:
		case jpiGreater:
		case jpiLessOrEqual:
		case jpiGreaterOrEqual:
			printOperation(buf, v->type);
			jspGetArg(v, &elem);
			printJsonPathItem(buf, &elem, false, true);
			break;
		case jpiNot:
			appendBinaryStringInfo(buf, "(! ", 2);
			jspGetArg(v, &elem);
			printJsonPathItem(buf, &elem, false, true);
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
			read_int32(v->args.left, base, pos);
			read_int32(v->args.right, base, pos);
			break;
		case jpiEqual:
		case jpiLess:
		case jpiGreater:
		case jpiLessOrEqual:
		case jpiGreaterOrEqual:
		case jpiNot:
		case jpiExpression:
			read_int32(v->arg, base, pos);
			break;
		case jpiIndexArray:
			read_int32(v->array.nelems, base, pos);
			read_int32_n(v->array.elems, base, pos, v->array.nelems);
			break;
		default:
			elog(ERROR, "3Unknown type: %d", v->type);
	}
}

void
jspGetArg(JsonPathItem *v, JsonPathItem *a)
{
	Assert(
		v->type == jpiEqual ||
		v->type == jpiLess ||
		v->type == jpiGreater ||
		v->type == jpiLessOrEqual ||
		v->type == jpiGreaterOrEqual ||
		v->type == jpiExpression ||
		v->type == jpiNot
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
			v->type == jpiAnyArray ||
			v->type == jpiAnyKey ||
			v->type == jpiIndexArray ||
			v->type == jpiCurrent ||
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
		v->type == jpiOr
	);

	jspInitByBuffer(a, v->base, v->args.left);
}

void
jspGetRightArg(JsonPathItem *v, JsonPathItem *a)
{
	Assert(
		v->type == jpiAnd ||
		v->type == jpiOr
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
computeJsonPathItem(JsonPathItem *item, List *vars, JsonbValue *value)
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
			computeJsonPathVariable(item, vars, value);
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
checkScalarEquality(JsonPathItem *jsp,  List *vars, JsonbValue *jb)
{
	JsonbValue		computedValue;

	if (jb->type == jbvBinary)
		return false;

	computeJsonPathItem(jsp, vars, &computedValue);

	if (jb->type != computedValue.type)
		return false;

	switch(computedValue.type)
	{
		case jbvNull:
			return true;
		case jbvString:
			return (computedValue.val.string.len == jb->val.string.len &&
					memcmp(jb->val.string.val, computedValue.val.string.val,
						   computedValue.val.string.len) == 0);
		case jbvBool:
			return (jb->val.boolean == computedValue.val.boolean);
		case jbvNumeric:
			return (compareNumeric(computedValue.val.numeric, jb->val.numeric) == 0);
		default:
			elog(ERROR,"Wrong state");
	}

	return false;
}

static bool
makeCompare(JsonPathItem *jsp, List *vars, int32 op, JsonbValue *jb)
{
	int				res;
	JsonbValue		computedValue;

	if (jb->type != jbvNumeric)
		return false;

	computeJsonPathItem(jsp, vars, &computedValue);

	if (computedValue.type != jbvNumeric)
		return false;

	res = compareNumeric(jb->val.numeric, computedValue.val.numeric);

	switch(op)
	{
		case jpiEqual:
			return (res == 0);
		case jpiLess:
			return (res < 0);
		case jpiGreater:
			return (res > 0);
		case jpiLessOrEqual:
			return (res <= 0);
		case jpiGreaterOrEqual:
			return (res >= 0);
		default:
			elog(ERROR, "Unknown operation");
	}

	return false;
}

static JsonPathExecResult
executeExpr(JsonPathItem *jsp, List *vars, int32 op, JsonbValue *jb, JsonPathItem *jspLeftArg)
{
	JsonPathExecResult res = jperNotFound;
	/*
	 * read arg type
	 */
	Assert(jspGetNext(jsp, NULL) == false);
	Assert(jsp->type == jpiString || jsp->type == jpiNumeric ||
		   jsp->type == jpiNull || jsp->type == jpiBool ||
		   jsp->type == jpiVariable);

	switch(op)
	{
		case jpiEqual:
			res = checkScalarEquality(jsp, vars, jb) ? jperOk : jperNotFound;
			break;
		case jpiLess:
		case jpiGreater:
		case jpiLessOrEqual:
		case jpiGreaterOrEqual:
			res = makeCompare(jsp, vars, op, jb) ? jperOk : jperNotFound;
			break;
		default:
			elog(ERROR, "Unknown operation");
	}

	return res;
}

static JsonbValue*
copyJsonbValue(JsonbValue *src)
{
	JsonbValue	*dst = palloc(sizeof(*dst));

	*dst = *src;

	return dst;
}

static JsonPathExecResult
recursiveExecute(JsonPathItem *jsp, List *vars, JsonbValue *jb,
				 JsonPathItem *jspLeftArg, List **found)
{
	JsonPathItem		elem;
	JsonPathExecResult	res = jperNotFound;

	check_stack_depth();

	switch(jsp->type) {
		case jpiAnd:
			jspGetLeftArg(jsp, &elem);
			res = recursiveExecute(&elem, vars, jb, jspLeftArg, NULL);
			if (res == jperOk)
			{
				jspGetRightArg(jsp, &elem);
				res = recursiveExecute(&elem, vars, jb, jspLeftArg, NULL);
			}
			break;
		case jpiOr:
			jspGetLeftArg(jsp, &elem);
			res = recursiveExecute(&elem, vars, jb, jspLeftArg, NULL);
			if (res == jperNotFound)
			{
				jspGetRightArg(jsp, &elem);
				res = recursiveExecute(&elem, vars, jb, jspLeftArg, NULL);
			}
			break;
		case jpiNot:
			jspGetArg(jsp, &elem);
			switch((res = recursiveExecute(&elem, vars, jb, jspLeftArg, NULL)))
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
						res = recursiveExecute(&elem, vars, v, NULL, found);
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
			jspGetNext(jsp, &elem);
			if (JsonbType(jb) == jbvScalar)
			{
				JsonbIterator	*it;
				int32			r;
				JsonbValue		v;

				it = JsonbIteratorInit(jb->val.binary.data);

				r = JsonbIteratorNext(&it, &v, true);
				Assert(r == WJB_BEGIN_ARRAY);
				Assert(v.val.array.rawScalar == 1);
				Assert(v.val.array.nElems == 1);

				r = JsonbIteratorNext(&it, &v, true);
				Assert(r == WJB_ELEM);

				res = recursiveExecute(&elem, vars, &v, jspLeftArg, NULL);
			}
			else
			{
				res = recursiveExecute(&elem, vars, jb, jspLeftArg, NULL);
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
							res = recursiveExecute(&elem, vars, &v, NULL, found);

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
						res = recursiveExecute(&elem, vars, v, NULL, found);

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
							res = recursiveExecute(&elem, vars, &v, NULL, found);

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
		case jpiLess:
		case jpiGreater:
		case jpiLessOrEqual:
		case jpiGreaterOrEqual:
			jspGetArg(jsp, &elem);
			res = executeExpr(&elem, vars, jsp->type, jb, jspLeftArg);
			break;
		case jpiRoot:
			if (jspGetNext(jsp, &elem))
			{
				res = recursiveExecute(&elem, vars, jb, jspLeftArg, found);
			}
			else
			{
				res = jperOk;
				if (found)
					*found = lappend(*found, copyJsonbValue(jb));
			}

			break;
		case jpiExpression:
			/* no-op actually */
			jspGetArg(jsp, &elem);
			res = recursiveExecute(&elem, vars, jb, jspLeftArg, NULL);
			if (res == jperOk && found)
				*found = lappend(*found, copyJsonbValue(jb));
			break;
		default:
			elog(ERROR,"Wrong state: %d", jsp->type);
	}

	return res;
}

JsonPathExecResult
executeJsonPath(JsonPath *path, List *vars, Jsonb *json, List **foundJson)
{
	JsonPathItem	jsp;
	JsonbValue		jbv;

	jbv.type = jbvBinary;
	jbv.val.binary.data = &json->root;
	jbv.val.binary.len = VARSIZE_ANY_EXHDR(json);

	jspInit(&jsp, path);

	return recursiveExecute(&jsp, vars, &jbv, NULL, foundJson);
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
__jsonpath_exist(PG_FUNCTION_ARGS)
{
	JsonPath			*jp = PG_GETARG_JSONPATH(0);
	Jsonb				*jb = PG_GETARG_JSONB(1);
	JsonPathExecResult	res;
	List				*vars = NIL;

	if (PG_NARGS() == 3)
		vars = makePassingVars(PG_GETARG_JSONB(2));

	res = executeJsonPath(jp, vars, jb, NULL);

	PG_FREE_IF_COPY(jp, 0);
	PG_FREE_IF_COPY(jb, 1);

	if (res == jperError)
		elog(ERROR, "Something wrong");

	PG_RETURN_BOOL(res == jperOk);
}

Datum
_jsonpath_exist2(PG_FUNCTION_ARGS)
{
	return __jsonpath_exist(fcinfo);
}

Datum
_jsonpath_exist3(PG_FUNCTION_ARGS)
{
	return __jsonpath_exist(fcinfo);
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
		JsonPath			*jp = PG_GETARG_JSONPATH(0);
		Jsonb				*jb;
		JsonPathExecResult	res;
		MemoryContext		oldcontext;
		List				*vars = NIL;

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		jb = PG_GETARG_JSONB_COPY(1);
		if (PG_NARGS() == 3)
			vars = makePassingVars(PG_GETARG_JSONB(2));

		res = executeJsonPath(jp, vars, jb, &found);

		if (res == jperError)
			elog(ERROR, "Something wrong");

		PG_FREE_IF_COPY(jp, 0);

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
