#!/usr/bin/env python3
"""
Trade Data Query Helper - A tool to extract and analyze trade data from the SQLite database
"""

import sqlite3
import argparse
import json
import csv
import sys
import os
from datetime import datetime, timedelta
import pandas as pd
from io import StringIO

def connect_to_db(db_path):
    """Connect to the SQLite database."""
    if not os.path.exists(db_path):
        print(f"Error: Database {db_path} does not exist")
        sys.exit(1)
        
    try:
        conn = sqlite3.connect(db_path)
        conn.row_factory = sqlite3.Row  # This enables column access by name
        return conn
    except sqlite3.Error as e:
        print(f"Error connecting to database: {e}")
        sys.exit(1)

def execute_query(conn, query, params=None):
    """Execute a SQL query and return the results."""
    try:
        cursor = conn.cursor()
        if params:
            cursor.execute(query, params)
        else:
            cursor.execute(query)
        return cursor.fetchall()
    except sqlite3.Error as e:
        print(f"Error executing query: {e}")
        print(f"Query was: {query}")
        if params:
            print(f"Parameters: {params}")
        sys.exit(1)

def get_trades_for_symbol(conn, symbol, start_date=None, end_date=None, limit=100, format='pretty'):
    """Get trades for a specific symbol within a date range."""
    params = [symbol]
    query = "SELECT * FROM trades WHERE symbol = ?"
    
    if start_date:
        query += " AND timestamp >= ?"
        params.append(start_date)
    
    if end_date:
        query += " AND timestamp <= ?"
        params.append(end_date)
    
    query += " ORDER BY timestamp_epoch ASC"
    
    if limit:
        query += f" LIMIT {limit}"
    
    rows = execute_query(conn, query, params)
    
    if format == 'json':
        result = []
        for row in rows:
            result.append({k: row[k] for k in row.keys()})
        return json.dumps(result, indent=2)
    elif format == 'csv':
        if not rows:
            return "No data found"
        
        output = StringIO()
        writer = csv.writer(output)
        writer.writerow(rows[0].keys())
        for row in rows:
            writer.writerow(row)
        return output.getvalue()
    else:  # pretty print
        if not rows:
            return "No trades found for the specified criteria"
        
        result = f"Trades for {symbol}"
        if start_date:
            result += f" from {start_date}"
        if end_date:
            result += f" to {end_date}"
        result += f" (limited to {limit} records):\n\n"
        
        headers = rows[0].keys()
        col_widths = {h: max(len(h), max([len(str(r[h])) for r in rows])) for h in headers}
        
        # Print headers
        header_row = " | ".join(h.ljust(col_widths[h]) for h in headers)
        result += header_row + "\n"
        result += "-" * len(header_row) + "\n"
        
        # Print rows
        for row in rows:
            row_str = " | ".join(str(row[h]).ljust(col_widths[h]) for h in headers)
            result += row_str + "\n"
            
        return result

def get_vwap(conn, symbol, date, include_after_hours=False):
    """Calculate VWAP (Volume-Weighted Average Price) for a symbol on a specific date."""
    # Format date string for query
    date_str = date + "%"
    
    # Base query
    query = """
    SELECT 
        symbol,
        SUM(price * size) / SUM(size) AS vwap,
        MIN(price) AS low_price,
        MAX(price) AS high_price,
        SUM(size) AS total_volume,
        COUNT(*) AS trade_count
    FROM trades
    WHERE symbol = ? AND timestamp LIKE ?
    """
    
    # Add time filter for regular trading hours if needed
    if not include_after_hours:
        query += " AND (timestamp LIKE '%T09:3%' OR timestamp LIKE '%T1%' OR timestamp LIKE '%T2%' OR timestamp LIKE '%T15:5%')"
    
    query += " GROUP BY symbol"
    
    rows = execute_query(conn, query, [symbol, date_str])
    
    if not rows:
        return f"No trades found for {symbol} on {date}"
    
    row = rows[0]
    
    market_hours = "including after-hours" if include_after_hours else "regular trading hours only"
    
    result = f"VWAP Analysis for {symbol} on {date} ({market_hours}):\n\n"
    result += f"VWAP Price: ${row['vwap']:.2f}\n"
    result += f"Low Price:  ${row['low_price']:.2f}\n"
    result += f"High Price: ${row['high_price']:.2f}\n"
    result += f"Total Volume: {row['total_volume']:,} shares\n"
    result += f"Number of Trades: {row['trade_count']:,}\n"
    
    return result

