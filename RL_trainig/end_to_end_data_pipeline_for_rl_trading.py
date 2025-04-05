#!/usr/bin/env python3
"""
Complete data pipeline for stock trading RL:
1. Download historical trade data using your C++ program
2. Store in SQLite database
3. Preprocess for RL training
4. Train a simple RL agent
5. Evaluate performance
"""

import os
import subprocess
import sqlite3
import pandas as pd
import numpy as np
import datetime
import matplotlib.pyplot as plt
import gym
from stable_baselines3 import PPO
from stable_baselines3.common.env_util import make_vec_env
from stable_baselines3.common.evaluation import evaluate_policy

# Import the StockTradingEnv class from the previous code
from trading_env import StockTradingEnv, TradeDataHandler

# Configuration
DB_PATH = "alpaca_trades.db"
DATA_FETCH_EXECUTABLE = "./fetch_trades"  # Your compiled C++ program
SYMBOLS = ["AAPL", "NVDA", "MSFT", "AMZN", "GOOGL"]
DAYS = 30  # Number of days of historical data
MODELS_DIR = "models"
LOGS_DIR = "logs"

# Create directories if they don't exist
os.makedirs(MODELS_DIR, exist_ok=True)
os.makedirs(LOGS_DIR, exist_ok=True)

def fetch_historical_data():
    """Fetch historical trade data using the C++ executable"""
    print("Fetching historical trade data...")
    
    # Check if environment variables are set
    if not os.environ.get("APCA_API_KEY_ID") or not os.environ.get("APCA_API_SECRET_KEY"):
        raise ValueError("APCA_API_KEY_ID and APCA_API_SECRET_KEY must be set in environment")
    
    # Get today's date
    today = datetime.date.today()
    
    # Calculate the start date (N days ago)
    start_date = today - datetime.timedelta(days=DAYS)
    start_date_str = start_date.strftime("%Y-%m-%d")
    
    # For each symbol, download data
    for symbol in SYMBOLS:
        print(f"Downloading data for {symbol}...")
        
        # Build command
        cmd = [
            DATA_FETCH_EXECUTABLE,
            "-n", str(DAYS),
            "-l", "10000",
            "-f", "sip",
            "-s", symbol
        ]
        
        # Use subprocess to run the C++ program and capture output
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                check=True
            )
            
            # Save the raw JSON output for possible debugging
            with open(f"raw_{symbol}_data.json", "w") as f:
                f.write(result.stdout)
            
            print(f"Downloaded data for {symbol}")
            
        except subprocess.CalledProcessError as e:
            print(f"Error fetching data for {symbol}: {e}")
            print(f"stderr: {e.stderr}")
    
    print("Data download complete")

def verify_database():
    """Verify database contents and quality"""
    print("Verifying database...")
    
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    # Get count of symbols
    cursor.execute("SELECT symbol, COUNT(*) as trade_count FROM trades GROUP BY symbol")
    symbol_counts = cursor.fetchall()
    
    print("Symbol trade counts:")
    for symbol, count in symbol_counts:
        print(f"{symbol}: {count} trades")
    
    # Check for date range
    cursor.execute("""
    SELECT symbol, MIN(date) as first_date, MAX(date) as last_date 
    FROM trades 
    GROUP BY symbol
    """)
    date_ranges = cursor.fetchall()
    
    print("\nDate ranges:")
    for symbol, first_date, last_date in date_ranges:
        print(f"{symbol}: {first_date} to {last_date}")
    
    # Check for any data quality issues (e.g., negative prices)
    cursor.execute("SELECT COUNT(*) FROM trades WHERE price <= 0")
    invalid_prices = cursor.fetchone()[0]
    
    if invalid_prices > 0:
        print(f"\nWARNING: Found {invalid_prices} trades with invalid prices")
    
    # Check for gaps in trading hours
    for symbol in SYMBOLS:
        cursor.execute("""
        SELECT date, 
               MIN(SUBSTR(timestamp, 12, 8)) as first_trade_time,
               MAX(SUBSTR(timestamp, 12, 8)) as last_trade_time,
               COUNT(*) as trade_count
        FROM trades 
        WHERE symbol = ?
        GROUP BY date
        """, (symbol,))
        
        trading_hours = cursor.fetchall()
        print(f"\nTrading hours for {symbol}:")
        for date, first_time, last_time, count in trading_hours:
            print(f"{date}: {first_time} to {last_time} ({count} trades)")
    
    conn.close()
    print("Database verification complete")

