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
#include "catalog/pg_collation.h"
#include "catalog/pg_type.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/formatting.h"
#include "utils/json.h"
#include "utils/jsonpath.h"
#include "utils/varlena.h"

typedef struct JsonPathExecContext
{
	List	   *vars;
	bool		lax;
	JsonbValue *root;				/* for $ evaluation */
	JsonbValue *innermostArray;		/* for LAST array index evaluation */
} JsonPathExecContext;

static JsonPathExecResult recursiveExecute(JsonPathExecContext *cxt,
										   JsonPathItem *jsp, JsonbValue *jb,
										   JsonValueList *found);

static JsonPathExecResult recursiveExecuteUnwrap(JsonPathExecContext *cxt,
							JsonPathItem *jsp, JsonbValue *jb, JsonValueList *found);

static inline JsonbValue *wrapItemsInArray(const JsonValueList *items);


static inline void
JsonValueListAppend(JsonValueList *jvl, JsonbValue *jbv)
{
	if (jvl->singleton)
	{
		jvl->list = list_make2(jvl->singleton, jbv);
		jvl->singleton = NULL;
	}
	else if (!jvl->list)
		jvl->singleton = jbv;
	else
		jvl->list = lappend(jvl->list, jbv);
}

static inline void
JsonValueListConcat(JsonValueList *jvl1, JsonValueList jvl2)
{
	if (jvl1->singleton)
	{
		if (jvl2.singleton)
			jvl1->list = list_make2(jvl1->singleton, jvl2.singleton);
		else
			jvl1->list = lcons(jvl1->singleton, jvl2.list);

		jvl1->singleton = NULL;
	}
	else if (jvl2.singleton)
	{
		if (jvl1->list)
			jvl1->list = lappend(jvl1->list, jvl2.singleton);
		else
			jvl1->singleton = jvl2.singleton;
	}
	else if (jvl1->list)
		jvl1->list = list_concat(jvl1->list, jvl2.list);
	else
		jvl1->list = jvl2.list;
}

static inline int
JsonValueListLength(JsonValueList *jvl)
{
	return jvl->singleton ? 1 : list_length(jvl->list);
}

static inline bool
JsonValueListIsEmpty(JsonValueList *jvl)
{
	return !jvl->singleton && list_length(jvl->list) <= 0;
}

static inline JsonbValue *
JsonValueListHead(JsonValueList *jvl)
{
	return jvl->singleton ? jvl->singleton : linitial(jvl->list);
}

static inline void
JsonValueListClear(JsonValueList *jvl)
{
	jvl->singleton = NULL;
	jvl->list = NIL;
}

static inline List *
JsonValueListGetList(JsonValueList *jvl)
{
	if (jvl->singleton)
		return list_make1(jvl->singleton);

	return jvl->list;
}

typedef struct JsonValueListIterator
{
	ListCell   *lcell;
} JsonValueListIterator;

#define JsonValueListIteratorEnd ((ListCell *) -1)

static inline JsonbValue *
JsonValueListNext(const JsonValueList *jvl, JsonValueListIterator *it)
{
	if (it->lcell == JsonValueListIteratorEnd)
		return NULL;

	if (it->lcell)
		it->lcell = lnext(it->lcell);
	else
	{
		if (jvl->singleton)
		{
			it->lcell = JsonValueListIteratorEnd;
			return jvl->singleton;
		}

		it->lcell = list_head(jvl->list);
	}

	if (!it->lcell)
	{
		it->lcell = JsonValueListIteratorEnd;
		return NULL;
	}

	return lfirst(it->lcell);
}

/*****************************INPUT/OUTPUT************************************/

/*
 * Convert AST to flat jsonpath type representation
 */
static int
flattenJsonPathParseItem(StringInfo buf, JsonPathParseItem *item,
						 bool allowCurrent, bool insideArraySubscript)
{
	/* position from begining of jsonpath data */
	int32	pos = buf->len - JSONPATH_HDRSZ;
	int32	chld, next;

	check_stack_depth();

	appendStringInfoChar(buf, (char)(item->type));
	alignStringInfoInt(buf);

	next = (item->next) ? buf->len : 0;

	/*
	 * actual value will be recorded later, after next and
	 * children processing
	 */
	appendBinaryStringInfo(buf, (char*)&next /* fake value */, sizeof(next));

	switch(item->type)
	{
		case jpiString:
		case jpiVariable:
		case jpiKey:
			appendBinaryStringInfo(buf, (char*)&item->value.string.len,
								   sizeof(item->value.string.len));
			appendBinaryStringInfo(buf, item->value.string.val, item->value.string.len);
			appendStringInfoChar(buf, '\0');
			break;
		case jpiNumeric:
			appendBinaryStringInfo(buf, (char*)item->value.numeric,
								   VARSIZE(item->value.numeric));
			break;
		case jpiBool:
			appendBinaryStringInfo(buf, (char*)&item->value.boolean,
								   sizeof(item->value.boolean));
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
		case jpiStartsWith:
			{
				int32	left, right;

				left = buf->len;

				/*
				 * first, reserve place for left/right arg's positions, then
				 * record both args and sets actual position in reserved places
				 */
				appendBinaryStringInfo(buf, (char*)&left /* fake value */, sizeof(left));
				right = buf->len;
				appendBinaryStringInfo(buf, (char*)&right /* fake value */, sizeof(right));

				chld = flattenJsonPathParseItem(buf, item->value.args.left,
												allowCurrent,
												insideArraySubscript);
				*(int32*)(buf->data + left) = chld;
				chld = flattenJsonPathParseItem(buf, item->value.args.right,
												allowCurrent,
												insideArraySubscript);
				*(int32*)(buf->data + right) = chld;
			}
			break;
		case jpiDatetime:
			if (!item->value.arg)
			{
				int32 arg = 0;

				appendBinaryStringInfo(buf, (char *) &arg, sizeof(arg));
				break;
			}
			/* fall through */
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

				chld = flattenJsonPathParseItem(buf, item->value.arg,
												item->type == jpiFilter ||
												allowCurrent,
												insideArraySubscript);
				*(int32*)(buf->data + arg) = chld;
			}
			break;
		case jpiNull:
			break;
		case jpiRoot:
			break;
		case jpiAnyArray:
		case jpiAnyKey:
			break;
		case jpiCurrent:
			if (!allowCurrent)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("@ is not allowed in root expressions")));
			break;
		case jpiLast:
			if (!insideArraySubscript)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("LAST is allowed only in array subscripts")));
			break;
		case jpiIndexArray:
			{
				int32		nelems = item->value.array.nelems;
				int			offset;
				int			i;

				appendBinaryStringInfo(buf, (char *) &nelems, sizeof(nelems));

				offset = buf->len;

				appendStringInfoSpaces(buf, sizeof(int32) * 2 * nelems);

				for (i = 0; i < nelems; i++)
				{
					int32	   *ppos;
					int32		topos;
					int32		frompos =
						flattenJsonPathParseItem(buf,
												item->value.array.elems[i].from,
												true, true);

					if (item->value.array.elems[i].to)
						topos = flattenJsonPathParseItem(buf,
												item->value.array.elems[i].to,
												true, true);
					else
						topos = 0;

					ppos = (int32 *) &buf->data[offset + i * 2 * sizeof(int32)];

					ppos[0] = frompos;
					ppos[1] = topos;
				}
			}
			break;
		case jpiAny:
			appendBinaryStringInfo(buf,
								   (char*)&item->value.anybounds.first,
								   sizeof(item->value.anybounds.first));
			appendBinaryStringInfo(buf,
								   (char*)&item->value.anybounds.last,
								   sizeof(item->value.anybounds.last));
			break;
		case jpiType:
		case jpiSize:
		case jpiAbs:
		case jpiFloor:
		case jpiCeiling:
		case jpiDouble:
		case jpiKeyValue:
			break;
		default:
			elog(ERROR, "Unknown type: %d", item->type);
	}

	if (item->next)
		*(int32*)(buf->data + next) =
			flattenJsonPathParseItem(buf, item->next, allowCurrent,
									 insideArraySubscript);

	return  pos;
}

