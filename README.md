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
