-- JSON_OBJECT()
SELECT JSON_OBJECT();
SELECT JSON_OBJECT(RETURNING json);
SELECT JSON_OBJECT(RETURNING json FORMAT JSON);
SELECT JSON_OBJECT(RETURNING json FORMAT JSONB);
SELECT JSON_OBJECT(RETURNING jsonb);
SELECT JSON_OBJECT(RETURNING jsonb FORMAT JSON);
SELECT JSON_OBJECT(RETURNING jsonb FORMAT JSONB);
SELECT JSON_OBJECT(RETURNING text);
SELECT JSON_OBJECT(RETURNING text FORMAT JSON);
SELECT JSON_OBJECT(RETURNING text FORMAT JSON ENCODING UTF8);
SELECT JSON_OBJECT(RETURNING text FORMAT JSON ENCODING INVALID_ENCODING);
SELECT JSON_OBJECT(RETURNING text FORMAT JSONB);
SELECT JSON_OBJECT(RETURNING text FORMAT JSONB ENCODING UTF8);
SELECT JSON_OBJECT(RETURNING bytea);
SELECT JSON_OBJECT(RETURNING bytea FORMAT JSON);
SELECT JSON_OBJECT(RETURNING bytea FORMAT JSON ENCODING UTF8);
SELECT JSON_OBJECT(RETURNING bytea FORMAT JSON ENCODING UTF16);
SELECT JSON_OBJECT(RETURNING bytea FORMAT JSON ENCODING UTF32);
SELECT JSON_OBJECT(RETURNING bytea FORMAT JSONB);

SELECT JSON_OBJECT(NULL: 1);
SELECT JSON_OBJECT('a': 2 + 3);
SELECT JSON_OBJECT('a' VALUE 2 + 3);
--SELECT JSON_OBJECT(KEY 'a' VALUE 2 + 3);
SELECT JSON_OBJECT('a' || 2: 1);
SELECT JSON_OBJECT(('a' || 2) VALUE 1);
--SELECT JSON_OBJECT('a' || 2 VALUE 1);
--SELECT JSON_OBJECT(KEY 'a' || 2 VALUE 1);
SELECT JSON_OBJECT('a': 2::text);
SELECT JSON_OBJECT('a' VALUE 2::text);
--SELECT JSON_OBJECT(KEY 'a' VALUE 2::text);
SELECT JSON_OBJECT(1::text: 2);
SELECT JSON_OBJECT((1::text) VALUE 2);
--SELECT JSON_OBJECT(1::text VALUE 2);
--SELECT JSON_OBJECT(KEY 1::text VALUE 2);
SELECT JSON_OBJECT(json '[1]': 123);
SELECT JSON_OBJECT(ARRAY[1,2,3]: 'aaa');

SELECT JSON_OBJECT(
	'a': '123',
	1.23: 123,
	'c': json '[ 1,true,{ } ]',
	'd': jsonb '{ "x" : 123.45 }'
);

SELECT JSON_OBJECT(
	'a': '123',
	1.23: 123,
	'c': json '[ 1,true,{ } ]',
	'd': jsonb '{ "x" : 123.45 }'
	RETURNING jsonb
);

/*
SELECT JSON_OBJECT(
	'a': '123',
	KEY 1.23 VALUE 123,
	'c' VALUE json '[1, true, {}]'
);
*/

SELECT JSON_OBJECT('a': '123', 'b': JSON_OBJECT('a': 111, 'b': 'aaa'));
SELECT JSON_OBJECT('a': '123', 'b': JSON_OBJECT('a': 111, 'b': 'aaa' RETURNING jsonb));

SELECT JSON_OBJECT('a': JSON_OBJECT('b': 1 RETURNING text));
SELECT JSON_OBJECT('a': JSON_OBJECT('b': 1 RETURNING text) FORMAT JSON);
SELECT JSON_OBJECT('a': JSON_OBJECT('b': 1 RETURNING text) FORMAT JSONB);
SELECT JSON_OBJECT('a': JSON_OBJECT('b': 1 RETURNING bytea));
SELECT JSON_OBJECT('a': JSON_OBJECT('b': 1 RETURNING bytea) FORMAT JSON);
SELECT JSON_OBJECT('a': JSON_OBJECT('b': 1 RETURNING bytea FORMAT JSONB));
SELECT JSON_OBJECT('a': JSON_OBJECT('b': 1 RETURNING bytea FORMAT JSONB) FORMAT JSONB);
SELECT JSON_OBJECT('a': JSON_OBJECT('b': 1 RETURNING bytea FORMAT JSON) FORMAT JSONB);

SELECT JSON_OBJECT('a': '1', 'b': NULL, 'c': 2);
SELECT JSON_OBJECT('a': '1', 'b': NULL, 'c': 2 NULL ON NULL);
SELECT JSON_OBJECT('a': '1', 'b': NULL, 'c': 2 ABSENT ON NULL);

SELECT JSON_OBJECT(1: 1, '1': NULL WITH UNIQUE);
SELECT JSON_OBJECT(1: 1, '1': NULL ABSENT ON NULL WITH UNIQUE);
SELECT JSON_OBJECT(1: 1, '1': NULL NULL ON NULL WITH UNIQUE RETURNING jsonb);
SELECT JSON_OBJECT(1: 1, '1': NULL ABSENT ON NULL WITH UNIQUE RETURNING jsonb);

SELECT JSON_OBJECT(1: 1, '2': NULL, '1': 1 NULL ON NULL WITH UNIQUE);
SELECT JSON_OBJECT(1: 1, '2': NULL, '1': 1 ABSENT ON NULL WITH UNIQUE);
SELECT JSON_OBJECT(1: 1, '2': NULL, '1': 1 ABSENT ON NULL WITHOUT UNIQUE);
SELECT JSON_OBJECT(1: 1, '2': NULL, '1': 1 ABSENT ON NULL WITH UNIQUE RETURNING jsonb);
SELECT JSON_OBJECT(1: 1, '2': NULL, '1': 1 ABSENT ON NULL WITHOUT UNIQUE RETURNING jsonb);
SELECT JSON_OBJECT(1: 1, '2': NULL, '3': 1, 4: NULL, '5': 'a' ABSENT ON NULL WITH UNIQUE RETURNING jsonb);


-- JSON_ARRAY()
SELECT JSON_ARRAY();
SELECT JSON_ARRAY(RETURNING json);
SELECT JSON_ARRAY(RETURNING json FORMAT JSON);
SELECT JSON_ARRAY(RETURNING json FORMAT JSONB);
SELECT JSON_ARRAY(RETURNING jsonb);
SELECT JSON_ARRAY(RETURNING jsonb FORMAT JSON);
SELECT JSON_ARRAY(RETURNING jsonb FORMAT JSONB);
SELECT JSON_ARRAY(RETURNING text);
SELECT JSON_ARRAY(RETURNING text FORMAT JSON);
SELECT JSON_ARRAY(RETURNING text FORMAT JSON ENCODING UTF8);
SELECT JSON_ARRAY(RETURNING text FORMAT JSON ENCODING INVALID_ENCODING);
SELECT JSON_ARRAY(RETURNING text FORMAT JSONB);
SELECT JSON_ARRAY(RETURNING text FORMAT JSONB ENCODING UTF8);
SELECT JSON_ARRAY(RETURNING bytea);
SELECT JSON_ARRAY(RETURNING bytea FORMAT JSON);
SELECT JSON_ARRAY(RETURNING bytea FORMAT JSON ENCODING UTF8);
SELECT JSON_ARRAY(RETURNING bytea FORMAT JSON ENCODING UTF16);
SELECT JSON_ARRAY(RETURNING bytea FORMAT JSON ENCODING UTF32);
SELECT JSON_ARRAY(RETURNING bytea FORMAT JSONB);

SELECT JSON_ARRAY('aaa', 111, true, array[1,2,3], NULL, json '{"a": [1]}', jsonb '["a",3]');

SELECT JSON_ARRAY('a',  NULL, 'b' NULL   ON NULL);
SELECT JSON_ARRAY('a',  NULL, 'b' ABSENT ON NULL);
SELECT JSON_ARRAY(NULL, NULL, 'b' ABSENT ON NULL);
SELECT JSON_ARRAY('a',  NULL, 'b' NULL   ON NULL RETURNING jsonb);
SELECT JSON_ARRAY('a',  NULL, 'b' ABSENT ON NULL RETURNING jsonb);
SELECT JSON_ARRAY(NULL, NULL, 'b' ABSENT ON NULL RETURNING jsonb);

