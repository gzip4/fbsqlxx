#pragma once

#include <firebird/Interface.h>

#include <cmath>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>



#ifndef FB_EXCEPTION_BUFFER_SIZE
#define FB_EXCEPTION_BUFFER_SIZE 512
#endif // !FB_EXCEPTION_BUFFER_SIZE


namespace fbsqlxx {

// types
using octets = std::vector<unsigned char>;


// exceptions
class error : public std::exception
{
public:
    error(const char* msg)
        : std::exception(msg)
    {}
};

class sql_error : public error
{
public:
    sql_error(const char* msg, const Firebird::FbException* cause)
        : error(msg), _cause{ cause }
    {}
    const Firebird::FbException* cause() const { return _cause; }

private:
    const Firebird::FbException* _cause;
};

class logic_error : public error
{
public:
    logic_error(const char* msg)
        : error(msg)
    {}
};



inline std::string type_name(unsigned int type)
{
    switch (type)
    {
    case SQL_ARRAY:
        return "ARRAY";

    case SQL_BLOB:
        return "BLOB";

    case SQL_BOOLEAN:
        return "BOOLEAN";

    case SQL_DEC16:
        return "DEC16";

    case SQL_DEC34:
        return "DEC34";

    case SQL_DOUBLE:
        return "DOUBLE";

    case SQL_D_FLOAT:
        return "D_FLOAT";

    case SQL_FLOAT:
        return "FLOAT";

    case SQL_INT128:
        return "INT128";

    case SQL_INT64:
        return "BIGINT";

    case SQL_LONG:
        return "INT";

    case SQL_SHORT:
        return "SMALLINT";

    case SQL_TEXT:
        return "CHAR";

    case SQL_TIMESTAMP:
        return "TIMESTAMP";

    case SQL_TIMESTAMP_TZ:
        return "TIMESTAMP_TZ";

    case SQL_TIMESTAMP_TZ_EX:
        return "TIMESTAMP_TZ_EX";

    case SQL_TIME_TZ:
        return "TIME_TZ";

    case SQL_TIME_TZ_EX:
        return "TIME_TZ_EX";

    case SQL_TYPE_DATE:
        return "DATE";

    case SQL_TYPE_TIME:
        return "TIME";

    case SQL_VARYING:
        return "VARCHAR";

    default:
        break;
    }

    return "UNKNOWN";
}

// internal implementations
namespace _detail {

static inline Firebird::IMaster* master()
{
    static Firebird::IMaster* _master = Firebird::fb_get_master_interface();
    return _master;
}

static inline Firebird::IUtil* util()
{
    static Firebird::IUtil* _util = master()->getUtilInterface();
    return _util;
}

template <
    typename T,
    std::enable_if<std::is_member_function_pointer<decltype(&T::free)>::value, T>* = nullptr>
inline void destroy(T* _value)
{
    _value->free();
}

template <
    typename T,
    std::enable_if<std::is_member_function_pointer<decltype(&T::dispose)>::value, T>* = nullptr>
inline void destroy(T* _value)
{
    _value->dispose();
}

template <
    typename T,
    std::enable_if<std::is_member_function_pointer_v<decltype(&T::release)>, T>* = nullptr>
inline void destroy(T* _value)
{
    _value->release();
}

template <typename T>
class autodestroy
{
public:
    autodestroy(T* value) : _value{ value }
    {}
    ~autodestroy()
    {
        if (_value)
            destroy(_value);
    }
    autodestroy(const autodestroy&) = delete;
    autodestroy& operator=(const autodestroy&) = delete;

    T* operator->() const { return _value; }
    T* operator&() const { return _value; }

private:
    T* _value{};
};

template <typename T>
inline autodestroy<T> make_autodestroy(T* value)
{
    return autodestroy{ value };
}


struct iparam
{
    int type;
    int subtype;
    std::string str_value;
    octets octets_value;
    union
    {
        unsigned char bool_value;
        short short_value;
        long long_value;
        int64_t int64_value;
        float float_value;
        double double_value;
    };
};

class input_params
{
public:
    static constexpr unsigned MY_SQL_OCTETS = 10000001;

    bool empty() const
    {
        return params.empty();
    }

    void clear()
    {
        params.clear();
    }

