#pragma once

#include <firebird/Interface.h>

#include <cmath>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>



#ifndef FBSQLXX_EXCEPTION_BUFFER_SIZE
#define FBSQLXX_EXCEPTION_BUFFER_SIZE 512
#endif // !FBSQLXX_EXCEPTION_BUFFER_SIZE


namespace fbsqlxx {

// types
using octets = std::vector<unsigned char>;

struct date
{
    unsigned year;
    unsigned month;
    unsigned day;
};

struct time
{
    unsigned hours;
    unsigned minutes;
    unsigned seconds;
    unsigned fractions;
};

struct time_tz
{
    time utc_time;
    unsigned short time_zone;
};

struct time_tz_ex
{
    time utc_time;
    unsigned short time_zone;
    short ext_offset;
};

struct timestamp
{
    date date;
    time time;
};

struct timestamp_tz
{
    timestamp utc_timestamp;
    unsigned short time_zone;
};

struct timestamp_tz_ex
{
    timestamp utc_timestamp;
    unsigned short time_zone;
    short ext_offset;
};


// exceptions
class error : public std::runtime_error
{
public:
    error(const char* msg)
        : std::runtime_error(msg)
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

#define CATCH_SQL                                                           \
    catch (const Firebird::FbException& ex) {                               \
        char buf[FBSQLXX_EXCEPTION_BUFFER_SIZE];                            \
        _detail::util()->formatStatus(buf, sizeof(buf), ex.getStatus());    \
        throw sql_error{ buf, &ex }; }


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

} // namespace _detail

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

// sub-entities are rely on upper parties, ensure it's liveness
class connection;
class transaction;
class statement;
class result_set;
class field;
class blob;


class blob final
{
public:
    static constexpr unsigned MAX_SEGMENT_SIZE = 32 * 1024;

    ~blob()
    {
        if (m_blob) m_blob->release();
    }

    ISC_QUAD id() const
    {
        return m_id;
    }

    void close()
    {
        try
        {
            m_blob->close(&m_status);
            m_blob = nullptr;
        }
        CATCH_SQL
    }

    int64_t info(unsigned char item)
    {
        try
        {
            unsigned char items[] = { 0, isc_info_end };
            unsigned char buffer[16];
            items[0] = item;
            m_blob->getInfo(&m_status, sizeof(items), items, sizeof(buffer), buffer);
            short length = isc_portable_integer(buffer + 1, 2);
            return isc_portable_integer(buffer + 3, length);
        }
        CATCH_SQL
    }

    int64_t num_segments()
    {
        return info(isc_info_blob_num_segments);
    }

    int64_t max_segment()
    {
        return info(isc_info_blob_max_segment);
    }

    int64_t total_length()
    {
        return info(isc_info_blob_total_length);
    }

    int64_t type()
    {
        return info(isc_info_blob_type);
    }

    octets get(unsigned int length)
    {
        octets buffer(length);
        try
        {
            unsigned segment_length{};
            int rc = m_blob->getSegment(&m_status, length, buffer.data(), &segment_length);
            if (buffer.size() > segment_length)
                buffer.resize(segment_length);
            return buffer;
        }
        CATCH_SQL
    }

    octets get()
    {
        using namespace Firebird;
        const unsigned length = MAX_SEGMENT_SIZE;
        octets buffer(length);
        octets result;
        try
        {
            for (;;)
            {
                unsigned segment_length{};
                int rc = m_blob->getSegment(&m_status, length, buffer.data(), &segment_length);
                if (rc != IStatus::RESULT_OK && rc != IStatus::RESULT_SEGMENT)
                    break;
                // stackoverflow says this is the best vector append
                result.insert(result.end(), buffer.cbegin(), buffer.cbegin() + segment_length);
            }

            return result;
        }
        CATCH_SQL
    }

    std::string get_string()
    {
        auto buffer = get();
        return std::string{ buffer.cbegin(), buffer.cend() };
    }

    blob& put(const void* buffer, unsigned length)
    {
        try
        {
            if (length <= MAX_SEGMENT_SIZE)
                m_blob->putSegment(&m_status, length, buffer);
            else
            {
                unsigned pos = 0;
                const unsigned char* ptr = static_cast<const unsigned char*>(buffer);
                while (pos < length)
                {
                    unsigned len = std::min(MAX_SEGMENT_SIZE, length - pos);
                    m_blob->putSegment(&m_status, len, ptr + pos);
                    pos += len;
                }
            }
            return *this;
        }
        CATCH_SQL
    }

    blob& put(octets const& buffer)
    {
        put(buffer.data(), static_cast<unsigned>(buffer.size()));
        return *this;
    }

