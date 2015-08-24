Build:
```
cmake <path/to/sources/directory> -DPOSTGRESQL=ON
make
```
(to test it in CLI, without altering PostgreSQL, remove `-DPOSTGRESQL=ON`)

Install:
```
make install
```

Execute SQL statement that was output in PostgreSQL (in your database as a priviliged user).

Restart PostgreSQL.

Use the function. Example: `CREATE UNIQUE INDEX ON table_name(compile_query_int(query_int_column_name::text, TRUE, TRUE));`.

Prototype: `bytea compile_query_int(query text, bool throw_false, bool throw_true)`
* *query*: the text representation of the query_int
* *throw_false*: throw error is expression is always *false* (eg: `1&!1`)
* *true*: throw error is expression is always *true* (eg: `42|!42`)

GUC (configuration):
* intarray.query_int.max_symbols: maximum number of integers in a query_int (default: 16, minimum: 2, maximum: 31)
* intarray.query_int.max_stack_size: maximum stack size for query_int parsing (default: 256)