SELECT JSON_ARRAY(JSON_ARRAY('{ "a" : 123 }' RETURNING text));
SELECT JSON_ARRAY(JSON_ARRAY('{ "a" : 123 }' FORMAT JSON RETURNING text));
SELECT JSON_ARRAY(JSON_ARRAY('{ "a" : 123 }' FORMAT JSON RETURNING text) FORMAT JSON);
SELECT JSON_ARRAY(JSON_ARRAY('{ "a" : 123 }' FORMAT JSON RETURNING text) FORMAT JSONB);
SELECT JSON_ARRAY(JSON_ARRAY('{ "a" : 123 }' FORMAT JSON RETURNING bytea FORMAT JSONB));
SELECT JSON_ARRAY(JSON_ARRAY('{ "a" : 123 }' FORMAT JSON RETURNING bytea FORMAT JSONB) FORMAT JSONB);

SELECT JSON_ARRAY(SELECT i FROM (VALUES (1), (2), (NULL), (4)) foo(i));
SELECT JSON_ARRAY(SELECT i FROM (VALUES (NULL::int[]), ('{1,2}'), (NULL), (NULL), ('{3,4}'), (NULL)) foo(i));
SELECT JSON_ARRAY(SELECT i FROM (VALUES (NULL::int[]), ('{1,2}'), (NULL), (NULL), ('{3,4}'), (NULL)) foo(i) RETURNING jsonb);
--SELECT JSON_ARRAY(SELECT i FROM (VALUES (NULL::int[]), ('{1,2}'), (NULL), (NULL), ('{3,4}'), (NULL)) foo(i) NULL ON NULL);
--SELECT JSON_ARRAY(SELECT i FROM (VALUES (NULL::int[]), ('{1,2}'), (NULL), (NULL), ('{3,4}'), (NULL)) foo(i) NULL ON NULL RETURNING jsonb);
SELECT JSON_ARRAY(SELECT i FROM (VALUES (3), (1), (NULL), (2)) foo(i) ORDER BY i);

-- JSON_ARRAYAGG()
SELECT	JSON_ARRAYAGG(i) IS NULL,
		JSON_ARRAYAGG(i RETURNING jsonb) IS NULL
FROM generate_series(1, 0) i;

SELECT	JSON_ARRAYAGG(i),
		JSON_ARRAYAGG(i RETURNING jsonb)
FROM generate_series(1, 5) i;

SELECT JSON_ARRAYAGG(i ORDER BY i DESC)
FROM generate_series(1, 5) i;

SELECT JSON_ARRAYAGG(i::text::json)
FROM generate_series(1, 5) i;

SELECT JSON_ARRAYAGG(JSON_ARRAY(i, i + 1 RETURNING text) FORMAT JSON)
FROM generate_series(1, 5) i;

SELECT	JSON_ARRAYAGG(NULL),
		JSON_ARRAYAGG(NULL RETURNING jsonb)
FROM generate_series(1, 5);

SELECT	JSON_ARRAYAGG(NULL NULL ON NULL),
		JSON_ARRAYAGG(NULL NULL ON NULL RETURNING jsonb)
FROM generate_series(1, 5);

SELECT
	JSON_ARRAYAGG(bar),
	JSON_ARRAYAGG(bar RETURNING jsonb),
	JSON_ARRAYAGG(bar ABSENT ON NULL),
	JSON_ARRAYAGG(bar ABSENT ON NULL RETURNING jsonb),
	JSON_ARRAYAGG(bar NULL ON NULL),
	JSON_ARRAYAGG(bar NULL ON NULL RETURNING jsonb),
	JSON_ARRAYAGG(foo),
	JSON_ARRAYAGG(foo RETURNING jsonb),
	JSON_ARRAYAGG(foo ORDER BY bar) FILTER (WHERE bar > 2),
	JSON_ARRAYAGG(foo ORDER BY bar RETURNING jsonb) FILTER (WHERE bar > 2)
FROM
	(VALUES (NULL), (3), (1), (NULL), (NULL), (5), (2), (4), (NULL)) foo(bar);


-- JSON_OBJECTAGG()
SELECT	JSON_OBJECTAGG('key': 1) IS NULL,
		JSON_OBJECTAGG('key': 1 RETURNING jsonb) IS NULL
WHERE FALSE;

SELECT JSON_OBJECTAGG(NULL: 1);

SELECT JSON_OBJECTAGG(NULL: 1 RETURNING jsonb);

SELECT
	JSON_OBJECTAGG(i: i),
--	JSON_OBJECTAGG(i VALUE i),
--	JSON_OBJECTAGG(KEY i VALUE i),
	JSON_OBJECTAGG(i: i RETURNING jsonb)
FROM
	generate_series(1, 5) i;

SELECT
	JSON_OBJECTAGG(k: v),
	JSON_OBJECTAGG(k: v NULL ON NULL),
	JSON_OBJECTAGG(k: v ABSENT ON NULL),
	JSON_OBJECTAGG(k: v RETURNING jsonb),
	JSON_OBJECTAGG(k: v NULL ON NULL RETURNING jsonb),
	JSON_OBJECTAGG(k: v ABSENT ON NULL RETURNING jsonb)
FROM
	(VALUES (1, 1), (1, NULL), (2, NULL), (3, 3)) foo(k, v);

SELECT JSON_OBJECTAGG(k: v WITH UNIQUE KEYS)
FROM (VALUES (1, 1), (1, NULL), (2, 2)) foo(k, v);

SELECT JSON_OBJECTAGG(k: v ABSENT ON NULL WITH UNIQUE KEYS)
FROM (VALUES (1, 1), (1, NULL), (2, 2)) foo(k, v);

SELECT JSON_OBJECTAGG(k: v ABSENT ON NULL WITH UNIQUE KEYS)
FROM (VALUES (1, 1), (0, NULL), (3, NULL), (2, 2), (4, NULL)) foo(k, v);

SELECT JSON_OBJECTAGG(k: v WITH UNIQUE KEYS RETURNING jsonb)
FROM (VALUES (1, 1), (1, NULL), (2, 2)) foo(k, v);

SELECT JSON_OBJECTAGG(k: v ABSENT ON NULL WITH UNIQUE KEYS RETURNING jsonb)
FROM (VALUES (1, 1), (1, NULL), (2, 2)) foo(k, v);

-- IS JSON predicate
SELECT NULL IS JSON;
SELECT NULL IS NOT JSON;
SELECT NULL::json IS JSON;
SELECT NULL::jsonb IS JSON;
SELECT NULL::text IS JSON;
SELECT NULL::bytea IS JSON;
SELECT NULL::int IS JSON;

SELECT '' FORMAT JSONB IS JSON;

SELECT bytea '\x00' IS JSON;
SELECT bytea '\x00' FORMAT JSON IS JSON;
SELECT bytea '\x00' FORMAT JSONB IS JSON;

CREATE TABLE test_is_json (js text);

INSERT INTO test_is_json VALUES
 (NULL),
 (''),
 ('123'),
 ('"aaa "'),
 ('true'),
 ('null'),
 ('[]'),
 ('[1, "2", {}]'),
 ('{}'),
 ('{ "a": 1, "b": null }'),
 ('{ "a": 1, "a": null }'),
 ('{ "a": 1, "b": [{ "a": 1 }, { "a": 2 }] }'),
 ('{ "a": 1, "b": [{ "a": 1, "b": 0, "a": 2 }] }'),
 ('aaa'),
 ('{a:1}'),
 ('["a",]');

SELECT
	js,
	js IS JSON "IS JSON",
	js IS NOT JSON "IS NOT JSON",
	js IS JSON VALUE "IS VALUE",
	js IS JSON OBJECT "IS OBJECT",
	js IS JSON ARRAY "IS ARRAY",
	js IS JSON SCALAR "IS SCALAR",
	js IS JSON WITHOUT UNIQUE KEYS "WITHOUT UNIQUE",
	js IS JSON WITH UNIQUE KEYS "WITH UNIQUE",
	js FORMAT JSON IS JSON "FORMAT JSON IS JSON"
FROM
	test_is_json;

