#include <iostream>
#include <string>
#include <sqlite3.h>
#include <vector>
#include <chrono>
#include <ctime>
#include <sstream>
#include <regex>
#include <map>
#include <iomanip>
#include <fstream>

// Forward declarations
std::string extract_json_string(const std::string& json, const std::string& key);

// SQLite callback for executing queries
static int callback(void* data, int argc, char** argv, char** azColName) {
    // Suppress unused parameter warnings
    (void)data;
    
    for (int i = 0; i < argc; i++) {
        std::cout << azColName[i] << " = " << (argv[i] ? argv[i] : "NULL") << std::endl;
    }
    std::cout << std::endl;
    
    return 0;
}

// Simple JSON string extraction helper
std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string search_key = "\"" + key + "\":";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) {
        return "";
    }
    
    pos += search_key.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\t' || json[pos] == '\r')) {
        pos++;
    }
    
    if (pos >= json.length()) {
        return "";
    }
    
    if (json[pos] == 'n' && pos + 3 < json.length() && json.substr(pos, 4) == "null") {
        return "";  // null value
    }
    
    if (json[pos] != '"') {
        return "";  // Not a string value
    }
    
    pos++;  // Skip opening quote
    std::string result;
    bool escaped = false;
    
    while (pos < json.length()) {
        char c = json[pos++];
        if (escaped) {
            result += c;
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            break;  // End of string
        } else {
            result += c;
        }
    }
    
    return result;
}

// Convert ISO timestamp to epoch nanoseconds
int64_t iso_timestamp_to_epoch_ns(const std::string& iso_timestamp) {
    // Parse ISO 8601 format: YYYY-MM-DDTHH:MM:SS.mmmmmmmmZ
    std::tm tm = {};
    int year, month, day, hour, minute, second;
    int64_t nanosecond = 0;
    
    // Extract the main components
    if (sscanf(iso_timestamp.c_str(), "%d-%d-%dT%d:%d:%d", 
               &year, &month, &day, &hour, &minute, &second) != 6) {
        std::cerr << "Error parsing timestamp: " << iso_timestamp << std::endl;
        return 0;
    }
    
    // Extract fractional seconds (nanoseconds)
    size_t dot_pos = iso_timestamp.find('.');
    if (dot_pos != std::string::npos) {
        size_t z_pos = iso_timestamp.find('Z', dot_pos);
        if (z_pos != std::string::npos) {
            std::string ns_str = iso_timestamp.substr(dot_pos + 1, z_pos - dot_pos - 1);
            // Pad with zeros to nanosecond precision
            ns_str.append(9 - ns_str.length(), '0');
            nanosecond = std::stoll(ns_str);
        }
    }
    
    // Set tm struct
    tm.tm_year = year - 1900;  // years since 1900
    tm.tm_mon = month - 1;     // months since January [0-11]
    tm.tm_mday = day;          // day of the month [1-31]
    tm.tm_hour = hour;         // hours since midnight [0-23]
    tm.tm_min = minute;        // minutes after the hour [0-59]
    tm.tm_sec = second;        // seconds after the minute [0-60]
    tm.tm_isdst = -1;          // let system determine DST
    
    // Convert to time_t (seconds since epoch)
    std::time_t time_sec = timegm(&tm);  // Use timegm for UTC
    
    // Calculate final epoch in nanoseconds
    return (static_cast<int64_t>(time_sec) * 1000000000) + nanosecond;
}

// Join a vector of strings with a separator
std::string join(const std::vector<std::string>& elements, const std::string& separator) {
    std::string result;
    for (size_t i = 0; i < elements.size(); ++i) {
        if (i > 0) {
            result += separator;
        }
        result += elements[i];
    }
    return result;
}

// Extract conditions array from a trade object
std::string extract_conditions(const std::string& trade_json) {
    // Match the conditions array: "c":["condition1","condition2"]
    std::regex condition_regex("\"c\"\\s*:\\s*\\[(.*?)\\]");
    std::smatch match;
    
    if (std::regex_search(trade_json, match, condition_regex) && match.size() > 1) {
        std::string conditions_str = match.str(1);
        
        // Extract individual conditions
        std::vector<std::string> conditions;
        std::regex condition_item_regex("\"([^\"]*)\"");
        std::string::const_iterator search_start(conditions_str.cbegin());
        
        while (std::regex_search(search_start, conditions_str.cend(), match, condition_item_regex)) {
            conditions.push_back(match.str(1));
            search_start = match.suffix().first;
        }
        
        return join(conditions, ",");
    }
    
    return "";
}