def get_exchange_breakdown(conn, symbol, date=None):
    """Get breakdown of trades by exchange for a symbol."""
    params = [symbol]
    query = """
    SELECT 
        exchange,
        COUNT(*) AS trade_count,
        SUM(size) AS total_volume,
        ROUND(AVG(price), 2) AS avg_price,
        ROUND(MIN(price), 2) AS min_price,
        ROUND(MAX(price), 2) AS max_price
    FROM trades
    WHERE symbol = ?
    """
    
    if date:
        query += " AND timestamp LIKE ?"
        params.append(date + "%")
    
    query += " GROUP BY exchange ORDER BY total_volume DESC"
    
    rows = execute_query(conn, query, params)
    
    if not rows:
        return f"No trades found for {symbol}" + (f" on {date}" if date else "")
    
    time_range = f" on {date}" if date else ""
    result = f"Exchange Breakdown for {symbol}{time_range}:\n\n"
    
    # Determine column widths
    headers = ["Exchange", "Trades", "Volume", "Avg Price", "Min Price", "Max Price"]
    col_data = [
        [row["exchange"] for row in rows],
        [f"{row['trade_count']:,}" for row in rows],
        [f"{row['total_volume']:,}" for row in rows],
        [f"${row['avg_price']:.2f}" for row in rows],
        [f"${row['min_price']:.2f}" for row in rows],
        [f"${row['max_price']:.2f}" for row in rows]
    ]
    
    col_widths = [
        max(len(headers[i]), max(len(item) for item in col_data[i]))
        for i in range(len(headers))
    ]
    
    # Print headers
    header_row = " | ".join(h.ljust(col_widths[i]) for i, h in enumerate(headers))
    result += header_row + "\n"
    result += "-" * len(header_row) + "\n"
    
    # Print rows
    for row in rows:
        row_str = " | ".join([
            row["exchange"].ljust(col_widths[0]),
            f"{row['trade_count']:,}".ljust(col_widths[1]),
            f"{row['total_volume']:,}".ljust(col_widths[2]),
            f"${row['avg_price']:.2f}".ljust(col_widths[3]),
            f"${row['min_price']:.2f}".ljust(col_widths[4]),
            f"${row['max_price']:.2f}".ljust(col_widths[5])
        ])
        result += row_str + "\n"
        
    # Calculate totals
    total_trades = sum(row["trade_count"] for row in rows)
    total_volume = sum(row["total_volume"] for row in rows)
    
    result += "-" * len(header_row) + "\n"
    result += f"Total Trades: {total_trades:,}\n"
    result += f"Total Volume: {total_volume:,} shares\n"
    
    return result

def get_conditions_breakdown(conn, symbol, date=None):
    """Get breakdown of trade conditions for a symbol."""
    # First, get all unique conditions
    conditions_query = """
    SELECT DISTINCT conditions 
    FROM trades 
    WHERE symbol = ? AND conditions IS NOT NULL AND conditions != ''
    """
    
    params = [symbol]
    if date:
        conditions_query += " AND timestamp LIKE ?"
        params.append(date + "%")
    
    rows = execute_query(conn, conditions_query, params)
    
    if not rows:
        return f"No trades with conditions found for {symbol}" + (f" on {date}" if date else "")
    
    # Extract and flatten all unique condition codes
    all_conditions = set()
    for row in rows:
        if row["conditions"]:
            conditions = row["conditions"].split(",")
            all_conditions.update(conditions)
    
    all_conditions = sorted(all_conditions)
    
    # Now count trades for each condition
    result = f"Trade Conditions Breakdown for {symbol}" + (f" on {date}" if date else "") + ":\n\n"
    
    for condition in all_conditions:
        count_query = """
        SELECT COUNT(*) as count, SUM(size) as volume
        FROM trades
        WHERE symbol = ? AND conditions LIKE ?
        """
        
        like_param = f"%{condition}%"
        count_params = [symbol, like_param]
        
        if date:
            count_query += " AND timestamp LIKE ?"
            count_params.append(date + "%")
        
        count_row = execute_query(conn, count_query, count_params)[0]
        
        result += f"Condition '{condition}': {count_row['count']:,} trades, {count_row['volume']:,} shares\n"
    
    return result