SELECT
	js,
	js IS JSON "IS JSON",
	js IS NOT JSON "IS NOT JSON",
	js IS JSON VALUE "IS VALUE",
	js IS JSON OBJECT "IS OBJECT",
	js IS JSON ARRAY "IS ARRAY",
	js IS JSON SCALAR "IS SCALAR",
	js IS JSON WITHOUT UNIQUE KEYS "WITHOUT UNIQUE",
	js IS JSON WITH UNIQUE KEYS "WITH UNIQUE",
	js FORMAT JSON IS JSON "FORMAT JSON IS JSON"
FROM
	(SELECT js::json FROM test_is_json WHERE js IS JSON) foo(js);

SELECT
	js0,
	js IS JSON "IS JSON",
	js IS NOT JSON "IS NOT JSON",
	js IS JSON VALUE "IS VALUE",
	js IS JSON OBJECT "IS OBJECT",
	js IS JSON ARRAY "IS ARRAY",
	js IS JSON SCALAR "IS SCALAR",
	js IS JSON WITHOUT UNIQUE KEYS "WITHOUT UNIQUE",
	js IS JSON WITH UNIQUE KEYS "WITH UNIQUE",
	js FORMAT JSON IS JSON "FORMAT JSON IS JSON",
	js FORMAT JSONB IS JSON "FORMAT JSONB IS JSON"
FROM
	(SELECT js, js::bytea FROM test_is_json WHERE js IS JSON) foo(js0, js);

SELECT
	js,
	js IS JSON "IS JSON",
	js IS NOT JSON "IS NOT JSON",
	js IS JSON VALUE "IS VALUE",
	js IS JSON OBJECT "IS OBJECT",
	js IS JSON ARRAY "IS ARRAY",
	js IS JSON SCALAR "IS SCALAR",
	js IS JSON WITHOUT UNIQUE KEYS "WITHOUT UNIQUE",
	js IS JSON WITH UNIQUE KEYS "WITH UNIQUE",
	js FORMAT JSON IS JSON "FORMAT JSON IS JSON"
FROM
	(SELECT js::jsonb FROM test_is_json WHERE js IS JSON) foo(js);

SELECT
	js0,
	js FORMAT JSONB IS JSON "IS JSON",
	js FORMAT JSONB IS NOT JSON "IS NOT JSON",
	js FORMAT JSONB IS JSON VALUE "IS VALUE",
	js FORMAT JSONB IS JSON OBJECT "IS OBJECT",
	js FORMAT JSONB IS JSON ARRAY "IS ARRAY",
	js FORMAT JSONB IS JSON SCALAR "IS SCALAR",
	js FORMAT JSONB IS JSON WITHOUT UNIQUE KEYS "WITHOUT UNIQUE",
	js FORMAT JSONB IS JSON WITH UNIQUE KEYS "WITH UNIQUE"
FROM
	(SELECT js, js::jsonb::bytea FROM test_is_json WHERE js IS JSON) foo(js0, js);

-- JSON_EXISTS

SELECT JSON_EXISTS(NULL, '$');
SELECT JSON_EXISTS(NULL::text, '$');
SELECT JSON_EXISTS(NULL::bytea, '$');
SELECT JSON_EXISTS(NULL::json, '$');
SELECT JSON_EXISTS(NULL::jsonb, '$');
SELECT JSON_EXISTS(NULL FORMAT JSON, '$');
SELECT JSON_EXISTS(NULL FORMAT JSONB, '$');

SELECT JSON_EXISTS('' FORMAT JSONB, '$');
SELECT JSON_EXISTS('' FORMAT JSONB, '$' TRUE ON ERROR);
SELECT JSON_EXISTS('' FORMAT JSONB, '$' FALSE ON ERROR);
SELECT JSON_EXISTS('' FORMAT JSONB, '$' UNKNOWN ON ERROR);
SELECT JSON_EXISTS('' FORMAT JSONB, '$' ERROR ON ERROR);

SELECT JSON_EXISTS(bytea '' FORMAT JSONB, '$' ERROR ON ERROR);

SELECT JSON_EXISTS(jsonb '[]', '$');
SELECT JSON_EXISTS('[]' FORMAT JSONB, '$');
SELECT JSON_EXISTS(JSON_OBJECT(RETURNING bytea FORMAT JSONB) FORMAT JSONB, '$');

SELECT JSON_EXISTS(jsonb '1', '$');
SELECT JSON_EXISTS(jsonb 'null', '$');
SELECT JSON_EXISTS(jsonb '[]', '$');

SELECT JSON_EXISTS(jsonb '1', '$.a');
SELECT JSON_EXISTS(jsonb 'null', '$.a');
SELECT JSON_EXISTS(jsonb '[]', '$.a');
SELECT JSON_EXISTS(jsonb '[1, "aaa", {"a": 1}]', '$.a');
SELECT JSON_EXISTS(jsonb '{}', '$.a');
SELECT JSON_EXISTS(jsonb '{"b": 1, "a": 2}', '$.a');

SELECT JSON_EXISTS(jsonb '1', '$.a.b');
SELECT JSON_EXISTS(jsonb '{"a": {"b": 1}}', '$.a.b');
SELECT JSON_EXISTS(jsonb '{"a": 1, "b": 2}', '$.a.b');

SELECT JSON_EXISTS(jsonb '{"a": 1, "b": 2}', '$.* ? (@ > $x)' PASSING 1 AS x);
SELECT JSON_EXISTS(jsonb '{"a": 1, "b": 2}', '$.* ? (@ > $x)' PASSING '1' AS x);
SELECT JSON_EXISTS(jsonb '{"a": 1, "b": 2}', '$.* ? (@ > $x && @ < $y)' PASSING 0 AS x, 2 AS y);
SELECT JSON_EXISTS(jsonb '{"a": 1, "b": 2}', '$.* ? (@ > $x && @ < $y)' PASSING 0 AS x, 1 AS y);

-- JSON_VALUE

SELECT JSON_VALUE(NULL, '$');
SELECT JSON_VALUE(NULL::text, '$');
SELECT JSON_VALUE(NULL::bytea, '$');
SELECT JSON_VALUE(NULL::json, '$');
SELECT JSON_VALUE(NULL::jsonb, '$');
SELECT JSON_VALUE(NULL FORMAT JSON, '$');
SELECT JSON_VALUE(NULL FORMAT JSONB, '$');

SELECT JSON_VALUE('' FORMAT JSONB, '$');
SELECT JSON_VALUE('' FORMAT JSONB, '$' NULL ON ERROR);
SELECT JSON_VALUE('' FORMAT JSONB, '$' DEFAULT '"default value"' ON ERROR);
SELECT JSON_VALUE('' FORMAT JSONB, '$' ERROR ON ERROR);

SELECT JSON_VALUE(jsonb 'null', '$');
SELECT JSON_VALUE(jsonb 'null', '$' RETURNING int);

SELECT JSON_VALUE(jsonb 'true', '$');
SELECT JSON_VALUE(jsonb 'true', '$' RETURNING bool);

SELECT JSON_VALUE(jsonb '123', '$');
SELECT JSON_VALUE(jsonb '123', '$' RETURNING int) + 234;
SELECT JSON_VALUE(jsonb '123', '$' RETURNING text);
/* XXX jsonb bytea ??? */
SELECT JSON_VALUE(jsonb '123', '$' RETURNING bytea);

SELECT JSON_VALUE(jsonb '1.23', '$');
SELECT JSON_VALUE(jsonb '"1.23"', '$' RETURNING numeric);

SELECT JSON_VALUE(jsonb '"aaa"', '$');
SELECT JSON_VALUE(jsonb '"aaa"', '$' RETURNING text);
SELECT JSON_VALUE(jsonb '"aaa"', '$' RETURNING char(5));
SELECT JSON_VALUE(jsonb '"aaa"', '$' RETURNING char(2));
SELECT JSON_VALUE(jsonb '"aaa"', '$' RETURNING json);
SELECT JSON_VALUE(jsonb '"aaa"', '$' RETURNING jsonb);
SELECT JSON_VALUE(jsonb '"aaa"', '$' RETURNING json ERROR ON ERROR);
SELECT JSON_VALUE(jsonb '"aaa"', '$' RETURNING jsonb ERROR ON ERROR);
SELECT JSON_VALUE(jsonb '"\"aaa\""', '$' RETURNING json);
SELECT JSON_VALUE(jsonb '"\"aaa\""', '$' RETURNING jsonb);
SELECT JSON_VALUE(jsonb '"aaa"', '$' RETURNING int);
SELECT JSON_VALUE(jsonb '"aaa"', '$' RETURNING int ERROR ON ERROR);
SELECT JSON_VALUE(jsonb '"aaa"', '$' RETURNING int DEFAULT 111 ON ERROR);
SELECT JSON_VALUE(jsonb '"123"', '$' RETURNING int) + 234;