// Initialize the SQLite database
bool init_database(sqlite3*& db, const std::string& db_path) {
    // Open database connection
    int rc = sqlite3_open(db_path.c_str(), &db);
    
    if (rc != SQLITE_OK) {
        std::cerr << "Error opening SQLite database: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return false;
    }

    // Create tables if they don't exist
    const char* create_trades_table_sql = 
        "CREATE TABLE IF NOT EXISTS trades ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "symbol TEXT NOT NULL,"
        "trade_id INTEGER NOT NULL,"
        "price REAL NOT NULL,"
        "size INTEGER NOT NULL,"
        "timestamp TEXT NOT NULL,"
        "exchange TEXT NOT NULL,"
        "tape TEXT NOT NULL,"
        "conditions TEXT,"
        "timestamp_epoch INTEGER NOT NULL,"
        "UNIQUE(symbol, trade_id, timestamp_epoch)"
        ");";
    
    char* err_msg = nullptr;
    rc = sqlite3_exec(db, create_trades_table_sql, callback, nullptr, &err_msg);
    
    if (rc != SQLITE_OK) {
        std::cerr << "Error creating trades table: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return false;
    }
    
    // Create index on symbol and timestamp for faster queries
    const char* create_index_sql = 
        "CREATE INDEX IF NOT EXISTS idx_symbol_timestamp ON trades (symbol, timestamp_epoch);";
    
    rc = sqlite3_exec(db, create_index_sql, callback, nullptr, &err_msg);
    
    if (rc != SQLITE_OK) {
        std::cerr << "Error creating index: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return false;
    }
    
    // Create page tokens table
    const char* create_tokens_table_sql = 
        "CREATE TABLE IF NOT EXISTS page_tokens ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "token TEXT UNIQUE,"
        "processed_at TEXT NOT NULL"
        ");";
    
    rc = sqlite3_exec(db, create_tokens_table_sql, callback, nullptr, &err_msg);
    
    if (rc != SQLITE_OK) {
        std::cerr << "Error creating page_tokens table: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return false;
    }
    
    return true;
}

// Store the next_page_token
bool store_page_token(sqlite3* db, const std::string& token) {
    if (token.empty()) {
        return true;  // No token to store
    }
    
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&now_time), "%Y-%m-%dT%H:%M:%SZ");
    std::string timestamp = ss.str();
    
    // Prepare the SQL statement
    const char* sql = "INSERT OR IGNORE INTO page_tokens (token, processed_at) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Error preparing token insert statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    // Bind parameters
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, timestamp.c_str(), -1, SQLITE_STATIC);
    
    // Execute the statement
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Error inserting token: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }
    
    sqlite3_finalize(stmt);
    return true;
}