    void add(bool x)
    {
        iparam p{ SQL_BOOLEAN, 0 };
        p.bool_value = x;
        params.push_back(p);
    }

    void add(short x)
    {
        iparam p{ SQL_SHORT, 0 };
        p.short_value = x;
        params.push_back(p);
    }

    void add(int x)
    {
        iparam p{ SQL_LONG, 0 };
        p.long_value = x;
        params.push_back(p);
    }

    void add(long x)
    {
        iparam p{ SQL_LONG, 0 };
        p.long_value = x;
        params.push_back(p);
    }

    void add(int64_t x)
    {
        iparam p{ SQL_INT64, 0 };
        p.int64_value = x;
        params.push_back(p);
    }

    void add(float x)
    {
        iparam p{ SQL_FLOAT, 0 };
        p.float_value = x;
        params.push_back(p);
    }

    void add(double x)
    {
        iparam p{ SQL_DOUBLE, 0 };
        p.double_value = x;
        params.push_back(p);
    }

    void add(std::string const& x)
    {
        iparam p{ SQL_TEXT, 0 };
        p.str_value = x;
        params.push_back(p);
    }

    void add(const char* x)
    {
        iparam p{ SQL_TEXT, 0 };
        p.str_value = x;
        params.push_back(p);
    }

    void add(char x)
    {
        iparam p{ SQL_TEXT, 0 };
        p.str_value = x;
        params.push_back(p);
    }

    void add(octets const& x)
    {
        iparam p{ MY_SQL_OCTETS, 0 };
        p.octets_value = x;
        params.push_back(p);
    }

    void add(nullptr_t)
    {
        iparam p{ SQL_NULL, 0 };
        params.push_back(p);
    }

    template <typename T>
    T& cast(void* offset) const
    {
        return *((T*)offset);
    }

    template <typename Status>
    Firebird::IMessageMetadata* make_input(std::vector<unsigned char>& buffer, Status& status) const
    {
        using namespace Firebird;

        auto count = static_cast<unsigned>(params.size());
        auto builder = make_autodestroy(master()->getMetadataBuilder(&status, count));

        for (unsigned i = 0; i < count; ++i)
        {
            auto& param = params[i];

            if (param.type == SQL_NULL)
            {
                builder->setType(&status, i, SQL_SHORT + 1);
            }
            else if (param.type == MY_SQL_OCTETS)
            {
                builder->setType(&status, i, SQL_TEXT + 1);
            }
            else
            {
                builder->setType(&status, i, param.type + 1);
            }

            builder->setSubType(&status, i, param.subtype);

            if (param.type == SQL_TEXT)
            {
                auto length = static_cast<unsigned>(param.str_value.size());
                builder->setLength(&status, i, length);
            }
            else if (param.type == MY_SQL_OCTETS)
            {
                auto length = static_cast<unsigned>(param.octets_value.size());
                builder->setLength(&status, i, length);
            }
        }

        auto imeta = builder->getMetadata(&status);
        buffer.resize(imeta->getMessageLength(&status));

        for (unsigned i = 0; i < count; ++i)
        {
            void* offset = &buffer[imeta->getOffset(&status, i)];
            short* null = (short*)&buffer[imeta->getNullOffset(&status, i)];
            auto const& param = params[i];

            auto len = imeta->getLength(&status, i);

            *null = (param.type == SQL_NULL) ? 1 : 0;

            switch (param.type)
            {
            case SQL_BOOLEAN:
                //*((unsigned char*)offset) = param.bool_value;
                cast<unsigned char>(offset) = param.bool_value;
                break;

            case SQL_SHORT:
                cast<short>(offset) = param.short_value;
                break;

            case SQL_LONG:
                cast<long>(offset) = param.long_value;
                break;

            case SQL_INT64:
                cast<int64_t>(offset) = param.int64_value;
                break;

            case SQL_FLOAT:
                cast<float>(offset) = param.float_value;
                break;

            case SQL_DOUBLE:
                cast<double>(offset) = param.double_value;
                break;

            case SQL_TEXT:  // no break
            case SQL_VARYING:
                memcpy(((void*)offset), param.str_value.c_str(), param.str_value.size());
                break;

            case MY_SQL_OCTETS:
                memcpy(((void*)offset), param.octets_value.data(), param.octets_value.size());
                break;

            case SQL_NULL:
                break;

            default:
            {
                std::string msg = "Not implemented type: ";
                msg += type_name(param.type);
                throw error(msg.data());
            }
            break;
            }
        }

        return imeta;
    }

private:
    std::vector<iparam> params;
};

} // namespace _detail



// sql entities implementation


// sub-entities are rely on upper parties, ensure it's liveness
class connection;
class transaction;
class statement;
class result_set;
class field;


// not owns any other entities, may be freely copied/moved
class field final
{
public:
    std::string name() const
    {
        return m_meta->getField(&m_status, m_index);
    }

