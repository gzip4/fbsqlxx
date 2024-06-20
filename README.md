# fbsqlxx
Headers-only C++ FirebirdSQL database client wrapper library

## Dependencies
FirebirdSQL database installation contains directories _path/include_ and _path/lib_, it's all we need.

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