    blob& put_string(std::string const& buffer)
    {
        try
        {
            if (buffer.size() <= MAX_SEGMENT_SIZE)
                m_blob->putSegment(&m_status, static_cast<unsigned>(buffer.size()), buffer.data());
            else
            {
                unsigned pos = 0;
                const unsigned size = static_cast<unsigned>(buffer.size());
                while (pos < size)
                {
                    unsigned length = std::min(MAX_SEGMENT_SIZE, size - pos);
                    m_blob->putSegment(&m_status, length, buffer.data() + pos);
                    pos += length;
                }
            }
            return *this;
        }
        CATCH_SQL
    }

    blob& put_string(const char* buffer)
    {
        auto str_length = static_cast<unsigned>(strlen(buffer));
        try
        {
            if (str_length <= MAX_SEGMENT_SIZE)
                m_blob->putSegment(&m_status, str_length, buffer);
            else
            {
                unsigned pos = 0;
                while (pos < str_length)
                {
                    unsigned length = std::min(MAX_SEGMENT_SIZE, str_length - pos);
                    m_blob->putSegment(&m_status, length, buffer + pos);
                    pos += length;
                }
            }
            return *this;
        }
        CATCH_SQL
    }

private:
    friend class transaction;
    blob(Firebird::IAttachment* att, Firebird::ITransaction* tra, Firebird::ThrowStatusWrapper& status)
        : m_status{ status }, m_blob{}, m_id{}
    {
        m_blob = att->createBlob(&m_status, tra, &m_id, 0, NULL);
    }

    blob(Firebird::IAttachment* att, Firebird::ITransaction* tra, Firebird::ThrowStatusWrapper& status, ISC_QUAD& id)
        : m_status{ status }, m_blob{}, m_id{ id }
    {
        m_blob = att->openBlob(&m_status, tra, &m_id, 0, NULL);
    }

private:
    Firebird::ThrowStatusWrapper& m_status;
    Firebird::IBlob* m_blob;
    ISC_QUAD m_id;
};



// internal implementations
namespace _detail {

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
        ISC_QUAD quad_value;
        ISC_DATE date_value;
        ISC_TIME time_value;
        ISC_TIME_TZ time_tz_value;
        ISC_TIME_TZ_EX time_tz_ex_value;
        ISC_TIMESTAMP timestamp_value;
        ISC_TIMESTAMP_TZ timestamp_tz_value;
        ISC_TIMESTAMP_TZ_EX timestamp_tz_ex_value;
        FB_DEC16 dec16_value;
        FB_DEC34 dec34_value;
        FB_I128 i128_value;
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

    void add(FB_DEC16 x)
    {
        iparam p{ SQL_DEC16, 0 };
        p.dec16_value = x;
        params.push_back(p);
    }

    void add(FB_DEC34 x)
    {
        iparam p{ SQL_DEC34, 0 };
        p.dec34_value = x;
        params.push_back(p);
    }