SELECT JSON_VALUE(jsonb '"2017-02-20"', '$' RETURNING date) + 9;

-- Test NULL checks execution in domain types
CREATE DOMAIN int_not_null AS int NOT NULL;
SELECT JSON_VALUE(jsonb '1', '$.a' RETURNING int_not_null);
SELECT JSON_VALUE(jsonb '1', '$.a' RETURNING int_not_null NULL ON ERROR);
SELECT JSON_VALUE(jsonb '1', '$.a' RETURNING int_not_null DEFAULT NULL ON ERROR);
DROP DOMAIN int_not_null;

SELECT JSON_VALUE(jsonb '[]', '$');
SELECT JSON_VALUE(jsonb '[]', '$' ERROR ON ERROR);
SELECT JSON_VALUE(jsonb '{}', '$');
SELECT JSON_VALUE(jsonb '{}', '$' ERROR ON ERROR);

SELECT JSON_VALUE(jsonb '1', '$.a');
SELECT JSON_VALUE(jsonb '1', '$.a' ERROR ON ERROR);
SELECT JSON_VALUE(jsonb '1', '$.a' DEFAULT 2 ON ERROR);
SELECT JSON_VALUE(jsonb '1', '$.a' DEFAULT '2' ON ERROR);
SELECT JSON_VALUE(jsonb '1', '$.a' NULL ON EMPTY DEFAULT '2' ON ERROR);
SELECT JSON_VALUE(jsonb '1', '$.a' DEFAULT '2' ON EMPTY DEFAULT '3' ON ERROR);
SELECT JSON_VALUE(jsonb '1', '$.a' ERROR ON EMPTY DEFAULT '3' ON ERROR);

SELECT JSON_VALUE(jsonb '[1,2]', '$[*]' ERROR ON ERROR);
SELECT JSON_VALUE(jsonb '[1,2]', '$[*]' DEFAULT '0' ON ERROR);
SELECT JSON_VALUE(jsonb '[" "]', '$[*]' RETURNING int ERROR ON ERROR);
SELECT JSON_VALUE(jsonb '[" "]', '$[*]' RETURNING int DEFAULT 2 + 3 ON ERROR);
SELECT JSON_VALUE(jsonb '["1"]', '$[*]' RETURNING int DEFAULT 2 + 3 ON ERROR);

SELECT
	x,
	JSON_VALUE(
		jsonb '{"a": 1, "b": 2}',
		'$.* ? (@ > $x)' PASSING x AS x
		RETURNING int
		DEFAULT -1 ON EMPTY
		DEFAULT -2 ON ERROR
	) y
FROM
	generate_series(0, 2) x;

-- JSON_QUERY

SELECT
	JSON_QUERY(js FORMAT JSONB, '$'),
	JSON_QUERY(js FORMAT JSONB, '$' WITHOUT WRAPPER),
	JSON_QUERY(js FORMAT JSONB, '$' WITH CONDITIONAL WRAPPER),
	JSON_QUERY(js FORMAT JSONB, '$' WITH UNCONDITIONAL ARRAY WRAPPER),
	JSON_QUERY(js FORMAT JSONB, '$' WITH ARRAY WRAPPER)
FROM
	(VALUES
		('null'),
		('12.3'),
		('true'),
		('"aaa"'),
		('[1, null, "2"]'),
		('{"a": 1, "b": [2]}')
	) foo(js);

SELECT
	JSON_QUERY(js FORMAT JSONB, '$[*]') AS "unspec",
	JSON_QUERY(js FORMAT JSONB, '$[*]' WITHOUT WRAPPER) AS "without",
	JSON_QUERY(js FORMAT JSONB, '$[*]' WITH CONDITIONAL WRAPPER) AS "with cond",
	JSON_QUERY(js FORMAT JSONB, '$[*]' WITH UNCONDITIONAL ARRAY WRAPPER) AS "with uncond",
	JSON_QUERY(js FORMAT JSONB, '$[*]' WITH ARRAY WRAPPER) AS "with"
FROM
	(VALUES
		('1'),
		('[]'),
		('[null]'),
		('[12.3]'),
		('[true]'),
		('["aaa"]'),
		('[[1, 2, 3]]'),
		('[{"a": 1, "b": [2]}]'),
		('[1, "2", null, [3]]')
	) foo(js);

SELECT JSON_QUERY('"aaa"' FORMAT JSONB, '$' RETURNING text);
SELECT JSON_QUERY('"aaa"' FORMAT JSONB, '$' RETURNING text KEEP QUOTES);
SELECT JSON_QUERY('"aaa"' FORMAT JSONB, '$' RETURNING text KEEP QUOTES ON SCALAR STRING);
SELECT JSON_QUERY('"aaa"' FORMAT JSONB, '$' RETURNING text OMIT QUOTES);
SELECT JSON_QUERY('"aaa"' FORMAT JSONB, '$' RETURNING text OMIT QUOTES ON SCALAR STRING);
SELECT JSON_QUERY('"aaa"' FORMAT JSONB, '$' OMIT QUOTES ERROR ON ERROR);
SELECT JSON_QUERY('"aaa"' FORMAT JSONB, '$' RETURNING json OMIT QUOTES ERROR ON ERROR);
SELECT JSON_QUERY('"aaa"' FORMAT JSONB, '$' RETURNING bytea FORMAT JSON OMIT QUOTES ERROR ON ERROR);
SELECT JSON_QUERY('"aaa"' FORMAT JSONB, '$' RETURNING bytea FORMAT JSONB OMIT QUOTES ERROR ON ERROR);

-- QUOTES behavior should not be specified when WITH WRAPPER used:
-- Should fail
SELECT JSON_QUERY(jsonb '[1]', '$' WITH WRAPPER OMIT QUOTES);
SELECT JSON_QUERY(jsonb '[1]', '$' WITH WRAPPER KEEP QUOTES);
SELECT JSON_QUERY(jsonb '[1]', '$' WITH CONDITIONAL WRAPPER KEEP QUOTES);
SELECT JSON_QUERY(jsonb '[1]', '$' WITH CONDITIONAL WRAPPER OMIT QUOTES);
-- Should succeed
SELECT JSON_QUERY(jsonb '[1]', '$' WITHOUT WRAPPER OMIT QUOTES);
SELECT JSON_QUERY(jsonb '[1]', '$' WITHOUT WRAPPER KEEP QUOTES);

SELECT JSON_QUERY('[]' FORMAT JSONB, '$[*]');
SELECT JSON_QUERY('[]' FORMAT JSONB, '$[*]' NULL ON EMPTY);
SELECT JSON_QUERY('[]' FORMAT JSONB, '$[*]' EMPTY ARRAY ON EMPTY);
SELECT JSON_QUERY('[]' FORMAT JSONB, '$[*]' EMPTY OBJECT ON EMPTY);
SELECT JSON_QUERY('[]' FORMAT JSONB, '$[*]' ERROR ON EMPTY);

SELECT JSON_QUERY('[]' FORMAT JSONB, '$[*]' ERROR ON EMPTY NULL ON ERROR);
SELECT JSON_QUERY('[]' FORMAT JSONB, '$[*]' ERROR ON EMPTY EMPTY ARRAY ON ERROR);
SELECT JSON_QUERY('[]' FORMAT JSONB, '$[*]' ERROR ON EMPTY EMPTY OBJECT ON ERROR);
SELECT JSON_QUERY('[]' FORMAT JSONB, '$[*]' ERROR ON EMPTY ERROR ON ERROR);
SELECT JSON_QUERY('[]' FORMAT JSONB, '$[*]' ERROR ON ERROR);

SELECT JSON_QUERY('[1,2]' FORMAT JSONB, '$[*]' ERROR ON ERROR);

