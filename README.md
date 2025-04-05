# Alpaca Trade Data Collection System

This system provides a set of tools to fetch, process, store, and analyze historical trade data from Alpaca's API. With these tools, you can collect detailed stock trade information and store it in an SQLite database for further analysis.

## System Components

The system consists of the following main components:

1. **Trade Fetcher (`trade_fetcher.cpp`)**: Fetches historical trade data from Alpaca's API
2. **Trade Processor (`trade_processor.cpp`)**: Processes and stores trade data in an SQLite database
3. **Python Trade Database Schema (`trade_db_schema.py`)**: Alternative Python implementation for database management
4. **Query Helper (`query_helper.py`)**: Provides analysis tools for querying and analyzing stored trade data
5. **Integration Script (`collect_trades.sh`)**: Automates the process of fetching and storing trade data
6. **Supporting Tools**: Additional utilities for Alpaca API access (`alpaca_account.cpp`, `get_bars.cpp`)

## Prerequisites

### API Credentials
You must have an Alpaca account with API access. Set your API keys as environment variables:

```bash
export APCA_API_KEY_ID="your_api_key_id"
export APCA_API_SECRET_KEY="your_api_secret_key"
```

### C++ Implementation Requirements
- C++ compiler with C++17 support
- SQLite3 development libraries
- libcurl development libraries

Installation on Ubuntu/Debian:
```bash
sudo apt-get install build-essential libsqlite3-dev libcurl4-openssl-dev
```

### Python Implementation Requirements
- Python 3.6 or higher
- Required packages:
  ```bash
  pip install pandas
  ```

## Building the Tools

A Makefile is included to simplify building the C++ components:

```bash
# Build all tools in the current directory
make

# Build and install tools to ~/bin
make install

# Build with debugging enabled
make debug

# Clean build artifacts
make clean
```

## Getting Started

### Step 1: Fetch Trade Data

Use the `trade_fetcher` tool to download historical trade data:

```bash
# Fetch trades for NVDA for the last 7 trading days
./trade_fetcher -n 7 -s NVDA -l 10000 -f sip > nvda_trades.json

# Fetch trades for multiple symbols
./trade_fetcher -n 5 -s AAPL,MSFT,NVDA -l 10000 -f sip > multiple_trades.json

# Fetch trades for symbols listed in a file
./trade_fetcher -n 3 -i symbols.txt -l 10000 -f sip > from_file_trades.json
```

Key options for `trade_fetcher`:
- `-n days`: Number of trading days to fetch data for
- `-s symbols`: Comma-separated list of stock symbols
- `-i file`: File containing stock symbols (one per line)
- `-l limit`: Maximum trades per request (default: 10000, max: 10000)
- `-f feed`: Feed source: sip, iex, boats, otc (default: sip)
- `-d`: Sort in descending order (default: ascending)

### Step 2: Process and Store the Data

#### Using the C++ Processor:

```bash
# Store trades from a JSON file
./trade_processor --db stock_trades.db --file nvda_trades.json

# Or pipe the output directly
./trade_fetcher -n 7 -s AAPL -l 10000 -f sip | ./trade_processor --db stock_trades.db

# View database statistics
./trade_processor --db stock_trades.db --stats
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

### Step 3: Analyze the Data

Use the `query_helper.py` tool to analyze the stored trade data:

```bash
# Get trades for a symbol
python query_helper.py --db stock_trades.db trades --symbol AAPL --limit 20

# Calculate VWAP (Volume-Weighted Average Price)
python query_helper.py --db stock_trades.db vwap --symbol MSFT --date 2025-03-14

# Get exchange breakdown
python query_helper.py --db stock_trades.db exchanges --symbol NVDA

# Get time-of-day analysis
python query_helper.py --db stock_trades.db time --symbol AAPL --date 2025-03-14

