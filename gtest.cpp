#include "gtest/gtest.h"
#include "sqlite3cpp.h"
#include <iostream>

TEST(basic, construct) {
    using namespace sqlite3cpp;
    database d("test.db");
}

TEST(basic, query) {
    using namespace sqlite3cpp;
    database db("test.db");
    auto c = db.make_cursor();

    c.executescript(
      "begin;"
      "create table T (a INT, b TEXT);"
      "insert into T values(1, 'test1');"
      "insert into T values(2, 'test2');"
      "insert into T values(2, 'abc');"
      "insert into T values(3, 'test3');"
      "commit;"
      );

    std::string pattern = "test%";
    char const *query = "select * from T where a > ? and a < ? and b like ?";

    int idx = 0;
    for(auto const &row : c.execute(query, 1, 3, pattern)) {
        auto cols = row.get<int, std::string>();

        ASSERT_EQ(2, std::get<0>(cols));
        ASSERT_STREQ("test2", std::get<1>(cols).c_str());

        std::cout << idx++ << ": " <<
            std::get<0>(cols) << "," <<
            std::get<1>(cols) << "\n";
    }
    ::remove("test.db");
}


TEST(basic, wrap_function) {
    std::function<int(int)> c;
    auto f = sqlite3cpp::sqlval2cpp::make_invoker(c);
}


TEST(basic, create_scalar) {
    using namespace sqlite3cpp;
    database db("test.db");
    auto c = db.make_cursor();

    c.executescript(
      "begin;"
      "create table T (a INT, b TEXT);"
      "insert into T values(1, 'test1');"
      "insert into T values(2, 'test2');"
      "insert into T values(2, 'abc');"
      "insert into T values(3, 'test3');"
      "commit;"
      );

    int x = 123;
    std::function<int(int)> func = [&x](int input) {
        return x + input;
    };

    db.create_scalar("plus123", func);

    char const *query = "select plus123(a) from T;";

    int idx = 0;
    for(auto const &row : c.execute(query)) {
        auto cols = row.get<int>();
        std::cout << idx++ << ": " <<
            std::get<0>(cols) << "\n";
    }
    ::remove("test.db");
}
