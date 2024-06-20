# fbsqlxx
Headers-only C++ FirebirdSQL database client wrapper library

## Dependecies
FirebirdSQL database installation contains directories _<path>/include_ and _<path>/lib_, it's all we need.

## Samples

First, include header file:
```c++
#include "fbsqlxx.hpp"
#include <iostream>
#include <iomanip>
namespace fbsql = fbsqlxx;
```

Create a connection to database:
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
Now we can execute immadiate SQL statements, with or without parameters:

```c++

void create_table(fbsql::connection& c)
{
    {
        auto tr1 = c.start();
        tr1.execute("create table test_table(id int primary key, text varchar(10), val float, val2 decimal(12, 3))");
        tr1.commit(); // transaction is not usable after commit/rollback
    }
    auto tr2 = c.start();
    tr2.execute("insert into test_table(id, text, val, val2) values(?, ?, ?, ?)", 1, "some text", 10.01f, 100500.001);
    tr2.execute("insert into test_table(id, text, val, val2) values(?, ?, ?, ?)", 2, fbsql::octets{'P', 'I', '/', 'E'}, 3.1415f, 2.71);
    tr2.commit();
}

```

