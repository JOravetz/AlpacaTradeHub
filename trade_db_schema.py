import sqlite3
import json
from datetime import datetime
import sys
import os
import argparse

def create_database(db_path):
    """Create a new SQLite database with the trade data schema."""
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    
    # Create the trades table
    cursor.execute('''
    CREATE TABLE IF NOT EXISTS trades (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        symbol TEXT NOT NULL,
        trade_id INTEGER NOT NULL,
        price REAL NOT NULL,
        size INTEGER NOT NULL,
        timestamp TEXT NOT NULL,
        exchange TEXT NOT NULL,
        tape TEXT NOT NULL,
        conditions TEXT,
        timestamp_epoch INTEGER NOT NULL,
        UNIQUE(symbol, trade_id, timestamp_epoch)
    )
    ''')
    
    # Create index on symbol and timestamp for faster queries
    cursor.execute('CREATE INDEX IF NOT EXISTS idx_symbol_timestamp ON trades (symbol, timestamp_epoch)')
    
    # Create a table to track parsed response tokens
    cursor.execute('''
    CREATE TABLE IF NOT EXISTS page_tokens (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        token TEXT UNIQUE,
        processed_at TEXT NOT NULL
    )
    ''')
    
    conn.commit()
    conn.close()
    
    print(f"Database created successfully at {db_path}")

def parse_iso_timestamp(timestamp_str):
    """Convert ISO timestamp string to both timestamp and epoch value."""
    dt = datetime.fromisoformat(timestamp_str.replace('Z', '+00:00'))
    return dt.isoformat(), int(dt.timestamp() * 1000000000)  # Nanoseconds for precision

def load_trade_data(db_path, json_data):
    """Load trade data from JSON into the SQLite database."""
    try:
        # Parse JSON if it's a string
        if isinstance(json_data, str):
            data = json.loads(json_data)
        else:
            data = json_data
            
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        
        # Store the next_page_token if it exists
        if 'next_page_token' in data and data['next_page_token']:
            cursor.execute(
                'INSERT OR IGNORE INTO page_tokens (token, processed_at) VALUES (?, ?)',
                (data['next_page_token'], datetime.now().isoformat())
            )
        
        # Check if the trades key exists
        if 'trades' not in data:
            print("No trades found in the data")
            conn.close()
            return 0
            
        trades_data = data['trades']
        total_inserted = 0
        
        # Process each symbol's trades
        for symbol, trades in trades_data.items():
            rows_to_insert = []
            
            for trade in trades:
                # Parse and validate required fields
                trade_id = trade.get('i')
                price = trade.get('p')
                size = trade.get('s')
                timestamp_str = trade.get('t')
                exchange = trade.get('x')
                tape = trade.get('z')
                
                # Skip incomplete records
                if None in (trade_id, price, size, timestamp_str, exchange, tape):
                    print(f"Skipping incomplete trade record: {trade}")
                    continue
                
                # Process conditions array if present
                conditions = None
                if 'c' in trade and trade['c']:
                    conditions = ','.join(trade['c'])
                
                # Parse timestamp
                timestamp, timestamp_epoch = parse_iso_timestamp(timestamp_str)
                
                # Prepare row for insertion
                row = (
                    symbol, 
                    trade_id, 
                    price, 
                    size, 
                    timestamp, 
                    exchange, 
                    tape, 
                    conditions, 
                    timestamp_epoch
                )
                rows_to_insert.append(row)
            
            # Batch insert using executemany for better performance
            if rows_to_insert:
                try:
                    cursor.executemany('''
                    INSERT OR IGNORE INTO trades 
                    (symbol, trade_id, price, size, timestamp, exchange, tape, conditions, timestamp_epoch)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                    ''', rows_to_insert)
                    
                    total_inserted += len(rows_to_insert)
                    print(f"Inserted {len(rows_to_insert)} trades for {symbol}")
                    
                except sqlite3.Error as e:
                    print(f"Error inserting trades for {symbol}: {e}")
        
        conn.commit()
        conn.close()
        return total_inserted
        
    except Exception as e:
        print(f"Error processing trade data: {e}")
        raise

def get_last_page_token(db_path):
    """Retrieve the most recent page token from the database."""
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    
    cursor.execute('''
    SELECT token FROM page_tokens
    ORDER BY id DESC LIMIT 1
    ''')
    
    result = cursor.fetchone()
    conn.close()
    
    if result:
        return result[0]
    return None

