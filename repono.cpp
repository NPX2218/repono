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
#include <openssl/sha.h>

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
        BOOLEAN,
        TIMESTAMP
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
        case DataType::TIMESTAMP:
            return "TIMESTAMP";
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
            case DataType::TIMESTAMP:
                type_ok = std::holds_alternative<int64_t>(v); // Store as int64_t
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

        Schema() = default;

        // Copy constructor
        Schema(const Schema &other)
            : columns_(other.columns_),
              column_indices_(other.column_indices_) {}

        // Copy assignment operator

        Schema &operator=(const Schema &other)
        {
            if (this != &other)
            { // Self-assignment check
                columns_ = other.columns_;
                column_indices_ = other.column_indices_;
            }
            return *this;
        }
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

    /**
     * COMMIT
     *
     * A commit is an immutable snapshot of the database state.
     * A unique identifier (hash)
     * A pointer to the parent commit
     * Metadata (message, timestamp, author)
     * The actual data (snapshots of all tables)
     * */

    struct Commit
    {
        std::string hash;
        std::string parent_hash;
        std::string message;
        int64_t timestamp;

        std::unordered_map<std::string, std::vector<Row>> table_data;
        std::unordered_map<std::string, Schema> table_schemas;
        /**
         * Checks if this is the initial commit/root, which is when the parent_hash is empty
         */
        bool is_root() const
        {
            return parent_hash.empty();
        }
    };
    /**
     * BRANCH
     *
     * A branch is just a named pointer to a commit.
     *
     *  branches["main"] = "a3f2b7c"
     *  After commit:
     *  branches["main"] = "b8e4d1a"  (new commit's hash)
     *  We just change the pointers to the main, and therefore dont need a new struct
     */

    /**
     * DIFF RESULT
     *
     * When comparing two commits, we produce a diff showing:
     * Added rows (exist in new but not old)
     * Deleted rows (exist in old but not new)
     * Modified rows (exist in both but different)
     *
     * CommitDiff = "what changed between version A and version B"
     * TableDiff = "what changed in this specific table"
     * RowDiff = "what changed in this specific row
     *
     */
    struct RowDiff
    {
        enum class Type
        {
            ADDED,
            DELETED,
            MODIFIED
        };
        Type type;
        Row old_row;
        Row new_row;

        RowDiff(Type t, Row old_r = {}, Row new_r = {}) : type(t), old_row(std::move(old_r)), new_row(std::move(new_r)) {}
    };

    struct TableDiff
    {
        std::string table_name;
        std::vector<RowDiff> row_diffs;
        bool schema_changed = false;
    };

    struct CommitDiff
    {
        std::string from_hash;
        std::string to_hash;
        std::vector<TableDiff> table_diffs;
        std::vector<std::string> tables_added;
        std::vector<std::string> tables_dropped;
    };

    std::string compute_hash(const std::string &data)
    {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char *>(data.c_str()),
               data.size(),
               hash);

        std::ostringstream oss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        {
            oss << std::hex << std::setfill('0') << std::setw(2)
                << static_cast<int>(hash[i]);
        }
        return oss.str(); // Full 64-char hash
    }

    /**
     * Verify a commit's hash matches its content
     *
     * @param commit The commit to validate
     * @return true if hash is correct
     */

    std::string compute_commit_hash(const Commit &commit)
    {
        std::ostringstream oss;

        // Include parent hash (or empty string for root)
        oss << "parent:" << commit.parent_hash << "\n";
        oss << "message:" << commit.message << "\n";
        oss << "timestamp:" << commit.timestamp << "\n";

        // Include table data (sorted for deterministic order)
        std::vector<std::string> table_names;
        for (const auto &[name, _] : commit.table_data)
        {
            table_names.push_back(name);
        }
        std::sort(table_names.begin(), table_names.end());

        for (const auto &name : table_names)
        {
            oss << "table:" << name << "\n";
            for (const auto &row : commit.table_data.at(name))
            {
                oss << "row:";
                for (size_t i = 0; i < row.size(); i++)
                {
                    if (i > 0)
                        oss << ",";
                    oss << value_to_string(row[i]);
                }
                oss << "\n";
            }
        }

        return compute_hash(oss.str());
    }

    bool validate_commit(const Commit &commit)
    {
        std::string computed = compute_commit_hash(commit);
        return computed == commit.hash;
    }

};
int main()
{
    using namespace repono;

    std::cout << "Testing ReponoDB\n\n";

    // Create some values
    Value null_val = std::monostate{};
    Value age = int64_t{19};
    Value gpa = 3.8;
    Value name = std::string{"Neel"};
    Value active = true;

    std::cout << "Values:\n";
    std::cout << "  " << value_to_string(null_val) << "\n";
    std::cout << "  " << value_to_string(age) << "\n";
    std::cout << "  " << value_to_string(gpa) << "\n";
    std::cout << "  " << value_to_string(name) << "\n";
    std::cout << "  " << value_to_string(active) << "\n\n";

    // Build a users table schema
    Schema users_schema;
    users_schema.add_column(ColumnDef("id", DataType::INTEGER, true, false));
    users_schema.add_column(ColumnDef("name", DataType::VARCHAR));
    users_schema.add_column(ColumnDef("age", DataType::INTEGER));

    std::cout << "Schema created with " << users_schema.num_columns() << " columns\n";

    // Check column lookup
    if (auto idx = users_schema.get_column_index("name"))
    {
        std::cout << "Found 'name' at index " << *idx << "\n";
    }

    if (!users_schema.get_column_index("email"))
    {
        std::cout << "No 'email' column (expected)\n";
    }

    // Create some rows
    Row neel = {int64_t{1}, std::string{"Neel"}, int64_t{19}};
    Row swati = {int64_t{2}, std::string{"Swati"}, int64_t{21}};

    // Validate them
    std::string err = users_schema.validate_row(neel);
    std::cout << "\nNeel valid: " << (err.empty() ? "yes" : err) << "\n";

    Row bad_row = {int64_t{1}, std::string{"Neel"}}; // missing age
    err = users_schema.validate_row(bad_row);
    std::cout << "Bad row: " << err << "\n";

    // Create first commit
    Commit first;
    first.parent_hash = "";
    first.message = "Initial commit";
    first.timestamp = 1703529600;
    first.table_schemas["users"] = users_schema;
    first.table_data["users"] = {neel};
    first.hash = compute_commit_hash(first);

    std::cout << "\nFirst commit: " << first.hash.substr(0, 8) << "...\n";

    // Create second commit
    Commit second;
    second.parent_hash = first.hash;
    second.message = "Added Swati";
    second.timestamp = 1703529700;
    second.table_schemas["users"] = users_schema;
    second.table_data["users"] = {neel, swati};
    second.hash = compute_commit_hash(second);

    std::cout << "Second commit: " << second.hash.substr(0, 8) << "...\n";
    std::cout << "  parent: " << second.parent_hash.substr(0, 8) << "...\n";

    std::cout << "\nDone!\n";
    return 0;
}