    std::string alias() const
    {
        return m_meta->getAlias(&m_status, m_index);
    }

    unsigned int charset() const
    {
        return m_meta->getCharSet(&m_status, m_index);
    }

    std::pair<unsigned, int> type() const
    {
        return { m_meta->getType(&m_status, m_index) & ~1u, m_meta->getSubType(&m_status, m_index) };
    }

    bool is_nullable() const
    {
        return m_meta->isNullable(&m_status, m_index);
    }

    bool is_null() const
    {
        return *((short*)&m_buffer[m_meta->getNullOffset(&m_status, m_index)]);
    }

    int scale() const
    {
        return m_meta->getScale(&m_status, m_index);
    }

    unsigned int length() const
    {
        return m_meta->getLength(&m_status, m_index);
    }

    template <typename T>
    T as()
    {
        static_assert(std::bool_constant<false>(), "Field type not implemented");
    }

private:
    friend class result_set;
    field(unsigned int index, Firebird::IMessageMetadata* meta, Firebird::ThrowStatusWrapper& status, unsigned char* buffer) noexcept
        : m_index{ index }, m_meta{ meta }, m_status{ status }, m_buffer{ buffer }
    {
        m_offset = m_meta->getOffset(&m_status, m_index);
        m_type = m_meta->getType(&m_status, m_index) & ~1u;
    }

    template <typename T>
    T& cast() const
    {
        return *((T*)&m_buffer[m_offset]);
    }

    template <typename T>
    float cvt_float(T&& value)
    {
        int scale = m_meta->getScale(&m_status, m_index);
        if (scale != 0)
            return static_cast<float>(value) / std::powf(10.0f, -scale);
        else
            return static_cast<float>(value);
    }