Datum
jsonpath_in(PG_FUNCTION_ARGS)
{
	char				*in = PG_GETARG_CSTRING(0);
	int32				len = strlen(in);
	JsonPathParseResult	*jsonpath = parsejsonpath(in, len);
	JsonPath			*res;
	StringInfoData		buf;

	initStringInfo(&buf);
	enlargeStringInfo(&buf, 4 * len /* estimation */);

	appendStringInfoSpaces(&buf, JSONPATH_HDRSZ);

	if (jsonpath != NULL)
	{
		flattenJsonPathParseItem(&buf, jsonpath->expr, false, false);

		res = (JsonPath*)buf.data;
		SET_VARSIZE(res, buf.len);
		res->header = JSONPATH_VERSION;
		if (jsonpath->lax)
			res->header |= JSONPATH_LAX;

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
		case jpiStartsWith:
			appendBinaryStringInfo(buf, " starts with ", 13); break;
		default:
			elog(ERROR, "Unknown type: %d", type);
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
		case jpiStartsWith:
			return 2;
		case jpiAdd:
		case jpiSub:
			return 3;
		case jpiMul:
		case jpiDiv:
		case jpiMod:
			return 4;
		case jpiPlus:
		case jpiMinus:
			return 5;
		default:
			return 6;
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
		case jpiStartsWith:
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
		case jpiPlus:
		case jpiMinus:
			if (printBracketes)
				appendStringInfoChar(buf, '(');
			appendStringInfoChar(buf, v->type == jpiPlus ? '+' : '-');
			jspGetArg(v, &elem);
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
		case jpiLast:
			appendBinaryStringInfo(buf, "last", 4);
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
			for (i = 0; i < v->content.array.nelems; i++)
			{
				JsonPathItem from;
				JsonPathItem to;
				bool		range = jspGetArraySubscript(v, &from, &to, i);

				if (i)
					appendStringInfoChar(buf, ',');

				printJsonPathItem(buf, &from, false, false);

				if (range)
				{
					appendBinaryStringInfo(buf, " to ", 4);
					printJsonPathItem(buf, &to, false, false);
				}
			}
			appendStringInfoChar(buf, ']');
			break;
		case jpiAny:
			if (inKey)
				appendStringInfoChar(buf, '.');

			if (v->content.anybounds.first == 0 &&
					v->content.anybounds.last == PG_UINT32_MAX)
				appendBinaryStringInfo(buf, "**", 2);
			else if (v->content.anybounds.first == 0)
				appendStringInfo(buf, "**{,%u}", v->content.anybounds.last);
			else if (v->content.anybounds.last == PG_UINT32_MAX)
				appendStringInfo(buf, "**{%u,}", v->content.anybounds.first);
			else if (v->content.anybounds.first == v->content.anybounds.last)
				appendStringInfo(buf, "**{%u}", v->content.anybounds.first);
			else
				appendStringInfo(buf, "**{%u,%u}", v->content.anybounds.first,
												   v->content.anybounds.last);
			break;
		case jpiType:
			appendBinaryStringInfo(buf, ".type()", 7);
			break;
		case jpiSize:
			appendBinaryStringInfo(buf, ".size()", 7);
			break;
		case jpiAbs:
			appendBinaryStringInfo(buf, ".abs()", 6);
			break;
		case jpiFloor:
			appendBinaryStringInfo(buf, ".floor()", 8);
			break;
		case jpiCeiling:
			appendBinaryStringInfo(buf, ".ceiling()", 10);
			break;
		case jpiDouble:
			appendBinaryStringInfo(buf, ".double()", 9);
			break;
		case jpiDatetime:
			appendBinaryStringInfo(buf, ".datetime(", 10);
			if (v->content.arg)
			{
				jspGetArg(v, &elem);
				printJsonPathItem(buf, &elem, false, false);
			}
			appendStringInfoChar(buf, ')');
			break;
		case jpiKeyValue:
			appendBinaryStringInfo(buf, ".keyvalue()", 11);
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

	if (in->header & JSONPATH_LAX)
		appendBinaryStringInfo(&buf, "lax ", 4);

	jspInit(&v, in);
	printJsonPathItem(&buf, &v, false, true);

	PG_RETURN_CSTRING(buf.data);
}

/********************Support functions for JsonPath****************************/

/*
 * Support macroses to read stored values
 */

#define read_byte(v, b, p) do {			\
	(v) = *(uint8*)((b) + (p));			\
	(p) += 1;							\
} while(0)								\

#define read_int32(v, b, p) do {		\
	(v) = *(uint32*)((b) + (p));		\
	(p) += sizeof(int32);				\
} while(0)								\

#define read_int32_n(v, b, p, n) do {	\
	(v) = (void *)((b) + (p));			\
	(p) += sizeof(int32) * (n);			\
} while(0)								\

/*
 * Read root node and fill root node representation
 */
void
jspInit(JsonPathItem *v, JsonPath *js)
{
	Assert((js->header & ~JSONPATH_LAX) == JSONPATH_VERSION);
	jspInitByBuffer(v, js->data, 0);
}

/*
 * Read node from buffer and fill its representation
 */
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
		case jpiType:
		case jpiSize:
		case jpiAbs:
		case jpiFloor:
		case jpiCeiling:
		case jpiDouble:
		case jpiKeyValue:
		case jpiLast:
			break;
		case jpiKey:
		case jpiString:
		case jpiVariable:
			read_int32(v->content.value.datalen, base, pos);
			/* follow next */
		case jpiNumeric:
		case jpiBool:
			v->content.value.data = base + pos;
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
		case jpiStartsWith:
			read_int32(v->content.args.left, base, pos);
			read_int32(v->content.args.right, base, pos);
			break;
		case jpiNot:
		case jpiExists:
		case jpiIsUnknown:
		case jpiPlus:
		case jpiMinus:
		case jpiFilter:
		case jpiDatetime:
			read_int32(v->content.arg, base, pos);
			break;
		case jpiIndexArray:
			read_int32(v->content.array.nelems, base, pos);
			read_int32_n(v->content.array.elems, base, pos,
						 v->content.array.nelems * 2);
			break;
		case jpiAny:
			read_int32(v->content.anybounds.first, base, pos);
			read_int32(v->content.anybounds.last, base, pos);
			break;
		default:
			elog(ERROR, "Unknown type: %d", v->type);
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
		v->type == jpiMinus ||
		v->type == jpiDatetime
	);

	jspInitByBuffer(a, v->base, v->content.arg);
}

bool
jspGetNext(JsonPathItem *v, JsonPathItem *a)
{
	if (jspHasNext(v))
	{
		Assert(
			v->type == jpiString ||
			v->type == jpiNumeric ||
			v->type == jpiBool ||
			v->type == jpiNull ||
			v->type == jpiKey ||
			v->type == jpiAny ||
			v->type == jpiAnyArray ||
			v->type == jpiAnyKey ||
			v->type == jpiIndexArray ||
			v->type == jpiFilter ||
			v->type == jpiCurrent ||
			v->type == jpiExists ||
			v->type == jpiRoot ||
			v->type == jpiVariable ||
			v->type == jpiLast ||
			v->type == jpiAdd ||
			v->type == jpiSub ||
			v->type == jpiMul ||
			v->type == jpiDiv ||
			v->type == jpiMod ||
			v->type == jpiPlus ||
			v->type == jpiMinus ||
			v->type == jpiType ||
			v->type == jpiSize ||
			v->type == jpiAbs ||
			v->type == jpiFloor ||
			v->type == jpiCeiling ||
			v->type == jpiDouble ||
			v->type == jpiDatetime ||
			v->type == jpiKeyValue ||
			v->type == jpiStartsWith
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
		v->type == jpiMod ||
		v->type == jpiStartsWith
	);

	jspInitByBuffer(a, v->base, v->content.args.left);
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
		v->type == jpiMod ||
		v->type == jpiStartsWith
	);

	jspInitByBuffer(a, v->base, v->content.args.right);
}

