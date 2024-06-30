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
    auto tr0 = conn.start(); // default transaction options
```

Each transaction may have it's own set of options ([read documentation](https://www.firebirdsql.org/file/documentation/chunk/en/refdocs/fblangref40/fblangref40-transacs.html)). Let's see how to use them:
```c++
    using namespace fbsqlxx;
    auto tr0 = c.start(
        isolation_level::read_committed(), // required
        lock_resolution::no_wait(),        // optional, default = wait()
        data_access::read_only());         // optional, default = read_write()

    // isolation_level::concurrency() - shapshot
    // isolation_level::consistency() - shapshot table stability
    // isolation_level::read_committed(bool = false) - read committed with (no record_version | record_version) sub-option
    // isolation_level::read_committed_consistency() - read committed with read consistency sub-option

    // lock_resolution::no_wait()
    // lock_resolution::wait(int = -1) - timeout in seconds, if positive

    // data_access::read_only()
    // data_access::read_write()
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

We need a transaction or a prepared statement in order to query (select) data from a database. Data can be fetched in a form of result set, and can be accessed from row by row, sequentially.

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

    // Immediate cursor, parameters after sql string
    auto rs2 = tr0.cursor("select * from test_table where id > ?", 3);
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

## BLOBs
There are two operations with blobs, to put some data to a database (insert or update), and to get it back (select). A transaction object has methods ```transaction.create_blob()``` and ```transaction.open_blob()``` to produce corresponding kind of blob objects. For example:

```c++
void write_blob(fbsqlxx::connection& conn, long id, fbsqlxx::octets const& data)
{
    auto tr0 = conn.start();
    auto blob0 = tr0.create_blob();  // this blob object is for write only
    blob0.put(data);
    blob0.close();     // IMPORTANT! blob0 object becomes unusable, but it contains a BLOB ID to insert or update operations

    tr0.execute("insert into btable values(?, ?)", id, blob0);  // use blob object itself as a parameter
    // or tr0.execute("update or insert into btable(id, blb0) values(?, ?) matching(id)", id, blob0);
    // or tr0.execute("update btable set blb0 = ? where id = ?", blob0, id);
    // ...
    // there are some additional 'put' methods
    // blob0.put({1, 2, 3, 0xff, 006}); - put list of bytes
    // blob0.put(buffer, buffer_length); - put bytes from raw buffer
    // blob0.put(data1).put("end").put({'\n'}).close(); - chain put operations, all data would be concatenated
    // blob0.put_string(std::string{"string blob"});
    // blob0.put_string("another string blob");
    tr0.commit();
}

void read_blob(fbsqlxx::connection& conn, long id)
{
    auto tr0 = conn.start();
    auto rs0 = tr0.cursor("select id, blob0 from btable where id = ?", id);
    if (rs0.next())
    {
        auto [type, subtype] = rs0.get(1).type();  // 1 - column number of blob0
        auto b0 = tr0.open_blob(rs0, 1);  // this blob object is for read only
        auto total_length = b0.total_length();  // blob contents length in bytes
        auto value = b0.get();  // container with full blob contents
        // ...
        // there are some additional 'get' methods
        // std::string str = b0.get_string();
        // auto value = b0.get(16); - get next 16 bytes from blob
        // a blob is like a file, we can read from it until the end of data
    }
    tr0.commit();
}
```

## Database metadata
A library provides the thin layer of abstraction of database metadata requests, avoiding use of arrays etc, and helps to parse the replies incoming. Let's see how it looks like.

```c++
    auto buffer = conn.info({ isc_info_page_size, isc_info_db_size_in_pages });
    connection::parse_info_buffer(buffer, [](uint8_t item, short len, const uint8_t* p)
        {
            // item = isc_info_page_size, then isc_info_db_size_in_pages
            int64_t val = portable_integer(p, len); // metadata item numeric value
        });
```

```connection::parse_info_buffer()``` helper function invokes a callback on every metadata item the buffer contains. Library's users must know particular items layout in the buffer (numbers, strings, arrays, etc). Of course, the buffer contents may be used directly, for example:

```c++
long database_page_size(fbsqlxx::connection const& conn)
{
    // set initial buffer_size to 16 bytes as long as we know it would be enough
    auto buffer = conn.info({ isc_info_page_size }, 16); // buffer is a byte buffer
    // buffer[0] = isc_info_page_size, buffer[1] and buffer[2] = short length, must be 4
    // buffer[3]..buffer[6] = 32-bit page_size value
    return static_cast<long>(fbsqlxx::portable_integer(buffer.data() + 3, 4));
}
```

## Exceptions
A library defines following exceptions:

```c++
std::runtime_error => fbsql::error => (fbsql::sql_error | fbsql::logic_error)
```

## ToDo

- [x] Extend connection parameters list
- [x] Add transaction parameters
- [ ] Add other numeric datatypes
- [ ] Add blobs
- [ ] Add charsets
- [ ] Add date/time datatypes
- [x] Add database metadata support