    template <typename T>
    double cvt_double(T&& value)
    {
        int scale = m_meta->getScale(&m_status, m_index);
        if (scale != 0)
            return static_cast<double>(value) / std::pow(10.0, -scale);
        else
            return static_cast<double>(value);
    }

private:
    unsigned int m_index;
    Firebird::IMessageMetadata* m_meta;
    Firebird::ThrowStatusWrapper& m_status;
    unsigned char* m_buffer;
    unsigned int m_offset;
    unsigned int m_type;
};


#define CHECK_TYPE(x)   if (m_type != (x)) throw logic_error("Wrong type: " #x)

template <>
inline bool field::as()
{
    CHECK_TYPE(SQL_BOOLEAN);
    char value = cast<char>();
    return static_cast<bool>(value);
}

template <>
inline short field::as()
{
    switch (m_type)
    {
    case SQL_SHORT:
        return cast<short>();

    case SQL_DOUBLE:
        return static_cast<short>(cast<double>());

    case SQL_FLOAT:
        return static_cast<short>(cast<float>());

    case SQL_INT64:
        return static_cast<short>(cast<int64_t>());

    case SQL_LONG:
        return static_cast<short>(cast<long>());

    default:
        break;
    }

    std::string msg{ "Invalid conversion from type <" };
    msg += type_name(m_type) + "> to smallint";
    throw logic_error{ msg.c_str() };
}

template <>
inline long field::as()
{
    switch (m_type)
    {
    case SQL_LONG:
        return cast<long>();

    case SQL_DOUBLE:
        return static_cast<long>(cast<double>());

    case SQL_FLOAT:
        return static_cast<long>(cast<float>());

    case SQL_INT64:
        return static_cast<long>(cast<int64_t>());

    case SQL_SHORT:
        return static_cast<long>(cast<short>());

    default:
        break;
    }

    std::string msg{ "Invalid conversion from type <" };
    msg += type_name(m_type) + "> to int";
    throw logic_error{ msg.c_str() };
}

template <>
inline int64_t field::as()
{
    switch (m_type)
    {
    case SQL_INT64:
        return cast<int64_t>();

    case SQL_DOUBLE:
        return static_cast<int64_t>(cast<double>());

    case SQL_FLOAT:
        return static_cast<int64_t>(cast<float>());

    case SQL_LONG:
        return static_cast<int64_t>(cast<long>());

    case SQL_SHORT:
        return static_cast<int64_t>(cast<short>());

    default:
        break;
    }

    std::string msg{ "Invalid conversion from type <" };
    msg += type_name(m_type) + "> to bigint";
    throw logic_error{ msg.c_str() };
}

template <>
inline double field::as()
{
    switch (m_type)
    {
    case SQL_DOUBLE:
        return cast<double>();

    case SQL_FLOAT:
        return static_cast<double>(cast<float>());

    case SQL_INT64:
        return cvt_double(cast<int64_t>());

    case SQL_LONG:
        return cvt_double(cast<long>());

    case SQL_SHORT:
        return cvt_double(cast<short>());

    default:
        break;
    }

    std::string msg{ "Invalid conversion from <" };
    msg += type_name(m_type) + "> to double";
    throw logic_error{ msg.c_str() };
}

template <>
inline float field::as()
{
    switch (m_type)
    {
    case SQL_DOUBLE:
        return static_cast<float>(cast<double>());

    case SQL_FLOAT:
        return cast<float>();

    case SQL_INT64:
        return cvt_float(cast<int64_t>());

    case SQL_LONG:
        return cvt_float(cast<long>());

    case SQL_SHORT:
        return cvt_float(cast<short>());

    default:
        break;
    }

    std::string msg{ "Invalid conversion from type <" };
    msg += type_name(m_type) + "> to double";
    throw logic_error{ msg.c_str() };
}

template <>
inline std::string field::as()
{
    auto type = m_meta->getType(&m_status, m_index) & ~1u;
    switch (type)
    {
    case SQL_VARYING:
    {
        short length = cast<short>();
        const char* from = (const char*)&m_buffer[m_offset + sizeof(short)];
        const char* to = from + length;
        return std::string{ from, to };
    }

    case SQL_TEXT:
    {
        const char* from = (const char*)&m_buffer[m_offset];
        const char* to = from + m_meta->getLength(&m_status, m_index);
        return std::string{ from, to };
    }

    case SQL_BOOLEAN:
    {
        return as<bool>() ? "true" : "false";
    }

    case SQL_FLOAT:
    {
        return std::to_string(as<float>());
    }

    case SQL_DOUBLE:
    {
        return std::to_string(as<double>());
    }

    case SQL_SHORT:
    {
        return std::to_string(as<short>());
    }

    case SQL_LONG:
    {
        int scale = m_meta->getScale(&m_status, m_index);
        return scale != 0 ? std::to_string(as<double>()) : std::to_string(as<long>());
    }

    case SQL_INT64:
    {
        int scale = m_meta->getScale(&m_status, m_index);
        return scale != 0 ? std::to_string(as<double>()) : std::to_string(as<int64_t>());
    }

    default:
        break;
    }

    std::string msg{ "Invalid conversion from type <" };
    msg += type_name(type) + "> to string";
    throw logic_error(msg.c_str());
}

template <>
inline octets field::as()
{
    const unsigned char* from = (const unsigned char*)&m_buffer[m_offset];
    const unsigned char* to = from + m_meta->getLength(&m_status, m_index);
    return octets{ from, to };
}

#undef CHECK_TYPE



class result_set final
{
public:
    result_set(result_set const&) = delete;
    result_set& operator=(result_set const&) = delete;
    result_set& operator=(result_set&&) = delete;

    result_set(result_set&& rhs) noexcept
        : m_rs{ rhs.m_rs }
        , m_meta{ rhs.m_meta }
        , m_status{ rhs.m_status }
        , m_buffer{ rhs.m_buffer }
        , m_count{ rhs.m_count }
    {
        rhs.m_rs = nullptr;
        rhs.m_meta = nullptr;
        rhs.m_buffer = nullptr;
    }

