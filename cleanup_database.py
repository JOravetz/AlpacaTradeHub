#!/usr/bin/env python3
"""
SQLite Database Cleanup Script for the Alpaca Trade Data Collection System.

This script deletes trade data older than a specified date from the stock_trades.db
database, and then performs VACUUM and ANALYZE operations to optimize the database.
"""

import argparse
import sqlite3
import datetime
import os
import sys
import textwrap

def show_manual():
    """Display comprehensive manual page for the script."""
    script_name = os.path.basename(sys.argv[0])
    
    manual = f"""
    NAME
        {script_name} - Database Cleanup Tool for Alpaca Trade Data Collection System

    SYNOPSIS
        {script_name} [OPTIONS]

    DESCRIPTION
        This script deletes trade data older than a specified date from the 
        SQLite database, and then performs VACUUM and ANALYZE operations to 
        optimize the database after deletion.

        The script provides the following features:
          * Deletes trades older than the specified date
          * Performs VACUUM and ANALYZE operations to optimize the database
          * Supports a "dry run" mode to see what would be deleted
          * Provides a summary of deleted and remaining records
          * Shows remaining data grouped by symbol
          * Uses today's date as the default cutoff
          * Validates the date format
          * Handles error conditions gracefully

    OPTIONS
        --db PATH
            Path to the SQLite database
            Default: stock_trades.db

        --date DATE
            Delete trades older than this date (YYYY-MM-DD format)
            Default: today's date ({datetime.date.today().isoformat()})

        --dry-run
            Show what would be deleted without actually deleting

        --help
            Display brief help message and exit

        --man, --manual
            Display this manual page and exit

    EXAMPLES
        # Delete trades older than today (default)
        {script_name}

        # Delete trades older than a specific date
        {script_name} --date 2025-01-01

        # Specify a different database file
        {script_name} --db my_trades.db --date 2025-02-15

        # Preview what would be deleted without making changes
        {script_name} --date 2025-03-01 --dry-run

    AUTHOR
        Alpaca Trading Data Team

    VERSION
        1.0.0
    """
    
    # Print with proper indentation
    print(textwrap.dedent(manual))

def cleanup_database(db_path, cutoff_date):
    """
    Delete trades older than the cutoff date and optimize the database.
    
    Args:
        db_path (str): Path to the SQLite database
        cutoff_date (str): Delete data older than this date (YYYY-MM-DD format)
    
    Returns:
        tuple: (deleted_count, remaining_count) - Number of records deleted and remaining
    """
    if not os.path.exists(db_path):
        print(f"Error: Database file '{db_path}' does not exist.")
        return (0, 0)

    try:
        # Connect to the database
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        
        # First get the current count of records
        cursor.execute("SELECT COUNT(*) FROM trades")
        initial_count = cursor.fetchone()[0]
        
        # Execute the deletion query with a timestamp comparison
        cursor.execute(f"DELETE FROM trades WHERE timestamp < '{cutoff_date}'")
        
        # Get how many rows were deleted
        deleted_count = cursor.rowcount if cursor.rowcount != -1 else "Unknown"
        
        # Commit the changes
        conn.commit()
        
        # Get the new count
        cursor.execute("SELECT COUNT(*) FROM trades")
        remaining_count = cursor.fetchone()[0]
        
        if deleted_count == "Unknown":
            deleted_count = initial_count - remaining_count
        
        # VACUUM to reclaim disk space
        print(f"Optimizing database with VACUUM and ANALYZE...")
        cursor.execute("VACUUM")
        
        # ANALYZE to update statistics
        cursor.execute("ANALYZE")
        
        # Display summary by symbol
        print("\nRemaining data by symbol:")
        cursor.execute("""
            SELECT symbol, COUNT(*) AS count, 
                   MIN(timestamp) AS oldest,
                   MAX(timestamp) AS newest
            FROM trades 
            GROUP BY symbol 
            ORDER BY count DESC
        """)
        
        symbol_data = cursor.fetchall()
        
        if symbol_data:
            # Format as a table
            print(f"{'Symbol':<10} {'Count':<10} {'Oldest':<25} {'Newest':<25}")
            print("-" * 70)
            for row in symbol_data:
                print(f"{row[0]:<10} {row[1]:<10} {row[2]:<25} {row[3]:<25}")
        else:
            print("No data remaining in the database.")
        
        # Close the connection
        conn.close()
        
        return (deleted_count, remaining_count)
    
    except sqlite3.Error as e:
        print(f"SQLite error: {e}")
        return (0, 0)

def main():
    """Parse arguments and run the cleanup."""
    # Check for manual flag first
    if "--man" in sys.argv or "--manual" in sys.argv:
        show_manual()
        return

    # Get today's date for the default value
    today = datetime.date.today().isoformat()
    
    # Create the argument parser with enhanced description
    description = textwrap.dedent("""\
        Clean up old trade data from the SQLite database.
        
        This script deletes trade data older than the specified date and
        performs VACUUM and ANALYZE operations to optimize the database.
        
        Use --man or --manual for comprehensive documentation.
    """)
    
    epilog = textwrap.dedent(f"""\
        Examples:
          %(prog)s                           # Delete trades older than today
          %(prog)s --date 2025-01-01         # Delete trades older than specific date
          %(prog)s --db my_trades.db --date 2025-02-15  # Use custom database
          %(prog)s --date 2025-03-01 --dry-run          # Preview deletion
    """)
    
    parser = argparse.ArgumentParser(
        description=description,
        epilog=epilog,
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    
    # Add arguments with enhanced help
    parser.add_argument('--db', 
                        default='stock_trades.db',
                        help='Path to the SQLite database (default: %(default)s)')
    
    parser.add_argument('--date', 
                        default=today,
                        help=f'Delete trades older than this date in YYYY-MM-DD format (default: {today})')
    
    parser.add_argument('--dry-run', 
                        action='store_true',
                        help='Show what would be deleted without actually deleting')
    
    # Parse the command line arguments
    args = parser.parse_args()
    
    # Validate the date format
    try:
        datetime.date.fromisoformat(args.date)
    except ValueError:
        print(f"Error: Date '{args.date}' is not in the correct format (YYYY-MM-DD)")
        return
    
    # Print information about what we're doing
    print(f"Database: {args.db}")
    print(f"Removing trades older than: {args.date}")
    
    if args.dry_run:
        print("DRY RUN - No changes will be made to the database")
        
        # Check if database exists
        if not os.path.exists(args.db):
            print(f"Error: Database file '{args.db}' does not exist.")
            return
            
        conn = sqlite3.connect(args.db)
        cursor = conn.cursor()
        
        # Get count of records that would be deleted
        cursor.execute(f"SELECT COUNT(*) FROM trades WHERE timestamp < '{args.date}'")
        would_delete = cursor.fetchone()[0]
        
        cursor.execute(f"SELECT COUNT(*) FROM trades")
        total = cursor.fetchone()[0]
        
        would_remain = total - would_delete
        
        print(f"Would delete: {would_delete} trades")
        print(f"Would keep: {would_remain} trades")
        
        conn.close()
    else:
        # Perform the actual cleanup
        deleted, remaining = cleanup_database(args.db, args.date)
        
        # Print the results
        print(f"\nDatabase cleanup summary:")
        print(f"Deleted: {deleted} trades")
        print(f"Remaining: {remaining} trades")
        print(f"Database has been optimized.")

if __name__ == "__main__":
    main()