// Extract a string field from JSON
std::string extract_json_field(const std::string& json, const std::string& field) {
    // Using regex to extract field - this is a simple approach that works for our case
    std::regex field_regex("\"" + field + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    
    if (std::regex_search(json, match, field_regex) && match.size() > 1) {
        return match.str(1);
    }
    
    return "";
}

// Extract a numeric field from JSON
template<typename T>
T extract_json_numeric(const std::string& json, const std::string& field, T default_value = T()) {
    std::regex field_regex("\"" + field + "\"\\s*:\\s*([0-9.]+)");
    std::smatch match;
    
    if (std::regex_search(json, match, field_regex) && match.size() > 1) {
        std::stringstream ss(match.str(1));
        T value;
        ss >> value;
        return value;
    }
    
    return default_value;
}

// Function to process trades from the Alpaca API response with optimized batch loading
bool process_trades(sqlite3* db, const std::string& json_response) {
    // Configure SQLite for maximum performance during bulk loading
    char* err_msg = nullptr;
    sqlite3_exec(db, "PRAGMA synchronous = OFF", nullptr, nullptr, &err_msg);
    sqlite3_exec(db, "PRAGMA journal_mode = MEMORY", nullptr, nullptr, &err_msg);
    sqlite3_exec(db, "PRAGMA temp_store = MEMORY", nullptr, nullptr, &err_msg);
    sqlite3_exec(db, "PRAGMA cache_size = 100000", nullptr, nullptr, &err_msg);

    // Extract next_page_token
    std::string next_page_token = extract_json_string(json_response, "next_page_token");

    // Store the token
    if (!store_page_token(db, next_page_token)) {
        std::cerr << "Warning: Failed to store page token" << std::endl;
    }

    // Find the trades section
    std::string trades_search = "\"trades\":{";
    size_t trades_pos = json_response.find(trades_search);
    if (trades_pos == std::string::npos) {
        std::cerr << "Error: No trades found in the response" << std::endl;
        return false;
    }

    // Begin transaction for better performance
    int rc = sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "Error beginning transaction: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return false;
    }

    // Prepare the insert statement
    const char* insert_sql =
        "INSERT OR IGNORE INTO trades "
        "(symbol, trade_id, price, size, timestamp, exchange, tape, conditions, timestamp_epoch) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Error preparing insert statement: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    // Keep track of total trades processed
    int total_processed = 0;
    int batch_count = 0;
    const int BATCH_SIZE = 100000; // Commit every 100,000 trades

    // Process each symbol's trades
    size_t symbol_start = trades_pos + trades_search.length() - 1;  // Start after "trades":{

    while (symbol_start < json_response.length()) {
        // Find next symbol key
        size_t quote_pos = json_response.find("\"", symbol_start);
        if (quote_pos == std::string::npos) break;

        size_t end_quote = json_response.find("\"", quote_pos + 1);
        if (end_quote == std::string::npos) break;

        std::string symbol = json_response.substr(quote_pos + 1, end_quote - quote_pos - 1);

        // Find the array of trades for this symbol
        size_t array_start = json_response.find("[", end_quote);
        if (array_start == std::string::npos) break;

        // Find the end of the array
        size_t array_end = array_start + 1;
        int bracket_count = 1;
        while (array_end < json_response.length() && bracket_count > 0) {
            if (json_response[array_end] == '[') bracket_count++;
            else if (json_response[array_end] == ']') bracket_count--;
            array_end++;
        }

        if (bracket_count != 0) break;

        // Process this symbol's data
        std::cout << "Processing trades for " << symbol << "..." << std::endl;
        int symbol_trade_count = 0;

        // Extract and process each trade object
        std::string trades_array = json_response.substr(array_start + 1, array_end - array_start - 2);

        size_t trade_start = 0;

        while (trade_start < trades_array.length()) {
            // Find start of a trade object
            size_t obj_start = trades_array.find("{", trade_start);
            if (obj_start == std::string::npos) break;

            // Find end of the trade object with proper nesting
            size_t obj_end = obj_start + 1;
            int brace_count = 1;
            while (obj_end < trades_array.length() && brace_count > 0) {
                if (trades_array[obj_end] == '{') brace_count++;
                else if (trades_array[obj_end] == '}') brace_count--;
                obj_end++;
            }

            if (brace_count != 0) break;

            // Extract the trade object
            std::string trade_obj = trades_array.substr(obj_start, obj_end - obj_start);

            // Extract trade fields
            int trade_id = extract_json_numeric<int>(trade_obj, "i");
            double price = extract_json_numeric<double>(trade_obj, "p");
            int size = extract_json_numeric<int>(trade_obj, "s");
            std::string timestamp = extract_json_field(trade_obj, "t");
            std::string exchange = extract_json_field(trade_obj, "x");
            std::string tape = extract_json_field(trade_obj, "z");
            std::string conditions = extract_conditions(trade_obj);

            // Convert timestamp to epoch
            int64_t timestamp_epoch = iso_timestamp_to_epoch_ns(timestamp);

            // Validate required fields
            if (trade_id <= 0 || price <= 0 || size <= 0 || timestamp.empty() ||
                exchange.empty() || tape.empty() || timestamp_epoch <= 0) {

                // Move to next trade
                trade_start = obj_end;
                if (trade_start < trades_array.length() && trades_array[trade_start] == ',') {
                    trade_start++;
                }
                continue;
            }

            // Reset the statement
            sqlite3_reset(stmt);

            // Bind parameters
            sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 2, trade_id);
            sqlite3_bind_double(stmt, 3, price);
            sqlite3_bind_int(stmt, 4, size);
            sqlite3_bind_text(stmt, 5, timestamp.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 6, exchange.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 7, tape.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 8, conditions.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int64(stmt, 9, timestamp_epoch);

            // Execute the insert
            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                // Only log severe errors to reduce console output
                if (rc != SQLITE_CONSTRAINT) {
                    std::cerr << "Error inserting trade: " << sqlite3_errmsg(db) << std::endl;
                }
            } else {
                symbol_trade_count++;
                total_processed++;
                batch_count++;
            }

            // Commit in batches to avoid excessive memory usage
            if (batch_count >= BATCH_SIZE) {
                // Commit the current batch
                sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &err_msg);

                // Start a new transaction
                sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg);

                // Print progress
                std::cout << "Batch committed: " << batch_count << " trades. Total processed: " << total_processed << std::endl;
                batch_count = 0;
            }

            // Move to next trade
            trade_start = obj_end;
            if (trade_start < trades_array.length() && trades_array[trade_start] == ',') {
                trade_start++;
            }
        }

        std::cout << "Processed " << symbol_trade_count << " trades for " << symbol << std::endl;

        // Move to next symbol
        symbol_start = array_end;

        // Skip to next symbol or end of trades object
        if (symbol_start < json_response.length() && json_response[symbol_start] == ',') {
            symbol_start++;
        } else {
            break;
        }
    }

    // Finalize the statement
    sqlite3_finalize(stmt);

    // Commit any remaining trades
    rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "Error committing transaction: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    // Restore normal SQLite settings
    sqlite3_exec(db, "PRAGMA synchronous = NORMAL", nullptr, nullptr, &err_msg);
    sqlite3_exec(db, "PRAGMA journal_mode = DELETE", nullptr, nullptr, &err_msg);
    sqlite3_exec(db, "PRAGMA temp_store = DEFAULT", nullptr, nullptr, &err_msg);

    std::cout << "Successfully processed " << total_processed << " trades in total" << std::endl;
    return true;
}