SELECT JSON_QUERY(jsonb '[1,2]', '$' RETURNING json);
SELECT JSON_QUERY(jsonb '[1,2]', '$' RETURNING json FORMAT JSON);
SELECT JSON_QUERY(jsonb '[1,2]', '$' RETURNING jsonb);
SELECT JSON_QUERY(jsonb '[1,2]', '$' RETURNING jsonb FORMAT JSON);
SELECT JSON_QUERY(jsonb '[1,2]', '$' RETURNING text);
SELECT JSON_QUERY(jsonb '[1,2]', '$' RETURNING char(10));
SELECT JSON_QUERY(jsonb '[1,2]', '$' RETURNING char(3));
SELECT JSON_QUERY(jsonb '[1,2]', '$' RETURNING text FORMAT JSON);
SELECT JSON_QUERY(jsonb '[1,2]', '$' RETURNING text FORMAT JSONB);
SELECT JSON_QUERY(jsonb '[1,2]', '$' RETURNING bytea);
SELECT JSON_QUERY(jsonb '[1,2]', '$' RETURNING bytea FORMAT JSON);
SELECT JSON_QUERY(jsonb '[1,2]', '$' RETURNING bytea FORMAT JSONB);

SELECT JSON_QUERY(jsonb '[1,2]', '$[*]' RETURNING bytea EMPTY OBJECT ON ERROR);
SELECT JSON_QUERY(jsonb '[1,2]', '$[*]' RETURNING bytea FORMAT JSON EMPTY OBJECT ON ERROR);
SELECT JSON_QUERY(jsonb '[1,2]', '$[*]' RETURNING bytea FORMAT JSONB EMPTY OBJECT ON ERROR);
SELECT JSON_QUERY(jsonb '[1,2]', '$[*]' RETURNING json EMPTY OBJECT ON ERROR);
SELECT JSON_QUERY(jsonb '[1,2]', '$[*]' RETURNING jsonb EMPTY OBJECT ON ERROR);

SELECT
	x, y,
	JSON_QUERY(
		jsonb '[1,2,3,4,5,null]',
		'$[*] ? (@ >= $x && @ <= $y)'
		PASSING x AS x, y AS y
		WITH CONDITIONAL WRAPPER
		EMPTY ARRAY ON EMPTY
	) list
FROM
	generate_series(0, 4) x,
	generate_series(0, 4) y;

-- Test constraints

CREATE TABLE test_json_constraints (
	js text,
	i int,
	x jsonb DEFAULT JSON_QUERY(jsonb '[1,2]', '$[*]' WITH WRAPPER)
	CONSTRAINT test_json_constraint1
		CHECK (js IS JSON)
	CONSTRAINT test_json_constraint2
		CHECK (JSON_EXISTS(js FORMAT JSONB, '$.a' PASSING i + 5 AS int, i::text AS txt))
	CONSTRAINT test_json_constraint3
		CHECK (JSON_VALUE(js::jsonb, '$.a' RETURNING int DEFAULT ('12' || i)::int ON EMPTY ERROR ON ERROR) > i)
	CONSTRAINT test_json_constraint4
		CHECK (JSON_QUERY(js FORMAT JSONB, '$.a' WITH CONDITIONAL WRAPPER EMPTY OBJECT ON ERROR) < jsonb '[10]')
);

\d test_json_constraints

SELECT check_clause
FROM information_schema.check_constraints
WHERE constraint_name LIKE 'test_json_constraint%';

SELECT adsrc FROM pg_attrdef WHERE adrelid = 'test_json_constraints'::regclass;

INSERT INTO test_json_constraints VALUES ('', 1);
INSERT INTO test_json_constraints VALUES ('1', 1);
INSERT INTO test_json_constraints VALUES ('[]');
INSERT INTO test_json_constraints VALUES ('{"b": 1}', 1);
INSERT INTO test_json_constraints VALUES ('{"a": 1}', 1);
INSERT INTO test_json_constraints VALUES ('{"a": 7}', 1);
INSERT INTO test_json_constraints VALUES ('{"a": 10}', 1);

DROP TABLE test_json_constraints;

-- JSON_TABLE

-- Should fail (JSON_TABLE can be used only in FROM clause)
SELECT JSON_TABLE('[]', '$');

-- Should fail (no columns)
SELECT * FROM JSON_TABLE(NULL, '$' COLUMNS ());

-- NULL => empty table
SELECT * FROM JSON_TABLE(NULL, '$' COLUMNS (foo int)) bar;

-- invalid json => empty table
SELECT * FROM JSON_TABLE('', '$' COLUMNS (foo int)) bar;
SELECT * FROM JSON_TABLE('' FORMAT JSON,  '$' COLUMNS (foo int)) bar;
SELECT * FROM JSON_TABLE('' FORMAT JSONB, '$' COLUMNS (foo int)) bar;

-- invalid json => error
SELECT * FROM JSON_TABLE('' FORMAT JSONB, '$' COLUMNS (foo int) ERROR ON ERROR) bar;

--
SELECT * FROM JSON_TABLE('123', '$'
	COLUMNS (item int PATH '$', foo int)) bar;

SELECT * FROM JSON_TABLE('123' FORMAT JSONB, '$'
	COLUMNS (item int PATH '$', foo int)) bar;

SELECT * FROM JSON_TABLE(jsonb '123', '$'
	COLUMNS (item int PATH '$', foo int)) bar;

-- JSON_TABLE: basic functionality
SELECT *
FROM
	(VALUES
		('1'),
		('[]'),
		('{}'),
		('[1, 1.23, "2", "aaaaaaa", null, false, true, {"aaa": 123}, "[1,2]", "\"str\""]'),
		('err')
	) vals(js)
	LEFT OUTER JOIN
-- JSON_TABLE is implicitly lateral
	JSON_TABLE(
		vals.js, '$[*]'
		COLUMNS (
			id FOR ORDINALITY,
			id2 FOR ORDINALITY, -- XXX allowed additional ordinality columns
			"int" int PATH '$',
			"text" text PATH '$',
			"char(4)" char(4) PATH '$',
			"bool" bool PATH '$',
			"numeric" numeric PATH '$',
			js json PATH '$',
			jb jsonb PATH '$',
			jst text    FORMAT JSON  PATH '$',
			jsc char(4) FORMAT JSON  PATH '$',
			jsv varchar(4) FORMAT JSON  PATH '$',
			jsb jsonb   FORMAT JSONB PATH '$',
			aaa int, -- implicit path '$."aaa"',
			aaa1 int PATH '$.aaa'
		)
	) jt
	ON true;

-- JSON_TABLE: ON EMPTY/ON ERROR behavior
SELECT *
FROM
	(VALUES ('1'), ('err'), ('"err"')) vals(js),
	JSON_TABLE(vals.js, '$' COLUMNS (a int PATH '$')) jt;

SELECT *
FROM
	(VALUES ('1'), ('err'), ('"err"')) vals(js)
		LEFT OUTER JOIN
	JSON_TABLE(vals.js, '$' COLUMNS (a int PATH '$') ERROR ON ERROR) jt
		ON true;

SELECT *
FROM
	(VALUES ('1'), ('err'), ('"err"')) vals(js)
		LEFT OUTER JOIN
	JSON_TABLE(vals.js, '$' COLUMNS (a int PATH '$' ERROR ON ERROR)) jt
		ON true;

SELECT * FROM JSON_TABLE('1', '$' COLUMNS (a int PATH '$.a' ERROR ON EMPTY)) jt;
SELECT * FROM JSON_TABLE('1', '$' COLUMNS (a int PATH '$.a' ERROR ON EMPTY)) jt;
SELECT * FROM JSON_TABLE('1', '$' COLUMNS (a int PATH '$.a' ERROR ON EMPTY) ERROR ON ERROR) jt;

SELECT * FROM JSON_TABLE('"a"', '$' COLUMNS (a int PATH '$'   DEFAULT 1 ON EMPTY DEFAULT 2 ON ERROR)) jt;
SELECT * FROM JSON_TABLE('"a"', '$' COLUMNS (a int PATH '$.a' DEFAULT 1 ON EMPTY DEFAULT 2 ON ERROR)) jt;

-- JSON_TABLE: nested paths and plans

-- Should fail (JSON_TABLE columns shall contain explicit AS path
-- specifications if explicit PLAN clause is used)
SELECT * FROM JSON_TABLE(
	jsonb '[]', '$' -- AS <path name> required here
	COLUMNS (
		foo int PATH '$'
	)
	PLAN DEFAULT (UNION)
) jt;

SELECT * FROM JSON_TABLE(
	jsonb '[]', '$' AS path1
	COLUMNS (
		NESTED PATH '$' COLUMNS ( -- AS <path name> required here
			foo int PATH '$'
		)
	)
	PLAN DEFAULT (UNION)
) jt;

