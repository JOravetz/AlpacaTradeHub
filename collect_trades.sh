#!/bin/bash

# Default configuration
DB_PATH="stock_trades.db"
DAYS_TO_FETCH=1
LIMIT=10000
FEED="sip"
SYMBOLS=""

# Function to display usage information
usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  -n DAYS     Number of trading days to fetch (default: 1)"
    echo "  -s SYMBOLS  Comma-separated list of stock symbols (required)"
    echo "  -d PATH     Database path (default: stock_trades.db)"
    echo "  -l LIMIT    Maximum trades per request (default: 10000)"
    echo "  -f FEED     Feed source: sip, iex, boats, otc (default: sip)"
    echo "  -h          Show this help message"
    echo ""
    echo "Example: $0 -n 5 -s aapl,msft,nvda"
}

# Parse command-line arguments
while getopts "n:s:d:l:f:h" opt; do
    case $opt in
        n)
            # Validate that days is a positive integer
            if [[ ! $OPTARG =~ ^[0-9]+$ ]] || [ $OPTARG -eq 0 ]; then
                echo "Error: Days must be a positive integer"
                exit 1
            fi
            DAYS_TO_FETCH=$OPTARG
            ;;
        s)
            # Convert symbols to uppercase
            SYMBOLS=$(echo $OPTARG | tr '[:lower:]' '[:upper:]')
            ;;
        d)
            DB_PATH=$OPTARG
            ;;
        l)
            LIMIT=$OPTARG
            ;;
        f)
            FEED=$OPTARG
            ;;
        h)
            usage
            exit 0
            ;;
        \?)
            echo "Invalid option: -$OPTARG" >&2
            usage
            exit 1
            ;;
    esac
done

# Check if symbols are provided
if [ -z "$SYMBOLS" ]; then
    echo "Error: Stock symbols are required (-s option)"
    usage
    exit 1
fi

# Check that API keys are set
if [ -z "$APCA_API_KEY_ID" ] || [ -z "$APCA_API_SECRET_KEY" ]; then
    echo "Error: Alpaca API keys not set. Please export APCA_API_KEY_ID and APCA_API_SECRET_KEY."
    exit 1
fi

# Find executables - use local directory first, then check $HOME/bin, then system path
TRADE_FETCHER="./trade_fetcher"
if [ ! -x "$TRADE_FETCHER" ]; then
    TRADE_FETCHER="$HOME/bin/trade_fetcher"
    if [ ! -x "$TRADE_FETCHER" ]; then
        TRADE_FETCHER=$(which trade_fetcher 2>/dev/null)
        if [ -z "$TRADE_FETCHER" ]; then
            echo "Error: trade_fetcher executable not found."
            exit 1
        fi
    fi
fi

TRADE_PROCESSOR="./trade_processor"
if [ ! -x "$TRADE_PROCESSOR" ]; then
    TRADE_PROCESSOR="$HOME/bin/trade_processor"
    if [ ! -x "$TRADE_PROCESSOR" ]; then
        TRADE_PROCESSOR=$(which trade_processor 2>/dev/null)
        if [ -z "$TRADE_PROCESSOR" ]; then
            echo "Error: trade_processor executable not found."
            exit 1
        fi
    fi
fi

echo "Starting data collection with the following parameters:"
echo "- Database: $DB_PATH"
echo "- Days to fetch: $DAYS_TO_FETCH"
echo "- Symbols: $SYMBOLS"
echo "- Feed: $FEED"
echo "- Limit per request: $LIMIT"
echo "- Using trade_fetcher: $TRADE_FETCHER"
echo "- Using trade_processor: $TRADE_PROCESSOR"
echo ""

# Fetch and store trade data
for symbol in $(echo $SYMBOLS | tr ',' ' '); do
    echo "Fetching trade data for $symbol for the last $DAYS_TO_FETCH days..."
    
    # Fetch trades and pipe directly to processor
    $TRADE_FETCHER -n $DAYS_TO_FETCH -s $symbol -l $LIMIT -f $FEED | $TRADE_PROCESSOR --db $DB_PATH
    
    # Check if the command was successful
    if [ $? -eq 0 ]; then
        echo "Successfully processed data for $symbol"
    else
        echo "Error processing data for $symbol"
    fi
    echo "-------------------------------------"
done

# Print database statistics
echo "Database Statistics:"
$TRADE_PROCESSOR --db $DB_PATH --stats

echo "Data collection complete."