# Export trades to CSV
python query_helper.py --db stock_trades.db export --symbol NVDA --output nvda_trades.csv
```

### Automated Data Collection

Use the provided bash script to automate the process:

```bash
# Configure and run the script
chmod +x collect_trades.sh
./collect_trades.sh -n 7 -s AAPL,MSFT,NVDA -d stock_trades.db
```

Key options for `collect_trades.sh`:
- `-n days`: Number of trading days to fetch (default: 1)
- `-s symbols`: Comma-separated list of stock symbols (required)
- `-d path`: Database path (default: stock_trades.db)
- `-l limit`: Maximum trades per request (default: 10000)
- `-f feed`: Feed source (default: sip)

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

The table includes a unique constraint on `(symbol, trade_id, timestamp_epoch)` to prevent duplicate entries.

## Common Use Cases

### 1. Daily Data Collection

Set up a cron job to collect trade data daily for a set of symbols:

```bash
# Edit your crontab
crontab -e

# Add a daily job at 20:00 (8 PM)
0 20 * * * cd /path/to/your/project && ./collect_trades.sh -n 1 -s AAPL,MSFT,NVDA,GOOGL,AMZN -d stock_trades.db >> collection_log.txt 2>&1
```

### 2. Calculate Daily VWAP for Multiple Symbols

Create a script to calculate VWAP for a list of symbols:

```bash
#!/bin/bash
DB_PATH="stock_trades.db"
DATE="2025-03-14"
SYMBOLS="AAPL MSFT NVDA GOOGL AMZN"

for symbol in $SYMBOLS; do
    echo "VWAP for $symbol on $DATE:"
    python query_helper.py --db $DB_PATH vwap --symbol $symbol --date $DATE
    echo "----------------------------------------"
done
```

### 3. Analyze Trading Patterns

Identify which exchanges have the most activity for a symbol:

```bash
python query_helper.py --db stock_trades.db exchanges --symbol NVDA
```

Analyze time-of-day trading patterns:

```bash
python query_helper.py --db stock_trades.db time --symbol AAPL
```

## Advanced SQL Queries

You can run custom SQL queries directly against the database:

### Get the most recent trades for a symbol:

```sql
SELECT * FROM trades 
WHERE symbol = 'NVDA' 
ORDER BY timestamp_epoch DESC 
LIMIT 10;
```

### Calculate VWAP for a symbol on a specific day:

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

## Database Maintenance

Regular maintenance is essential to keep the database performing optimally, especially as it grows with historical trade data.

### Cleanup and Vacuum Procedures

The system includes dedicated tools to clean up old data and optimize database performance:

#### Python Script: `cleanup_database.py`

```bash
# Delete trades older than today (default)
python cleanup_database.py

# Delete trades older than a specific date
python cleanup_database.py --date 2025-01-01

# Specify a different database file
python cleanup_database.py --db my_trades.db --date 2025-02-15

# Preview what would be deleted without making changes
python cleanup_database.py --date 2025-03-01 --dry-run

# Show detailed manual
python cleanup_database.py --man
```

#### Bash Script: `cleanup_database.sh`

```bash
# Make the script executable
chmod +x cleanup_database.sh

# Delete trades older than today (default)
./cleanup_database.sh

# Delete trades older than a specific date
./cleanup_database.sh --date 2025-01-01

# Specify a different database file
./cleanup_database.sh --db my_trades.db --date 2025-02-15

# Preview what would be deleted without making changes
./cleanup_database.sh --date 2025-03-01 --dry-run

# Show help information
./cleanup_database.sh --help
```

Both scripts provide the following features:
1. Delete trades older than a specified date (with today's date as the default)
2. Perform VACUUM and ANALYZE operations to optimize the database
3. Support "dry run" mode to preview changes without making them
4. Show a detailed summary of deleted and remaining records
5. Display remaining data grouped by symbol

### Manual SQLite Maintenance

If you prefer to handle database maintenance manually, you can use the SQLite command-line interface:

```bash
# Connect to the database
sqlite3 stock_trades.db

# Delete trades older than a specific date
sqlite> DELETE FROM trades WHERE timestamp < '2025-01-01';