// Function to query the database for statistics
void print_db_stats(sqlite3* db) {
    // Query total number of trades
    const char* count_sql = "SELECT COUNT(*) FROM trades;";
    char* err_msg = nullptr;
    
    std::cout << "Database Statistics:" << std::endl;
    std::cout << "-------------------" << std::endl;
    
    // Execute count query
    int rc = sqlite3_exec(db, count_sql, callback, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "Error querying trade count: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return;
    }
    
    // Query date range
    const char* dates_sql = 
        "SELECT MIN(timestamp) as earliest, MAX(timestamp) as latest FROM trades;";
    
    rc = sqlite3_exec(db, dates_sql, callback, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "Error querying date range: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return;
    }
    
    // Query symbols and counts
    const char* symbols_sql = 
        "SELECT symbol, COUNT(*) as trade_count FROM trades "
        "GROUP BY symbol ORDER BY trade_count DESC LIMIT 10;";
    
    std::cout << "Top Symbols by Trade Count:" << std::endl;
    rc = sqlite3_exec(db, symbols_sql, callback, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "Error querying symbols: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return;
    }
}

// Main function
int main(int argc, char* argv[]) {
    // Default database path
    std::string db_path = "stock_trades.db";
    std::string json_file;
    bool print_stats = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--db" && i + 1 < argc) {
            db_path = argv[++i];
        } else if (arg == "--file" && i + 1 < argc) {
            json_file = argv[++i];
        } else if (arg == "--stats") {
            print_stats = true;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --db PATH       Path to SQLite database (default: stock_trades.db)" << std::endl;
            std::cout << "  --file PATH     Path to JSON file containing trade data" << std::endl;
            std::cout << "  --stats         Print database statistics" << std::endl;
            std::cout << "  --help          Show this help message" << std::endl;
            std::cout << std::endl;
            std::cout << "Note: If no JSON file is provided, the program will read from standard input." << std::endl;
            return 0;
        }
    }
    
    // Open database connection
    sqlite3* db = nullptr;
    if (!init_database(db, db_path)) {
        std::cerr << "Failed to initialize database." << std::endl;
        return 1;
    }
    
    // Process trades or print stats
    if (print_stats) {
        print_db_stats(db);
    } else {
        // Read JSON data
        std::string json_data;
        
        if (!json_file.empty()) {
            // Read from file
            std::ifstream file(json_file);
            if (!file.is_open()) {
                std::cerr << "Error: Could not open file " << json_file << std::endl;
                sqlite3_close(db);
                return 1;
            }
            
            std::stringstream buffer;
            buffer << file.rdbuf();
            json_data = buffer.str();
        } else {
            // Read from standard input
            std::stringstream buffer;
            buffer << std::cin.rdbuf();
            json_data = buffer.str();
        }
        
        // Process the trades
        if (!process_trades(db, json_data)) {
            std::cerr << "Error processing trades." << std::endl;
            sqlite3_close(db);
            return 1;
        }
        
        std::cout << "Trades successfully stored in database: " << db_path << std::endl;
    }
    
    // Close database connection
    sqlite3_close(db);
    return 0;
}
