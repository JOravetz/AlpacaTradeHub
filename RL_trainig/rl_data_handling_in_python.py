import sqlite3
import pandas as pd
import numpy as np
from typing import List, Tuple, Dict, Optional
import gym
from gym import spaces
import matplotlib.pyplot as plt

class TradeDataHandler:
    """Helper class to efficiently load and preprocess trade data for RL"""
    
    def __init__(self, db_path: str):
        """Initialize with path to SQLite database"""
        self.db_path = db_path
        self.conn = None
        self._connect()
    
    def _connect(self):
        """Connect to the SQLite database"""
        self.conn = sqlite3.connect(self.db_path)
        # Enable WAL mode for better concurrent access
        self.conn.execute('PRAGMA journal_mode = WAL')
    
    def get_available_symbols(self) -> List[str]:
        """Get list of available symbols in the database"""
        query = "SELECT DISTINCT symbol FROM trades ORDER BY symbol"
        return pd.read_sql_query(query, self.conn)['symbol'].tolist()
    
    def get_available_dates(self, symbol: str) -> List[str]:
        """Get list of available dates for a symbol"""
        query = "SELECT DISTINCT date FROM trades WHERE symbol = ? ORDER BY date"
        return pd.read_sql_query(query, self.conn, params=(symbol,))['date'].tolist()
    
    def load_trades(self, symbol: str, start_date: str, end_date: str) -> pd.DataFrame:
        """Load trades for a specific symbol and date range"""
        query = """
        SELECT * FROM trades 
        WHERE symbol = ? AND date BETWEEN ? AND ?
        ORDER BY timestamp
        """
        df = pd.read_sql_query(query, self.conn, params=(symbol, start_date, end_date))
        
        # Convert timestamp to datetime and set as index
        df['timestamp'] = pd.to_datetime(df['timestamp'])
        
        # Handle conditions column (convert from JSON to list)
        if 'conditions' in df.columns:
            import json
            df['conditions'] = df['conditions'].apply(
                lambda x: json.loads(x) if isinstance(x, str) else []
            )
        
        return df
    
    def create_ohlcv(self, trades_df: pd.DataFrame, freq: str = '1min') -> pd.DataFrame:
        """Convert trades to OHLCV candles with specified frequency"""
        # Ensure trades are sorted by timestamp
        trades_df = trades_df.sort_values('timestamp')
        
        # Group by time frequency
        grouped = trades_df.set_index('timestamp')
        
        # Create OHLCV data
        ohlc = grouped['price'].resample(freq).ohlc()
        volume = grouped['size'].resample(freq).sum()
        
        # Combine into single dataframe
        ohlcv = pd.concat([ohlc, volume], axis=1)
        ohlcv.rename(columns={'size': 'volume'}, inplace=True)
        
        return ohlcv
    
    def get_vwap(self, trades_df: pd.DataFrame, window: str = '1min') -> pd.Series:
        """Calculate Volume Weighted Average Price for the given trades"""
        # Calculate price * size for each trade
        trades_df['price_volume'] = trades_df['price'] * trades_df['size']
        
        # Group by time window
        df_window = trades_df.set_index('timestamp')
        
        # Calculate the sum of price*volume and the sum of volume in each window
        price_volume_sum = df_window['price_volume'].resample(window).sum()
        volume_sum = df_window['size'].resample(window).sum()
        
        # Calculate VWAP
        vwap = price_volume_sum / volume_sum
        
        return vwap
    
    def preprocess_for_rl(self, symbol: str, start_date: str, end_date: str, 
                          freq: str = '1min', features: List[str] = None) -> np.ndarray:
        """
        Preprocess trade data for reinforcement learning:
        1. Load raw trades
        2. Create OHLCV candles
        3. Calculate additional features (VWAP, etc.)
        4. Normalize data
        5. Return as numpy array suitable for RL
        """
        # Default features if none provided
        if features is None:
            features = ['open', 'high', 'low', 'close', 'volume', 'vwap']
        
        # Load raw trades
        trades_df = self.load_trades(symbol, start_date, end_date)
        
        if trades_df.empty:
            raise ValueError(f"No trades found for {symbol} between {start_date} and {end_date}")
        
        # Create OHLCV data
        ohlcv_df = self.create_ohlcv(trades_df, freq)
        
        # Calculate VWAP
        vwap = self.get_vwap(trades_df, freq)
        
        # Combine all features
        data_df = ohlcv_df.copy()
        data_df['vwap'] = vwap
        
        # Handle missing values
        data_df = data_df.fillna(method='ffill')
        data_df = data_df.dropna()
        
        # Select requested features
        data_df = data_df[[f for f in features if f in data_df.columns]]
        
        # Normalize data - use percentage changes for price features,
        # and log transformation for volume
        price_cols = ['open', 'high', 'low', 'close', 'vwap']
        price_cols = [col for col in price_cols if col in data_df.columns]
        
        # Calculate returns instead of using raw prices
        for col in price_cols:
            data_df[f'{col}_ret'] = data_df[col].pct_change()
        
        if 'volume' in data_df.columns:
            data_df['volume_log'] = np.log1p(data_df['volume'])
        
        # Drop the first row which has NaN after pct_change
        data_df = data_df.iloc[1:]
        
        # Keep only the transformed columns
        ret_cols = [f'{col}_ret' for col in price_cols]
        log_cols = ['volume_log'] if 'volume' in data_df.columns else []
        data_df = data_df[ret_cols + log_cols]
        
        # Z-score normalization for stability
        data_np = (data_df - data_df.mean()) / data_df.std()
        
        # Return as numpy array
        return data_np.values
    
    def close(self):
        """Close the database connection"""
        if self.conn:
            self.conn.close()
            self.conn = None
    
    def __del__(self):
        """Ensure connection is closed when object is deleted"""
        self.close()