    void add(FB_I128 x)
    {
        iparam p{ SQL_INT128, 0 };
        p.i128_value = x;
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

    void add(date x)
    {
        iparam p{ SQL_TYPE_DATE, 0 };
        p.date_value = util()->encodeDate(x.year, x.month, x.day);
        params.push_back(p);
    }

    void add(time x)
    {
        iparam p{ SQL_TYPE_TIME, 0 };
        p.time_value = util()->encodeTime(x.hours, x.minutes, x.seconds, x.fractions);
        params.push_back(p);
    }

    void add(time_tz x)
    {
        iparam p{ SQL_TIME_TZ, 0 };
        p.time_tz_value.utc_time = util()->encodeTime(x.utc_time.hours, x.utc_time.minutes, x.utc_time.seconds, x.utc_time.fractions);
        p.time_tz_value.time_zone = x.time_zone;
        params.push_back(p);
    }

    void add(timestamp x)
    {
        iparam p{ SQL_TIMESTAMP, 0 };
        p.timestamp_value.timestamp_date = util()->encodeDate(x.date.year, x.date.month, x.date.day);
        p.timestamp_value.timestamp_time = util()->encodeTime(x.time.hours, x.time.minutes, x.time.seconds, x.time.fractions);
        params.push_back(p);
    }

    void add(timestamp_tz x)
    {
        iparam p{ SQL_TIMESTAMP_TZ, 0 };
        p.timestamp_tz_value.utc_timestamp.timestamp_date = util()->encodeDate(x.utc_timestamp.date.year, x.utc_timestamp.date.month, x.utc_timestamp.date.day);
        p.timestamp_tz_value.utc_timestamp.timestamp_time = util()->encodeTime(x.utc_timestamp.time.hours, x.utc_timestamp.time.minutes, x.utc_timestamp.time.seconds, x.utc_timestamp.time.fractions);
        p.timestamp_tz_value.time_zone = x.time_zone;
        params.push_back(p);
    }

    void add(octets const& x)
    {
        iparam p{ MY_SQL_OCTETS, 0 };
        p.octets_value = x;
        params.push_back(p);
    }

    void add(blob const& x)
    {
        iparam p{ SQL_BLOB, 0 };
        p.quad_value = x.id();
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

            if (param.type == SQL_BLOB)
                builder->setSubType(&status, i, param.subtype);
        }

        auto imeta = builder->getMetadata(&status);
        buffer.resize(imeta->getMessageLength(&status));

        for (unsigned i = 0; i < count; ++i)
        {
            unsigned char* offset = &buffer[imeta->getOffset(&status, i)];
            short* null = (short*)&buffer[imeta->getNullOffset(&status, i)];
            auto const& param = params[i];
            auto len = imeta->getLength(&status, i);

            *null = (param.type == SQL_NULL) ? -1 : 0;

            switch (param.type)
            {
            case SQL_BOOLEAN:
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

            case SQL_DEC16:
                cast<FB_DEC16>(offset) = param.dec16_value;
                break;

            case SQL_DEC34:
                cast<FB_DEC34>(offset) = param.dec34_value;
                break;

            case SQL_INT128:
                cast<FB_I128>(offset) = param.i128_value;
                break;

            case SQL_BLOB:
                cast<ISC_QUAD>(offset) = param.quad_value;
                break;

            case SQL_TYPE_DATE:
                cast<ISC_DATE>(offset) = param.date_value;
                break;

            case SQL_TYPE_TIME:
                cast<ISC_TIME>(offset) = param.time_value;
                break;

            case SQL_TIME_TZ:
                cast<ISC_TIME_TZ>(offset) = param.time_tz_value;
                break;

            case SQL_TIMESTAMP:
                cast<ISC_TIMESTAMP>(offset) = param.timestamp_value;
                break;

            case SQL_TIMESTAMP_TZ:
                cast<ISC_TIMESTAMP_TZ>(offset) = param.timestamp_tz_value;
                break;

            case SQL_TEXT:
                memcpy(((void*)offset), param.str_value.data(), param.str_value.size());
                break;

            case SQL_VARYING:
                cast<short>(offset) = static_cast<short>(param.str_value.size());
                memcpy(offset + 2, param.str_value.data(), param.str_value.size());
                break;

            case MY_SQL_OCTETS:
                memcpy(((void*)offset), param.octets_value.data(), param.octets_value.size());
                break;

            case SQL_NULL:
                break;

            default:
            {
                std::string msg = "Not implemented parameter type: ";
                msg += type_name(param.type);
                throw logic_error(msg.data());
            }
            } // switch
        } // for loop

        return imeta;
    }

private:
    std::vector<iparam> params;
};

class executor;

} // namespace _detail



// sql entities implementation


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
            return static_cast<float>(value) / std::powf(10.0f, static_cast<float>(-scale));
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

    std::string cvt_string(std::string const& str, int scale)
    {
        if (scale != 0)
        {
            auto left = str.substr(0, str.size() - scale);
            auto right = str.substr(str.size() - scale, str.size());
            return left + "." + right;
        }
        return str;
    }

private:
    unsigned int m_index;
    Firebird::IMessageMetadata* m_meta;
    Firebird::ThrowStatusWrapper& m_status;
    unsigned char* m_buffer;
    unsigned int m_offset;
    unsigned int m_type;
};


#define CHECK_TYPE(x)   if (m_type != (x)) throw logic_error{ "Wrong type: " #x }

#define INVALID_CONVERSION(from, to) do{std::string msg{"Invalid conversion from type "};msg+=type_name(m_type)+" to " to;throw logic_error{msg.c_str()};}while(0)

template <>
inline ISC_QUAD field::as()
{
    CHECK_TYPE(SQL_BLOB);
    return cast<ISC_QUAD>();
}

template <>
inline date field::as()
{
    switch (m_type)
    {
    case SQL_TYPE_DATE:
    {
        ISC_DATE isc_date = cast<ISC_DATE>();
        date d;
        _detail::util()->decodeDate(isc_date, &d.year, &d.month, &d.day);
        return d;
    }

    case SQL_TIMESTAMP:
    {
        ISC_TIMESTAMP isc_ts = cast<ISC_TIMESTAMP>();
        date d;
        _detail::util()->decodeDate(isc_ts.timestamp_date, &d.year, &d.month, &d.day);
        return d;
    }

    case SQL_TIMESTAMP_TZ:
    {
        ISC_TIMESTAMP_TZ isc_ts = cast<ISC_TIMESTAMP_TZ>();
        date d;
        _detail::util()->decodeDate(isc_ts.utc_timestamp.timestamp_date, &d.year, &d.month, &d.day);
        return d;
    }

    default:
        break;
    }

    INVALID_CONVERSION(m_type, "DATE");
}