# Delete all data for a specific symbol
sqlite> DELETE FROM trades WHERE symbol = 'AAPL';

# Reclaim disk space after deletion
sqlite> VACUUM;

# Update database statistics for query optimizer
sqlite> ANALYZE;

# Exit SQLite
sqlite> .exit
```

### Scheduled Maintenance

For automated regular maintenance, consider adding a cron job:

```bash
# Edit your crontab
crontab -e

# Add a weekly cleanup job (runs every Sunday at 1 AM)
0 1 * * 0 /path/to/your/project/cleanup_database.sh --date $(date -d "30 days ago" +\%Y-\%m-\%d) >> /path/to/cleanup_log.txt 2>&1
```

## Troubleshooting

### API Key Issues
- Ensure your Alpaca API keys are properly set as environment variables
- Verify your API keys have the correct permissions

### Build Problems
- Make sure all development libraries are installed
- Check for compiler errors in the output

### Performance Optimization
- For large datasets, consider database maintenance:
  ```sql
  VACUUM;
  ANALYZE;
  ```
- Use appropriate indexes when running custom queries
- Limit query results to avoid memory issues

## Potential Applications

The Alpaca Trade Data Collection System can be utilized for a wide range of financial analysis and trading applications:

### Market Analysis
- **Historical Price Analysis**: Analyze micro-structure patterns and price movements at a granular level not visible in traditional candlestick charts
- **Volume Profile Analysis**: Understand how trading volume distributes across different price levels to identify significant support and resistance zones
- **Exchange Performance Comparison**: See which exchanges handle more volume for particular stocks, potentially helping with optimal order routing
- **Market Impact Analysis**: Study how large trades affected prices historically to optimize execution strategies for future large orders

### Trading Strategy Development
- **Algorithmic Trading Strategy Development**: Backtest trading algorithms against historical tick-by-tick data for more realistic results than using only OHLC data
- **Volatility Modeling**: Calculate more accurate volatility measurements and models that capture intraday patterns
- **Mean Reversion Analysis**: Identify opportunities for mean reversion trading by analyzing price deviations from average values

### Technical Applications
- **Machine Learning Model Training**: Use detailed historical data as rich features for training AI models to predict price movements or identify trading opportunities
- **Anomaly Detection**: Identify unusual trading patterns or potential market manipulation by examining detailed trade history
- **Data Visualization Projects**: Create custom visualizations of market microstructure and trading patterns

### Research and Compliance
- **Academic Research**: Study market microstructure, price formation processes, and other market behavior topics
- **Regulatory Compliance**: Maintain historical trade records for compliance purposes or trade reconstruction
- **Risk Management Analysis**: Assess market liquidity and trading risks at different times of day or market conditions

### Advanced Use Cases
- **Cross-Asset Correlation Analysis**: Study relationships between different securities by analyzing their trading patterns
- **Market Maker Behavior Analysis**: Identify patterns in market maker activity by analyzing trades across different exchanges
- **High-Frequency Trading Research**: Study trade timing and market response at millisecond granularity

## Extending the System

You can extend this system in several ways:

1. Add support for different data types (quotes, bars, etc.)
2. Implement real-time data collection
3. Create a web dashboard for visualization
4. Add automated analysis and alerts
5. Integrate with trading algorithms
6. Build custom reporting and analytics tools
7. Implement machine learning pipelines for predictive analytics

## Files Overview

- `trade_fetcher.cpp`: Main tool for fetching historical trade data from Alpaca
- `trade_processor.cpp`: C++ implementation for processing and storing trade data
- `trade_db_schema.py`: Python implementation for database management
- `query_helper.py`: Analysis and querying tool
- `collect_trades.sh`: Automation script for data collection
- `Makefile`: Build configuration
- `alpaca_account.cpp`: Utility for accessing Alpaca account information
- `get_bars.cpp`: Tool for fetching bar (OHLC) data from Alpaca
- `integration_guide.md`: Initial documentation provided with the system
