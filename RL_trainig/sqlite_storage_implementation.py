#include <sqlite3.h>
#include <iostream>
#include <string>

// Add this to your existing includes

// Function to initialize database
sqlite3* initialize_database(const std::string& db_path) {
    sqlite3 *db;
    char *err_msg = nullptr;
    
    // Open database
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return nullptr;
    }
    
    // Create trades table if it doesn't exist
    const char* create_table_sql = R"(
        CREATE TABLE IF NOT EXISTS trades (
            symbol TEXT NOT NULL,
            trade_id INTEGER NOT NULL,
            price REAL NOT NULL,
            size INTEGER NOT NULL,
            timestamp TEXT NOT NULL,
            exchange TEXT NOT NULL,
            tape TEXT NOT NULL,
            conditions TEXT,
            date TEXT GENERATED ALWAYS AS (date(timestamp)) STORED,
            PRIMARY KEY (symbol, timestamp, trade_id)
        );
        
        CREATE INDEX IF NOT EXISTS idx_trades_symbol_date ON trades(symbol, date);
        CREATE INDEX IF NOT EXISTS idx_trades_timestamp ON trades(timestamp);
    )";
    
    rc = sqlite3_exec(db, create_table_sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return nullptr;
    }
    
    return db;
}

// Function to extract and store trades in database
bool store_trades_in_db(sqlite3* db, const std::string& json_data) {
    if (!db) return false;
    
    // Begin transaction
    sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
    
    // Find the trades section
    std::string trades_search = "\"trades\":{";
    size_t trades_pos = json_data.find(trades_search);
    if (trades_pos == std::string::npos) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    
    // Prepare SQL statement
    const char* insert_sql = R"(
        INSERT OR REPLACE INTO trades 
        (symbol, trade_id, price, size, timestamp, exchange, tape, conditions) 
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    
    // Process each symbol
    size_t symbol_start = trades_pos + 9; // Skip "trades":{
    while (symbol_start < json_data.length()) {
        // Find next symbol
        size_t quote_pos = json_data.find("\"", symbol_start);
        if (quote_pos == std::string::npos) break;
        
        size_t end_quote = json_data.find("\"", quote_pos + 1);
        if (end_quote == std::string::npos) break;
        
        std::string symbol = json_data.substr(quote_pos + 1, end_quote - quote_pos - 1);
        
        // Find array start
        size_t array_start = json_data.find("[", end_quote);
        if (array_start == std::string::npos) break;
        
        // Find array end with proper bracket nesting
        size_t array_end = array_start + 1;
        int bracket_count = 1;
        while (array_end < json_data.length() && bracket_count > 0) {
            if (json_data[array_end] == '[') bracket_count++;
            else if (json_data[array_end] == ']') bracket_count--;
            array_end++;
        }
        
        if (bracket_count != 0) break;
        
        // Extract the array content
        std::string array_content = json_data.substr(array_start + 1, array_end - array_start - 2);
        
        // Parse individual trade objects and insert into database
        size_t pos = 0;
        while (pos < array_content.length()) {
            // Find opening brace
            size_t obj_start = array_content.find("{", pos);
            if (obj_start == std::string::npos) break;
            
            // Find closing brace with proper nesting
            size_t obj_end = obj_start + 1;
            int brace_count = 1;
            while (obj_end < array_content.length() && brace_count > 0) {
                if (array_content[obj_end] == '{') brace_count++;
                else if (array_content[obj_end] == '}') brace_count--;
                obj_end++;
            }
            
            if (brace_count != 0) break;
            
            // Extract the trade object
            std::string trade_obj = array_content.substr(obj_start, obj_end - obj_start);
            
            // Extract trade fields
            int trade_id = std::stoi(extract_json_value(trade_obj, "\"i\""));
            double price = std::stod(extract_json_value(trade_obj, "\"p\""));
            int size = std::stoi(extract_json_value(trade_obj, "\"s\""));
            std::string timestamp = extract_json_string(trade_obj, "t");
            std::string exchange = extract_json_string(trade_obj, "x");
            std::string tape = extract_json_string(trade_obj, "z");
            
            // Extract conditions (as JSON array string)
            std::string conditions_json;
            size_t c_pos = trade_obj.find("\"c\":");
            if (c_pos != std::string::npos) {
                size_t c_array_start = trade_obj.find("[", c_pos);
                if (c_array_start != std::string::npos) {
                    size_t c_array_end = c_array_start + 1;
                    int c_bracket_count = 1;
                    while (c_array_end < trade_obj.length() && c_bracket_count > 0) {
                        if (trade_obj[c_array_end] == '[') c_bracket_count++;
                        else if (trade_obj[c_array_end] == ']') c_bracket_count--;
                        c_array_end++;
                    }
                    
                    if (c_bracket_count == 0) {
                        conditions_json = trade_obj.substr(c_array_start, c_array_end - c_array_start);
                    }
                }
            }
            
            // Bind parameters
            sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, trade_id);
            sqlite3_bind_double(stmt, 3, price);
            sqlite3_bind_int(stmt, 4, size);
            sqlite3_bind_text(stmt, 5, timestamp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, exchange.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 7, tape.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 8, conditions_json.c_str(), -1, SQLITE_TRANSIENT);
            
            // Execute
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                std::cerr << "Insertion failed: " << sqlite3_errmsg(db) << std::endl;
            }
            
            // Reset statement for next use
            sqlite3_reset(stmt);
            
            // Move to next position
            pos = obj_end;
            
            // Skip comma if present
            if (pos < array_content.length() && array_content[pos] == ',') {
                pos++;
            }
        }
        
        // Move to next symbol
        symbol_start = array_end;
    }
    
    // Finalize statement
    sqlite3_finalize(stmt);
    
    // Commit transaction
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    
    return true;
}

// Helper function for extracting numeric values
std::string extract_json_value(const std::string& json, const std::string& key) {
    std::string search_key = key + ":";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) {
        return "0";
    }
    
    pos += search_key.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\t' || json[pos] == '\r')) {
        pos++;
    }
    
    if (pos >= json.length()) {
        return "0";
    }
    
    // Extract the number
    size_t value_end = pos;
    while (value_end < json.length() && 
           (std::isdigit(json[value_end]) || json[value_end] == '.' || json[value_end] == '-')) {
        value_end++;
    }
    
    if (value_end > pos) {
        return json.substr(pos, value_end - pos);
    }
    
    return "0";
}

// Add this to main() function
int main(int argc, char* argv[]) {
    // ... existing code ...
    
    // Path to SQLite database
    std::string db_path = "alpaca_trades.db";
    
    // Initialize database
    sqlite3* db = initialize_database(db_path);
    if (!db) {
        std::cerr << "Failed to initialize database." << std::endl;
        return 1;
    }
    
    // ... existing code to fetch trade data ...
    
    // Store the response data in database
    if (!store_trades_in_db(db, combined_response)) {
        std::cerr << "Error: Failed to store trades in database." << std::endl;
    } else {
        std::cout << "Successfully stored trades in database." << std::endl;
    }
    
    // Close database
    sqlite3_close(db);
    
    return 0;
}