    ~result_set()
    {
        delete[] m_buffer;
        if (m_meta)
            m_meta->release();
        if (m_rs)
            m_rs->close(&m_status);
    }

    void close()
    {
        delete[] m_buffer;
        m_buffer = nullptr;
        m_meta->release();
        m_meta = nullptr;
        m_rs->close(&m_status);
        m_rs = nullptr;
    }

    bool next()
    {
        using namespace Firebird;
        try
        {
            return m_rs->fetchNext(&m_status, m_buffer) == Firebird::IStatus::RESULT_OK;
        }
        catch (const FbException& ex)
        {
            char buf[FB_EXCEPTION_BUFFER_SIZE];
            _detail::util()->formatStatus(buf, sizeof(buf), ex.getStatus());
            throw sql_error(buf, &ex);
        }
    }

    unsigned int ncols() const
    {
        return m_count;
    }

    std::vector<std::string> names() const
    {
        std::vector<std::string> res;
        for (unsigned int i = 0; i < m_count; ++i)
        {
            res.emplace_back(m_meta->getField(&m_status, i));
        }
        return res;
    }

    std::vector<std::string> aliases() const
    {
        std::vector<std::string> res;
        for (unsigned int i = 0; i < m_count; ++i)
        {
            res.emplace_back(m_meta->getAlias(&m_status, i));
        }
        return res;
    }

    std::vector<unsigned int> types() const
    {
        std::vector<unsigned int> res;
        for (unsigned int i = 0; i < m_count; ++i)
        {
            res.emplace_back(m_meta->getType(&m_status, i));
        }
        return res;
    }

    field get(unsigned int index) const
    {
        if (index >= m_count)
        {
            throw logic_error("Row index out of bounds");
        }

        return field{ index, m_meta, m_status, m_buffer };
    }


private:
    friend class statement;
    result_set(Firebird::IResultSet* rs, Firebird::IMessageMetadata* meta, Firebird::ThrowStatusWrapper& status)
        : m_rs{ rs }
        , m_meta{ meta }
        , m_status{ status }
    {
        auto length = m_meta->getMessageLength(&status);
        m_buffer = new unsigned char[length];
        m_count = m_meta->getCount(&m_status);
    }

private:
    Firebird::IResultSet* m_rs;
    Firebird::IMessageMetadata* m_meta;
    Firebird::ThrowStatusWrapper& m_status;
    unsigned char* m_buffer{};
    unsigned int m_count;
};



class statement final
{
public:
    statement(statement const&) = delete;
    statement& operator=(statement const&) = delete;
    statement& operator=(statement&&) = delete;

    statement(statement&& rhs) noexcept
        : m_status{ rhs.m_status }
        , m_tra{ rhs.m_tra }
        , m_stmt{ rhs.m_stmt }
        , m_iparams{ std::move(rhs.m_iparams) }
    {
        rhs.m_stmt = nullptr;
    }

    ~statement()
    {
        if (m_stmt) m_stmt->free(&m_status);
    }

    void close()
    {
        m_iparams.clear();
        m_stmt->free(&m_status);
    }

    void clear()
    {
        m_iparams.clear();
    }

    result_set cursor() const;

    template <typename ...Args>
    result_set cursor(Args&& ...args) const;

    size_t execute() const;

    template <typename ...Args>
    size_t execute(Args&& ...args) const;

    template<typename T>
    statement& add(T&& value)
    {
        m_iparams.add(std::forward<T>(value));
        return *this;
    }

private:
    statement(Firebird::IAttachment* att, Firebird::ThrowStatusWrapper& status, Firebird::ITransaction* tra, const char* sql)
        : m_status{ status }, m_tra{ tra }, m_stmt{}
    {
        using namespace Firebird;
        try
        {
            m_stmt = att->prepare(&status, tra, 0, sql, SQL_DIALECT_V6, IStatement::PREPARE_PREFETCH_METADATA);
        }
        catch (const FbException& ex)
        {
            char buf[FB_EXCEPTION_BUFFER_SIZE];
            _detail::util()->formatStatus(buf, sizeof(buf), ex.getStatus());
            throw sql_error(buf, &ex);
        }
    }

private:
    friend class transaction;
    Firebird::ThrowStatusWrapper& m_status;
    Firebird::ITransaction* m_tra;
    Firebird::IStatement* m_stmt;

