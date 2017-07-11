/*-------------------------------------------------------------------------
 *
 * jsonpath_gram.y
 *	 Grammar definitions for jsonpath datatype
 *
 * Copyright (c) 2017, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	src/backend/utils/adt/jsonpath_gram.y
 *
 *-------------------------------------------------------------------------
 */

%{
#include "postgres.h"

#include "fmgr.h"
#include "nodes/pg_list.h"
#include "utils/builtins.h"
#include "utils/jsonpath.h"

#include "utils/jsonpath_scanner.h"

/*
 * Bison doesn't allocate anything that needs to live across parser calls,
 * so we can easily have it use palloc instead of malloc.  This prevents
 * memory leaks if we error out during parsing.  Note this only works with
 * bison >= 2.0.  However, in bison 1.875 the default is to use alloca()
 * if possible, so there's not really much problem anyhow, at least if
 * you're building with gcc.
 */
#define YYMALLOC palloc
#define YYFREE   pfree

static JsonPathParseItem*
makeItemType(int type)
{
	JsonPathParseItem* v = palloc(sizeof(*v));

	v->type = type;
	v->next = NULL;

	return v;
}

static JsonPathParseItem*
makeItemString(string *s)
{
	JsonPathParseItem *v;

	if (s == NULL)
	{
		v = makeItemType(jpiNull);
	}
	else
	{
		v = makeItemType(jpiString);
		v->value.string.val = s->val;
		v->value.string.len = s->len;
	}

	return v;
}

static JsonPathParseItem*
makeItemVariable(string *s)
{
	JsonPathParseItem *v;

	v = makeItemType(jpiVariable);
	v->value.string.val = s->val;
	v->value.string.len = s->len;

	return v;
}

static JsonPathParseItem*
makeItemKey(string *s)
{
	JsonPathParseItem *v;

	v = makeItemString(s);
	v->type = jpiKey;

	return v;
}

static JsonPathParseItem*
makeItemNumeric(string *s)
{
	JsonPathParseItem		*v;

	v = makeItemType(jpiNumeric);
	v->value.numeric =
		DatumGetNumeric(DirectFunctionCall3(numeric_in, CStringGetDatum(s->val), 0, -1));

	return v;
}

static JsonPathParseItem*
makeItemBool(bool val) {
	JsonPathParseItem *v = makeItemType(jpiBool);

	v->value.boolean = val;

	return v;
}

static JsonPathParseItem*
makeItemBinary(int type, JsonPathParseItem* la, JsonPathParseItem *ra)
{
	JsonPathParseItem  *v = makeItemType(type);

	v->value.args.left = la;
	v->value.args.right = ra;

	return v;
}

static JsonPathParseItem*
makeItemUnary(int type, JsonPathParseItem* a)
{
	JsonPathParseItem  *v = makeItemType(type);

	v->value.arg = a;

	return v;
}

static JsonPathParseItem*
makeItemList(List *list)
{
	JsonPathParseItem *head, *end;
	ListCell   *cell = list_head(list);

	head = end = (JsonPathParseItem *) lfirst(cell);

	if (!lnext(cell))
		return head;

	/* append items to the end of already existing list */
	while (end->next)
		end = end->next;

	for_each_cell(cell, lnext(cell))
	{
		JsonPathParseItem *c = (JsonPathParseItem *) lfirst(cell);

		end->next = c;
		end = c;
	}

	return head;
}

static JsonPathParseItem*
makeIndexArray(List *list)
{
	JsonPathParseItem	*v = makeItemType(jpiIndexArray);
	ListCell			*cell;
	int					i = 0;

	Assert(list_length(list) > 0);
	v->value.array.nelems = list_length(list);

	v->value.array.elems = palloc(sizeof(v->value.array.elems[0]) * v->value.array.nelems);

	foreach(cell, list)
	{
		JsonPathParseItem *jpi = lfirst(cell);

		Assert(jpi->type == jpiSubscript);

		v->value.array.elems[i].from = jpi->value.args.left;
		v->value.array.elems[i++].to = jpi->value.args.right;
	}

	return v;
}

static JsonPathParseItem*
makeAny(int first, int last)
{
	JsonPathParseItem *v = makeItemType(jpiAny);

	v->value.anybounds.first = (first > 0) ? first : 0;
	v->value.anybounds.last = (last >= 0) ? last : PG_UINT32_MAX;

	return v;
}

static JsonPathParseItem *
makeItemSequence(List *elems)
{
	JsonPathParseItem  *v = makeItemType(jpiSequence);

	v->value.sequence.elems = elems;

	return v;
}

static JsonPathParseItem *
makeItemObject(List *fields)
{
	JsonPathParseItem *v = makeItemType(jpiObject);

	v->value.object.fields = fields;

	return v;
}

%}