bool
jspGetBool(JsonPathItem *v)
{
	Assert(v->type == jpiBool);

	return (bool)*v->content.value.data;
}

Numeric
jspGetNumeric(JsonPathItem *v)
{
	Assert(v->type == jpiNumeric);

	return (Numeric)v->content.value.data;
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
		*len = v->content.value.datalen;
	return v->content.value.data;
}

bool
jspGetArraySubscript(JsonPathItem *v, JsonPathItem *from, JsonPathItem *to,
					 int i)
{
	Assert(v->type == jpiIndexArray);

	jspInitByBuffer(from, v->base, v->content.array.elems[i].from);

	if (!v->content.array.elems[i].to)
		return false;

	jspInitByBuffer(to, v->base, v->content.array.elems[i].to);

	return true;
}

/********************Execute functions for JsonPath***************************/

/*
 * Find value of jsonpath variable in a list of passing params
 */
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
		ereport(ERROR,
				(errcode(ERRCODE_NO_DATA_FOUND),
				 errmsg("could not find '%s' passed variable",
						pnstrdup(varName, varNameLength))));

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
			break;
		case INT2OID:
			value->type = jbvNumeric;
			value->val.numeric = DatumGetNumeric(DirectFunctionCall1(
												int2_numeric, computedValue));
			break;
		case INT4OID:
			value->type = jbvNumeric;
			value->val.numeric = DatumGetNumeric(DirectFunctionCall1(
												int4_numeric, computedValue));
			break;
		case INT8OID:
			value->type = jbvNumeric;
			value->val.numeric = DatumGetNumeric(DirectFunctionCall1(
												int8_numeric, computedValue));
			break;
		case FLOAT4OID:
			value->type = jbvNumeric;
			value->val.numeric = DatumGetNumeric(DirectFunctionCall1(
												float4_numeric, computedValue));
			break;
		case FLOAT8OID:
			value->type = jbvNumeric;
			value->val.numeric = DatumGetNumeric(DirectFunctionCall1(
												float4_numeric, computedValue));
			break;
		case TEXTOID:
		case VARCHAROID:
			value->type = jbvString;
			value->val.string.val = VARDATA_ANY(computedValue);
			value->val.string.len = VARSIZE_ANY_EXHDR(computedValue);
			break;
		case DATEOID:
		case TIMEOID:
		case TIMETZOID:
		case TIMESTAMPOID:
		case TIMESTAMPTZOID:
			value->type = jbvDatetime;
			value->val.datetime.typid = var->typid;
			value->val.datetime.typmod = var->typmod;
			value->val.datetime.value = computedValue;
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
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("only bool, numeric and text types could be casted to supported jsonpath types")));
	}
}

/*
 * Convert jsonpath's scalar or variable node to actual jsonb value
 */
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


/*
 * Returns jbv* type of of JsonbValue. Note, it never returns
 * jbvBinary as is - jbvBinary is used as mark of store naked
 * scalar value. To improve readability it defines jbvScalar
 * as alias to jbvBinary
 */
#define jbvScalar jbvBinary
static int
JsonbType(JsonbValue *jb)
{
	int type = jb->type;

	if (jb->type == jbvBinary)
	{
		JsonbContainer	*jbc = jb->val.binary.data;

		if (JsonContainerIsScalar(jbc))
			type = jbvScalar;
		else if (JsonContainerIsObject(jbc))
			type = jbvObject;
		else if (JsonContainerIsArray(jbc))
			type = jbvArray;
		else
			elog(ERROR, "Unknown container type: 0x%08x", jbc->header);
	}

	return type;
}

static const char *
JsonbTypeName(JsonbValue *jb)
{
	JsonbValue jbvbuf;

	if (jb->type == jbvBinary)
	{
		JsonbContainer *jbc = jb->val.binary.data;

		if (JsonContainerIsScalar(jbc))
			jb = JsonbExtractScalar(jbc, &jbvbuf);
		else if (JsonContainerIsArray(jbc))
			return "array";
		else if (JsonContainerIsObject(jbc))
			return "object";
		else
			elog(ERROR, "Unknown container type: 0x%08x", jbc->header);
	}

	switch (jb->type)
	{
		case jbvObject:
			return "object";
		case jbvArray:
			return "array";
		case jbvNumeric:
			return "number";
		case jbvString:
			return "string";
		case jbvBool:
			return "boolean";
		case jbvNull:
			return "null";
		case jbvDatetime:
			switch (jb->val.datetime.typid)
			{
				case DATEOID:
					return "date";
				case TIMEOID:
					return "time without time zone";
				case TIMETZOID:
					return "time with time zone";
				case TIMESTAMPOID:
					return "timestamp without time zone";
				case TIMESTAMPTZOID:
					return "timestamp with time zone";
				default:
					elog(ERROR, "unknown jsonb value datetime type oid %d",
						 jb->val.datetime.typid);
			}
			return "unknown";
		default:
			elog(ERROR, "Unknown jsonb value type: %d", jb->type);
			return "unknown";
	}
}

