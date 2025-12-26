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
#include <unordered_map>
#include <optional>

namespace repono
{
    using Value = std::variant< // variant actually holds data
        std::monostate,         // Basically null
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

        if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b))
        {
            return std::get<double>(a) < std::get<double>(b);
        }

        if ((std::holds_alternative<int64_t>(a) || std::holds_alternative<double>(a)) &&
            (std::holds_alternative<int64_t>(b) || std::holds_alternative<double>(b)))
        {
            double da = std::holds_alternative<int64_t>(a) ? static_cast<double>(std::get<int64_t>(a)) : std::get<double>(a);

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

    enum class DataType
    {
        INTEGER,
        FLOAT,
        VARCHAR, // text strings (stored as std::string)
        BOOLEAN
    };

    /**
     * Get the string version of a datatype
     *
     * @param type The type to convert
     */

    std::string datatype_to_string(DataType type)
    {
        switch (type)
        {
        case DataType::INTEGER:
            return "INTEGER";
        case DataType::FLOAT:
            return "FLOAT";
        case DataType::VARCHAR:
            return "VARCHAR";
        case DataType::BOOLEAN:
            return "BOOLEAN";
        default:
            return "UNKNOWN";
        }
    };

    /*
     * Describes one column in a table, e.g.:
     * name: "id"
     * type: INTEGER, VARCHAR, etc.
     * is_primary_key: Is this the primary key?
     * is_nullable: Can this column contain NULL?
     */

    struct ColumnDef
    {
        std::string name;
        DataType type = DataType::INTEGER;
        bool is_primary_key = false;
        bool is_nullable = true;

        ColumnDef() = default; // default constructor

        ColumnDef(std::string n, DataType t, bool pk = false, bool nullable = true) : name(std::move(n)), type(t), is_primary_key(pk), is_nullable(nullable) {};

        /**
         * Validate that a value matches this column's type
         *
         * @param v The Value to validate
         * @returns "" if valid or an error message if invalid
         */
        std::string validate(const Value &v) const
        {
            if (is_null(v))
            {
                if (!is_nullable)
                {
                    return "Column '" + name + "' cannot be NULL";
                }
                return "";
            }
            bool type_ok = false;

            switch (type)
            {
            case DataType::INTEGER:
                type_ok = std::holds_alternative<int64_t>(v);
                break;
            case DataType::FLOAT:
                // Accept both int and float (int gets converted)
                type_ok = std::holds_alternative<double>(v) || std::holds_alternative<int64_t>(v);
                break;
            case DataType::VARCHAR:
                type_ok = std::holds_alternative<std::string>(v);
                break;
            case DataType::BOOLEAN:
                type_ok = std::holds_alternative<bool>(v);
                break;
            }

            if (!type_ok)
            {
                return "Column '" + name + "' expects " +
                       datatype_to_string(type) + ", got wrong type";
            }
            return "";
        }
    };

    class Schema
    {

    public:
        /**
         * Add a column to the schema
         *
         * @param column The column to add
         */
        void add_column(const ColumnDef &column)
        {
            column_indices_[column.name] = columns_.size();
            columns_.push_back(column);
        }

        /**
         * Get all the columns
         */
        const std::vector<ColumnDef> &get_columns() const { return columns_; }

        /**
         * Get the number of columns
         */

        size_t num_columns() const { return columns_.size(); }

        /**
         * Look up a column's index by name
         * Returns std::nullopt if not found
         *
         * @param name The name of the specific column
         * @return The index of the column, or std::nullopt if not found

         */
        std::optional<size_t> get_column_index(const std::string &name) const
        {
            auto it = column_indices_.find(name);
            if (it != column_indices_.end()) // checking that it doesnt point to the end of the array
            {
                return it->second;
            }
            return std::nullopt;
        };

        /**
         * Get a column definition by its name
         *
         * @param name The name of the column
         * @returns Pointer to the ColumnDef, or nullptr if not found
         */
        const ColumnDef *get_column(const std::string &name) const
        {
            auto idx = get_column_index(name);
            if (idx.has_value())
            {
                return &columns_[idx.value()];
            }
            return nullptr;
        }

        /**
         * Check if a column exists
         * @param name The name of the column
         * @returns true if the column exists, false otherwise
         */

        bool has_column(const std::string &name) const
        {
            return column_indices_.find(name) != column_indices_.end();
        }

        /**
         * Validates that the schema and the rows match up
         * @param row The row of values to validate against the schema

         */

        std::string validate_row(const Row &row) const
        {
            if (row.size() != columns_.size())
            {
                return "Expected " + std::to_string(columns_.size()) +
                       " columns, got " + std::to_string(row.size());
            }
            for (size_t i = 0; i < row.size(); i++)
            {
                std::string error = columns_[i].validate(row[i]);
                if (!error.empty())
                {
                    return error;
                }
            }
            return "";
        }

    private:
        std::vector<ColumnDef> columns_; // Ordered list  e.g. [ ColumnDef("id"), ColumnDef("name"), ColumnDef("age") ]

        std::unordered_map<std::string, size_t> column_indices_; // Name -> index  e.g. { "id"→0, "name"→1, "age"→2 }
    };

}
int main()
{
    using namespace repono;

    Schema schema;
    schema.add_column(ColumnDef("id", DataType::INTEGER, true, false));
    schema.add_column(ColumnDef("name", DataType::VARCHAR));
    schema.add_column(ColumnDef("age", DataType::FLOAT));

    Row good_row = {int64_t(1), std::string("Neel"), 10};
    std::cout << "Valid row: " << schema.validate_row(good_row) << std::endl;

    Row bad_row = {int64_t(1), std::string("Neel")};
    std::cout << "Bad row: " << schema.validate_row(bad_row) << std::endl;

    auto idx = schema.get_column_index("name");
    if (idx.has_value())
    {
        std::cout << "name is at index: " << idx.value() << std::endl;
    }
}