template <>
inline time field::as()
{
    switch (m_type)
    {
    case SQL_TYPE_TIME:
    {
        ISC_TIME isc_time = cast<ISC_TIME>();
        time t;
        _detail::util()->decodeTime(isc_time, &t.hours, &t.minutes, &t.seconds, &t.fractions);
        return t;
    }
    case SQL_TIMESTAMP:
    {
        ISC_TIMESTAMP isc_ts = cast<ISC_TIMESTAMP>();
        time t;
        _detail::util()->decodeTime(isc_ts.timestamp_time, &t.hours, &t.minutes, &t.seconds, &t.fractions);
        return t;
    }
    case SQL_TIMESTAMP_TZ:
    {
        ISC_TIMESTAMP_TZ isc_ts = cast<ISC_TIMESTAMP_TZ>();
        time t;
        _detail::util()->decodeTime(isc_ts.utc_timestamp.timestamp_time, &t.hours, &t.minutes, &t.seconds, &t.fractions);
        return t;
    }
    } // switch

    INVALID_CONVERSION(m_type, "TIME");
}

template <>
inline time_tz field::as()
{
    switch (m_type)
    {
    case SQL_TIME_TZ:
    {
        ISC_TIME_TZ isc_time = cast<ISC_TIME_TZ>();
        time t;
        _detail::util()->decodeTime(isc_time.utc_time, &t.hours, &t.minutes, &t.seconds, &t.fractions);
        return time_tz{ t, isc_time.time_zone };
    }
    case SQL_TIMESTAMP_TZ:
    {
        ISC_TIMESTAMP_TZ isc_ts = cast<ISC_TIMESTAMP_TZ>();
        time t;
        _detail::util()->decodeTime(isc_ts.utc_timestamp.timestamp_time, &t.hours, &t.minutes, &t.seconds, &t.fractions);
        return time_tz{ t, isc_ts.time_zone };
    }
    } // switch

    INVALID_CONVERSION(m_type, "TIME WITH TIME ZONE");
}

template <>
inline timestamp field::as()
{
    switch (m_type)
    {
    case SQL_TIMESTAMP:
    {
        ISC_TIMESTAMP isc_ts = cast<ISC_TIMESTAMP>();
        date d;
        time t;
        _detail::util()->decodeDate(isc_ts.timestamp_date, &d.year, &d.month, &d.day);
        _detail::util()->decodeTime(isc_ts.timestamp_time, &t.hours, &t.minutes, &t.seconds, &t.fractions);
        return timestamp{ d, t };
    }
    case SQL_TIMESTAMP_TZ:
    {
        ISC_TIMESTAMP_TZ isc_ts = cast<ISC_TIMESTAMP_TZ>();
        date d;
        time t;
        _detail::util()->decodeDate(isc_ts.utc_timestamp.timestamp_date, &d.year, &d.month, &d.day);
        _detail::util()->decodeTime(isc_ts.utc_timestamp.timestamp_time, &t.hours, &t.minutes, &t.seconds, &t.fractions);
        return timestamp{ d, t };
    }
    } // switch

    INVALID_CONVERSION(m_type, "TIMESTAMP");
}

template <>
inline timestamp_tz field::as()
{
    CHECK_TYPE(SQL_TIMESTAMP_TZ);
    ISC_TIMESTAMP_TZ isc_ts = cast<ISC_TIMESTAMP_TZ>();
    date d;
    time t;
    _detail::util()->decodeDate(isc_ts.utc_timestamp.timestamp_date, &d.year, &d.month, &d.day);
    _detail::util()->decodeTime(isc_ts.utc_timestamp.timestamp_time, &t.hours, &t.minutes, &t.seconds, &t.fractions);
    return timestamp_tz{ {d, t}, isc_ts.time_zone };
}

template <>
inline bool field::as()
{
    CHECK_TYPE(SQL_BOOLEAN);
    return static_cast<bool>(cast<char>());
}

template <>
inline short field::as()
{
    switch (m_type)
    {
    case SQL_SHORT:
        return cast<short>();
    case SQL_BOOLEAN:
        return cast<unsigned char>();
    }

    INVALID_CONVERSION(m_type, "SMALLINT");
}

template <>
inline long field::as()
{
    switch (m_type)
    {
    case SQL_LONG:
        return cast<long>();
    case SQL_SHORT:
        return cast<short>();
    case SQL_BOOLEAN:
        return cast<unsigned char>();
    }

    INVALID_CONVERSION(m_type, "INTEGER");
}

template <>
inline int field::as()
{
    switch (m_type)
    {
    case SQL_LONG:
        return cast<int>();
    case SQL_SHORT:
        return cast<short>();
    case SQL_BOOLEAN:
        return cast<unsigned char>();
    }

    INVALID_CONVERSION(m_type, "INTEGER");
}

template <>
inline int64_t field::as()
{
    switch (m_type)
    {
    case SQL_INT64:
        return cast<int64_t>();
    case SQL_LONG:
        return cast<long>();
    case SQL_SHORT:
        return cast<short>();
    case SQL_BOOLEAN:
        return cast<unsigned char>();
    }

    INVALID_CONVERSION(m_type, "BIGINT");
}