static int
JsonbArraySize(JsonbValue *jb)
{
	if (jb->type == jbvArray)
		return jb->val.array.nElems;

	if (jb->type == jbvBinary)
	{
		JsonbContainer *jbc = jb->val.binary.data;

		if (JsonContainerIsArray(jbc) && !JsonContainerIsScalar(jbc))
			return JsonContainerSize(jbc);
	}

	return -1;
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

static JsonPathExecResult
checkEquality(JsonbValue *jb1, JsonbValue *jb2, bool not)
{
	bool	eq = false;

	if (jb1->type != jb2->type)
	{
		if (jb1->type == jbvNull || jb2->type == jbvNull)
			return jperNotFound;

		return jperError;
	}

	if (jb1->type == jbvBinary)
		return jperError;

	switch (jb1->type)
	{
		case jbvNull:
			eq = true;
			break;
		case jbvString:
			eq = (jb1->val.string.len == jb2->val.string.len &&
					memcmp(jb2->val.string.val, jb1->val.string.val,
						   jb1->val.string.len) == 0);
			break;
		case jbvBool:
			eq = (jb2->val.boolean == jb1->val.boolean);
			break;
		case jbvNumeric:
			eq = (compareNumeric(jb1->val.numeric, jb2->val.numeric) == 0);
			break;
		case jbvDatetime:
			{
				PGFunction	eqfunc;

				if (jb1->val.datetime.typid != jb2->val.datetime.typid)
					return jperError;

				/* TODO cross-type comparison */

				switch (jb1->val.datetime.typid)
				{
					case DATEOID:
						eqfunc = date_eq;
						break;
					case TIMEOID:
						eqfunc = time_eq;
						break;
					case TIMETZOID:
						eqfunc = timetz_eq;
						break;
					case TIMESTAMPOID:
					case TIMESTAMPTZOID:
						eqfunc = timestamp_eq;
						break;
					default:
						elog(ERROR, "unknown jsonb value datetime type oid %d",
							 jb1->val.datetime.typid);
				}

				eq = DatumGetBool(DirectFunctionCall2(eqfunc,
													  jb1->val.datetime.value,
													  jb2->val.datetime.value));
				break;
			}
		default:
			elog(ERROR,"Wrong state");
	}

	return (not ^ eq) ? jperOk : jperNotFound;
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
		case jbvString:
			cmp = varstr_cmp(jb1->val.string.val, jb1->val.string.len,
							 jb2->val.string.val, jb2->val.string.len,
							 DEFAULT_COLLATION_OID);
			break;
		case jbvDatetime:
			{
				PGFunction	cmpfunc;

				if (jb1->val.datetime.typid != jb2->val.datetime.typid)
					return jperError;

				/* TODO cross-type comparison */

				switch (jb1->val.datetime.typid)
				{
					case DATEOID:
						cmpfunc = date_cmp;
						break;
					case TIMEOID:
						cmpfunc = time_cmp;
						break;
					case TIMETZOID:
						cmpfunc = timetz_cmp;
						break;
					case TIMESTAMPOID:
					case TIMESTAMPTZOID:
						cmpfunc = timestamp_cmp;
						break;
					default:
						elog(ERROR, "unknown jsonb value datetime type oid %d",
							 jb1->val.datetime.typid);
				}

				cmp = DatumGetInt32(DirectFunctionCall2(cmpfunc,
													jb1->val.datetime.value,
													jb2->val.datetime.value));
				break;
			}
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

static JsonbValue *
copyJsonbValue(JsonbValue *src)
{
	JsonbValue	*dst = palloc(sizeof(*dst));

	*dst = *src;

	return dst;
}

static inline JsonPathExecResult
recursiveExecuteNext(JsonPathExecContext *cxt,
					 JsonPathItem *cur, JsonPathItem *next,
					 JsonbValue *v, JsonValueList *found, bool copy)
{
	JsonPathItem elem;
	bool		hasNext;

	if (!cur)
		hasNext = next != NULL;
	else if (next)
		hasNext = jspHasNext(cur);
	else
	{
		next = &elem;
		hasNext = jspGetNext(cur, next);
	}

	if (hasNext)
		return recursiveExecute(cxt, next, v, found);

	if (found)
		JsonValueListAppend(found, copy ? copyJsonbValue(v) : v);

	return jperOk;
}

static JsonPathExecResult
executeExpr(JsonPathExecContext *cxt, JsonPathItem *jsp, JsonbValue *jb)
{
	JsonPathExecResult res;
	JsonPathItem elem;
	JsonValueList lseq = { 0 };
	JsonValueList rseq = { 0 };
	JsonValueListIterator lseqit = { 0 };
	JsonbValue *lval;
	bool		error = false;
	bool		found = false;

	jspGetLeftArg(jsp, &elem);
	res = recursiveExecuteUnwrap(cxt, &elem, jb, &lseq);
	if (jperIsError(res))
		return jperError;

	jspGetRightArg(jsp, &elem);
	res = recursiveExecuteUnwrap(cxt, &elem, jb, &rseq);
	if (jperIsError(res))
		return jperError;

	while ((lval = JsonValueListNext(&lseq, &lseqit)))
	{
		JsonValueListIterator rseqit = { 0 };
		JsonbValue *rval;

		while ((rval = JsonValueListNext(&rseq, &rseqit)))
		{
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

	if (error) /* possible only in lax mode */
		return jperError;

	return jperNotFound;
}

static JsonPathExecResult
executeBinaryArithmExpr(JsonPathExecContext *cxt, JsonPathItem *jsp,
						JsonbValue *jb, JsonValueList *found)
{
	JsonPathExecResult jper;
	JsonPathItem elem;
	JsonValueList lseq = { 0 };
	JsonValueList rseq = { 0 };
	JsonbValue *lval;
	JsonbValue *rval;
	JsonbValue	lvalbuf;
	JsonbValue	rvalbuf;
	Datum		ldatum;
	Datum		rdatum;
	Datum		res;
	bool		hasNext;

	jspGetLeftArg(jsp, &elem);

	jper = recursiveExecute(cxt, &elem, jb, &lseq);

	if (jper == jperOk)
	{
		jspGetRightArg(jsp, &elem);
		jper = recursiveExecute(cxt, &elem, jb, &rseq);
	}

	if (jper != jperOk ||
		JsonValueListLength(&lseq) != 1 ||
		JsonValueListLength(&rseq) != 1)
		return jperMakeError(ERRCODE_SINGLETON_JSON_ITEM_REQUIRED);

	lval = JsonValueListHead(&lseq);

	if (JsonbType(lval) == jbvScalar)
		lval = JsonbExtractScalar(lval->val.binary.data, &lvalbuf);

	if (lval->type != jbvNumeric)
		return jperMakeError(ERRCODE_SINGLETON_JSON_ITEM_REQUIRED);

	rval = JsonValueListHead(&rseq);

	if (JsonbType(rval) == jbvScalar)
		rval = JsonbExtractScalar(rval->val.binary.data, &rvalbuf);

	if (rval->type != jbvNumeric)
		return jperMakeError(ERRCODE_SINGLETON_JSON_ITEM_REQUIRED);

	hasNext = jspGetNext(jsp, &elem);

	if (!found && !hasNext)
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

	return recursiveExecuteNext(cxt, jsp, &elem, lval, found, false);
}

static JsonPathExecResult
executeUnaryArithmExpr(JsonPathExecContext *cxt, JsonPathItem *jsp,
					   JsonbValue *jb,  JsonValueList *found)
{
	JsonPathExecResult jper;
	JsonPathExecResult jper2;
	JsonPathItem elem;
	JsonValueList seq = { 0 };
	JsonValueListIterator it = { 0 };
	JsonbValue *val;
	bool		hasNext;

	jspGetArg(jsp, &elem);
	jper = recursiveExecute(cxt, &elem, jb, &seq);

	if (jperIsError(jper))
		return jperMakeError(ERRCODE_JSON_NUMBER_NOT_FOUND);

	jper = jperNotFound;

	hasNext = jspGetNext(jsp, &elem);

	while ((val = JsonValueListNext(&seq, &it)))
	{
		if (JsonbType(val) == jbvScalar)
			JsonbExtractScalar(val->val.binary.data, val);

		if (val->type == jbvNumeric)
		{
			if (!found && !hasNext)
				return jperOk;
		}
		else if (!found && !hasNext)
			continue; /* skip non-numerics processing */

		if (val->type != jbvNumeric)
			return jperMakeError(ERRCODE_JSON_NUMBER_NOT_FOUND);

		switch (jsp->type)
		{
			case jpiPlus:
				break;
			case jpiMinus:
				val->val.numeric =
					DatumGetNumeric(DirectFunctionCall1(
						numeric_uminus, NumericGetDatum(val->val.numeric)));
				break;
			default:
				elog(ERROR, "unknown jsonpath arithmetic operation %d", jsp->type);
		}

		jper2 = recursiveExecuteNext(cxt, jsp, &elem, val, found, false);

		if (jperIsError(jper2))
			return jper2;

		if (jper2 == jperOk)
		{
			if (!found)
				return jperOk;
			jper = jperOk;
		}
	}

	return jper;
}

/*
 * implements jpiAny node (** operator)
 */
static JsonPathExecResult
recursiveAny(JsonPathExecContext *cxt, JsonPathItem *jsp, JsonbValue *jb,
			 JsonValueList *found, uint32 level, uint32 first, uint32 last)
{
	JsonPathExecResult	res = jperNotFound;
	JsonbIterator		*it;
	int32				r;
	JsonbValue			v;

	check_stack_depth();

	if (level > last)
		return res;

	it = JsonbIteratorInit(jb->val.binary.data);

	/*
	 * Recursivly iterate over jsonb objects/arrays
	 */
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
				res = recursiveExecuteNext(cxt, NULL, jsp, &v, found, true);

				if (jperIsError(res))
					break;

				if (res == jperOk && !found)
					break;
			}

			if (level < last && v.type == jbvBinary)
			{
				res = recursiveAny(cxt, jsp, &v, found, level + 1, first, last);

				if (jperIsError(res))
					break;

				if (res == jperOk && found == NULL)
					break;
			}
		}
	}

	return res;
}

static JsonPathExecResult
getArrayIndex(JsonPathExecContext *cxt, JsonPathItem *jsp, JsonbValue *jb,
			  int32 *index)
{
	JsonbValue *jbv;
	JsonValueList found = { 0 };
	JsonbValue	tmp;
	JsonPathExecResult res = recursiveExecute(cxt, jsp, jb, &found);

	if (jperIsError(res))
		return res;

	if (JsonValueListLength(&found) != 1)
		return jperMakeError(ERRCODE_INVALID_JSON_SUBSCRIPT);

	jbv = JsonValueListHead(&found);

	if (JsonbType(jbv) == jbvScalar)
		jbv = JsonbExtractScalar(jbv->val.binary.data, &tmp);

	if (jbv->type != jbvNumeric)
		return jperMakeError(ERRCODE_INVALID_JSON_SUBSCRIPT);

	*index = DatumGetInt32(DirectFunctionCall1(numeric_int4,
							DirectFunctionCall2(numeric_trunc,
											NumericGetDatum(jbv->val.numeric),
											Int32GetDatum(0))));

	return jperOk;
}

static JsonPathExecResult
executeStartsWithPredicate(JsonPathExecContext *cxt, JsonPathItem *jsp,
						   JsonbValue *jb)
{
	JsonPathExecResult res;
	JsonPathItem elem;
	JsonValueList lseq = { 0 };
	JsonValueList rseq = { 0 };
	JsonValueListIterator lit = { 0 };
	JsonbValue *whole;
	JsonbValue *initial;
	JsonbValue	initialbuf;
	bool		error = false;
	bool		found = false;

	jspGetRightArg(jsp, &elem);
	res = recursiveExecute(cxt, &elem, jb, &rseq);
	if (jperIsError(res))
		return jperError;

	if (JsonValueListLength(&rseq) != 1)
		return jperError;

	initial = JsonValueListHead(&rseq);

	if (JsonbType(initial) == jbvScalar)
		initial = JsonbExtractScalar(initial->val.binary.data, &initialbuf);

	if (initial->type != jbvString)
		return jperError;

	jspGetLeftArg(jsp, &elem);
	res = recursiveExecuteUnwrap(cxt, &elem, jb, &lseq);
	if (jperIsError(res))
		return jperError;

	while ((whole = JsonValueListNext(&lseq, &lit)))
	{
		JsonbValue	wholebuf;

		if (JsonbType(whole) == jbvScalar)
			whole = JsonbExtractScalar(whole->val.binary.data, &wholebuf);

		if (whole->type != jbvString)
		{
			if (!cxt->lax)
				return jperError;

			error = true;
		}
		else if (whole->val.string.len >= initial->val.string.len &&
				 !memcmp(whole->val.string.val,
						 initial->val.string.val,
						 initial->val.string.len))
		{
			if (cxt->lax)
				return jperOk;

			found = true;
		}
	}

	if (found) /* possible only in strict mode */
		return jperOk;

	if (error) /* possible only in lax mode */
		return jperError;

	return jperNotFound;
}

static bool
tryToParseDatetime(const char *template, text *datetime,
				   Datum *value, Oid *typid, int32 *typmod)
{
	MemoryContext mcxt = CurrentMemoryContext;
	bool		ok = false;

	PG_TRY();
	{
		*value = to_datetime(datetime, template, -1, true, typid, typmod);
		ok = true;
	}
	PG_CATCH();
	{
		if (ERRCODE_TO_CATEGORY(geterrcode()) != ERRCODE_DATA_EXCEPTION)
			PG_RE_THROW();

		FlushErrorState();
		MemoryContextSwitchTo(mcxt);
	}
	PG_END_TRY();

	return ok;
}

/*
 * Main executor function: walks on jsonpath structure and tries to find
 * correspoding parts of jsonb. Note, jsonb and jsonpath values should be
 * avaliable and untoasted during work because JsonPathItem, JsonbValue
 * and found could have pointers into input values. If caller wants just to
 * check matching of json by jsonpath then it doesn't provide a found arg.
 * In this case executor works till first positive result and does not check
 * the rest if it is possible. In other case it tries to find all satisfied
 * results
 */
static JsonPathExecResult
recursiveExecuteNoUnwrap(JsonPathExecContext *cxt, JsonPathItem *jsp,
						 JsonbValue *jb, JsonValueList *found)
{
	JsonPathItem		elem;
	JsonPathExecResult	res = jperNotFound;
	bool				hasNext;

	check_stack_depth();

	switch(jsp->type) {
		case jpiAnd:
			jspGetLeftArg(jsp, &elem);
			res = recursiveExecute(cxt, &elem, jb, NULL);
			if (res != jperNotFound)
			{
				JsonPathExecResult res2;

				/*
				 * SQL/JSON says that we should check second arg
				 * in case of jperError
				 */

				jspGetRightArg(jsp, &elem);
				res2 = recursiveExecute(cxt, &elem, jb, NULL);

				res = (res2 == jperOk) ? res : res2;
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

				res = (res2 == jperNotFound) ? res : res2;
			}
			break;
		case jpiNot:
			jspGetArg(jsp, &elem);
			switch ((res = recursiveExecute(cxt, &elem, jb, NULL)))
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
			res = jperIsError(res) ? jperOk : jperNotFound;
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
					res = recursiveExecuteNext(cxt, jsp, NULL, v, found, false);

					if (jspHasNext(jsp) || !found)
						pfree(v); /* free value if it was not added to found list */
				}
				else if (!cxt->lax && found)
					res = jperMakeError(ERRCODE_JSON_MEMBER_NOT_FOUND);
			}
			else if (!cxt->lax && found)
				res = jperMakeError(ERRCODE_JSON_MEMBER_NOT_FOUND);
			break;
		case jpiRoot:
			jb = cxt->root;
			/* fall through */
		case jpiCurrent:
			{
				JsonbValue *v;
				JsonbValue	vbuf;
				bool		copy = true;

				if (JsonbType(jb) == jbvScalar)
				{
					if (jspHasNext(jsp))
						v = &vbuf;
					else
					{
						v = palloc(sizeof(*v));
						copy = false;
					}

					JsonbExtractScalar(jb->val.binary.data, v);
				}
				else
					v = jb;

				res = recursiveExecuteNext(cxt, jsp, NULL, v, found, copy);
				break;
			}
		case jpiAnyArray:
			if (JsonbType(jb) == jbvArray)
			{
				JsonbIterator	*it;
				int32			r;
				JsonbValue		v;

				hasNext = jspGetNext(jsp, &elem);
				it = JsonbIteratorInit(jb->val.binary.data);

				while((r = JsonbIteratorNext(&it, &v, true)) != WJB_DONE)
				{
					if (r == WJB_ELEM)
					{
						res = recursiveExecuteNext(cxt, jsp, &elem, &v, found, true);

						if (jperIsError(res))
							break;

						if (res == jperOk && !found)
							break;
					}
				}
			}
			else if (found)
				res = jperMakeError(ERRCODE_JSON_ARRAY_NOT_FOUND);
			break;

		case jpiIndexArray:
			if (JsonbType(jb) == jbvArray)
			{
				JsonbValue *innermostArray = cxt->innermostArray;
				int			i;
				int			size = JsonbArraySize(jb);

				cxt->innermostArray = jb; /* for LAST evaluation */

				hasNext = jspGetNext(jsp, &elem);

				for (i = 0; i < jsp->content.array.nelems; i++)
				{
					JsonPathItem from;
					JsonPathItem to;
					int32		index;
					int32		index_from;
					int32		index_to;
					bool		range = jspGetArraySubscript(jsp, &from, &to, i);

					res = getArrayIndex(cxt, &from, jb, &index_from);

					if (jperIsError(res))
						break;

					if (range)
					{
						res = getArrayIndex(cxt, &to, jb, &index_to);

						if (jperIsError(res))
							break;
					}
					else
						index_to = index_from;

					if (!cxt->lax &&
						(index_from < 0 ||
						 index_from > index_to ||
						 index_to >= size))
					{
						res = jperMakeError(ERRCODE_INVALID_JSON_SUBSCRIPT);
						break;
					}

					if (index_from < 0)
						index_from = 0;

					if (index_to >= size)
						index_to = size - 1;

					res = jperNotFound;

					for (index = index_from; index <= index_to; index++)
					{
						JsonbValue *v =
							getIthJsonbValueFromContainer(jb->val.binary.data,
														  (uint32) index);

						if (v == NULL)
							continue;

						res = recursiveExecuteNext(cxt, jsp, &elem, v, found, false);

						if (jperIsError(res))
							break;

						if (res == jperOk && !found)
							break;
					}

					if (jperIsError(res))
						break;

					if (res == jperOk && !found)
						break;
				}

				cxt->innermostArray = innermostArray;
			}
			else if (found)
				res = jperMakeError(ERRCODE_JSON_ARRAY_NOT_FOUND);
			break;

		case jpiLast:
			{
				JsonbValue	tmpjbv;
				JsonbValue *lastjbv;
				int			last;
				bool		hasNext;

				if (!cxt->innermostArray)
					elog(ERROR,
						 "evaluating jsonpath LAST outside of array subscript");

				hasNext = jspGetNext(jsp, &elem);

				if (!hasNext && !found)
				{
					res = jperOk;
					break;
				}

				last = JsonbArraySize(cxt->innermostArray) - 1;

				lastjbv = hasNext ? &tmpjbv : palloc(sizeof(*lastjbv));

				lastjbv->type = jbvNumeric;
				lastjbv->val.numeric = DatumGetNumeric(DirectFunctionCall1(
											int4_numeric, Int32GetDatum(last)));

				res = recursiveExecuteNext(cxt, jsp, &elem, lastjbv, found, hasNext);
			}
			break;
		case jpiAnyKey:
			if (JsonbType(jb) == jbvObject)
			{
				JsonbIterator	*it;
				int32			r;
				JsonbValue		v;

				hasNext = jspGetNext(jsp, &elem);
				it = JsonbIteratorInit(jb->val.binary.data);

				while((r = JsonbIteratorNext(&it, &v, true)) != WJB_DONE)
				{
					if (r == WJB_VALUE)
					{
						res = recursiveExecuteNext(cxt, jsp, &elem, &v, found, true);

						if (jperIsError(res))
							break;

						if (res == jperOk && !found)
							break;
					}
				}
			}
			else if (!cxt->lax && found)
				res = jperMakeError(ERRCODE_JSON_OBJECT_NOT_FOUND);
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
			res = executeBinaryArithmExpr(cxt, jsp, jb, found);
			break;
		case jpiPlus:
		case jpiMinus:
			res = executeUnaryArithmExpr(cxt, jsp, jb, found);
			break;
		case jpiFilter:
			jspGetArg(jsp, &elem);
			res = recursiveExecute(cxt, &elem, jb, NULL);
			if (res != jperOk)
				res = jperNotFound;
			else
				res = recursiveExecuteNext(cxt, jsp, NULL, jb, found, true);
			break;
		case jpiAny:
		{
			bool hasNext = jspGetNext(jsp, &elem);

			/* first try without any intermediate steps */
			if (jsp->content.anybounds.first == 0)
			{
				res = recursiveExecuteNext(cxt, jsp, &elem, jb, found, true);

				if (res == jperOk && !found)
						break;
			}

			if (jb->type == jbvBinary)
				res = recursiveAny(cxt, hasNext ? &elem : NULL, jb, found,
								   1,
								   jsp->content.anybounds.first,
								   jsp->content.anybounds.last);
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
			{
				JsonbValue	vbuf;
				JsonbValue *v;
				bool		hasNext = jspGetNext(jsp, &elem);

				if (!hasNext && !found)
				{
					res = jperOk; /* skip evaluation */
					break;
				}

				v = hasNext ? &vbuf : palloc(sizeof(*v));

				computeJsonPathItem(cxt, jsp, v);

				res = recursiveExecuteNext(cxt, jsp, &elem, v, found, hasNext);
			}
			break;
		case jpiType:
			{
				JsonbValue *jbv = palloc(sizeof(*jbv));

				jbv->type = jbvString;
				jbv->val.string.val = pstrdup(JsonbTypeName(jb));
				jbv->val.string.len = strlen(jbv->val.string.val);

				res = recursiveExecuteNext(cxt, jsp, NULL, jbv, found, false);
			}
			break;
		case jpiSize:
			{
				int			size = JsonbArraySize(jb);

				if (size < 0)
				{
					if (!cxt->lax)
					{
						res = jperMakeError(ERRCODE_JSON_ARRAY_NOT_FOUND);
						break;
					}

					size = 1;
				}

				jb = palloc(sizeof(*jb));

				jb->type = jbvNumeric;
				jb->val.numeric =
					DatumGetNumeric(DirectFunctionCall1(int4_numeric,
														Int32GetDatum(size)));

				res = recursiveExecuteNext(cxt, jsp, NULL, jb, found, false);
			}
			break;
		case jpiAbs:
		case jpiFloor:
		case jpiCeiling:
			{
				JsonbValue jbvbuf;

				if (JsonbType(jb) == jbvScalar)
					jb = JsonbExtractScalar(jb->val.binary.data, &jbvbuf);

				if (jb->type == jbvNumeric)
				{
					Datum		datum = NumericGetDatum(jb->val.numeric);

					switch (jsp->type)
					{
						case jpiAbs:
							datum = DirectFunctionCall1(numeric_abs, datum);
							break;
						case jpiFloor:
							datum = DirectFunctionCall1(numeric_floor, datum);
							break;
						case jpiCeiling:
							datum = DirectFunctionCall1(numeric_ceil, datum);
							break;
						default:
							break;
					}

					jb = palloc(sizeof(*jb));

					jb->type = jbvNumeric;
					jb->val.numeric = DatumGetNumeric(datum);

					res = recursiveExecuteNext(cxt, jsp, NULL, jb, found, false);
				}
				else
					res = jperMakeError(ERRCODE_NON_NUMERIC_JSON_ITEM);
			}
			break;
		case jpiDouble:
			{
				JsonbValue jbv;
				MemoryContext mcxt = CurrentMemoryContext;

				if (JsonbType(jb) == jbvScalar)
					jb = JsonbExtractScalar(jb->val.binary.data, &jbv);

				PG_TRY();
				{
					if (jb->type == jbvNumeric)
					{
						/* only check success of numeric to double cast */
						DirectFunctionCall1(numeric_float8,
											NumericGetDatum(jb->val.numeric));
						res = jperOk;
					}
					else if (jb->type == jbvString)
					{
						/* cast string as double */
						char	   *str = pnstrdup(jb->val.string.val,
												   jb->val.string.len);
						Datum		val = DirectFunctionCall1(
											float8in, CStringGetDatum(str));
						pfree(str);

						jb = &jbv;
						jb->type = jbvNumeric;
						jb->val.numeric = DatumGetNumeric(DirectFunctionCall1(
														float8_numeric, val));
						res = jperOk;

					}
					else
						res = jperMakeError(ERRCODE_NON_NUMERIC_JSON_ITEM);
				}
				PG_CATCH();
				{
					if (ERRCODE_TO_CATEGORY(geterrcode()) !=
														ERRCODE_DATA_EXCEPTION)
						PG_RE_THROW();

					FlushErrorState();
					MemoryContextSwitchTo(mcxt);
					res = jperMakeError(ERRCODE_NON_NUMERIC_JSON_ITEM);
				}
				PG_END_TRY();

				if (res == jperOk)
					res = recursiveExecuteNext(cxt, jsp, NULL, jb, found, true);
			}
			break;
		case jpiDatetime:
			{
				JsonbValue	jbvbuf;
				Datum		value;
				Oid			typid;
				int32		typmod = -1;
				bool		hasNext;

				if (JsonbType(jb) == jbvScalar)
					jb = JsonbExtractScalar(jb->val.binary.data, &jbvbuf);

				if (jb->type == jbvNumeric && !jsp->content.arg)
				{
					/* Standard extension: unix epoch to timestamptz */
					MemoryContext mcxt = CurrentMemoryContext;

					PG_TRY();
					{
						Datum		unix_epoch =
								DirectFunctionCall1(numeric_float8,
											NumericGetDatum(jb->val.numeric));

						value = DirectFunctionCall1(float8_timestamptz,
													unix_epoch);
						typid = TIMESTAMPTZOID;
						res = jperOk;
					}
					PG_CATCH();
					{
						if (ERRCODE_TO_CATEGORY(geterrcode()) !=
														ERRCODE_DATA_EXCEPTION)
							PG_RE_THROW();

						FlushErrorState();
						MemoryContextSwitchTo(mcxt);

						res = jperMakeError(ERRCODE_INVALID_ARGUMENT_FOR_JSON_DATETIME_FUNCTION);
					}
					PG_END_TRY();
				}
				else if (jb->type == jbvString)
				{

					text	   *datetime_txt =
							cstring_to_text_with_len(jb->val.string.val,
													 jb->val.string.len);

					res = jperOk;

					if (jsp->content.arg)
					{
						text	   *template_txt;
						char	   *template_str;
						int			template_len;
						MemoryContext mcxt = CurrentMemoryContext;

						jspGetArg(jsp, &elem);

						if (elem.type != jpiString)
							elog(ERROR, "invalid jsonpath item type for .datetime() argument");

						template_str = jspGetString(&elem, &template_len);
						template_txt = cstring_to_text_with_len(template_str,
																template_len);

						PG_TRY();
						{
							value = to_datetime(datetime_txt,
												template_str, template_len,
												false,
												&typid, &typmod);
						}
						PG_CATCH();
						{
							if (ERRCODE_TO_CATEGORY(geterrcode()) !=
														ERRCODE_DATA_EXCEPTION)
								PG_RE_THROW();

							FlushErrorState();
							MemoryContextSwitchTo(mcxt);

							res = jperMakeError(ERRCODE_INVALID_ARGUMENT_FOR_JSON_DATETIME_FUNCTION);
						}
						PG_END_TRY();

						pfree(template_txt);
					}
					else
					{
						if (!tryToParseDatetime("yyyy-mm-dd HH24:MI:SS TZH:TZM",
									datetime_txt, &value, &typid, &typmod) &&
							!tryToParseDatetime("yyyy-mm-dd HH24:MI:SS TZH",
									datetime_txt, &value, &typid, &typmod) &&
							!tryToParseDatetime("yyyy-mm-dd HH24:MI:SS",
									datetime_txt, &value, &typid, &typmod) &&
							!tryToParseDatetime("yyyy-mm-dd",
									datetime_txt, &value, &typid, &typmod) &&
							!tryToParseDatetime("HH24:MI:SS TZH:TZM",
									datetime_txt, &value, &typid, &typmod) &&
							!tryToParseDatetime("HH24:MI:SS TZH",
									datetime_txt, &value, &typid, &typmod) &&
							!tryToParseDatetime("HH24:MI:SS",
									datetime_txt, &value, &typid, &typmod))
							res = jperMakeError(ERRCODE_INVALID_ARGUMENT_FOR_JSON_DATETIME_FUNCTION);
					}

					pfree(datetime_txt);
				}
				else
				{
					res = jperMakeError(ERRCODE_INVALID_ARGUMENT_FOR_JSON_DATETIME_FUNCTION);
					break;
				}

				if (jperIsError(res))
					break;

				hasNext = jspGetNext(jsp, &elem);

				if (!hasNext && !found)
					break;

				jb = hasNext ? &jbvbuf : palloc(sizeof(*jb));

				jb->type = jbvDatetime;
				jb->val.datetime.value = value;
				jb->val.datetime.typid = typid;
				jb->val.datetime.typmod = typmod;

				res = recursiveExecuteNext(cxt, jsp, &elem, jb, found, hasNext);
			}
			break;
		case jpiKeyValue:
			if (JsonbType(jb) != jbvObject)
				res = jperMakeError(ERRCODE_JSON_OBJECT_NOT_FOUND);
			else
			{
				int32		r;
				JsonbValue	key;
				JsonbValue	val;
				JsonbValue	obj;
				JsonbValue	keystr;
				JsonbValue	valstr;
				JsonbIterator *it;
				JsonbParseState *ps = NULL;

				hasNext = jspGetNext(jsp, &elem);

				if (!JsonContainerSize(jb->val.binary.data))
				{
					res = jperNotFound;
					break;
				}

				/* make template object */
				obj.type = jbvBinary;

				keystr.type = jbvString;
				keystr.val.string.val = "key";
				keystr.val.string.len = 3;

				valstr.type = jbvString;
				valstr.val.string.val = "value";
				valstr.val.string.len = 5;

				it = JsonbIteratorInit(jb->val.binary.data);

				while ((r = JsonbIteratorNext(&it, &key, true)) != WJB_DONE)
				{
					if (r == WJB_KEY)
					{
						Jsonb	   *jsonb;
						JsonbValue  *keyval;

						res = jperOk;

						if (!hasNext && !found)
							break;

						r = JsonbIteratorNext(&it, &val, true);
						Assert(r == WJB_VALUE);

						pushJsonbValue(&ps, WJB_BEGIN_OBJECT, NULL);

						pushJsonbValue(&ps, WJB_KEY, &keystr);
						pushJsonbValue(&ps, WJB_VALUE, &key);


						pushJsonbValue(&ps, WJB_KEY, &valstr);
						pushJsonbValue(&ps, WJB_VALUE, &val);

						keyval = pushJsonbValue(&ps, WJB_END_OBJECT, NULL);

						jsonb = JsonbValueToJsonb(keyval);

						obj.val.binary.data = &jsonb->root;
						obj.val.binary.len = VARSIZE(jsonb) - VARHDRSZ;

						res = recursiveExecuteNext(cxt, jsp, &elem, &obj, found, true);

						if (jperIsError(res))
							break;

						if (res == jperOk && !found)
							break;
					}
				}
			}
			break;
		case jpiStartsWith:
			res = executeStartsWithPredicate(cxt, jsp, jb);
			break;
		default:
			elog(ERROR,"Wrong state: %d", jsp->type);
	}

	return res;
}

