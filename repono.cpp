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
#include <algorithm>

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

    // TOKENIZER (LEXER)

    enum class TokenType
    {
        INTEGER_LITERAL,
        FLOAT_LITERAL,
        STRING_LITERAL,

        IDENTIFIER, // table_name, column_name

        // SQL Keywords
        SELECT,
        FROM,
        WHERE,
        INSERT,
        INTO,
        VALUES,
        UPDATE,
        BETWEEN,
        SET,
        DELETE,

        // Table keywords
        CREATE,
        TABLE,
        DROP,

        // Logical Keywords
        AND,
        OR,
        NOT,

        // Value Keywords
        NULL_KEYWORD,
        TRUE_KEYWORD,
        FALSE_KEYWORD,

        // Constraint keywords

        PRIMARY,
        KEY,

        // Type keywords
        INTEGER_TYPE, // INTEGER, INT
        VARCHAR_TYPE, // VARCHAR, TEXT
        FLOAT_TYPE,   // FLOAT, DOUBLE
        BOOLEAN_TYPE, // BOOLEAN, BOOL

        // Ordering keywords
        ORDER,
        BY,
        ASC,
        DESC,
        LIMIT,
        OFFSET,

        // Comparison
        EQUALS,        // =
        NOT_EQUALS,    // != or <>
        LESS_THAN,     // <
        GREATER_THAN,  // >
        LESS_EQUAL,    // <=
        GREATER_EQUAL, // >=

        // Arithmetic
        PLUS,     // +
        MINUS,    // -
        ASTERISK, // *
        SLASH,    // /

        // Punctuation
        COMMA,       // ,
        SEMICOLON,   // ;
        LEFT_PAREN,  // (
        RIGHT_PAREN, // )
        DOT,         // .

        // Special
        END_OF_FILE, // End of input
        INVALID      // Unknown/error token
    };

    /**
     * Convert TokenType to a readable string (for debugging)
     */
    std::string token_type_to_string(TokenType type)
    {
        switch (type)
        {
        case TokenType::INTEGER_LITERAL:
            return "INTEGER_LITERAL";
        case TokenType::FLOAT_LITERAL:
            return "FLOAT_LITERAL";
        case TokenType::STRING_LITERAL:
            return "STRING_LITERAL";
        case TokenType::IDENTIFIER:
            return "IDENTIFIER";
        case TokenType::SELECT:
            return "SELECT";
        case TokenType::FROM:
            return "FROM";
        case TokenType::WHERE:
            return "WHERE";
        case TokenType::INSERT:
            return "INSERT";
        case TokenType::BETWEEN:
            return "BETWEEN";
        case TokenType::INTO:
            return "INTO";
        case TokenType::VALUES:
            return "VALUES";
        case TokenType::UPDATE:
            return "UPDATE";
        case TokenType::SET:
            return "SET";
        case TokenType::DELETE:
            return "DELETE";
        case TokenType::CREATE:
            return "CREATE";
        case TokenType::TABLE:
            return "TABLE";
        case TokenType::DROP:
            return "DROP";
        case TokenType::AND:
            return "AND";
        case TokenType::OR:
            return "OR";
        case TokenType::NOT:
            return "NOT";
        case TokenType::NULL_KEYWORD:
            return "NULL";
        case TokenType::TRUE_KEYWORD:
            return "TRUE";
        case TokenType::FALSE_KEYWORD:
            return "FALSE";
        case TokenType::PRIMARY:
            return "PRIMARY";
        case TokenType::KEY:
            return "KEY";
        case TokenType::INTEGER_TYPE:
            return "INTEGER_TYPE";
        case TokenType::VARCHAR_TYPE:
            return "VARCHAR_TYPE";
        case TokenType::FLOAT_TYPE:
            return "FLOAT_TYPE";
        case TokenType::BOOLEAN_TYPE:
            return "BOOLEAN_TYPE";
        case TokenType::ORDER:
            return "ORDER";
        case TokenType::BY:
            return "BY";
        case TokenType::ASC:
            return "ASC";
        case TokenType::DESC:
            return "DESC";
        case TokenType::LIMIT:
            return "LIMIT";
        case TokenType::OFFSET:
            return "OFFSET";
        case TokenType::EQUALS:
            return "EQUALS";
        case TokenType::NOT_EQUALS:
            return "NOT_EQUALS";
        case TokenType::LESS_THAN:
            return "LESS_THAN";
        case TokenType::GREATER_THAN:
            return "GREATER_THAN";
        case TokenType::LESS_EQUAL:
            return "LESS_EQUAL";
        case TokenType::GREATER_EQUAL:
            return "GREATER_EQUAL";
        case TokenType::PLUS:
            return "PLUS";
        case TokenType::MINUS:
            return "MINUS";
        case TokenType::ASTERISK:
            return "ASTERISK";
        case TokenType::SLASH:
            return "SLASH";
        case TokenType::COMMA:
            return "COMMA";
        case TokenType::SEMICOLON:
            return "SEMICOLON";
        case TokenType::LEFT_PAREN:
            return "LEFT_PAREN";
        case TokenType::RIGHT_PAREN:
            return "RIGHT_PAREN";
        case TokenType::DOT:
            return "DOT";
        case TokenType::END_OF_FILE:
            return "EOF";
        case TokenType::INVALID:
            return "INVALID";
        default:
            return "UNKNOWN";
        }
    }

    /**
     * Token
     *
     *  Represents a single token from the input.
     */
    struct Token
    {
        TokenType type;   // What kind of token
        std::string text; // The orginal text

        // the actual value for the literals
        std::variant<std::monostate, int64_t, double, std::string> value;

        int line;   // position in source
        int column; // position in source

        // Constructor
        Token(TokenType t = TokenType::INVALID,
              std::string txt = "",
              int ln = 1,
              int col = 1)
            : type(t), text(std::move(txt)), value(std::monostate{}), line(ln), column(col)
        {
        }

        bool is(TokenType t) const
        {
            return type == t;
        }

        bool is_keyword() const
        {
            return type >= TokenType::SELECT && type <= TokenType::OFFSET;
        }

        bool is_comparison() const
        {
            return type >= TokenType::EQUALS && type <= TokenType::GREATER_EQUAL;
        }

        std::string to_string() const
        {
            std::string result = token_type_to_string(type);
            if (!text.empty() && type != TokenType::END_OF_FILE)
            {
                result += "('" + text + "')";
            }
            return result;
        }
    };

    class Lexer
    {
    public:
        explicit Lexer(std::string source)
            : source_(std::move(source)), current_(0), line_(1), column_(1)
        {
            init_keywords();
        }

        std::vector<Token> tokenize()
        {
            std::vector<Token> tokens;
            while (!is_at_end())
            {
                skip_whitespace_and_comments();

                if (is_at_end())
                    break;

                Token token = scan_token();
                tokens.push_back(token);
            }
            tokens.push_back(Token(TokenType::END_OF_FILE, "", line_, column_));

            return tokens;
        }

    private:
        std::string source_;                                  // input SQL
        size_t current_;                                      // current pos
        int line_;                                            // current line
        int column_;                                          // current col
        std::unordered_map<std::string, TokenType> keywords_; // keyword lookup

        void init_keywords()
        {
            keywords_ = {
                // Query keywords
                {"SELECT", TokenType::SELECT},
                {"FROM", TokenType::FROM},
                {"WHERE", TokenType::WHERE},
                {"INSERT", TokenType::INSERT},
                {"INTO", TokenType::INTO},
                {"VALUES", TokenType::VALUES},
                {"UPDATE", TokenType::UPDATE},
                {"SET", TokenType::SET},
                {"DELETE", TokenType::DELETE},
                {"BETWEEN", TokenType::BETWEEN},
                // Table keywords
                {"CREATE", TokenType::CREATE},
                {"TABLE", TokenType::TABLE},
                {"DROP", TokenType::DROP},

                // Logical keywords
                {"AND", TokenType::AND},
                {"OR", TokenType::OR},
                {"NOT", TokenType::NOT},

                // Value keywords
                {"NULL", TokenType::NULL_KEYWORD},
                {"TRUE", TokenType::TRUE_KEYWORD},
                {"FALSE", TokenType::FALSE_KEYWORD},

                // Constraint keywords
                {"PRIMARY", TokenType::PRIMARY},
                {"KEY", TokenType::KEY},

                // Type keywords (multiple spellings)
                {"INTEGER", TokenType::INTEGER_TYPE},
                {"INT", TokenType::INTEGER_TYPE},
                {"VARCHAR", TokenType::VARCHAR_TYPE},
                {"TEXT", TokenType::VARCHAR_TYPE},
                {"FLOAT", TokenType::FLOAT_TYPE},
                {"DOUBLE", TokenType::FLOAT_TYPE},
                {"BOOLEAN", TokenType::BOOLEAN_TYPE},
                {"BOOL", TokenType::BOOLEAN_TYPE},

                // Ordering keywords
                {"ORDER", TokenType::ORDER},
                {"BY", TokenType::BY},
                {"ASC", TokenType::ASC},
                {"DESC", TokenType::DESC},
                {"LIMIT", TokenType::LIMIT},
                {"OFFSET", TokenType::OFFSET}};
        }
        bool is_at_end() const
        {
            return current_ >= source_.length();
        }
        char peek() const
        {
            if (is_at_end())
                return '\0';
            return source_[current_];
        }

        char peek_next() const
        {
            if (current_ + 1 >= source_.length())
                return '\0';
            return source_[current_ + 1];
        }

        char advance()
        {
            char c = source_[current_++];
            if (c == '\n')
            {
                column_ = 1;
                line_++;
            }
            else
            {
                column_++;
            }
            return c;
        }

        bool match(char expected)
        {
            if (is_at_end())
                return false;
            if (expected != source_[current_])
                return false;
            advance();
            return true;
        }

        void skip_whitespace_and_comments()
        {
            while (!is_at_end())
            {
                char c = peek();

                // Whitespace
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                {
                    advance();
                    continue;
                };

                // Single line comment: -- ..
                if (c == '-' && peek_next() == '-')
                {
                    while (!is_at_end() && peek() != '\n')
                    {
                        advance();
                    }
                    continue;
                }

                // Block comment: /* */
                if (c == '/' && peek_next() == '*')
                {
                    advance(); // Skip /
                    advance(); // Skip *

                    while (!is_at_end())
                    {
                        if (peek() == '*' && peek_next() == '/')
                        {
                            advance(); // Skip *
                            advance(); // Skip /
                            break;
                        }
                        advance();
                    }
                    continue;
                }
                break;
            }
        }

        Token scan_token()
        {
            int start_line = line_;
            int start_column = column_;
            size_t start_pos = current_;

            char c = advance();

            switch (c)
            {
            case '(':
                return Token(TokenType::LEFT_PAREN, "(", start_line, start_column);
            case ')':
                return Token(TokenType::RIGHT_PAREN, ")", start_line, start_column);
            case ',':
                return Token(TokenType::COMMA, ",", start_line, start_column);
            case ';':
                return Token(TokenType::SEMICOLON, ";", start_line, start_column);
            case '.':
                return Token(TokenType::DOT, ".", start_line, start_column);
            case '+':
                return Token(TokenType::PLUS, "+", start_line, start_column);
            case '-':
                return Token(TokenType::MINUS, "-", start_line, start_column);
            case '*':
                return Token(TokenType::ASTERISK, "*", start_line, start_column);
            case '/':
                return Token(TokenType::SLASH, "/", start_line, start_column);
            case '=':
                return Token(TokenType::EQUALS, "=", start_line, start_column);

            // Checking two character operators
            case '!':
                if (match('='))
                {
                    return Token(TokenType::NOT_EQUALS, "!=", start_line, start_column);
                }
                // Just ! alone is invalid
                return make_error_token("Unexpected character", c, start_line, start_column);

            case '<':
                if (match('='))
                {
                    return Token(TokenType::LESS_EQUAL, "<=", start_line, start_column);
                }
                if (match('>'))
                {
                    return Token(TokenType::NOT_EQUALS, "<>", start_line, start_column);
                }
                return Token(TokenType::LESS_THAN, "<", start_line, start_column);

            case '>':
                if (match('='))
                {
                    return Token(TokenType::GREATER_EQUAL, ">=", start_line, start_column);
                }
                return Token(TokenType::GREATER_THAN, ">", start_line, start_column);
            case '`':
                return scan_backtick_identifier(start_line, start_column);

            case '\'':
            case '"':
                return scan_string(c, start_line, start_column);
            }
            if (std::isdigit(c))
            {
                return scan_number(start_pos, start_line, start_column);
            }

            // Indentifiers and keyword
            if (std::isalpha(c) || c == '_')
            {
                return scan_identifier(start_pos, start_line, start_column);
            }

            // Unknown character
            return make_error_token("Unexpected character", c, start_line, start_column);
        };

        Token make_error_token(const std::string &message, char bad_char, int line, int col)
        {
            std::string error_msg = message + " '" + std::string(1, bad_char) + "'" + " (ASCII " + std::to_string(static_cast<int>(bad_char)) + ")";
            return Token(TokenType::INVALID, error_msg, line, col);
        }

        Token scan_string(char quote, int start_line, int start_column)
        {
            std::string value;
            while (!is_at_end() && peek() != quote)
            {
                if (peek() == '\\')
                {
                    advance();
                    if (!is_at_end())
                    {
                        char escaped = advance();
                        switch (escaped)
                        {
                        case 'n':
                            value += '\n';
                            break;
                        case 't':
                            value += '\t';
                            break;
                        case 'r':
                            value += '\r';
                            break;
                        case '\\':
                            value += '\\';
                            break;
                        case '\'':
                            value += '\'';
                            break;
                        case '"':
                            value += '"';
                            break;
                        default:
                            value += escaped;
                            break;
                        }
                    }
                }
                else
                {
                    value += advance();
                }
            }

            if (is_at_end())
            {
                return Token(TokenType::INVALID, "Unterminated string", start_line, start_column);
            };
            advance();

            Token token(TokenType::STRING_LITERAL, value, start_line, start_column);
            token.value = value;
            return token;
        };

        Token scan_number(size_t start_pos, int start_line, int start_column)
        {
            current_ = start_pos;
            column_ = start_column;

            if (peek() == '0' && (peek_next() == 'x' || peek_next() == 'X'))
            {
                advance();
                advance();

                size_t hex_start = current_;

                while (!is_at_end() && std::isxdigit(peek()))
                {
                    advance();
                }

                if (current_ == hex_start)
                {
                    return Token(TokenType::INVALID, "Invalid hex number", start_line, start_column);
                }

                std::string hex_text = source_.substr(hex_start, current_ - hex_start);

                int64_t value = std::stoll(hex_text, nullptr, 16); // base 16

                Token token(TokenType::INTEGER_LITERAL, source_.substr(start_pos, current_ - start_pos), start_line, start_column);
                token.value = value;
                return token;
            };

            bool is_float = false;

            while (!is_at_end() && std::isdigit(peek()))
            {
                advance();
            }
            if (!is_at_end() && peek() == '.' && std::isdigit(peek_next()))
            {
                is_float = true;
                advance();
                while (!is_at_end() && std::isdigit(peek()))
                {
                    advance();
                };
            };
            std::string text = source_.substr(start_pos, current_ - start_pos);
            if (is_float)
            {
                Token token(TokenType::FLOAT_LITERAL, text, start_line, start_column);
                token.value = std::stod(text); // string to double
                return token;
            }
            else
            {
                Token token(TokenType::INTEGER_LITERAL, text, start_line, start_column);
                token.value = std::stoll(text); // string to long long
                return token;
            }
        }

        Token scan_backtick_identifier(int start_line, int start_column)
        {
            std::string value;

            while (!is_at_end() && peek() != '`')
            {
                if (peek() == '\n')
                {
                    return Token(TokenType::INVALID, "Newline in backtick identifier", start_line, start_column);
                };
                value += advance();
            }

            if (is_at_end())
            {
                return Token(TokenType::INVALID, "Unterminated backtick identifier", start_line, start_column);
            }
            advance();
            if (value.empty())
            {
                return Token(TokenType::INVALID, "Empty backtick identifier", start_line, start_column);
            }
            return Token(TokenType::IDENTIFIER, value, start_line, start_column);
        }

        Token scan_identifier(size_t start_pos, int start_line, int start_column)
        {
            current_ = start_pos;
            column_ = start_column;

            // checks if its alphanumeric or underscore
            while (!is_at_end() && (std::isalnum(peek()) || peek() == '_'))
            {
                advance();
            };

            std::string text = source_.substr(start_pos, current_ - start_pos);

            // converts text to uppercase
            std::string upper = text;
            for (char &c : upper)
            {
                c = std::toupper(static_cast<unsigned char>(c));
            };

            auto it = keywords_.find(upper);
            if (it != keywords_.end())
            {
                return Token(it->second, text, start_line, start_column);
            }
            return Token(TokenType::IDENTIFIER, text, start_line, start_column);
        }
    };
};

int main()
{
    using namespace repono;

    std::vector<std::string> test_queries = {
        "SELECT * FROM users",
        "SELECT name, age FROM users WHERE age > 25",
        "INSERT INTO users VALUES (1, 'Neel', 15)",
        "INSERT INTO users VALUES (1, 'Soham', 25)",

        "CREATE TABLE test (id INTEGER PRIMARY KEY, name VARCHAR)",
        "SELECT * FROM users ORDER BY age DESC LIMIT 10", "SELECT * FROM users WHERE flags = 0xFF", "SELECT * FROM users WHERE age BETWEEN 18 AND 65", "SELECT @ FROM users", "SELECT `first-name`, `user.email` FROM `my-table`"};

    for (const auto &sql : test_queries)
    {
        std::cout << "SQL: " << sql << std::endl;
        std::cout << "Tokens: ";

        Lexer lexer(sql);
        auto tokens = lexer.tokenize();

        for (const auto &token : tokens)
        {
            if (token.type != TokenType::END_OF_FILE)
            {
                std::cout << token.to_string() << " ";
            }
        }
        std::cout << std::endl
                  << std::endl;
    }

    return 0;
}