-- Should fail (column names anf path names shall be distinct)
SELECT * FROM JSON_TABLE(
	jsonb '[]', '$' AS a
	COLUMNS (
		a int
	)
) jt;

SELECT * FROM JSON_TABLE(
	jsonb '[]', '$' AS a
	COLUMNS (
		b int,
		NESTED PATH '$' AS a
		COLUMNS (
			c int
		)
	)
) jt;

SELECT * FROM JSON_TABLE(
	jsonb '[]', '$'
	COLUMNS (
		b int,
		NESTED PATH '$' AS b
		COLUMNS (
			c int
		)
	)
) jt;

SELECT * FROM JSON_TABLE(
	jsonb '[]', '$'
	COLUMNS (
		NESTED PATH '$' AS a
		COLUMNS (
			b int
		),
		NESTED PATH '$'
		COLUMNS (
			NESTED PATH '$' AS a
			COLUMNS (
				c int
			)
		)
	)
) jt;

-- JSON_TABLE: plan validation

SELECT * FROM JSON_TABLE(
	'null', '$[*]' AS p0
	COLUMNS (
		NESTED PATH '$' AS p1 COLUMNS (
			NESTED PATH '$' AS p11 COLUMNS ( foo int ),
			NESTED PATH '$' AS p12 COLUMNS ( bar int )
		),
		NESTED PATH '$' AS p2 COLUMNS (
			NESTED PATH '$' AS p21 COLUMNS ( baz int )
		)
	)
	PLAN (p1)
) jt;

SELECT * FROM JSON_TABLE(
	'null', '$[*]' AS p0
	COLUMNS (
		NESTED PATH '$' AS p1 COLUMNS (
			NESTED PATH '$' AS p11 COLUMNS ( foo int ),
			NESTED PATH '$' AS p12 COLUMNS ( bar int )
		),
		NESTED PATH '$' AS p2 COLUMNS (
			NESTED PATH '$' AS p21 COLUMNS ( baz int )
		)
	)
	PLAN (p0)
) jt;

SELECT * FROM JSON_TABLE(
	'null', '$[*]' AS p0
	COLUMNS (
		NESTED PATH '$' AS p1 COLUMNS (
			NESTED PATH '$' AS p11 COLUMNS ( foo int ),
			NESTED PATH '$' AS p12 COLUMNS ( bar int )
		),
		NESTED PATH '$' AS p2 COLUMNS (
			NESTED PATH '$' AS p21 COLUMNS ( baz int )
		)
	)
	PLAN (p0 OUTER p3)
) jt;

SELECT * FROM JSON_TABLE(
	'null', '$[*]' AS p0
	COLUMNS (
		NESTED PATH '$' AS p1 COLUMNS (
			NESTED PATH '$' AS p11 COLUMNS ( foo int ),
			NESTED PATH '$' AS p12 COLUMNS ( bar int )
		),
		NESTED PATH '$' AS p2 COLUMNS (
			NESTED PATH '$' AS p21 COLUMNS ( baz int )
		)
	)
	PLAN (p0 OUTER (p1 CROSS p13))
) jt;

SELECT * FROM JSON_TABLE(
	'null', '$[*]' AS p0
	COLUMNS (
		NESTED PATH '$' AS p1 COLUMNS (
			NESTED PATH '$' AS p11 COLUMNS ( foo int ),
			NESTED PATH '$' AS p12 COLUMNS ( bar int )
		),
		NESTED PATH '$' AS p2 COLUMNS (
			NESTED PATH '$' AS p21 COLUMNS ( baz int )
		)
	)
	PLAN (p0 OUTER (p1 CROSS p2))
) jt;

SELECT * FROM JSON_TABLE(
	'null', '$[*]' AS p0
	COLUMNS (
		NESTED PATH '$' AS p1 COLUMNS (
			NESTED PATH '$' AS p11 COLUMNS ( foo int ),
			NESTED PATH '$' AS p12 COLUMNS ( bar int )
		),
		NESTED PATH '$' AS p2 COLUMNS (
			NESTED PATH '$' AS p21 COLUMNS ( baz int )
		)
	)
	PLAN (p0 OUTER ((p1 UNION p11) CROSS p2))
) jt;

SELECT * FROM JSON_TABLE(
	'null', '$[*]' AS p0
	COLUMNS (
		NESTED PATH '$' AS p1 COLUMNS (
			NESTED PATH '$' AS p11 COLUMNS ( foo int ),
			NESTED PATH '$' AS p12 COLUMNS ( bar int )
		),
		NESTED PATH '$' AS p2 COLUMNS (
			NESTED PATH '$' AS p21 COLUMNS ( baz int )
		)
	)
	PLAN (p0 OUTER ((p1 INNER p11) CROSS p2))
) jt;

SELECT * FROM JSON_TABLE(
	'null', '$[*]' AS p0
	COLUMNS (
		NESTED PATH '$' AS p1 COLUMNS (
			NESTED PATH '$' AS p11 COLUMNS ( foo int ),
			NESTED PATH '$' AS p12 COLUMNS ( bar int )
		),
		NESTED PATH '$' AS p2 COLUMNS (
			NESTED PATH '$' AS p21 COLUMNS ( baz int )
		)
	)
	PLAN (p0 OUTER ((p1 INNER (p12 CROSS p11)) CROSS p2))
) jt;

SELECT * FROM JSON_TABLE(
	'null', '$[*]' AS p0
	COLUMNS (
		NESTED PATH '$' AS p1 COLUMNS (
			NESTED PATH '$' AS p11 COLUMNS ( foo int ),
			NESTED PATH '$' AS p12 COLUMNS ( bar int )
		),
		NESTED PATH '$' AS p2 COLUMNS (
			NESTED PATH '$' AS p21 COLUMNS ( baz int )
		)
	)
	PLAN (p0 OUTER ((p1 INNER (p12 CROSS p11)) CROSS (p2 INNER p21)))
) jt;

SELECT * FROM JSON_TABLE(
	'null', '$[*]' -- without root path name
	COLUMNS (
		NESTED PATH '$' AS p1 COLUMNS (
			NESTED PATH '$' AS p11 COLUMNS ( foo int ),
			NESTED PATH '$' AS p12 COLUMNS ( bar int )
		),
		NESTED PATH '$' AS p2 COLUMNS (
			NESTED PATH '$' AS p21 COLUMNS ( baz int )
		)
	)
	PLAN ((p1 INNER (p12 CROSS p11)) CROSS (p2 INNER p21))
) jt;

-- JSON_TABLE: plan execution

CREATE TEMP TABLE json_table_test (js text);

INSERT INTO json_table_test
VALUES (
	'[
		{"a":  1,  "b": [], "c": []},
		{"a":  2,  "b": [1, 2, 3], "c": [10, null, 20]},
		{"a":  3,  "b": [1, 2], "c": []}, 
		{"x": "4", "b": [1, 2], "c": 123}
	 ]'
);

-- unspecified plan (outer, union)
select
	jt.*
from
	json_table_test jtt,
	json_table (
		jtt.js,'$[*]' as p
		columns (
			n for ordinality,
			a int default -1 on empty,
			nested path '$.b[*]' as pb columns ( b int path '$' ),
			nested path '$.c[*]' as pc columns ( c int path '$' )
		)
	) jt;

-- default plan (outer, union)
select
	jt.*
from
	json_table_test jtt,
	json_table (
		jtt.js,'$[*]' as p
		columns (
			n for ordinality,
			a int default -1 on empty,
			nested path '$.b[*]' as pb columns ( b int path '$' ),
			nested path '$.c[*]' as pc columns ( c int path '$' )
		)
		plan default (outer, union)
	) jt;

-- specific plan (p outer (pb union pc))
select
	jt.*
from
	json_table_test jtt,
	json_table (
		jtt.js,'$[*]' as p
		columns (
			n for ordinality,
			a int default -1 on empty,
			nested path '$.b[*]' as pb columns ( b int path '$' ),
			nested path '$.c[*]' as pc columns ( c int path '$' )
		)
		plan (p outer (pb union pc))
	) jt;

-- specific plan (p outer (pc union pb))
select
	jt.*