class StockTradingEnv(gym.Env):
    """
    A simple stock trading environment for RL
    Uses detailed trade data to simulate realistic trading
    """
    
    metadata = {'render.modes': ['human']}
    
    def __init__(self, data_handler: TradeDataHandler, symbol: str, 
                 start_date: str, end_date: str, initial_balance: float = 10000.0,
                 commission_rate: float = 0.001, max_position: int = 100,
                 window_size=30
            )
            
            # Simple random agent example
            total_reward = 0
            done = False
            obs = env.reset()
            
            while not done:
                # Random action
                action = env.action_space.sample()
                obs, reward, done, info = env.step(action)
                total_reward += reward
                
                if done:
                    print(f"Episode finished with total reward: {total_reward}")
                    env.plot_performance()
            
            # Close connections
            data_handler.close(): int = 30):
        """
        Initialize the environment
        
        Args:
            data_handler: TradeDataHandler instance
            symbol: Stock symbol to trade
            start_date: Start date for historical data
            end_date: End date for historical data
            initial_balance: Starting cash balance
            commission_rate: Trading commission as a fraction of trade value
            max_position: Maximum number of shares to hold (long or short)
            window_size: Number of past observations to include in state
        """
        super(StockTradingEnv, self).__init__()
        
        self.data_handler = data_handler
        self.symbol = symbol
        self.initial_balance = initial_balance
        self.commission_rate = commission_rate
        self.max_position = max_position
        self.window_size = window_size
        
        # Load and preprocess data
        self.data = data_handler.preprocess_for_rl(symbol, start_date, end_date)
        self.n_features = self.data.shape[1]
        
        # Define action and observation space
        # Actions: buy/sell/hold
        self.action_space = spaces.Discrete(3)
        
        # Observation space: historical window of features + current position + balance
        self.observation_space = spaces.Box(
            low=-np.inf, 
            high=np.inf, 
            shape=(self.window_size * self.n_features + 2,),
            dtype=np.float32
        )
        
        # Initialize state
        self.reset()
    
    def _get_observation(self):
        """
        Get the current observation (state) for the RL agent
        Includes historical window of features plus current position and balance
        """
        # Get historical window of features
        end_idx = self.current_step
        start_idx = end_idx - self.window_size + 1
        
        if start_idx < 0:
            # Pad with zeros for the first few steps
            padding = -start_idx
            window_data = np.vstack([
                np.zeros((padding, self.n_features)),
                self.data[0:end_idx+1]
            ])
        else:
            window_data = self.data[start_idx:end_idx+1]
        
        # Flatten window data
        obs_market = window_data.flatten()
        
        # Add position and balance information
        obs_position = np.array([self.current_position / self.max_position])
        obs_balance = np.array([self.balance / self.initial_balance])
        
        # Combine all parts of the observation
        observation = np.concatenate([obs_market, obs_position, obs_balance])
        
        return observation
    
    def _calculate_reward(self, action):
        """
        Calculate reward for the current step
        
        Reward is based on change in portfolio value (balance + position value)
        with penalties for excessive trading
        """
        # Current portfolio value
        current_value = self.balance + self.current_position * self.current_price
        
        # Calculate change in portfolio value
        reward = (current_value - self.prev_portfolio_value) / self.initial_balance
        
        # Add penalty for trading (to discourage excessive trading)
        if action != 1:  # If not holding
            reward -= self.commission_rate * 0.1
        
        # Update previous portfolio value
        self.prev_portfolio_value = current_value
        
        return reward
    
    def reset(self):
        """Reset the environment to initial state"""
        self.balance = self.initial_balance
        self.current_step = self.window_size - 1  # Start with enough history for a full window
        self.current_position = 0
        self.current_price = 1.0  # Will be updated in first step
        self.prev_portfolio_value = self.initial_balance
        self.done = False
        self.trades = []
        
        return self._get_observation()
    
    def step(self, action):
        """
        Take a step in the environment based on the action
        
        Args:
            action: 0 (buy), 1 (hold), 2 (sell)
            
        Returns:
            observation, reward, done, info
        """
        # Ensure current step is valid
        if self.current_step >= len(self.data) - 1:
            self.done = True
            return self._get_observation(), 0, self.done, {}
        
        # Move to next time step
        self.current_step += 1
        
        # Simulate price for current step (in a real environment, this would be actual market data)
        # Here we use the close price from our feature data
        self.current_price = 1.0  # Base price (since we're using returns)
        
        # Execute trading action
        if action == 0:  # Buy
            max_shares_to_buy = min(
                int(self.balance / (self.current_price * (1 + self.commission_rate))),
                self.max_position - self.current_position
            )
            if max_shares_to_buy > 0:
                # Buy shares
                shares_bought = max_shares_to_buy
                cost = shares_bought * self.current_price
                commission = cost * self.commission_rate
                self.balance -= (cost + commission)
                self.current_position += shares_bought
                self.trades.append({
                    'step': self.current_step,
                    'type': 'buy',
                    'shares': shares_bought,
                    'price': self.current_price,
                    'cost': cost,
                    'commission': commission
                })
                
        elif action == 2:  # Sell
            shares_to_sell = min(self.current_position, self.max_position)
            if shares_to_sell > 0:
                # Sell shares
                proceeds = shares_to_sell * self.current_price
                commission = proceeds * self.commission_rate
                self.balance += (proceeds - commission)
                self.current_position -= shares_to_sell
                self.trades.append({
                    'step': self.current_step,
                    'type': 'sell',
                    'shares': shares_to_sell,
                    'price': self.current_price,
                    'proceeds': proceeds,
                    'commission': commission
                })
        
        # Calculate reward
        reward = self._calculate_reward(action)
        
        # Check if episode is done
        if self.current_step >= len(self.data) - 1:
            self.done = True
            
            # Liquidate final position when done
            if self.current_position > 0:
                proceeds = self.current_position * self.current_price
                commission = proceeds * self.commission_rate
                self.balance += (proceeds - commission)
                self.trades.append({
                    'step': self.current_step,
                    'type': 'sell',
                    'shares': self.current_position,
                    'price': self.current_price,
                    'proceeds': proceeds,
                    'commission': commission
                })
                self.current_position = 0
        
        # Get observation
        observation = self._get_observation()
        
        # Additional info
        info = {
            'step': self.current_step,
            'balance': self.balance,
            'position': self.current_position,
            'portfolio_value': self.balance + self.current_position * self.current_price
        }
        
        return observation, reward, self.done, info
    
    def render(self, mode='human'):
        """Render the environment"""
        if mode == 'human':
            portfolio_value = self.balance + self.current_position * self.current_price
            print(f"Step: {self.current_step}")
            print(f"Price: {self.current_price:.2f}")
            print(f"Balance: {self.balance:.2f}")
            print(f"Position: {self.current_position}")
            print(f"Portfolio Value: {portfolio_value:.2f}")
            print(f"Trades Made: {len(self.trades)}")
            print("--------------------")
    
    def close(self):
        """Clean up resources"""
        pass
    
    def plot_performance(self):
        """Plot performance of the trading strategy"""
        if not self.trades:
            print("No trades to plot")
            return
        
        # Create a dataframe with portfolio value at each step
        portfolio_values = []
        for step in range(self.window_size, len(self.data)):
            # Find all trades up to this step
            position = 0
            balance = self.initial_balance
            
            for trade in self.trades:
                if trade['step'] <= step:
                    if trade['type'] == 'buy':
                        position += trade['shares']
                        balance -= (trade['cost'] + trade['commission'])
                    else:  # sell
                        position -= trade['shares']
                        balance += (trade['proceeds'] - trade['commission'])
            
            # Calculate portfolio value
            price = 1.0  # Placeholder since we're using returns
            portfolio_value = balance + position * price
            
            portfolio_values.append({
                'step': step,
                'balance': balance,
                'position': position,
                'portfolio_value': portfolio_value
            })
        
        # Convert to dataframe
        df = pd.DataFrame(portfolio_values)
        
        # Plot
        plt.figure(figsize=(12, 6))
        plt.plot(df['step'], df['portfolio_value'], label='Portfolio Value')
        
        # Add buy/sell markers
        for trade in self.trades:
            if trade['type'] == 'buy':
                plt.scatter(trade['step'], trade['price'], color='green', marker='^', s=100)
            else:  # sell
                plt.scatter(trade['step'], trade['price'], color='red', marker='v', s=100)
        
        plt.title(f'Trading Performance - {self.symbol}')
        plt.xlabel('Step')
        plt.ylabel('Value ($)')
        plt.legend()
        plt.grid(True)
        plt.show()


# Example usage:
if __name__ == "__main__":
    # Initialize data handler
    data_handler = TradeDataHandler("alpaca_trades.db")
    
    # Get available symbols
    symbols = data_handler.get_available_symbols()
    print(f"Available symbols: {symbols}")
    
    if symbols:
        # Choose a symbol
        symbol = symbols[0]
        
        # Get available dates
        dates = data_handler.get_available_dates(symbol)
        print(f"Available dates for {symbol}: {dates}")
        
        if len(dates) >= 2:
            # Create environment
            env = StockTradingEnv(
                data_handler=data_handler,
                symbol=symbol,
                start_date=dates[0],
                end_date=dates[-1],
                initial_balance=10000.0,
                commission_rate=0.001,
                max_position=100,
                window_size
