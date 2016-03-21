#include <type_traits>

namespace sqlite3cpp {
namespace detail {

template<int>
struct placeholder_tmpl{};

template<int...> struct indexes { typedef indexes type; };
template<int Max, int...Is> struct make_indexes : make_indexes<Max-1, Max-1, Is...>{};
template<int... Is> struct make_indexes<0, Is...> : indexes<Is...>{};
template<int Max> using make_indexes_t=typename make_indexes<Max>::type;
 
}} // namespace sqlite3cpp::detail

// custome placeholders
namespace std {
    template<int N>
    struct is_placeholder<sqlite3cpp::detail::placeholder_tmpl<N>>
    : integral_constant<int, N+1>
    {};
}

namespace sqlite3cpp {
namespace detail {

/**
 * tuple foreach
 */
template<typename tuple_type, typename F, int Index, int Max>
struct foreach_tuple_impl {
    void operator()(tuple_type & t, F f) {
        f(std::get<Index>(t), Index);
        foreach_tuple_impl<tuple_type, F, Index + 1, Max>()(t, f);
    }
};

template<typename tuple_type, typename F, int Max>
struct foreach_tuple_impl<tuple_type, F, Max, Max> {
    void operator()(tuple_type & t, F f) {
        f(std::get<Max>(t), Max);
    }
};

template<typename tuple_type, typename F>
void foreach_tuple_element(tuple_type & t, F f)
{
    foreach_tuple_impl<
        tuple_type,
        F,
        0,
        std::tuple_size<tuple_type>::value - 1>()(t, f);
}

/**
 * Helpers for retrieve column values.
 */
inline void get_col_val(sqlite3_stmt *stmt, int index, int& val)
{ val = sqlite3_column_int(stmt, index); }

inline void get_col_val(sqlite3_stmt *stmt, int index, double& val)
{ val = sqlite3_column_double(stmt, index); }

inline void get_col_val(sqlite3_stmt *stmt, int index, std::string &val)
{ val = (char const *)sqlite3_column_text(stmt, index); }

struct set_col_val {
    set_col_val(sqlite3_stmt *stmt) : m_stmt(stmt) {}

    template<typename T>
    void operator()(T &out, int index) const
    { get_col_val(m_stmt, index, out); }
private:
    sqlite3_stmt *m_stmt;
};

/*
 * Helpers for binding values to sqlite3_stmt.
 */
inline void bind_to_stmt(sqlite3_stmt *stmt, int i){}

inline int bind_val(sqlite3_stmt *stmt, int index, int val) {
    return sqlite3_bind_int(stmt, index, val);
}

inline int bind_val(sqlite3_stmt *stmt, int index, std::string const &val) {
    return sqlite3_bind_text(stmt, index, val.c_str(), val.size(), SQLITE_STATIC);
}

inline int bind_val(sqlite3_stmt *stmt, int index, std::nullptr_t _) {
    return sqlite3_bind_null(stmt, index);
}

template <typename T, typename ... Args>
void bind_to_stmt(sqlite3_stmt *stmt, int index, T val, Args&& ... args)
{
    if(bind_val(stmt, index, val))
        throw std::runtime_error("bind error");
    bind_to_stmt(stmt, index+1, std::forward<Args>(args)...);
}

/**
 * Helpers for converting value from sqlite3_value.
 */
template<typename T> struct Type{};

inline int get(Type<int>, sqlite3_value** v, int const index)
{ return sqlite3_value_int(v[index]); }

inline double get(Type<double>, sqlite3_value** v, int const index)
{ return sqlite3_value_double(v[index]); }

inline std::string get(Type<std::string>, sqlite3_value **v, int const index)
{ return std::string((char const *)sqlite3_value_text(v[index]), (size_t)sqlite3_value_bytes(v[index])); }

/**
 * Helpers for setting result of scalar functions.
 */
inline void result(int val, sqlite3_context *ctx)
{ sqlite3_result_int(ctx, val); }

inline void result(double val, sqlite3_context *ctx)
{ sqlite3_result_double(ctx, val); }

inline void result(std::string const &val, sqlite3_context *ctx)
{ sqlite3_result_text(ctx, val.c_str(), val.size(), SQLITE_TRANSIENT); }

/**
 * Magic for typesafe invoking lambda/std::function from sqlite3
 * (registered via sqlite3_create_function).
 */
template<typename R, typename ...Args, int ...Is>
R invoke(std::function<R(Args...)> func, sqlite3_value **argv, indexes<Is...>) {
    // Expand argv per index
    return func( get(Type<Args>{}, argv, Is)... ); 
}

template<typename R, typename... Args>
R invoke( std::function<R(Args...)> func, sqlite3_value **argv) {
    return invoke(func, argv, make_indexes_t<sizeof...(Args)>{} );
}

template<typename R, typename ... Args>
database::xfunc_t
make_invoker(std::function<R(Args...)>&& func)
{
    return [func](sqlite3_context *ctx, sqlite3_value **argv) {
        result(invoke(func, argv), ctx);
    };
}

template<typename ... Args>
database::xfunc_t
make_invoker(std::function<void(Args...)>&& func)
{
    return [func](sqlite3_context *ctx, sqlite3_value **argv) {
        invoke(func, argv);
    };
}


/**
 * Function traits for supporting lambda.
 */
// For generic types that are functors, delegate to its 'operator()'
template <typename T>
struct function_traits
    : public function_traits<decltype(&T::operator())>
{};

// for pointers to member function
template <typename C, typename R, typename... Args>
struct function_traits<R(C::*)(Args...)> {
    typedef std::function<R (Args...)> f_type;
    static const size_t arity = sizeof...(Args);
};

// for pointers to const member function
template <typename C, typename R, typename... Args>
struct function_traits<R(C::*)(Args...) const> {
    typedef std::function<R (Args...)> f_type;
    static const size_t arity = sizeof...(Args);
};

/**
 * Member function binder helpers (auto expand by passed function prototype)
 */
template<typename F, typename C, int ... Is>
typename function_traits<F>::f_type
bind_this(F f, C *this_, indexes<Is...>) {
    return std::bind(f, this_, placeholder_tmpl<Is>{}...);
}

template<typename F, typename C>
typename function_traits<F>::f_type
bind_this(F f, C *this_) {
    using traits = function_traits<F>;
    return bind_this(f, this_, make_indexes_t<traits::arity>{});
}

}} // namespace sqlite3cpp::detail