template <>
inline FB_I128 field::as()
{
    switch (m_type)
    {
    case SQL_INT128:
        return cast<FB_I128>();
    case SQL_INT64:
        return { cast<uint64_t>(), 0 };
    case SQL_LONG:
        return { cast<unsigned long>(), 0 };
    case SQL_SHORT:
        return { cast<unsigned short>(), 0 };
    case SQL_BOOLEAN:
        return { cast<unsigned char>(), 0 };
    }

    INVALID_CONVERSION(m_type, "INT128");
}

template <>
inline double field::as()
{
    switch (m_type)
    {
    case SQL_DOUBLE:
        return cast<double>();
    case SQL_FLOAT:
        return cast<float>();
    case SQL_INT64:
        return cvt_double(cast<int64_t>());
    case SQL_LONG:
        return cvt_double(cast<long>());
    case SQL_SHORT:
        return cvt_double(cast<short>());
    }

    INVALID_CONVERSION(m_type, "DOUBLE PRECISION");
}

template <>
inline float field::as()
{
    switch (m_type)
    {
    case SQL_FLOAT:
        return cast<float>();
    case SQL_INT64:
        return cvt_float(cast<int64_t>());
    case SQL_LONG:
        return cvt_float(cast<long>());
    case SQL_SHORT:
        return cvt_float(cast<short>());
    }

    INVALID_CONVERSION(m_type, "FLOAT");
}

template <>
inline FB_DEC16 field::as()
{
    CHECK_TYPE(SQL_DEC16);
    return cast<FB_DEC16>();
}

template <>
inline FB_DEC34 field::as()
{
    CHECK_TYPE(SQL_DEC34);
    return cast<FB_DEC34>();
}

template <>
inline std::string field::as()
{
    switch (m_type)
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
    } // switch

    INVALID_CONVERSION(m_type, "TEXT");
}

template <>
inline octets field::as()
{
    if (m_type == SQL_VARYING)
    {
        short length = cast<short>();
        const char* from = (const char*)&m_buffer[m_offset + sizeof(short)];
        const char* to = from + length;
        return octets{ from, to };
    }
    const unsigned char* from = (const unsigned char*)&m_buffer[m_offset];
    const unsigned char* to = from + m_meta->getLength(&m_status, m_index);
    return octets{ from, to };
}