/* BISON Declarations */
%pure-parser
%expect 0
%name-prefix="jsonpath_yy"
%error-verbose
%parse-param {JsonPathParseResult **result}

%union {
	string				str;
	List				*elems;		/* list of JsonPathParseItem */
	List				*indexs;	/* list of integers */
	JsonPathParseItem	*value;
	JsonPathParseResult *result;
	JsonPathItemType	optype;
	bool				boolean;
}

%token	<str>		TO_P NULL_P TRUE_P FALSE_P IS_P UNKNOWN_P STARTS_P WITH_P
%token	<str>		STRING_P NUMERIC_P INT_P EXISTS_P STRICT_P LAX_P LAST_P
%token	<str>		ABS_P SIZE_P TYPE_P FLOOR_P DOUBLE_P CEILING_P DATETIME_P
%token	<str>		KEYVALUE_P MAP_P REDUCE_P FOLD_P FOLDL_P FOLDR_P

%token	<str>		OR_P AND_P NOT_P
%token	<str>		LESS_P LESSEQUAL_P EQUAL_P NOTEQUAL_P GREATEREQUAL_P GREATER_P
%token	<str>		ANY_P

%type	<result>	result

%type	<value>		scalar_value path_primary expr pexpr array_accessor
					any_path accessor_op key predicate delimited_predicate
					index_elem starts_with_initial opt_datetime_template
					expr_or_predicate expr_or_seq expr_seq object_field

%type	<elems>		accessor_expr expr_list object_field_list

%type	<indexs>	index_list

%type	<optype>	comp_op method fold

%type	<boolean>	mode

%type	<str>		key_name


%left	OR_P
%left	AND_P
%right	NOT_P
%left	'+' '-'
%left	'*' '/' '%'
%left	UMINUS
%nonassoc '(' ')'

/* Grammar follows */
%%

result:
	mode expr_or_seq				{
										*result = palloc(sizeof(JsonPathParseResult));
										(*result)->expr = $2;
										(*result)->lax = $1;
									}
	| /* EMPTY */					{ *result = NULL; }
	;

expr_or_predicate:
	expr							{ $$ = $1; }
	| predicate						{ $$ = $1; }
	;

expr_or_seq:
	expr_or_predicate				{ $$ = $1; }
	| expr_seq						{ $$ = $1; }
	;

expr_seq:
	expr_list						{ $$ = makeItemSequence($1); }
	;

expr_list:
	expr_or_predicate ',' expr_or_predicate	{ $$ = list_make2($1, $3); }
	| expr_list ',' expr_or_predicate		{ $$ = lappend($1, $3); }
	;

mode:
	STRICT_P						{ $$ = false; }
	| LAX_P							{ $$ = true; }
	| /* EMPTY */					{ $$ = true; }
	;

scalar_value:
	STRING_P						{ $$ = makeItemString(&$1); }
	| NULL_P						{ $$ = makeItemString(NULL); }
	| TRUE_P						{ $$ = makeItemBool(true); }
	| FALSE_P						{ $$ = makeItemBool(false); }
	| NUMERIC_P						{ $$ = makeItemNumeric(&$1); }
	| INT_P							{ $$ = makeItemNumeric(&$1); }
	| '$' STRING_P					{ $$ = makeItemVariable(&$2); }
	| '$' INT_P						{ $$ = makeItemVariable(&$2); }
	;