def get_time_of_day_analysis(conn, symbol, date=None):
    """Analyze trade activity by hour of day."""
    params = [symbol]
    query = """
    SELECT 
        SUBSTR(timestamp, 12, 2) as hour,
        COUNT(*) as trade_count,
        SUM(size) as volume,
        ROUND(AVG(price), 2) as avg_price
    FROM trades
    WHERE symbol = ?
    """
    
    if date:
        query += " AND timestamp LIKE ?"
        params.append(date + "%")
    
    query += " GROUP BY hour ORDER BY hour"
    
    rows = execute_query(conn, query, params)
    
    if not rows:
        return f"No trades found for {symbol}" + (f" on {date}" if date else "")
    
    time_range = f" on {date}" if date else ""
    result = f"Time of Day Analysis for {symbol}{time_range}:\n\n"
    
    # Determine column widths
    headers = ["Hour", "Trades", "Volume", "Avg Price"]
    col_data = [
        [row["hour"] + ":00" for row in rows],
        [f"{row['trade_count']:,}" for row in rows],
        [f"{row['volume']:,}" for row in rows],
        [f"${row['avg_price']:.2f}" for row in rows]
    ]
    
    col_widths = [
        max(len(headers[i]), max(len(item) for item in col_data[i]))
        for i in range(len(headers))
    ]
    
    # Print headers
    header_row = " | ".join(h.ljust(col_widths[i]) for i, h in enumerate(headers))
    result += header_row + "\n"
    result += "-" * len(header_row) + "\n"
    
    # Print rows
    for row in rows:
        row_str = " | ".join([
            (row["hour"] + ":00").ljust(col_widths[0]),
            f"{row['trade_count']:,}".ljust(col_widths[1]),
            f"{row['volume']:,}".ljust(col_widths[2]),
            f"${row['avg_price']:.2f}".ljust(col_widths[3])
        ])
        result += row_str + "\n"
    
    return result

def get_database_stats(conn):
    """Get overall database statistics."""
    # Get trade count and date range
    stats_query = """
    SELECT 
        COUNT(*) as total_trades,
        MIN(timestamp) as earliest_trade,
        MAX(timestamp) as latest_trade,
        COUNT(DISTINCT symbol) as symbol_count
    FROM trades
    """
    
    stats_row = execute_query(conn, stats_query)[0]
    
    # Get storage info
    storage_query = "SELECT page_count * page_size as size FROM pragma_page_count(), pragma_page_size()"
    storage_row = execute_query(conn, storage_query)[0]
    db_size_mb = storage_row["size"] / (1024 * 1024)
    
    # Get top symbols by trade count
    top_symbols_query = """
    SELECT 
        symbol,
        COUNT(*) as trade_count,
        SUM(size) as volume
    FROM trades
    GROUP BY symbol
    ORDER BY trade_count DESC
    LIMIT 10
    """
    
    top_symbols = execute_query(conn, top_symbols_query)
    
    result = "Database Statistics\n"
    result += "==================\n\n"
    result += f"Total Trades: {stats_row['total_trades']:,}\n"
    result += f"Unique Symbols: {stats_row['symbol_count']}\n"
    result += f"Date Range: {stats_row['earliest_trade']} to {stats_row['latest_trade']}\n"
    result += f"Database Size: {db_size_mb:.2f} MB\n\n"
    
    result += "Top Symbols by Trade Count:\n"
    result += "----------------------------\n"
    
    for i, row in enumerate(top_symbols, 1):
        result += f"{i}. {row['symbol']}: {row['trade_count']:,} trades, {row['volume']:,} shares\n"
    
    return result