#undef CHECK_TYPE
#undef INVALID_CONVERSION


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
            m_rs->release();
    }

    void close()
    {
        delete[] m_buffer;
        m_buffer = nullptr;
        m_meta->release();
        m_meta = nullptr;

        auto temp = m_rs;
        m_rs = nullptr;

        try
        {
            temp->close(&m_status);
        }
        CATCH_SQL
    }

    bool next()
    {
        try
        {
            return m_rs->fetchNext(&m_status, m_buffer) == Firebird::IStatus::RESULT_OK;
        }
        CATCH_SQL
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
    friend class _detail::executor;
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


namespace _detail {

class executor
{
public:
    static result_set cursor(input_params const& params, Firebird::IStatement* stmt, Firebird::ThrowStatusWrapper& status,
        Firebird::ITransaction* tra)
    {
        using namespace Firebird;

        try
        {
            if (params.empty())
            {
                IResultSet* rs = stmt->openCursor(&status, tra, NULL, NULL, NULL, 0);
                auto ometa = stmt->getOutputMetadata(&status);
                return result_set{ rs, ometa, status };
            }
            else
            {
                std::vector<unsigned char> buffer;
                auto imeta = make_autodestroy(params.make_input(buffer, status));
                IResultSet* rs = stmt->openCursor(&status, tra, &imeta, buffer.data(), NULL, 0);
                auto ometa = stmt->getOutputMetadata(&status);
                return result_set{ rs, ometa, status };
            }
        }
        CATCH_SQL
    }

    static result_set cursor(input_params const& params, Firebird::IAttachment* att, Firebird::ThrowStatusWrapper& status,
        Firebird::ITransaction* tra, const char* sql)
    {
        using namespace Firebird;

        try
        {
            if (params.empty())
            {
                IResultSet* rs = att->openCursor(&status, tra, 0, sql, SQL_DIALECT_V6, NULL, NULL, NULL, NULL, 0);
                auto ometa = rs->getMetadata(&status);
                return result_set{ rs, ometa, status };
            }
            else
            {
                std::vector<unsigned char> buffer;
                auto imeta = make_autodestroy(params.make_input(buffer, status));
                IResultSet* rs = att->openCursor(&status, tra, 0, sql, SQL_DIALECT_V6, &imeta, buffer.data(), NULL, NULL, 0);
                auto ometa = rs->getMetadata(&status);
                return result_set{ rs, ometa, status };
            }
        }
        CATCH_SQL
    }

    static size_t execute(input_params const& params, Firebird::IStatement* stmt, Firebird::ThrowStatusWrapper& status,
        Firebird::ITransaction* tra)
    {
        using namespace Firebird;

        try
        {
            if (params.empty())
            {
                stmt->execute(&status, tra, NULL, NULL, NULL, NULL);
            }
            else
            {
                std::vector<unsigned char> buffer;
                auto imeta = make_autodestroy(params.make_input(buffer, status));
                stmt->execute(&status, tra, &imeta, buffer.data(), NULL, NULL);
            }
            return stmt->getAffectedRecords(&status);
        }
        CATCH_SQL
    }

    static void execute(Firebird::IAttachment* att, Firebird::ThrowStatusWrapper& status, Firebird::ITransaction* tra, const char* sql)
    {
        try
        {
            att->execute(&status, tra, 0, sql, SQL_DIALECT_V6, NULL, NULL, NULL, NULL);
        }
        CATCH_SQL
    }

    static void execute(input_params const& params, Firebird::IAttachment* att, Firebird::ThrowStatusWrapper& status, Firebird::ITransaction* tra, const char* sql)
    {
        try
        {
            std::vector<unsigned char> buffer;
            auto imeta = make_autodestroy(params.make_input(buffer, status));
            att->execute(&status, tra, 0, sql, SQL_DIALECT_V6, &imeta, buffer.data(), NULL, NULL);
        }
        CATCH_SQL
    }
};

} // namespace _detail


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
        if (m_stmt) m_stmt->release();
    }

    void close()
    {
        m_iparams.clear();
        auto temp = m_stmt;
        m_stmt = nullptr;

        try
        {
            temp->free(&m_status);
        }
        CATCH_SQL
    }

    template<typename T>
    statement& add(T&& value)
    {
        m_iparams.add(std::forward<T>(value));
        return *this;
    }

    void clear()
    {
        m_iparams.clear();
    }

    result_set cursor() const
    {
        return _detail::executor::cursor(m_iparams, m_stmt, m_status, m_tra);
    }

    template <typename ...Args>
    result_set cursor(Args&& ...args) const
    {
        _detail::input_params params;
        (..., params.add(std::forward<Args>(args)));
        return _detail::executor::cursor(params, m_stmt, m_status, m_tra);
    }

    size_t execute() const
    {
        return _detail::executor::execute(m_iparams, m_stmt, m_status, m_tra);
    }

    template <typename ...Args>
    size_t execute(Args&& ...args) const
    {
        _detail::input_params params;
        (..., params.add(std::forward<Args>(args)));
        return _detail::executor::execute(params, m_stmt, m_status, m_tra);
    }

private:
    statement(Firebird::IStatement* stmt, Firebird::ThrowStatusWrapper& status, Firebird::ITransaction* tra)
        : m_status{ status }, m_tra{ tra }, m_stmt{ stmt }
    {}

private:
    friend class transaction;
    Firebird::ThrowStatusWrapper& m_status;
    Firebird::ITransaction* m_tra;
    Firebird::IStatement* m_stmt;

    _detail::input_params m_iparams;
};


struct data_access
{
    bool mode{ true };
    static data_access read_only()
    {
        return { false };
    }
    static data_access read_write()
    {
        return { true };
    }
};

struct lock_resolution
{
    bool mode{ true };
    int timeout{ -1 };
    static lock_resolution wait(int lock_timeout = -1)
    {
        return { true, lock_timeout };
    }
    static lock_resolution no_wait(int lock_timeout = -1)
    {
        return { false, lock_timeout };
    }
};