def prepare_train_test_split(symbol, test_ratio=0.2):
    """Prepare train/test split for a symbol"""
    print(f"Preparing train/test split for {symbol}...")
    
    # Initialize data handler
    data_handler = TradeDataHandler(DB_PATH)
    
    # Get available dates for this symbol
    dates = data_handler.get_available_dates(symbol)
    
    if not dates:
        print(f"No data available for {symbol}")
        return None, None, None
    
    # Calculate split
    split_idx = int(len(dates) * (1 - test_ratio))
    train_dates = dates[:split_idx]
    test_dates = dates[split_idx:]
    
    print(f"Train period: {train_dates[0]} to {train_dates[-1]} ({len(train_dates)} days)")
    print(f"Test period: {test_dates[0]} to {test_dates[-1]} ({len(test_dates)} days)")
    
    return data_handler, train_dates, test_dates

def train_rl_model(symbol, train_dates, data_handler, total_timesteps=50000):
    """Train an RL model on the historical data"""
    print(f"Training RL model for {symbol}...")
    
    if not train_dates or len(train_dates) < 2:
        print(f"Insufficient training data for {symbol}")
        return None
    
    # Create the trading environment
    env = StockTradingEnv(
        data_handler=data_handler,
        symbol=symbol,
        start_date=train_dates[0],
        end_date=train_dates[-1],
        initial_balance=10000.0,
        commission_rate=0.001,
        max_position=100,
        window_size=30
    )
    
    # Create a vectorized environment (for RL algorithms that support it)
    vec_env = make_vec_env(lambda: env, n_envs=1)
    
    # Initialize PPO agent
    model = PPO(
        policy="MlpPolicy",
        env=vec_env,
        learning_rate=0.0003,
        n_steps=2048,
        batch_size=64,
        n_epochs=10,
        gamma=0.99,
        gae_lambda=0.95,
        clip_range=0.2,
        verbose=1
    )
    
    # Train the agent
    model.learn(total_timesteps=total_timesteps)
    
    # Save the trained model
    model_path = os.path.join(MODELS_DIR, f"{symbol}_ppo_model")
    model.save(model_path)
    
    print(f"Model for {symbol} saved to {model_path}")
    
    return model