    _detail::input_params m_iparams;
};


inline result_set statement::cursor() const
{
    using namespace Firebird;
    using namespace _detail;

    try
    {
        if (m_iparams.empty())
        {
            IResultSet* rs = m_stmt->openCursor(&m_status, m_tra, NULL, NULL, NULL, 0);
            auto ometa = m_stmt->getOutputMetadata(&m_status);
            return result_set{ rs, ometa, m_status };
        }
        else
        {
            std::vector<unsigned char> buffer;
            auto imeta = make_autodestroy(m_iparams.make_input(buffer, m_status));
            IResultSet* rs = m_stmt->openCursor(&m_status, m_tra, &imeta, buffer.data(), NULL, 0);
            auto ometa = m_stmt->getOutputMetadata(&m_status);
            return result_set{ rs, ometa, m_status };
        }
    }
    catch (const FbException& ex)
    {
        char buf[FB_EXCEPTION_BUFFER_SIZE];
        util()->formatStatus(buf, sizeof(buf), ex.getStatus());
        throw sql_error(buf, &ex);
    }
}

template <typename ...Args>
inline result_set statement::cursor(Args&& ...args) const
{
    using namespace Firebird;
    using namespace _detail;

    try
    {
        input_params params;
        (..., params.add(std::forward<Args>(args)));

        std::vector<unsigned char> buffer;
        auto imeta = make_autodestroy(params.make_input(buffer, m_status));

        IResultSet* rs = m_stmt->openCursor(&m_status, m_tra, &imeta, buffer.data(), NULL, 0);
        auto ometa = m_stmt->getOutputMetadata(&m_status);
        return result_set{ rs, ometa, m_status };
    }
    catch (const FbException& ex)
    {
        char buf[FB_EXCEPTION_BUFFER_SIZE];
        util()->formatStatus(buf, sizeof(buf), ex.getStatus());
        throw sql_error(buf, &ex);
    }
}


inline size_t statement::execute() const
{
    using namespace Firebird;
    using namespace _detail;

    try
    {
        if (m_iparams.empty())
        {
            m_stmt->execute(&m_status, m_tra, NULL, NULL, NULL, NULL);
        }
        else
        {
            std::vector<unsigned char> buffer;
            auto imeta = make_autodestroy(m_iparams.make_input(buffer, m_status));
            m_stmt->execute(&m_status, m_tra, &imeta, buffer.data(), NULL, NULL);
        }
    }
    catch (const FbException& ex)
    {
        char buf[FB_EXCEPTION_BUFFER_SIZE];
        util()->formatStatus(buf, sizeof(buf), ex.getStatus());
        throw sql_error(buf, &ex);
    }

    return m_stmt->getAffectedRecords(&m_status);
}

template <typename ...Args>
inline size_t statement::execute(Args&& ...args) const
{
    using namespace Firebird;
    using namespace _detail;

    try
    {
        input_params params;
        (..., params.add(std::forward<Args>(args)));

        std::vector<unsigned char> buffer;
        auto imeta = make_autodestroy(params.make_input(buffer, m_status));

        m_stmt->execute(&m_status, m_tra, &imeta, buffer.data(), NULL, NULL);
    }
    catch (const FbException& ex)
    {
        char buf[FB_EXCEPTION_BUFFER_SIZE];
        util()->formatStatus(buf, sizeof(buf), ex.getStatus());
        throw sql_error(buf, &ex);
    }

    return m_stmt->getAffectedRecords(&m_status);
}



class transaction final
{
public:
    transaction(transaction const&) = delete;
    transaction& operator=(transaction const&) = delete;
    transaction& operator=(transaction&&) = delete;

    transaction(transaction&& rhs) noexcept
        : m_att{ rhs.m_att }
        , m_status{ rhs.m_status }
        , m_tra{ rhs.m_tra }
    {
        rhs.m_tra = nullptr;
    }

    ~transaction()
    {
        if (m_tra) m_tra->rollback(&m_status);
    }

    void commit()
    {
        m_tra->commit(&m_status);
        m_tra = nullptr;
    }

    void rollback()
    {
        m_tra->rollback(&m_status);
        m_tra = nullptr;
    }