from
	json_table_test jtt,
	json_table (
		jtt.js,'$[*]' as p
		columns (
			n for ordinality,
			a int default -1 on empty,
			nested path '$.b[*]' as pb columns ( b int path '$' ),
			nested path '$.c[*]' as pc columns ( c int path '$' )
		)
		plan (p outer (pc union pb))
	) jt;

-- default plan (inner, union)
select
	jt.*
from
	json_table_test jtt,
	json_table (
		jtt.js,'$[*]' as p
		columns (
			n for ordinality,
			a int default -1 on empty,
			nested path '$.b[*]' as pb columns ( b int path '$' ),
			nested path '$.c[*]' as pc columns ( c int path '$' )
		)
		plan default (inner)
	) jt;

-- specific plan (p inner (pb union pc))
select
	jt.*
from
	json_table_test jtt,
	json_table (
		jtt.js,'$[*]' as p
		columns (
			n for ordinality,
			a int default -1 on empty,
			nested path '$.b[*]' as pb columns ( b int path '$' ),
			nested path '$.c[*]' as pc columns ( c int path '$' )
		)
		plan (p inner (pb union pc))
	) jt;

-- default plan (inner, cross)
select
	jt.*
from
	json_table_test jtt,
	json_table (
		jtt.js,'$[*]' as p
		columns (
			n for ordinality,
			a int default -1 on empty,
			nested path '$.b[*]' as pb columns ( b int path '$' ),
			nested path '$.c[*]' as pc columns ( c int path '$' )
		)
		plan default (cross, inner)
	) jt;

-- specific plan (p inner (pb cross pc))
select
	jt.*
from
	json_table_test jtt,
	json_table (
		jtt.js,'$[*]' as p
		columns (
			n for ordinality,
			a int default -1 on empty,
			nested path '$.b[*]' as pb columns ( b int path '$' ),
			nested path '$.c[*]' as pc columns ( c int path '$' )
		)
		plan (p inner (pb cross pc))
	) jt;

-- default plan (outer, cross)
select
	jt.*
from
	json_table_test jtt,
	json_table (
		jtt.js,'$[*]' as p
		columns (
			n for ordinality,
			a int default -1 on empty,
			nested path '$.b[*]' as pb columns ( b int path '$' ),
			nested path '$.c[*]' as pc columns ( c int path '$' )
		)
		plan default (outer, cross)
	) jt;

-- specific plan (p outer (pb cross pc))
select
	jt.*
from
	json_table_test jtt,
	json_table (
		jtt.js,'$[*]' as p
		columns (
			n for ordinality,
			a int default -1 on empty,
			nested path '$.b[*]' as pb columns ( b int path '$' ),
			nested path '$.c[*]' as pc columns ( c int path '$' )
		)
		plan (p outer (pb cross pc))
	) jt;


select
	jt.*, b1 + 100 as b
from
	json_table (
		'[
			{"a":  1,  "b": [[1, 10], [2], [3, 30, 300]], "c": [1, null, 2]},
			{"a":  2,  "b": [10, 20], "c": [1, null, 2]}, 
			{"x": "3", "b": [11, 22, 33, 44]}
		 ]', 
		'$[*]' as p
		columns (
			n for ordinality,
			a int default -1 on error,
			nested path '$.b[*]' as pb columns (
				b text format json path '$', 
				nested path '$[*]' as pb1 columns (
					b1 int path '$'
				)
			),
			nested path '$.c[*]' as pc columns (
				c text format json path '$',
				nested path '$[*]' as pc1 columns (
					c1 int path '$'
				)
			)
		)
		--plan default(outer, cross)
		plan(p outer ((pb inner pb1) cross (pc outer pc1)))
	) jt;

-- Should succeed (JSON arguments are passed to root and nested paths)
SELECT *
FROM
	generate_series(1, 4) x,
	generate_series(1, 3) y,
	JSON_TABLE(
		'[[1,2,3],[2,3,4,5],[3,4,5,6]]',
		'$[*] ? (@.[*] < $x)'
		PASSING x AS x, y AS y
		COLUMNS (
			y text FORMAT JSON PATH '$',
			NESTED PATH '$[*] ? (@ >= $y)'
			COLUMNS (
				z int PATH '$'
			)
		)
	) jt;

-- Should fail (JSON arguments are not passed to column paths)
SELECT *
FROM JSON_TABLE(
	'[1,2,3]',
	'$[*] ? (@ < $x)'
		PASSING 10 AS x
		COLUMNS (y text FORMAT JSON PATH '$ ? (@ < $x)')
	) jt;

--jsonpath io

select '$'::jsonpath;
select '$.a'::jsonpath;
select '$.a.v'::jsonpath;
select '$.a.*'::jsonpath;
select '$.*.[*]'::jsonpath;
select '$.*[*]'::jsonpath;
select '$.a.[*]'::jsonpath;
select '$.a[*]'::jsonpath;
select '$.a.[*][*]'::jsonpath;
select '$.a.[*].[*]'::jsonpath;
select '$.a[*][*]'::jsonpath;
select '$.a[*].[*]'::jsonpath;
select '$[*]'::jsonpath;
select '$[0]'::jsonpath;
select '$[*][0]'::jsonpath;
select '$[*].a'::jsonpath;
select '$[*][0].a.b'::jsonpath;
select '$.a.**.b'::jsonpath;
select '$.a.**{2}.b'::jsonpath;
select '$.a.**{2,2}.b'::jsonpath;
select '$.a.**{2,5}.b'::jsonpath;
select '$.a.**{,5}.b'::jsonpath;
select '$.a.**{5,}.b'::jsonpath;

select '$.g ? (@ = 1)'::jsonpath;
select '$.g ? (a = 1)'::jsonpath;
select '$.g ? (.a = 1)'::jsonpath;
select '$.g ? (@.a = 1)'::jsonpath;
select '$.g ? (@.a = 1 || a = 4)'::jsonpath;
select '$.g ? (@.a = 1 && a = 4)'::jsonpath;
select '$.g ? (@.a = 1 || a = 4 && b = 7)'::jsonpath;
select '$.g ? (@.a = 1 || !(a = 4) && b = 7)'::jsonpath;
select '$.g ? (@.a = 1 || !(x >= 123 || a = 4) && b = 7)'::jsonpath;
select '$.g ? (.x >= @[*]?(@.a > "abc"))'::jsonpath;

select '$.g ? (zip = $zip)'::jsonpath;
select '$.a.[1,2, 3 to 16]'::jsonpath;
select '$.a[1,2, 3 to 16]'::jsonpath;

select '$ ? (a < 1)'::jsonpath;
select '$ ? (a < -1)'::jsonpath;
select '$ ? (a < +1)'::jsonpath;
select '$ ? (a < .1)'::jsonpath;
select '$ ? (a < -.1)'::jsonpath;
select '$ ? (a < +.1)'::jsonpath;
select '$ ? (a < 0.1)'::jsonpath;
select '$ ? (a < -0.1)'::jsonpath;
select '$ ? (a < +0.1)'::jsonpath;
select '$ ? (a < 10.1)'::jsonpath;
select '$ ? (a < -10.1)'::jsonpath;
select '$ ? (a < +10.1)'::jsonpath;
select '$ ? (a < 1e1)'::jsonpath;
select '$ ? (a < -1e1)'::jsonpath;
select '$ ? (a < +1e1)'::jsonpath;
select '$ ? (a < .1e1)'::jsonpath;
select '$ ? (a < -.1e1)'::jsonpath;
select '$ ? (a < +.1e1)'::jsonpath;
select '$ ? (a < 0.1e1)'::jsonpath;
select '$ ? (a < -0.1e1)'::jsonpath;
select '$ ? (a < +0.1e1)'::jsonpath;
select '$ ? (a < 10.1e1)'::jsonpath;
select '$ ? (a < -10.1e1)'::jsonpath;
select '$ ? (a < +10.1e1)'::jsonpath;
select '$ ? (a < 1e-1)'::jsonpath;
select '$ ? (a < -1e-1)'::jsonpath;
select '$ ? (a < +1e-1)'::jsonpath;
select '$ ? (a < .1e-1)'::jsonpath;
select '$ ? (a < -.1e-1)'::jsonpath;
select '$ ? (a < +.1e-1)'::jsonpath;
select '$ ? (a < 0.1e-1)'::jsonpath;
select '$ ? (a < -0.1e-1)'::jsonpath;
select '$ ? (a < +0.1e-1)'::jsonpath;
select '$ ? (a < 10.1e-1)'::jsonpath;
select '$ ? (a < -10.1e-1)'::jsonpath;
select '$ ? (a < +10.1e-1)'::jsonpath;
select '$ ? (a < 1e+1)'::jsonpath;
select '$ ? (a < -1e+1)'::jsonpath;
select '$ ? (a < +1e+1)'::jsonpath;
select '$ ? (a < .1e+1)'::jsonpath;
select '$ ? (a < -.1e+1)'::jsonpath;
select '$ ? (a < +.1e+1)'::jsonpath;
select '$ ? (a < 0.1e+1)'::jsonpath;
select '$ ? (a < -0.1e+1)'::jsonpath;
select '$ ? (a < +0.1e+1)'::jsonpath;
select '$ ? (a < 10.1e+1)'::jsonpath;
select '$ ? (a < -10.1e+1)'::jsonpath;
select '$ ? (a < +10.1e+1)'::jsonpath;

