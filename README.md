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

    fbsql::connection c{ params };
    // ...
```
