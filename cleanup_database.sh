#!/bin/bash

# ----------------------------------------------------------------------------

# Database Cleanup Script for Alpaca Trade Data Collection System
# 
# This script deletes trade data older than a specified date from the 
# stock_trades.db database, then performs VACUUM and ANALYZE operations
# to optimize the database.

# This bash script provides the following features:

# 1. Deletes trades older than the specified date (with today as the default)
# 2. Performs VACUUM and ANALYZE operations to optimize the database
# 3. Supports a "dry run" mode to preview what would be deleted
# 4. Provides a summary of deleted and remaining records
# 5. Shows remaining data grouped by symbol in a table format
# 6. Validates the date format (basic check for YYYY-MM-DD)
# 7. Handles error conditions such as non-existent database files

# You can run it like this:

# Make the script executable
# chmod +x cleanup_database.sh

# Delete trades older than today (default)
# ./cleanup_database.sh

# Delete trades older than a specific date
# ./cleanup_database.sh --date 2025-01-01

# Specify a different database file
# ./cleanup_database.sh --db my_trades.db --date 2025-02-15

# Preview what would be deleted without making changes
# ./cleanup_database.sh --date 2025-03-01 --dry-run

# Show help information
# ./cleanup_database.sh --help

# ----------------------------------------------------------------------------

# Default values
DB_PATH="stock_trades.db"
TODAY=$(date +%Y-%m-%d)
CUTOFF_DATE="$TODAY"
DRY_RUN=false

# Function to display usage information
usage() {
    echo "Database Cleanup Tool for Alpaca Trade Data Collection System"
    echo ""
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --db PATH       Path to the SQLite database (default: stock_trades.db)"
    echo "  --date DATE     Delete trades older than this date (YYYY-MM-DD format)"
    echo "                  (default: today's date - $TODAY)"
    echo "  --dry-run       Show what would be deleted without making changes"
    echo "  --help          Show this help message"
    echo ""
    echo "Example: $0 --date 2025-01-01 --db my_trades.db"
}

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --db)
            DB_PATH="$2"
            shift 2
            ;;
        --date)
            CUTOFF_DATE="$2"
            shift 2
            ;;
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "Error: Unknown option $1"
            usage
            exit 1
            ;;
    esac
done

# Validate date format (simple check for YYYY-MM-DD)
if ! [[ $CUTOFF_DATE =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]]; then
    echo "Error: Date '$CUTOFF_DATE' is not in the correct format (YYYY-MM-DD)"
    exit 1
fi

# Check if database file exists
if [ ! -f "$DB_PATH" ]; then
    echo "Error: Database file '$DB_PATH' does not exist."
    exit 1
fi

# Print information about what we're doing
echo "Database: $DB_PATH"
echo "Removing trades older than: $CUTOFF_DATE"

if [ "$DRY_RUN" = true ]; then
    echo "DRY RUN - No changes will be made to the database"
    
    # Get count of records that would be deleted
    WOULD_DELETE=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM trades WHERE timestamp < '$CUTOFF_DATE';")
    TOTAL=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM trades;")
    WOULD_REMAIN=$((TOTAL - WOULD_DELETE))
    
    echo "Would delete: $WOULD_DELETE trades"
    echo "Would keep: $WOULD_REMAIN trades"
else
    # Get initial count
    INITIAL_COUNT=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM trades;")
    
    echo "Initial trade count: $INITIAL_COUNT"
    echo "Deleting trades older than $CUTOFF_DATE..."
    
    # Execute deletion and get results
    sqlite3 "$DB_PATH" <<EOF
-- Delete trades older than cutoff date
DELETE FROM trades WHERE timestamp < '$CUTOFF_DATE';

-- Get the new count
SELECT 'Remaining trades: ' || COUNT(*) FROM trades;
EOF
    
    # Calculate deleted count
    REMAINING_COUNT=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM trades;")
    DELETED_COUNT=$((INITIAL_COUNT - REMAINING_COUNT))
    
    echo "Deleted: $DELETED_COUNT trades"
    echo "Optimizing database with VACUUM and ANALYZE..."
    
    # Optimize database
    sqlite3 "$DB_PATH" <<EOF
VACUUM;
ANALYZE;
EOF
    
    echo "Database optimization complete."
    echo ""
    echo "Remaining data by symbol:"
    
    # Format the output as a table
    echo "Symbol     Count      Oldest                    Newest"
    echo "------------------------------------------------------------"
    
    # Get symbol data
    sqlite3 "$DB_PATH" <<EOF
.mode column
.headers off
.width 10 10 25 25
SELECT 
    symbol, 
    COUNT(*) AS count,
    MIN(timestamp) AS oldest,
    MAX(timestamp) AS newest
FROM trades 
GROUP BY symbol 
ORDER BY count DESC;
EOF
    
    echo ""
    echo "Database cleanup summary:"
    echo "Deleted: $DELETED_COUNT trades"
    echo "Remaining: $REMAINING_COUNT trades"
    echo "Database has been optimized."
fi

exit 0
