# ReponoDB

A Git-versioned SQL database written in C++17. "Repono" is Latin for "to store."

> ⚠️ **Work in Progress**: This project is under active development.

## What is this?

ReponoDB combines database fundamentals with version control. Every change to your data creates an immutable commit, letting you:

- **Time-travel**: Query your database as it existed at any point in history
- **Branch**: Create isolated environments for experiments or features
- **Diff**: See exactly what changed between any two commits
- **Merge**: Combine changes from different branches

Think of it as Git for your database tables.

## Building

Requires OpenSSL for SHA-256 hashing.

```bash
# Install OpenSSL (macOS)
brew install openssl

# Build
make

# Run
./repono
```

### Makefile

```makefile
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -I/opt/homebrew/opt/openssl/include
LDFLAGS = -L/opt/homebrew/opt/openssl/lib -lssl -lcrypto

all: repono

repono: repono.cpp
	$(CXX) $(CXXFLAGS) -o repono repono.cpp $(LDFLAGS)

clean:
	rm -f repono
```

## Core Concepts

### Values & Types

```cpp
using Value = std::variant<
    std::monostate,  // NULL
    int64_t,         // INTEGER, TIMESTAMP
    double,          // FLOAT
    std::string,     // VARCHAR
    bool             // BOOLEAN
>;
```

### Commits

Each commit is an immutable snapshot containing:

- SHA-256 hash (computed from content)
- Parent commit hash (creates the chain)
- Timestamp and message
- Complete table data

```
Commit A          Commit B          Commit C
┌──────────┐      ┌──────────┐      ┌──────────┐
│ hash: abc│◄─────│parent:abc│◄─────│parent:def│
│ parent:  │      │ hash: def│      │ hash: 789│
│ tables   │      │ tables   │      │ tables   │
└──────────┘      └──────────┘      └──────────┘
```

### Diffs

Comparing commits produces a structured diff:

```cpp
CommitDiff
├── tables_added: ["new_table"]
├── tables_dropped: ["old_table"]
└── table_diffs:
    └── TableDiff("users")
        ├── RowDiff(ADDED, {}, {1, "Alice"})
        ├── RowDiff(DELETED, {2, "Bob"}, {})
        └── RowDiff(MODIFIED, {3, "Carol", 28}, {3, "Carol", 29})
```

## Usage (Planned)

```sql
-- Create a table
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name VARCHAR NOT NULL,
    created_at TIMESTAMP
);

-- Insert data
INSERT INTO users VALUES (1, 'Neel', 1703619600);

-- Commit changes
COMMIT 'Added first user';

-- View history
LOG;

-- Time travel
SELECT * FROM users AS OF 'abc123';

-- Branch
CHECKOUT -b feature;
```

## Author

Neel Bansal

## License

MIT