comp_op:
	EQUAL_P							{ $$ = jpiEqual; }
	| NOTEQUAL_P					{ $$ = jpiNotEqual; }
	| LESS_P						{ $$ = jpiLess; }
	| GREATER_P						{ $$ = jpiGreater; }
	| LESSEQUAL_P					{ $$ = jpiLessOrEqual; }
	| GREATEREQUAL_P				{ $$ = jpiGreaterOrEqual; }
	;

delimited_predicate:
	'(' predicate ')'					{ $$ = $2; }
	| EXISTS_P '(' accessor_expr ')'	{ $$ = makeItemUnary(jpiExists, makeItemList($3)); }
	;

predicate:
	delimited_predicate				{ $$ = $1; }
	| pexpr comp_op pexpr			{ $$ = makeItemBinary($2, $1, $3); }
	| predicate AND_P predicate		{ $$ = makeItemBinary(jpiAnd, $1, $3); }
	| predicate OR_P predicate		{ $$ = makeItemBinary(jpiOr, $1, $3); }
	| NOT_P delimited_predicate		{ $$ = makeItemUnary(jpiNot, $2); }
	| '(' predicate ')' IS_P UNKNOWN_P	{ $$ = makeItemUnary(jpiIsUnknown, $2); }
	| pexpr STARTS_P WITH_P starts_with_initial
		{ $$ = makeItemBinary(jpiStartsWith, $1, $4); }
/* Left for the future (needs XQuery support)
	| pexpr LIKE_REGEX pattern [FLAG_P flags]	{ $$ = ...; };
*/
	;

starts_with_initial:
	STRING_P						{ $$ = makeItemString(&$1); }
	| '$' STRING_P					{ $$ = makeItemVariable(&$2); }
	;

path_primary:
	scalar_value					{ $$ = $1; }
	| '$'							{ $$ = makeItemType(jpiRoot); }
	| '@'							{ $$ = makeItemType(jpiCurrent); }
	| LAST_P						{ $$ = makeItemType(jpiLast); }
	| '(' expr_seq ')'				{ $$ = $2; }
	| '[' ']'						{ $$ = makeItemUnary(jpiArray, NULL); }
	| '[' expr_or_seq ']'			{ $$ = makeItemUnary(jpiArray, $2); }
	| '{' object_field_list '}'		{ $$ = makeItemObject($2); }
	;

object_field_list:
	/* EMPTY */								{ $$ = NIL; }
	| object_field							{ $$ = list_make1($1); }
	| object_field_list ',' object_field	{ $$ = lappend($1, $3); }
	;

object_field:
	key_name ':' expr_or_predicate
		{ $$ = makeItemBinary(jpiObjectField, makeItemString(&$1), $3); }
	;

accessor_expr:
	path_primary					{ $$ = list_make1($1); }
	| '.' key						{ $$ = list_make2(makeItemType(jpiCurrent), $2); }
	| '(' expr ')' accessor_op		{ $$ = list_make2($2, $4); }
	| '(' predicate ')'	accessor_op	{ $$ = list_make2($2, $4); }
	| accessor_expr accessor_op		{ $$ = lappend($1, $2); }
	;

pexpr:
	expr							{ $$ = $1; }
	| '(' expr ')'					{ $$ = $2; }
	;

expr:
	accessor_expr						{ $$ = makeItemList($1); }
	| '+' pexpr %prec UMINUS			{ $$ = makeItemUnary(jpiPlus, $2); }
	| '-' pexpr %prec UMINUS			{ $$ = makeItemUnary(jpiMinus, $2); }
	| pexpr '+' pexpr					{ $$ = makeItemBinary(jpiAdd, $1, $3); }
	| pexpr '-' pexpr					{ $$ = makeItemBinary(jpiSub, $1, $3); }
	| pexpr '*' pexpr					{ $$ = makeItemBinary(jpiMul, $1, $3); }
	| pexpr '/' pexpr					{ $$ = makeItemBinary(jpiDiv, $1, $3); }
	| pexpr '%' pexpr					{ $$ = makeItemBinary(jpiMod, $1, $3); }
	;

