/**
 *  ReponoDB: A Git-Versioned SQL Database
 *  "Repono" is Latin for "to store"
 *  Author: Neel Bansal
 */

#include <iostream>
#include <string>
#include <vector>
#include <variant>
#include <sstream>
#include <iomanip>

namespace repono
{
    using Value = std::variant<
        std::monostate, // Basically null
        int64_t,
        double,
        std::string,
        bool>;

    using Row = std::vector<Value>;

    // Row is just a collection of values

    /**
     * Convert a Value to a string for display
     *
     * @param v The Value to convert
     * @return String representation of the value
     */

    std::string value_to_string(const Value &v)
    {
        if (std::holds_alternative<std::monostate>(v))
        {
            return "NULL";
        }

        if (std::holds_alternative<std::int64_t>(v))
        {
            return std::to_string(std::get<int64_t>(v));
        }

        if (std::holds_alternative<double>(v))
        {
            std::ostringstream oss;

            oss << std::fixed << std::setprecision(2) << std::get<double>(v);
            return oss.str();
        }

        if (std::holds_alternative<std::string>(v))
        {
            return std::get<std::string>(v);
        }

        if (std::holds_alternative<bool>(v))
        {
            return std::get<bool>(v) ? "true" : "false";
        }

        return "???";
    }

    /**
     * Compare two Values for equality
     *
     * NULL = NULL is FALSE
     * NULL = anything is FALSE
     * NULL <> anything is also FALSE!
     *
     * @param a One of the values to compare
     * @param b Second one of the values to compare
     */

    bool values_equal(const Value &a, const Value &b)
    {
        if (std::holds_alternative<std::monostate>(a) || std::holds_alternative<std::monostate>(b))
        {
            return false;
        }
        if (a.index() != b.index())
        {
            return false;
        }

        return a == b; // using inbuilt variant checker
    }

    /**
     * Compare two Values for ordering (less than)
     *
     * Used for ORDER BY and comparisons like <, >, <=, >=
     *
     * We put NULLs at the end (convention, could be start)
     * This makes ORDER BY behave consistently
     *
     * @param a One of the values to compare
     * @param b Second one of the values to compare
     *
     * @return true if a < b
     */

    bool value_less_than(const Value &a, const Value &b)
    {
        if (std::holds_alternative<std::monostate>(a))
            return false;
        if (std::holds_alternative<std::monostate>(b))
            return true;
        if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b))
        {
            return std::get<int64_t>(a) < std::get<int64_t>(b);
        }

        if (std::holds_alternative<double>(a) && std::holds_alternative<double>(a))
        {
            return std::get<double>(a) < std::get<double>(b);
        }

        if ((std::holds_alternative<int64_t>(a) || std::holds_alternative<double>(a)) &&
            (std::holds_alternative<int64_t>(b) || std::holds_alternative<double>(b)))
        {
            double da = std::holds_alternative<int64_t>(a) ? static_cast<double>(std::get<int64_t>(a)) : std::get<int64_t>(a);

            double db = std::holds_alternative<int64_t>(b)
                            ? static_cast<double>(std::get<int64_t>(b))
                            : std::get<double>(b);
            return da < db;
        }

        if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b))
        {
            return std::get<std::string>(a) < std::get<std::string>(b);
        }

        if (std::holds_alternative<bool>(a) && std::holds_alternative<bool>(b))
        {
            return !std::get<bool>(a) && std::get<bool>(b);
            // false < true
        }

        // compare by type index (arbitrary but consistent)

        return a.index() < b.index();
    }

    /**
     * Check if a value is NULL
     *
     * @param v Value to check if its null
     */
    bool is_null(const Value &v)
    {
        return std::holds_alternative<std::monostate>(v);
    }
};

int main()
{
    std::cout << "ReponoDB" << std::endl;
    return 0;
}