namespace sqlite3cpp {

/**
 * row impl
 */
template<typename ... Cols>
std::tuple<Cols...> row::get() const
{
    std::tuple<Cols ...> result;
    detail::foreach_tuple_element(result, detail::set_col_val(m_stmt));
    return result;
}

/**
 * cursor impl
 */
template<typename ... Args>
cursor& cursor::execute(std::string const &sql, Args&& ... args)
{
    sqlite3_stmt *stmt = 0;
    if(sqlite3_prepare_v2(m_db, sql.c_str(), sql.size(), &stmt, 0)) {
        throw std::runtime_error("execute statement failure");
    }
    m_stmt.reset(stmt);
    detail::bind_to_stmt(m_stmt.get(), 1, std::forward<Args>(args)...);
    step();
    return *this;
}

/**
 * database impl
 */
template<typename FUNC>
void database::create_scalar(std::string const &name, FUNC&& func,
                             int flags)
{
    using traits = detail::function_traits<FUNC>;

    auto *xfunc_ptr =
        new xfunc_t(detail::make_invoker(typename traits::f_type(func)));

    if(sqlite3_create_function_v2(
      m_db.get(),
      name.c_str(),
      (int)traits::arity,
      flags,
      (void*)xfunc_ptr,
      &database::forward,
      0, 0,
      &dispose))
    {
        delete xfunc_ptr;
        throw std::runtime_error("create_function failure");
    }
}

template<typename AG>
void database::create_aggregate(std::string const &name,
                                int flags)
{
    using detail::make_invoker;
    using detail::bind_this;
    using detail::result;
    using traits = detail::function_traits<
        decltype(&AG::step)>;

    aggregate_wrapper_t *wrapper = new aggregate_wrapper_t;
    AG *inst = new AG;
    wrapper->reset = [inst]() { *inst = AG(); };
    wrapper->release = [inst]() { delete inst; };
    wrapper->step = make_invoker(bind_this(&AG::step, inst));
    wrapper->fin = [inst](sqlite3_context* ctx) { result(inst->finalize(), ctx); };

    if(sqlite3_create_function_v2(
        m_db.get(),
        name.c_str(),
        (int)traits::arity,
        flags,
        (void*)wrapper,
        0,
        &step_ag,
        &final_ag,
        &dispose_ag))
    {
        delete inst;
        delete wrapper;
        throw std::runtime_error("create_aggregate failure");
    }
}

} // namespace sqlite3cpp