    statement prepare(const char* sql) const
    {
        return statement{ m_att, m_status, m_tra, sql };
    }

    template <typename ...Args>
    statement prepare(const char* sql, Args&& ...args) const
    {
        statement st{ m_att, m_status, m_tra, sql };
        (..., st.add(std::forward<Args>(args)));
        return st;
    }

    void execute(const char* sql) const
    {
        using namespace Firebird;

        try
        {
            m_att->execute(&m_status, m_tra, 0, sql, SQL_DIALECT_V6, NULL, NULL, NULL, NULL);
        }
        catch (const FbException& ex)
        {
            char buf[FB_EXCEPTION_BUFFER_SIZE];
            _detail::util()->formatStatus(buf, sizeof(buf), ex.getStatus());
            throw sql_error(buf, &ex);
        }
    }

    template <typename ...Args>
    void execute(const char* sql, Args&& ...args) const
    {
        using namespace Firebird;
        using namespace _detail;

        try
        {
            _detail::input_params params;
            (..., params.add(std::forward<Args>(args)));

            std::vector<unsigned char> buffer;
            auto imeta = make_autodestroy(params.make_input(buffer, m_status));
            m_att->execute(&m_status, m_tra, 0, sql, SQL_DIALECT_V6, &imeta, buffer.data(), NULL, NULL);
        }
        catch (const FbException& ex)
        {
            char buf[FB_EXCEPTION_BUFFER_SIZE];
            util()->formatStatus(buf, sizeof(buf), ex.getStatus());
            throw sql_error(buf, &ex);
        }
    }

private:
    transaction(Firebird::IAttachment* att, Firebird::ThrowStatusWrapper& status)
        : m_att{ att }, m_status{ status }
    {
        m_tra = att->startTransaction(&status, 0, nullptr);
    }

private:
    friend class connection;
    Firebird::IAttachment* m_att;
    Firebird::ThrowStatusWrapper& m_status;
    Firebird::ITransaction* m_tra;
};



struct connection_params
{
    const char* database;
    const char* user;
    const char* password;
    const char* role;
};

class connection
{
public:
    connection(const connection_params&);
    ~connection();

    connection(const connection&) = delete;
    connection& operator=(connection&&) = delete;
    connection& operator=(const connection&) = delete;

    connection(connection&& rhs) noexcept
        : m_status{ _detail::master()->getStatus() } // create new status, old would be disposed
        , m_att{ rhs.m_att }
    {
        rhs.m_att = nullptr;
    }

    transaction start()
    {
        return transaction{ m_att, m_status };
    }

private:
    Firebird::ThrowStatusWrapper m_status;
    Firebird::IAttachment* m_att;
};

inline connection::connection(const connection_params& params)
    : m_status{ _detail::master()->getStatus() }
    , m_att{ nullptr }
{
    if (!params.database) throw logic_error("Database location must be supplied");

    using namespace Firebird;
    using namespace _detail;

    auto dpb = make_autodestroy(util()->getXpbBuilder(&m_status, IXpbBuilder::DPB, nullptr, 0));
    if (params.user)
        dpb->insertString(&m_status, isc_dpb_user_name, params.user);
    if (params.password)
        dpb->insertString(&m_status, isc_dpb_password, params.password);
    if (params.role)
        dpb->insertString(&m_status, isc_dpb_sql_role_name, params.role);

    try
    {
        auto provider = make_autodestroy(master()->getDispatcher());
        m_att = provider->attachDatabase(&m_status, params.database, dpb->getBufferLength(&m_status), dpb->getBuffer(&m_status));
    }
    catch (const FbException& ex)
    {
        char buf[FB_EXCEPTION_BUFFER_SIZE];
        util()->formatStatus(buf, sizeof(buf), ex.getStatus());
        throw sql_error(buf, &ex);
    }
}

inline connection::~connection()
{
    try
    {
        if (m_att) m_att->detach(&m_status);
    }
    catch (const Firebird::FbException& ex)
    {
        // XXX: what to do here
        char buf[FB_EXCEPTION_BUFFER_SIZE];
        _detail::util()->formatStatus(buf, sizeof(buf), ex.getStatus());
        //std::cerr << buf << std::endl;
    }

    m_status.dispose();
}

} // namespace fbsqlxx