struct isolation_level
{
    enum
    {
        il_concurrency, il_consistency, il_read_committed
    };
    enum
    {
        rc_no_record_version, rc_record_version, rc_consistency
    };
    int mode{ il_concurrency };
    int rc{ rc_no_record_version };
    static isolation_level concurrency()
    {
        return { il_concurrency, -1 };
    }
    static isolation_level consistency()
    {
        return { il_consistency, -1 };
    }
    static isolation_level read_committed(bool record_version = false)
    {
        return { il_read_committed, record_version ? rc_record_version : rc_no_record_version };
    }
    static isolation_level read_committed_consistency()
    {
        return { il_read_committed, rc_consistency };
    }
};


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
        using namespace Firebird;
        try
        {
            IStatement* stmt = m_att->prepare(&m_status, m_tra, 0, sql, SQL_DIALECT_V6, IStatement::PREPARE_PREFETCH_METADATA);
            return statement{ stmt, m_status, m_tra };
        }
        CATCH_SQL
    }

    template <typename ...Args>
    statement prepare(const char* sql, Args&& ...args) const
    {
        using namespace Firebird;
        try
        {
            IStatement* stmt = m_att->prepare(&m_status, m_tra, 0, sql, SQL_DIALECT_V6, IStatement::PREPARE_PREFETCH_METADATA);
            statement st{ stmt, m_status, m_tra };
            (..., st.add(std::forward<Args>(args)));
            return st;
        }
        CATCH_SQL
    }

    void execute(const char* sql) const
    {
        return _detail::executor::execute(m_att, m_status, m_tra, sql);
    }

    template <typename ...Args>
    void execute(const char* sql, Args&& ...args) const
    {
        _detail::input_params params;
        (..., params.add(std::forward<Args>(args)));

        return _detail::executor::execute(params, m_att, m_status, m_tra, sql);
    }

    result_set cursor(const char* sql) const
    {
        return _detail::executor::cursor({}, m_att, m_status, m_tra, sql);
    }

    template <typename ...Args>
    result_set cursor(const char* sql, Args&& ...args) const
    {
        _detail::input_params params;
        (..., params.add(std::forward<Args>(args)));

        return _detail::executor::cursor(params, m_att, m_status, m_tra, sql);
    }

    /// <summary>
    /// Produce blob object prepared for writing
    /// </summary>
    /// <returns>blob object</returns>
    blob create_blob()
    {
        try
        {
            return blob{ m_att, m_tra, m_status };
        }
        CATCH_SQL
    }

    /// <summary>
    /// Produce blob object prepared for reading
    /// </summary>
    /// <param name="rs">- result set</param>
    /// <param name="column_number">- blob column number</param>
    /// <returns>blob object</returns>
    blob open_blob(result_set const& rs, unsigned column_number)
    {
        auto id = rs.get(column_number).as<ISC_QUAD>();
        try
        {
            return blob{ m_att, m_tra, m_status, id };
        }
        CATCH_SQL
    }

private:
    transaction(Firebird::IAttachment* att, Firebird::ThrowStatusWrapper& status)
        : m_att{ att }, m_status{ status }
    {
        m_tra = att->startTransaction(&status, 0, NULL);
    }

    transaction(Firebird::IAttachment* att, Firebird::ThrowStatusWrapper& status,
        isolation_level const& il, lock_resolution const& lr, data_access const& da)
        : m_att{ att }, m_status{ status }
    {
        using namespace Firebird;
        using namespace _detail;

        auto tpb = make_autodestroy(util()->getXpbBuilder(&m_status, IXpbBuilder::TPB, nullptr, 0));
        switch (il.mode)
        {
        case isolation_level::il_concurrency:
            tpb->insertTag(&status, isc_tpb_concurrency);
            break;
        case isolation_level::il_consistency:
            tpb->insertTag(&status, isc_tpb_consistency);
            break;
        case isolation_level::il_read_committed:
            tpb->insertTag(&status, isc_tpb_read_committed);
            {
                switch (il.rc)
                {
                case isolation_level::rc_no_record_version:
                    tpb->insertTag(&status, isc_tpb_no_rec_version);
                    break;
                case isolation_level::rc_record_version:
                    tpb->insertTag(&status, isc_tpb_rec_version);
                    break;
                case isolation_level::rc_consistency:
                    tpb->insertTag(&status, isc_tpb_read_consistency);
                    break;
                default:
                    throw logic_error("Wrong read-committed transaction variant");
                    break;
                }
            }
            break;
        default:
            throw logic_error("Wrong isolation level");
            break;
        }

        if (lr.mode)
        {
            tpb->insertTag(&status, isc_tpb_wait);
            if (lr.timeout > 0)
                tpb->insertInt(&status, isc_tpb_lock_timeout, lr.timeout);
        }
        else
            tpb->insertTag(&status, isc_tpb_nowait);

        if (da.mode)
            tpb->insertTag(&status, isc_tpb_write);
        else
            tpb->insertTag(&status, isc_tpb_read);

        m_tra = att->startTransaction(&status, tpb->getBufferLength(&status), tpb->getBuffer(&status));
    }

private:
    friend class connection;
    Firebird::IAttachment* m_att;
    Firebird::ThrowStatusWrapper& m_status;
    Firebird::ITransaction* m_tra;
};


static inline int64_t portable_integer(const uint8_t* p, short length)
{
    return isc_portable_integer(p, length);
}


struct connection_params
{
    const char* database;
    const char* user;
    const char* password;
    const char* role;
    const char* lc_messages;    // path to custom firebird.msg file
    const char* lc_ctype;       // connection charset
    const char* session_time_zone;
    const char* trusted_role;
    int connect_timeout;
    int dialect{ SQL_DIALECT_CURRENT };
    bool trusted_auth;
};

class connection
{
public:
    connection(const connection_params& params)
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
        if (params.lc_ctype)
            dpb->insertString(&m_status, isc_dpb_lc_ctype, params.lc_ctype);
        if (params.lc_messages)
            dpb->insertString(&m_status, isc_dpb_lc_messages, params.lc_messages);
        if (params.session_time_zone)
            dpb->insertString(&m_status, isc_dpb_session_time_zone, params.session_time_zone);