select _jsonpath_exist('$.a.b', '{"a": 12}');
select _jsonpath_exist('$.b', '{"a": 12}');
select _jsonpath_exist('$.a.a', '{"a": {"a": 12}}');
select _jsonpath_exist('$.*.a', '{"a": {"a": 12}}');
select _jsonpath_exist('$.*.a', '{"b": {"a": 12}}');
select _jsonpath_exist('$.*', '{}');
select _jsonpath_exist('$.*', '{"a": 1}');
select _jsonpath_exist('$.**{1}', '{"a": {"b": 1}}');
select _jsonpath_exist('$.**{2}', '{"a": {"b": 1}}');
select _jsonpath_exist('$.**{3}', '{"a": {"b": 1}}');

select _jsonpath_exist('$.[*]', '[]');
select _jsonpath_exist('$.[*]', '[1]');
select _jsonpath_exist('$.[1]', '[1]');
select _jsonpath_exist('$.[0]', '[1]');
select _jsonpath_exist('$ ? (@.a[*] >  @.b[*])', '{"a": [1,2,3], "b": [3,4,5]}');
select _jsonpath_exist('$ ? (@.a[*] >= @.b[*])', '{"a": [1,2,3], "b": [3,4,5]}');
select _jsonpath_exist('$ ? (@.a[*] >= @.b[*])', '{"a": [1,2,3], "b": [3,4,"5"]}');
select _jsonpath_exist('$ ? (@.a[*] >= @.b[*])', '{"a": [1,2,3], "b": [3,4,null]}');

select * from _jsonpath_object('$.a', '{"a": 12, "b": {"a": 13}}');
select * from _jsonpath_object('$.b', '{"a": 12, "b": {"a": 13}}');
select * from _jsonpath_object('$.*', '{"a": 12, "b": {"a": 13}}');
select * from _jsonpath_object('$.*.a', '{"a": 12, "b": {"a": 13}}');
select * from _jsonpath_object('$.[*].a', '[12, {"a": 13}, {"b": 14}]');
select * from _jsonpath_object('$.[*].*', '[12, {"a": 13}, {"b": 14}]');
select * from _jsonpath_object('$.[0].a', '[12, {"a": 13}, {"b": 14}]');
select * from _jsonpath_object('$.[1].a', '[12, {"a": 13}, {"b": 14}]');
select * from _jsonpath_object('$.[2].a', '[12, {"a": 13}, {"b": 14}]');
select * from _jsonpath_object('$.[0,1].a', '[12, {"a": 13}, {"b": 14}]');
select * from _jsonpath_object('$.[0 to 10].a', '[12, {"a": 13}, {"b": 14}]');

select * from _jsonpath_object('$', '{"a": 10}');
select * from _jsonpath_object('$ ? (.a < $value)', '{"a": 10}');
select * from _jsonpath_object('$ ? (.a < $value)', '{"a": 10}', '{"value" : 13}');
select * from _jsonpath_object('$ ? (.a < $value)', '{"a": 10}', '{"value" : 8}');
select * from _jsonpath_object('$.a ? (@ < $value)', '{"a": 10}', '{"value" : 13}');
select * from _jsonpath_object('$.[*] ? (@ < $value)', '[10,11,12,13,14,15]', '{"value" : 13}');
select * from _jsonpath_object('$.[0,1] ? (@ < $value)', '[10,11,12,13,14,15]', '{"value" : 13}');
select * from _jsonpath_object('$.[0 to 2] ? (@ < $value)', '[10,11,12,13,14,15]', '{"value" : 15}');
select * from _jsonpath_object('$.[*] ? (@ = "1")', '[1,"1",2,"2",null]');
select * from _jsonpath_object('$.[*] ? (@ = $value)', '[1,"1",2,"2",null]', '{"value" : "1"}');

select * from _jsonpath_object('$.**', '{"a": {"b": 1}}');
select * from _jsonpath_object('$.**{1}', '{"a": {"b": 1}}');
select * from _jsonpath_object('$.**{1,}', '{"a": {"b": 1}}');
select * from _jsonpath_object('$.**{2}', '{"a": {"b": 1}}');
select * from _jsonpath_object('$.**{2,}', '{"a": {"b": 1}}');
select * from _jsonpath_object('$.**{3,}', '{"a": {"b": 1}}');
select * from _jsonpath_object('$.**.b ? ( @ > 0)', '{"a": {"b": 1}}');
select * from _jsonpath_object('$.**{0}.b ? ( @ > 0)', '{"a": {"b": 1}}');
select * from _jsonpath_object('$.**{1}.b ? ( @ > 0)', '{"a": {"b": 1}}');
select * from _jsonpath_object('$.**{0,}.b ? ( @ > 0)', '{"a": {"b": 1}}');
select * from _jsonpath_object('$.**{1,}.b ? ( @ > 0)', '{"a": {"b": 1}}');
select * from _jsonpath_object('$.**{1,2}.b ? ( @ > 0)', '{"a": {"b": 1}}');
select * from _jsonpath_object('$.**.b ? ( @ > 0)', '{"a": {"c": {"b": 1}}}');
select * from _jsonpath_object('$.**{0}.b ? ( @ > 0)', '{"a": {"c": {"b": 1}}}');
select * from _jsonpath_object('$.**{1}.b ? ( @ > 0)', '{"a": {"c": {"b": 1}}}');
select * from _jsonpath_object('$.**{0,}.b ? ( @ > 0)', '{"a": {"c": {"b": 1}}}');
select * from _jsonpath_object('$.**{1,}.b ? ( @ > 0)', '{"a": {"c": {"b": 1}}}');
select * from _jsonpath_object('$.**{1,2}.b ? ( @ > 0)', '{"a": {"c": {"b": 1}}}');
select * from _jsonpath_object('$.**{2,3}.b ? ( @ > 0)', '{"a": {"c": {"b": 1}}}');

select * from _jsonpath_exist('$.**.b ? ( @ > 0)', '{"a": {"b": 1}}');
select * from _jsonpath_exist('$.**{0}.b ? ( @ > 0)', '{"a": {"b": 1}}');
select * from _jsonpath_exist('$.**{1}.b ? ( @ > 0)', '{"a": {"b": 1}}');
select * from _jsonpath_exist('$.**{0,}.b ? ( @ > 0)', '{"a": {"b": 1}}');
select * from _jsonpath_exist('$.**{1,}.b ? ( @ > 0)', '{"a": {"b": 1}}');
select * from _jsonpath_exist('$.**{1,2}.b ? ( @ > 0)', '{"a": {"b": 1}}');
select * from _jsonpath_exist('$.**.b ? ( @ > 0)', '{"a": {"c": {"b": 1}}}');
select * from _jsonpath_exist('$.**{0}.b ? ( @ > 0)', '{"a": {"c": {"b": 1}}}');
select * from _jsonpath_exist('$.**{1}.b ? ( @ > 0)', '{"a": {"c": {"b": 1}}}');
select * from _jsonpath_exist('$.**{0,}.b ? ( @ > 0)', '{"a": {"c": {"b": 1}}}');
select * from _jsonpath_exist('$.**{1,}.b ? ( @ > 0)', '{"a": {"c": {"b": 1}}}');
select * from _jsonpath_exist('$.**{1,2}.b ? ( @ > 0)', '{"a": {"c": {"b": 1}}}');
select * from _jsonpath_exist('$.**{2,3}.b ? ( @ > 0)', '{"a": {"c": {"b": 1}}}');