def evaluate_model(symbol, model, test_dates, data_handler):
    """Evaluate the trained model on test data"""
    print(f"Evaluating model for {symbol}...")
    
    if not test_dates or len(test_dates) < 2:
        print(f"Insufficient test data for {symbol}")
        return
    
    # Create test environment
    test_env = StockTradingEnv(
        data_handler=data_handler,
        symbol=symbol,
        start_date=test_dates[0],
        end_date=test_dates[-1],
        initial_balance=10000.0,
        commission_rate=0.001,
        max_position=100,
        window_size=30
    )
    
    # Run evaluation
    mean_reward, std_reward = evaluate_policy(
        model, 
        test_env, 
        n_eval_episodes=10,
        deterministic=True
    )
    
    print(f"Mean reward: {mean_reward:.2f} +/- {std_reward:.2f}")
    
    # Run a single episode with the trained model for visualization
    obs = test_env.reset()
    done = False
    total_reward = 0
    
    while not done:
        action, _states = model.predict(obs, deterministic=True)
        obs, reward, done, info = test_env.step(action)
        total_reward += reward
    
    # Plot results
    test_env.plot_performance()
    
    # Calculate final portfolio value
    final_value = info['portfolio_value']
    initial_value = 10000.0
    profit_percentage = (final_value - initial_value) / initial_value * 100
    
    print(f"Final portfolio value: ${final_value:.2f}")
    print(f"Profit/loss: {profit_percentage:.2f}%")
    
    # Calculate some trading metrics
    if test_env.trades:
        num_trades = len(test_env.trades)
        buy_trades = len([t for t in test_env.trades if t['type'] == 'buy'])
        sell_trades = len([t for t in test_env.trades if t['type'] == 'sell'])
        
        print(f"Total trades: {num_trades}")
        print(f"Buy trades: {buy_trades}")
        print(f"Sell trades: {sell_trades}")
        
        # Calculate trade-level profit/loss
        trade_pnl = []
        position = 0
        cost_basis = 0
        
        for trade in test_env.trades:
            if trade['type'] == 'buy':
                new_position = position + trade['shares']
                new_cost = cost_basis + trade['cost']
                position = new_position
                cost_basis = new_cost
            else:  # sell
                if position > 0:
                    avg_price = cost_basis / position
                    pnl = (trade['price'] - avg_price) * trade['shares']
                    trade_pnl.append(pnl)
                
                position -= trade['shares']
                if position > 0:
                    cost_basis = cost_basis * (position / (position + trade['shares']))
                else:
                    cost_basis = 0
        
        if trade_pnl:
            win_trades = len([p for p in trade_pnl if p > 0])
            loss_trades = len([p for p in trade_pnl if p <= 0])
            win_rate = win_trades / len(trade_pnl) if trade_pnl else 0
            
            print(f"Win rate: {win_rate:.2%}")
            print(f"Winning trades: {win_trades}")
            print(f"Losing trades: {loss_trades}")
            
            if win_trades > 0 and loss_trades > 0:
                avg_win = sum([p for p in trade_pnl if p > 0]) / win_trades if win_trades else 0
                avg_loss = sum([p for p in trade_pnl if p <= 0]) / loss_trades if loss_trades else 0
                profit_factor = abs(avg_win / avg_loss) if avg_loss != 0 else float('inf')
                
                print(f"Average win: ${avg_win:.2f}")
                print(f"Average loss: ${avg_loss:.2f}")
                print(f"Profit factor: {profit_factor:.2f}")