def list_symbols(db_path):
    """List all unique symbols in the database."""
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    
    cursor.execute('SELECT DISTINCT symbol FROM trades ORDER BY symbol')
    symbols = [row[0] for row in cursor.fetchall()]
    
    conn.close()
    return symbols

def get_trade_statistics(db_path, symbol=None):
    """Get statistics about trades in the database."""
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    
    stats = {}
    
    if symbol:
        # Stats for a specific symbol
        cursor.execute('''
        SELECT 
            COUNT(*) as count,
            MIN(timestamp) as oldest,
            MAX(timestamp) as newest,
            AVG(price) as avg_price,
            SUM(size) as total_volume
        FROM trades
        WHERE symbol = ?
        ''', (symbol,))
        
        row = cursor.fetchone()
        if row and row[0] > 0:
            stats[symbol] = {
                'count': row[0],
                'oldest_trade': row[1],
                'newest_trade': row[2],
                'avg_price': row[3],
                'total_volume': row[4]
            }
    else:
        # Overall stats
        cursor.execute('''
        SELECT 
            COUNT(DISTINCT symbol) as symbols,
            COUNT(*) as trades,
            MIN(timestamp) as oldest,
            MAX(timestamp) as newest
        FROM trades
        ''')
        
        row = cursor.fetchone()
        if row:
            stats['overall'] = {
                'symbol_count': row[0],
                'trade_count': row[1],
                'oldest_trade': row[2],
                'newest_trade': row[3]
            }
        
        # Stats per symbol
        cursor.execute('''
        SELECT 
            symbol,
            COUNT(*) as count,
            MIN(timestamp) as oldest,
            MAX(timestamp) as newest
        FROM trades
        GROUP BY symbol
        ORDER BY count DESC
        ''')
        
        stats['symbols'] = {}
        for row in cursor.fetchall():
            stats['symbols'][row[0]] = {
                'count': row[1],
                'oldest_trade': row[2],
                'newest_trade': row[3]
            }
    
    conn.close()
    return stats

def main():
    parser = argparse.ArgumentParser(description='Stock Trade Database Manager')
    subparsers = parser.add_subparsers(dest='command', help='Commands')
    
    # Create database command
    create_parser = subparsers.add_parser('create', help='Create a new database')
    create_parser.add_argument('--db', required=True, help='Path to the database file')
    
    # Load data command
    load_parser = subparsers.add_parser('load', help='Load trade data from JSON file')
    load_parser.add_argument('--db', required=True, help='Path to the database file')
    load_parser.add_argument('--file', required=True, help='Path to the JSON file containing trade data')
    
    # List symbols command
    list_parser = subparsers.add_parser('list-symbols', help='List all symbols in the database')
    list_parser.add_argument('--db', required=True, help='Path to the database file')
    
    # Stats command
    stats_parser = subparsers.add_parser('stats', help='Get statistics about the trades')
    stats_parser.add_argument('--db', required=True, help='Path to the database file')
    stats_parser.add_argument('--symbol', help='Get statistics for a specific symbol')
    
    args = parser.parse_args()
    
    if args.command == 'create':
        create_database(args.db)
    
    elif args.command == 'load':
        if not os.path.exists(args.file):
            print(f"Error: File {args.file} does not exist")
            return
            
        with open(args.file, 'r') as f:
            json_data = json.load(f)
            
        count = load_trade_data(args.db, json_data)
        print(f"Loaded {count} trades into the database")
    
    elif args.command == 'list-symbols':
        symbols = list_symbols(args.db)
        print(f"Found {len(symbols)} symbols in the database:")
        for symbol in symbols:
            print(f"  {symbol}")
    
    elif args.command == 'stats':
        stats = get_trade_statistics(args.db, args.symbol)
        
        if args.symbol:
            if args.symbol in stats:
                print(f"Statistics for {args.symbol}:")
                for key, value in stats[args.symbol].items():
                    print(f"  {key}: {value}")
            else:
                print(f"No trades found for symbol {args.symbol}")
        else:
            print("Overall Statistics:")
            for key, value in stats['overall'].items():
                print(f"  {key}: {value}")
                
            print("\nTop Symbols by Trade Count:")
            for i, (symbol, data) in enumerate(list(stats['symbols'].items())[:10], 1):
                print(f"  {i}. {symbol}: {data['count']} trades from {data['oldest_trade']} to {data['newest_trade']}")
    
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