def export_to_csv(conn, symbol, start_date=None, end_date=None, output_file=None):
    """Export trade data to CSV file."""
    params = [symbol]
    query = "SELECT * FROM trades WHERE symbol = ?"
    
    if start_date:
        query += " AND timestamp >= ?"
        params.append(start_date)
    
    if end_date:
        query += " AND timestamp <= ?"
        params.append(end_date)
    
    query += " ORDER BY timestamp_epoch ASC"
    
    rows = execute_query(conn, query, params)
    
    if not rows:
        print(f"No trades found for {symbol}")
        return
    
    if not output_file:
        date_part = ""
        if start_date:
            date_part = f"_{start_date.replace('-', '')}"
        output_file = f"{symbol}{date_part}_trades.csv"
    
    with open(output_file, 'w', newline='') as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(rows[0].keys())  # Write headers
        
        for row in rows:
            writer.writerow(row)
    
    print(f"Exported {len(rows):,} trades to {output_file}")

def main():
    parser = argparse.ArgumentParser(description='Trade Data Query Tool')
    parser.add_argument('--db', required=True, help='Path to the SQLite database')
    
    subparsers = parser.add_subparsers(dest='command', help='Commands')
    
    # Get trades command
    trades_parser = subparsers.add_parser('trades', help='Get trades for a symbol')
    trades_parser.add_argument('--symbol', required=True, help='Stock symbol')
    trades_parser.add_argument('--start', help='Start date (YYYY-MM-DD)')
    trades_parser.add_argument('--end', help='End date (YYYY-MM-DD)')
    trades_parser.add_argument('--limit', type=int, default=100, help='Maximum number of trades to return')
    trades_parser.add_argument('--format', choices=['pretty', 'json', 'csv'], default='pretty', help='Output format')
    
    # VWAP command
    vwap_parser = subparsers.add_parser('vwap', help='Calculate VWAP for a symbol')
    vwap_parser.add_argument('--symbol', required=True, help='Stock symbol')
    vwap_parser.add_argument('--date', required=True, help='Date (YYYY-MM-DD)')
    vwap_parser.add_argument('--all-hours', action='store_true', help='Include after-hours trading')
    
    # Exchange breakdown command
    exchange_parser = subparsers.add_parser('exchanges', help='Get exchange breakdown')
    exchange_parser.add_argument('--symbol', required=True, help='Stock symbol')
    exchange_parser.add_argument('--date', help='Filter by date (YYYY-MM-DD)')
    
    # Conditions breakdown command
    conditions_parser = subparsers.add_parser('conditions', help='Analyze trade conditions')
    conditions_parser.add_argument('--symbol', required=True, help='Stock symbol')
    conditions_parser.add_argument('--date', help='Filter by date (YYYY-MM-DD)')
    
    # Time of day analysis command
    time_parser = subparsers.add_parser('time', help='Analyze trades by time of day')
    time_parser.add_argument('--symbol', required=True, help='Stock symbol')
    time_parser.add_argument('--date', help='Filter by date (YYYY-MM-DD)')
    
    # Database stats command
    subparsers.add_parser('stats', help='Get database statistics')
    
    # Export to CSV command
    export_parser = subparsers.add_parser('export', help='Export trades to CSV')
    export_parser.add_argument('--symbol', required=True, help='Stock symbol')
    export_parser.add_argument('--start', help='Start date (YYYY-MM-DD)')
    export_parser.add_argument('--end', help='End date (YYYY-MM-DD)')
    export_parser.add_argument('--output', help='Output file name')
    
    args = parser.parse_args()
    
    # Connect to the database
    conn = connect_to_db(args.db)
    
    try:
        if args.command == 'trades':
            result = get_trades_for_symbol(
                conn, args.symbol, args.start, args.end, args.limit, args.format
            )
            print(result)
            
        elif args.command == 'vwap':
            result = get_vwap(conn, args.symbol, args.date, args.all_hours)
            print(result)
            
        elif args.command == 'exchanges':
            result = get_exchange_breakdown(conn, args.symbol, args.date)
            print(result)
            
        elif args.command == 'conditions':
            result = get_conditions_breakdown(conn, args.symbol, args.date)
            print(result)
            
        elif args.command == 'time':
            result = get_time_of_day_analysis(conn, args.symbol, args.date)
            print(result)
            
        elif args.command == 'stats':
            result = get_database_stats(conn)
            print(result)
            
        elif args.command == 'export':
            export_to_csv(conn, args.symbol, args.start, args.end, args.output)
            
        else:
            parser.print_help()
            
    finally:
        conn.close()

if __name__ == "__main__":
    main()
