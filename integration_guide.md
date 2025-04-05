# Historical Trade Data Storage System

This guide explains how to use the provided tools to store historical trade data from Alpaca's API into an SQLite database.

## Overview

The system consists of two main components:

1. **C++ Trade Data Fetcher**: Fetches historical trade data from Alpaca's API
2. **Trade Data Processor**: Processes and stores the trade data in an SQLite database

You can choose to use either the Python or C++ version of the Trade Data Processor, depending on your preferences.

## Prerequisites

### For C++ implementation:
- C++ compiler with C++11 support
- SQLite3 development libraries
- libcurl development libraries

Installation on Ubuntu/Debian:
```bash
sudo apt-get install build-essential libsqlite3-dev libcurl4-openssl-dev
```

### For Python implementation:
- Python 3.6 or higher
- SQLite3 (typically included with Python)

Installation of Python dependencies:
```bash
pip install sqlite3
```

## Building the Tools

### C++ Fetcher (from your existing code)

1. Save the C++ code to a file named `trade_fetcher.cpp`
2. Compile with the following command:

```bash
g++ -o trade_fetcher trade_fetcher.cpp -lcurl -std=c++11
```

### C++ Processor

1. Save the C++ processor code to a file named `trade_processor.cpp`
2. Compile with the following command:

```bash
g++ -o trade_processor trade_processor.cpp -lsqlite3 -std=c++11
```

## Using the Tools

### Step 1: Fetch Trade Data

Set your Alpaca API keys as environment variables:

```bash
export APCA_API_KEY_ID="your_api_key_id"
export APCA_API_SECRET_KEY="your_api_secret_key"
```

Fetch historical trade data for specific symbols:

```bash
# Fetch trades for NVDA for the last 7 days
./trade_fetcher -n 7 -s NVDA -l 10000 -f sip > nvda_trades.json

# Fetch trades for multiple symbols from a file
./trade_fetcher -n 5 -i symbols.txt -l 10000 -f sip > multiple_trades.json
```

### Step 2: Process and Store the Data

#### Using the C++ Processor:

```bash
# Store trades from a JSON file
./trade_processor --db stock_trades.db --file nvda_trades.json

# You can also pipe the output directly
./trade_fetcher -n 7 -s AAPL -l 10000 -f sip | ./trade_processor --db stock_trades.db
```

#### Using the Python Processor:

```bash
# Create the database
python trade_db_schema.py create --db stock_trades.db

# Load trades from a JSON file
python trade_db_schema.py load --db stock_trades.db --file nvda_trades.json

# View database statistics
python trade_db_schema.py stats --db stock_trades.db
```

## Integration Script

For convenience, you can use the following bash script to automate the process:

```bash
#!/bin/bash

# Configuration
DB_PATH="stock_trades.db"
DAYS_TO_FETCH=7
LIMIT=10000
FEED="sip"
SYMBOLS="AAPL,NVDA,MSFT,GOOGL,AMZN"

# Check that API keys are set
if [ -z "$APCA_API_KEY_ID" ] || [ -z "$APCA_API_SECRET_KEY" ]; then
    echo "Error: Alpaca API keys not set. Please export APCA_API_KEY_ID and APCA_API_SECRET_KEY."
    exit 1
fi

# Fetch and store trade data
for symbol in $(echo $SYMBOLS | tr ',' ' '); do
    echo "Fetching trade data for $symbol for the last $DAYS_TO_FETCH days..."
    
    # Fetch trades and pipe directly to processor
    ./trade_fetcher -n $DAYS_TO_FETCH -s $symbol -l $LIMIT -f $FEED | ./trade_processor --db $DB_PATH
    
    echo "Completed processing data for $symbol"
    echo "-------------------------------------"
done

# Print database statistics
echo "Database Statistics:"
./trade_processor --db $DB_PATH --stats

echo "Data collection complete."
```

Save this script as `collect_trades.sh`, make it executable with `chmod +x collect_trades.sh`, and run it.

## Database Schema

The trade data is stored in an SQLite database with the following schema:

### `trades` Table

| Column           | Type    | Description                                   |
|------------------|---------|-----------------------------------------------|
| id               | INTEGER | Primary key                                   |
| symbol           | TEXT    | Stock symbol (e.g., "NVDA")                   |
| trade_id         | INTEGER | Trade ID provided by the API                  |
| price            | REAL    | Trade price                                   |
| size             | INTEGER | Number of shares traded                       |
| timestamp        | TEXT    | ISO 8601 timestamp                            |
| exchange         | TEXT    | Exchange identifier (e.g., "P", "K")          |
| tape             | TEXT    | Tape identifier (e.g., "C")                   |
| conditions       | TEXT    | Comma-separated trade condition codes         |
| timestamp_epoch  | INTEGER | Timestamp as nanoseconds since epoch (for sorting) |

The table has a unique constraint on `(symbol, trade_id, timestamp_epoch)` to prevent duplicate entries. Trades are stored in chronological order (oldest first) to facilitate easy retrieval of historical data.

## Querying the Database

You can use SQL queries to extract and analyze the trade data:

### Get the most recent trades for a symbol:

```sql
SELECT * FROM trades 
WHERE symbol = 'NVDA' 
ORDER BY timestamp_epoch DESC 
LIMIT 10;
```

### Calculate VWAP (Volume-Weighted Average Price) for a symbol on a specific day:

```sql
SELECT 
    symbol,
    SUM(price * size) / SUM(size) AS vwap,
    SUM(size) AS total_volume
FROM trades
WHERE symbol = 'NVDA' 
    AND timestamp LIKE '2025-03-14%'
GROUP BY symbol;
```

### Get trade count per exchange for a symbol:

```sql
SELECT 
    exchange,
    COUNT(*) AS trade_count,
    SUM(size) AS total_volume
FROM trades
WHERE symbol = 'AAPL'
GROUP BY exchange
ORDER BY trade_count DESC;
```

## Performance Considerations

- The database uses indexes on `(symbol, timestamp_epoch)` to speed up queries.
- Transactions are used when inserting data to improve performance.
- For large amounts of data, consider using database maintenance:
  ```sql
  VACUUM;
  ANALYZE;
  ```

## Extending the System

You can extend this system in several ways:

1. Add scheduled data collection using cron jobs
2. Implement more sophisticated analysis tools
3. Create a dashboard for visualizing the trade data
4. Add support for other data types like quotes or bars
