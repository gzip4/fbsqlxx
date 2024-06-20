# fbsqlxx
Headers-only C++ [FirebirdSQL](https://firebirdsql.org/) database client wrapper library

## Dependencies
- C++17 compiler
- FirebirdSQL database installation contains directories _path/include_ and _path/lib_, it's all we need.

## Samples

First, include header file:
```c++
#include "fbsqlxx.hpp"
#include <iostream>
#include <iomanip>
namespace fbsql = fbsqlxx;
```

Create a connection to a database:
```c++
int main()
{
    fbsql::connection_params params{};
    params.database = "localhost:c:/db/testdb.fdb";
    params.user = "SYSDBA";
    params.password = "masterkey";

    fbsql::connection conn{ params };
    // ...
```

All database activity must exist within a transaction. A connection can have many active transactions simultaneously. A transaction should be committed explicitly, otherwise it will be rolled back on destruction.
```c++
    auto tr0 = conn.start();
```
Now we can execute immediate SQL statements, with or without parameters:

```c++
void create_table(fbsql::connection& conn)
{
    {
        auto tr1 = conn.start();
        tr1.execute("create table test_table(id int primary key, text varchar(10), val float, val2 decimal(12, 3))");
        tr1.commit(); // transaction is not usable after commit/rollback
    }
    auto tr2 = conn.start();
    auto sql = "insert into test_table(id, text, val, val2) values(?, ?, ?, ?)";
    tr2.execute(sql, 1, "some text", 10.01f, 100500.001);
    tr2.execute(sql, 2, fbsql::octets{'P', 'I', '/', 'E'}, 3.1415f, 2.71);
    tr2.commit();
}
```

If some statement needs to be executed many times with different set of parameters, we can "prepare" it

```c++
static void action_insert_2_records(fbsql::transaction const& tr0)
{
    auto sql = "insert into test_table(id, text, val, val2) values(?, ?, ?, ?)";
    auto st0 = tr0.prepare(sql);
    st0.execute(3, "some text", 10.01f, 100500.001);
    st0.execute(4, fbsql::octets{ 'P', 'I', '/', 'E' }, 3.1415f, 2.71);
}
```

We need a prepared statement in order to query (select) data from a database. Data can be fetched in a form of result set, and can be accessed from row by row, sequentially.

```c++
void action_do_queries(fbsql::transaction const& tr0)
{
    auto st0 = tr0.prepare("select * from test_table");
    auto rs0 = st0.cursor(); // no paramaters
    while (rs0.next())
    {
        // work with current row...
    }
    rs0.close(); // result set becomes unusable after close

    auto st1 = tr0.prepare("select * from test_table where id > ?");
    auto rs1 = st1.cursor(2); // paramaters
    // ...
}
```

A result set provides some information about columns it has access to.

```c++
void action_cursor_metadata(fbsql::transaction const& tr0)
{
    auto st0 = tr0.prepare("select * from test_table");
    auto rs0 = st0.cursor();
    auto ncols = rs0.ncols(); // columns count
    
    auto names = rs0.names(); // a collection of column's names
    auto aliases = rs0.aliases();
    auto types = rs0.types();
    for (unsigned i = 0; i < ncols; ++i)
    {
        std::cout << names[i] << ":" << fbsql::type_name(types[i]) << " ";
    }
    std::cout << std::endl;
    
    while (rs0.next())
    {
        // ...
```

Also we have an access to column's values. Row has become valid after successful (return true) call to _next()_ method. Columns are counted from zero.

```c++
void action_query(fbsql::transaction const& tr0)
{
    auto st0 = tr0.prepare("select id, text as text_alias, val, val2 from test_table where id > ?");
    auto rs0 = st0.cursor(2);
    while (rs0.next())
    {
        auto id = rs0.get(0);     // get fields
        auto text = rs0.get(1);
        auto val = rs0.get(2);
        auto val2 = rs0.get(3);

        // work with field's metadata
        bool cond1 = val2.is_nullable();
        int v2_scale = val2.scale();
        auto [type, subtype] = text.type();
        std::string text_col_name = text.name();
        std::string text_col_alias = text.alias();
        unsigned text_col_length = text.length();

        // work with field's values
        long id_value = id.as<long>();
        if (!text.is_null() && !val.is_null() && !val2.is_null())
        {
            auto str = text.as<std::string>();
            float x = val.as<float>();
            double y = val2.as<double>();
        }
    }
}
```
## Exceptions
A library defines following exceptions:

```c++
std::exception => fbsql::error => (fbsql::sql_error | fbsql::logic_error)
```

## ToDo

- [ ] Extend connection parameters list
- [ ] Add transaction parameters
- [ ] Add other numeric datatypes
- [ ] Add charsets
- [ ] Add date/time datatypes