index_elem:
	pexpr							{ $$ = makeItemBinary(jpiSubscript, $1, NULL); }
	| pexpr TO_P pexpr				{ $$ = makeItemBinary(jpiSubscript, $1, $3); }
	;

index_list:
	index_elem						{ $$ = list_make1($1); }
	| index_list ',' index_elem		{ $$ = lappend($1, $3); }
	;

array_accessor:
	'[' '*' ']'						{ $$ = makeItemType(jpiAnyArray); }
	| '[' index_list ']'			{ $$ = makeIndexArray($2); }
	;

any_path:
	ANY_P							{ $$ = makeAny(-1, -1); }
	| ANY_P '{' INT_P '}'			{ $$ = makeAny(pg_atoi($3.val, 4, 0),
												   pg_atoi($3.val, 4, 0)); }
	| ANY_P '{' ',' INT_P '}'		{ $$ = makeAny(-1, pg_atoi($4.val, 4, 0)); }
	| ANY_P '{' INT_P ',' '}'		{ $$ = makeAny(pg_atoi($3.val, 4, 0), -1); }
	| ANY_P '{' INT_P ',' INT_P '}'	{ $$ = makeAny(pg_atoi($3.val, 4, 0),
												   pg_atoi($5.val, 4, 0)); }
	;

accessor_op:
	'.' key							{ $$ = $2; }
	| '.' '*'						{ $$ = makeItemType(jpiAnyKey); }
	| array_accessor				{ $$ = $1; }
	| '.' array_accessor			{ $$ = $2; }
	| '.' any_path					{ $$ = $2; }
	| '.' method '(' ')'			{ $$ = makeItemType($2); }
	| '.' DATETIME_P '(' opt_datetime_template ')'
									{ $$ = makeItemUnary(jpiDatetime, $4); }
	| '.' MAP_P '(' expr_or_predicate ')'
									{ $$ = makeItemUnary(jpiMap, $4); }
	| '.' REDUCE_P '(' expr_or_predicate ')'
									{ $$ = makeItemUnary(jpiReduce, $4); }
	| '.' fold '(' expr_or_predicate ',' expr_or_predicate ')'
									{ $$ = makeItemBinary($2, $4, $6); }
	| '?' '(' predicate ')'			{ $$ = makeItemUnary(jpiFilter, $3); }
	;

fold:
	FOLD_P							{ $$ = jpiFold; }
	| FOLDL_P						{ $$ = jpiFoldl; }
	| FOLDR_P						{ $$ = jpiFoldr; }
	;

opt_datetime_template:
	STRING_P						{ $$ = makeItemString(&$1); }
	| /* EMPTY */					{ $$ = NULL; }
	;

key:
	key_name						{ $$ = makeItemKey(&$1); }
	;

key_name:
	STRING_P
	| TO_P
	| NULL_P
	| TRUE_P
	| FALSE_P
	| INT_P
	| IS_P
	| UNKNOWN_P
	| EXISTS_P
	| STRICT_P
	| LAX_P
	| ABS_P
	| SIZE_P
	| TYPE_P
	| FLOOR_P
	| DOUBLE_P
	| CEILING_P
	| DATETIME_P
	| KEYVALUE_P
	| LAST_P
	| STARTS_P
	| WITH_P
	| MAP_P
	| REDUCE_P
	| FOLD_P
	| FOLDL_P
	| FOLDR_P
	;

method:
	ABS_P							{ $$ = jpiAbs; }
	| SIZE_P						{ $$ = jpiSize; }
	| TYPE_P						{ $$ = jpiType; }
	| FLOOR_P						{ $$ = jpiFloor; }
	| DOUBLE_P						{ $$ = jpiDouble; }
	| CEILING_P						{ $$ = jpiCeiling; }
	| KEYVALUE_P					{ $$ = jpiKeyValue; }
	;
%%