static JsonPathExecResult
recursiveExecuteUnwrap(JsonPathExecContext *cxt, JsonPathItem *jsp,
					   JsonbValue *jb, JsonValueList *found)
{
	if (cxt->lax && JsonbType(jb) == jbvArray)
	{
		JsonbValue	v;
		JsonbIterator *it;
		JsonbIteratorToken tok;
		JsonPathExecResult res = jperNotFound;

		it = JsonbIteratorInit(jb->val.binary.data);

		while ((tok = JsonbIteratorNext(&it, &v, true)) != WJB_DONE)
		{
			if (tok == WJB_ELEM)
			{
				res = recursiveExecuteNoUnwrap(cxt, jsp, &v, found);
				if (jperIsError(res))
					break;
				if (res == jperOk && !found)
					break;
			}
		}

		return res;
	}

	return recursiveExecuteNoUnwrap(cxt, jsp, jb, found);
}

static JsonbValue *
wrapItem(JsonbValue *jbv)
{
	JsonbParseState *ps = NULL;
	Jsonb	   *jb;
	JsonbValue	jbvbuf;
	int			type = JsonbType(jbv);

	if (type == jbvArray)
		return jbv;

	if (type == jbvScalar)
		jbv = JsonbExtractScalar(jbv->val.binary.data, &jbvbuf);

	pushJsonbValue(&ps, WJB_BEGIN_ARRAY, NULL);
	pushJsonbValue(&ps, WJB_ELEM, jbv);
	jbv = pushJsonbValue(&ps, WJB_END_ARRAY, NULL);

	jb = JsonbValueToJsonb(jbv);

	jbv = palloc(sizeof(*jbv));
	jbv->type = jbvBinary;
	jbv->val.binary.data = &jb->root;
	jbv->val.binary.len = VARSIZE(jb) - VARHDRSZ;

	return jbv;
}

