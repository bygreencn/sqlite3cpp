#pragma once

#include <memory>
#include <string>
#include <cstdint>
#include "sqlite3.h" 

#ifndef SQLITE_DETERMINISTIC
#define SQLITE_DETERMINISTIC 0
#endif

#define C_STYLE_DELETER(T, F) \
    struct T##_deleter {\
        void operator()(T* mem) const { F(mem); } \
    }

namespace sqlite3cpp {

/**
 * Forwarded decls
 */
struct database;
struct cursor;
struct row_iter;
struct row;
struct aggregate;

C_STYLE_DELETER(sqlite3, sqlite3_close);
C_STYLE_DELETER(sqlite3_stmt, sqlite3_finalize);

struct row
{
    template<typename ... Cols>
    std::tuple<Cols...> get() const;
private:
    friend struct row_iter;
    row() : m_stmt(nullptr) {}
    sqlite3_stmt *m_stmt;
};

struct row_iter
{
    row_iter &operator++();
    bool operator == (row_iter const &i) const;
    bool operator != (row_iter const &i) const;
    row const &operator*() const { return m_row; }
    row const *operator->() const { return &m_row; }
private:
    friend struct cursor;
    row_iter() : m_csr(nullptr) {}
    row_iter(cursor &csr);
    cursor *m_csr;
    row m_row;
};

struct cursor
{
    template<typename ... Args>
    cursor& execute(std::string const &sql, Args&& ... args);

    void executescript(std::string const &sql);

    row_iter begin() { return row_iter(*this); }
    row_iter end()   { return row_iter(); }

    sqlite3_stmt *get() const { return m_stmt.get(); }

private:
    void step();
    friend struct row_iter;
    friend struct database;
    cursor(database const &db);
    sqlite3 *m_db;
    std::unique_ptr<sqlite3_stmt, sqlite3_stmt_deleter> m_stmt;
};

struct database
{
    using xfunc_t = std::function<void(sqlite3_context*, sqlite3_value **)>;
    using xfinal_t = std::function<void(sqlite3_context*)>;
    using xreset_t = std::function<void()>;

    database(std::string const &urn);

    cursor make_cursor() const;
    sqlite3 *get() const { return m_db.get(); }

    template<typename FUNC>
    void create_scalar(std::string const &name,
                       FUNC &&func,
                       int flags=SQLITE_UTF8 | SQLITE_DETERMINISTIC);

    template<typename AG>
    void create_aggregate(std::string const &name,
                          int flags=SQLITE_UTF8 | SQLITE_DETERMINISTIC);

private:

    struct aggregate_wrapper_t {
        xfunc_t step;
        xfinal_t fin;
        xreset_t reset;
        xreset_t release;
    };

    static void forward(sqlite3_context *ctx, int argc, sqlite3_value **argv);
    static void dispose(void *user_data);
    static void step_ag(sqlite3_context *ctx, int argc, sqlite3_value **argv);
    static void final_ag(sqlite3_context *ctx);
    static void dispose_ag(void *user_data);

    std::unique_ptr<sqlite3, sqlite3_deleter> m_db;
};

} // namespace sqlite3cpp


#include "sqlite3cpp.ipp"