        if (params.trusted_auth)
            dpb->insertTag(&m_status, isc_dpb_trusted_auth);
        if (params.trusted_role)
            dpb->insertString(&m_status, isc_dpb_trusted_role, params.trusted_role);

        if (params.connect_timeout > 0)
            dpb->insertInt(&m_status, isc_dpb_connect_timeout, params.connect_timeout);

        dpb->insertInt(&m_status, isc_dpb_sql_dialect, params.dialect);

        try
        {
            auto provider = make_autodestroy(master()->getDispatcher());
            m_att = provider->attachDatabase(&m_status, params.database, dpb->getBufferLength(&m_status), dpb->getBuffer(&m_status));
        }
        CATCH_SQL
    }

    ~connection()
    {
        try
        {
            if (m_att) m_att->detach(&m_status);
        }
        catch (const Firebird::FbException& ex)
        {
            // XXX: what to do here
            char buf[FBSQLXX_EXCEPTION_BUFFER_SIZE];
            _detail::util()->formatStatus(buf, sizeof(buf), ex.getStatus());
            //std::cerr << buf << std::endl;
        }

        m_status.dispose();
    }

    connection(const connection&) = delete;
    connection& operator=(connection&&) = delete;
    connection& operator=(const connection&) = delete;

    connection(connection&& rhs) noexcept
        : m_status{ _detail::master()->getStatus() } // create new status, old would be disposed
        , m_att{ rhs.m_att }
    {
        rhs.m_att = nullptr;
    }

    /// <summary>
    /// Ping database server
    /// </summary>
    void ping()
    {
        try
        {
            m_att->ping(&m_status);
        }
        CATCH_SQL
    }

    /// <summary>
    /// Execute SQL statement in a separate transaction, no input, no output
    /// </summary>
    /// <param name="sql">- SQL statement string</param>
    void immediate(const char* sql)
    {
        auto tra = start(isolation_level::read_committed(), lock_resolution::no_wait());
        tra.execute(sql);
        tra.commit();
    }

    /// <summary>
    /// Start new transaction with default options
    /// </summary>
    /// <returns>transaction object</returns>
    transaction start()
    {
        try
        {
            return transaction{ m_att, m_status };
        }
        CATCH_SQL
    }

    /// <summary>
    /// Start new transaction with specific options
    /// </summary>
    /// <param name="il">- isolation_level, snapshot, stability or read committed</param>
    /// <param name="lr">- lock_resolution, wait or no wait</param>
    /// <param name="da">- data_access, read only or read-write</param>
    /// <returns>transaction object</returns>
    transaction start(isolation_level il, lock_resolution lr = {}, data_access da = {})
    {
        try
        {
            return transaction{ m_att, m_status, il, lr, da };
        }
        CATCH_SQL
    }

    /// <summary>
    /// Database metadata info request
    /// </summary>
    /// <param name="items">- list of <em>enum db_info_types</em> constants (isc_info_*, fb_info_*)</param>
    /// <param name="buffer_size">- maximun size of output buffer in bytes, optional</param>
    /// <returns>buffer filled with info, needs to be parsed</returns>
    std::vector<uint8_t>
        info(std::initializer_list<uint8_t> items, size_t buffer_size = 16 * 1024) const
    {
        std::vector<uint8_t> buffer(buffer_size);
        std::vector<uint8_t> _items{ items };
        _items.push_back(isc_info_end);
        try
        {
            auto status = _detail::make_autodestroy(_detail::master()->getStatus());
            Firebird::ThrowStatusWrapper wrapper{ &status };
            m_att->getInfo(&wrapper, static_cast<unsigned>(_items.size()), _items.data(),
                static_cast<unsigned>(buffer.size()), buffer.data());

            if (buffer[0] == isc_info_truncated)
                throw logic_error("connection::info() - output buffer is truncated");

            auto it = std::find(buffer.cbegin(), buffer.cend(), isc_info_end);
            if (it == buffer.cend())
                throw logic_error("connection::info() - output buffer is broken");

            return { buffer.cbegin(), it + 1 };
        }
        CATCH_SQL
    }

    template <typename Func>
    static void parse_info_buffer(std::vector<uint8_t> const& buffer, Func func)
    {
        for (auto p = buffer.cbegin(); p != buffer.cend() && *p != isc_info_end; )
        {
            uint8_t item = *p++;
            short length = portable_integer(std::addressof(*p), 2);
            p += 2;

            func(item, length, std::addressof(*p));

            p += length;
        }
    }

private:
    Firebird::ThrowStatusWrapper m_status;
    Firebird::IAttachment* m_att;
};


#undef CATCH_SQL

} // namespace fbsqlxx