static JsonPathExecResult
recursiveExecute(JsonPathExecContext *cxt, JsonPathItem *jsp, JsonbValue *jb,
				 JsonValueList *found)
{
	check_stack_depth();

	if (cxt->lax)
	{
		switch (jsp->type)
		{
			case jpiKey:
			case jpiAnyKey:
		/*	case jpiAny: */
			case jpiFilter:
			case jpiPlus:
			case jpiMinus:
			/* all methods excluding type() and size() */
			case jpiAbs:
			case jpiFloor:
			case jpiCeiling:
			case jpiDouble:
			case jpiDatetime:
			case jpiKeyValue:
				return recursiveExecuteUnwrap(cxt, jsp, jb, found);

			case jpiAnyArray:
			case jpiIndexArray:
				jb = wrapItem(jb);
				break;

			default:
				break;
		}
	}

	return recursiveExecuteNoUnwrap(cxt, jsp, jb, found);
}

/*
 * Public interface to jsonpath executor
 */
JsonPathExecResult
executeJsonPath(JsonPath *path, List *vars, Jsonb *json, JsonValueList *foundJson)
{
	JsonPathExecContext cxt;
	JsonPathItem	jsp;
	JsonbValue		jbv;

	jbv.type = jbvBinary;
	jbv.val.binary.data = &json->root;
	jbv.val.binary.len = VARSIZE_ANY_EXHDR(json);

	jspInit(&jsp, path);

	cxt.vars = vars;
	cxt.lax = (path->header & JSONPATH_LAX) != 0;
	cxt.root = &jbv;
	cxt.innermostArray = NULL;

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

/*
 * Convert jsonb object into list of vars for executor
 */
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
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("passing variable json is not a object")));

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