def backtest_simple_strategies(symbol, test_dates, data_handler):
    """Backtest simple trading strategies for comparison"""
    print(f"Backtesting simple strategies for {symbol}...")
    
    if not test_dates or len(test_dates) < 2:
        print(f"Insufficient test data for {symbol}")
        return
    
    # Load raw trades
    trades_df = data_handler.load_trades(symbol, test_dates[0], test_dates[-1])
    
    if trades_df.empty:
        print(f"No trades found for {symbol} in test period")
        return
    
    # Create 1-minute OHLCV data
    ohlcv = data_handler.create_ohlcv(trades_df, freq='1min')
    
    # Calculate some simple technical indicators
    # 1. Moving Averages
    ohlcv['sma5'] = ohlcv['close'].rolling(window=5).mean()
    ohlcv['sma15'] = ohlcv['close'].rolling(window=15).mean()
    
    # 2. Relative Strength Index (RSI)
    delta = ohlcv['close'].diff()
    gain = delta.where(delta > 0, 0).fillna(0)
    loss = -delta.where(delta < 0, 0).fillna(0)
    
    avg_gain = gain.rolling(window=14).mean()
    avg_loss = loss.rolling(window=14).mean()
    
    rs = avg_gain / avg_loss
    ohlcv['rsi'] = 100 - (100 / (1 + rs))
    
    # Drop rows with NaN values
    ohlcv = ohlcv.dropna()
    
    # Strategy 1: Moving Average Crossover
    ohlcv['position_ma'] = 0
    ohlcv.loc[ohlcv['sma5'] > ohlcv['sma15'], 'position_ma'] = 1
    ohlcv.loc[ohlcv['sma5'] < ohlcv['sma15'], 'position_ma'] = -1
    
    # Strategy 2: RSI Overbought/Oversold
    ohlcv['position_rsi'] = 0
    ohlcv.loc[ohlcv['rsi'] < 30, 'position_rsi'] = 1
    ohlcv.loc[ohlcv['rsi'] > 70, 'position_rsi'] = -1
    
    # Calculate returns
    ohlcv['pct_change'] = ohlcv['close'].pct_change()
    
    # Calculate strategy returns
    ohlcv['ma_strategy_return'] = ohlcv['position_ma'].shift(1) * ohlcv['pct_change']
    ohlcv['rsi_strategy_return'] = ohlcv['position_rsi'].shift(1) * ohlcv['pct_change']
    
    # Calculate cumulative returns
    ohlcv['cumulative_return'] = (1 + ohlcv['pct_change']).cumprod() - 1
    ohlcv['ma_cumulative_return'] = (1 + ohlcv['ma_strategy_return']).cumprod() - 1
    ohlcv['rsi_cumulative_return'] = (1 + ohlcv['rsi_strategy_return']).cumprod() - 1
    
    # Plot results
    plt.figure(figsize=(12, 8))
    
    plt.subplot(2, 1, 1)
    plt.plot(ohlcv.index, ohlcv['close'], label='Close Price')
    plt.plot(ohlcv.index, ohlcv['sma5'], label='5-period SMA', alpha=0.7)
    plt.plot(ohlcv.index, ohlcv['sma15'], label='15-period SMA', alpha=0.7)
    plt.title(f'{symbol} Price and Moving Averages')
    plt.ylabel('Price')
    plt.legend()
    plt.grid(True)
    
    plt.subplot(2, 1, 2)
    plt.plot(ohlcv.index, ohlcv['cumulative_return'] * 100, label='Buy & Hold')
    plt.plot(ohlcv.index, ohlcv['ma_cumulative_return'] * 100, label='MA Crossover')
    plt.plot(ohlcv.index, ohlcv['rsi_cumulative_return'] * 100, label='RSI Strategy')
    plt.title('Strategy Comparison')
    plt.ylabel('Return (%)')
    plt.legend()
    plt.grid(True)
    
    plt.tight_layout()
    plt.savefig(os.path.join(LOGS_DIR, f"{symbol}_strategy_comparison.png"))
    plt.show()
    
    # Print strategy statistics
    final_bhreturn = ohlcv['cumulative_return'].iloc[-1] * 100
    final_mareturn = ohlcv['ma_cumulative_return'].iloc[-1] * 100
    final_rsireturn = ohlcv['rsi_cumulative_return'].iloc[-1] * 100
    
    print(f"Buy & Hold return: {final_bhreturn:.2f}%")
    print(f"MA Crossover return: {final_mareturn:.2f}%")
    print(f"RSI Strategy return: {final_rsireturn:.2f}%")
    
    # Calculate Sharpe ratio (assuming risk-free rate of 0%)
    sharpe_bh = ohlcv['pct_change'].mean() / ohlcv['pct_change'].std() * np.sqrt(252 * 390)  # Annualized
    sharpe_ma = ohlcv['ma_strategy_return'].mean() / ohlcv['ma_strategy_return'].std() * np.sqrt(252 * 390)
    sharpe_rsi = ohlcv['rsi_strategy_return'].mean() / ohlcv['rsi_strategy_return'].std() * np.sqrt(252 * 390)
    
    print(f"Buy & Hold Sharpe Ratio: {sharpe_bh:.2f}")
    print(f"MA Crossover Sharpe Ratio: {sharpe_ma:.2f}")
    print(f"RSI Strategy Sharpe Ratio: {sharpe_rsi:.2f}")

def main():
    """Main execution function"""
    print("Starting stock trading RL pipeline...")
    
    # 1. Fetch historical data from Alpaca
    fetch_historical_data()
    
    # 2. Verify database contents
    verify_database()
    
    # Process each symbol
    for symbol in SYMBOLS:
        print(f"\nProcessing {symbol}...")
        
        # 3. Prepare train/test split
        data_handler, train_dates, test_dates = prepare_train_test_split(symbol)
        
        if not data_handler or not train_dates or not test_dates:
            print(f"Skipping {symbol} due to insufficient data")
            continue
        
        # 4. Train RL model
        model = train_rl_model(symbol, train_dates, data_handler)
        
        if model:
            # 5. Evaluate model
            evaluate_model(symbol, model, test_dates, data_handler)
            
            # 6. Compare with simple strategies
            backtest_simple_strategies(symbol, test_dates, data_handler)
        
        # Close data handler
        data_handler.close()
    
    print("\nPipeline execution complete")

if __name__ == "__main__":
    main()