static void
throwJsonPathError(JsonPathExecResult res)
{
	if (!jperIsError(res))
		return;

	switch (jperGetError(res))
	{
		case ERRCODE_JSON_ARRAY_NOT_FOUND:
			ereport(ERROR,
					(errcode(jperGetError(res)),
					 errmsg("SQL/JSON array not found")));
			break;
		case ERRCODE_JSON_OBJECT_NOT_FOUND:
			ereport(ERROR,
					(errcode(jperGetError(res)),
					 errmsg("SQL/JSON object not found")));
			break;
		case ERRCODE_JSON_MEMBER_NOT_FOUND:
			ereport(ERROR,
					(errcode(jperGetError(res)),
					 errmsg("SQL/JSON member not found")));
			break;
		case ERRCODE_JSON_NUMBER_NOT_FOUND:
			ereport(ERROR,
					(errcode(jperGetError(res)),
					 errmsg("SQL/JSON number not found")));
			break;
		case ERRCODE_JSON_SCALAR_REQUIRED:
			ereport(ERROR,
					(errcode(jperGetError(res)),
					 errmsg("SQL/JSON scalar required")));
			break;
		case ERRCODE_SINGLETON_JSON_ITEM_REQUIRED:
			ereport(ERROR,
					(errcode(jperGetError(res)),
					 errmsg("Singleton SQL/JSON item required")));
			break;
		case ERRCODE_NON_NUMERIC_JSON_ITEM:
			ereport(ERROR,
					(errcode(jperGetError(res)),
					 errmsg("Non-numeric SQL/JSON item")));
			break;
		case ERRCODE_INVALID_JSON_SUBSCRIPT:
			ereport(ERROR,
					(errcode(jperGetError(res)),
					 errmsg("Invalid SQL/JSON subscript")));
			break;
		case ERRCODE_INVALID_ARGUMENT_FOR_JSON_DATETIME_FUNCTION:
			ereport(ERROR,
					(errcode(jperGetError(res)),
					 errmsg("Invalid argument for SQL/JSON datetime function")));
			break;
		default:
			ereport(ERROR,
					(errcode(jperGetError(res)),
					 errmsg("Unknown SQL/JSON error")));
			break;
	}
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

	throwJsonPathError(res);

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
__jsonpath_object(FunctionCallInfo fcinfo, bool safe)
{
	FuncCallContext	*funcctx;
	List			*found;
	JsonbValue		*v;
	ListCell		*c;

	if (SRF_IS_FIRSTCALL())
	{
		JsonPath			*jp = PG_GETARG_JSONPATH(1);
		Jsonb				*jb;
		JsonPathExecResult	res;
		MemoryContext		oldcontext;
		List				*vars = NIL;
		JsonValueList		found = { 0 };

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		jb = PG_GETARG_JSONB_COPY(0);
		if (PG_NARGS() == 3)
			vars = makePassingVars(PG_GETARG_JSONB(2));

		res = executeJsonPath(jp, vars, jb, &found);

		if (jperIsError(res))
		{
			if (safe)
				JsonValueListClear(&found);
			else
				throwJsonPathError(res);
		}

		PG_FREE_IF_COPY(jp, 1);

		funcctx->user_fctx = JsonValueListGetList(&found);

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
	return __jsonpath_object(fcinfo, false);
}

Datum
_jsonpath_object3(PG_FUNCTION_ARGS)
{
	return __jsonpath_object(fcinfo, false);
}

Datum
_jsonpath_object_safe2(PG_FUNCTION_ARGS)
{
	return __jsonpath_object(fcinfo, true);
}

Datum
_jsonpath_object_safe3(PG_FUNCTION_ARGS)
{
	return __jsonpath_object(fcinfo, true);
}

/********************Interface to pgsql's executor***************************/
bool
JsonbPathExists(Jsonb *jb, JsonPath *jp, List *vars)
{
	JsonPathExecResult res = executeJsonPath(jp, vars, jb, NULL);

	throwJsonPathError(res);

	return res == jperOk;
}

static inline JsonbValue *
wrapItemsInArray(const JsonValueList *items)
{
	JsonbParseState *ps = NULL;
	JsonValueListIterator it = { 0 };
	JsonbValue *jbv;

	pushJsonbValue(&ps, WJB_BEGIN_ARRAY, NULL);

	while ((jbv = JsonValueListNext(items, &it)))
	{
		if (jbv->type == jbvBinary &&
			JsonContainerIsScalar(jbv->val.binary.data))
			JsonbExtractScalar(jbv->val.binary.data, jbv);

		pushJsonbValue(&ps, WJB_ELEM, jbv);
	}

	return pushJsonbValue(&ps, WJB_END_ARRAY, NULL);
}

Jsonb *
JsonbPathQuery(Jsonb *jb, JsonPath *jp, JsonWrapper wrapper,
			   bool *empty, List *vars)
{
	JsonbValue *first;
	bool		wrap;
	JsonValueList found = { 0 };
	JsonPathExecResult jper = executeJsonPath(jp, vars, jb, &found);
	int			count;

	throwJsonPathError(jper);

	count = JsonValueListLength(&found);

	first = count ? JsonValueListHead(&found) : NULL;

	if (!first)
		wrap = false;
	else if (wrapper == JSW_NONE)
		wrap = false;
	else if (wrapper == JSW_UNCONDITIONAL)
		wrap = true;
	else if (wrapper == JSW_CONDITIONAL)
		wrap = count > 1 ||
			   IsAJsonbScalar(first) ||
			   (first->type == jbvBinary &&
				JsonContainerIsScalar(first->val.binary.data));
	else
	{
		elog(ERROR, "unrecognized json wrapper %d", wrapper);
		wrap = false;
	}

	if (wrap)
		return JsonbValueToJsonb(wrapItemsInArray(&found));

	if (count > 1)
		ereport(ERROR,
				(errcode(ERRCODE_MORE_THAN_ONE_JSON_ITEM),
				 errmsg("more than one SQL/JSON item")));

	if (first)
		return JsonbValueToJsonb(first);

	*empty = true;
	return NULL;
}

Jsonb *
JsonbPathValue(Jsonb *jb, JsonPath *jp, bool *empty, List *vars)
{
	JsonbValue *res;
	JsonValueList found = { 0 };
	JsonPathExecResult jper = executeJsonPath(jp, vars, jb, &found);
	int			count;

	throwJsonPathError(jper);

	count = JsonValueListLength(&found);

	*empty = !count;

	if (*empty)
		return NULL;

	if (count > 1)
		ereport(ERROR,
				(errcode(ERRCODE_MORE_THAN_ONE_JSON_ITEM),
				 errmsg("more than one SQL/JSON item")));

	res = JsonValueListHead(&found);

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
