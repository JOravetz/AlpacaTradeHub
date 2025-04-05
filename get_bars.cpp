#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <ctime>
#include <chrono>
#include <getopt.h>
#include <sstream>
#include <iomanip>
#include <algorithm> // For std::sort
#include <cmath>     // For std::ceil

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

// Get date string for N days ago
std::string get_date_days_ago(int days_ago) {
    // Get current time
    auto now = std::chrono::system_clock::now();
    
    // Subtract days
    auto past = now - std::chrono::hours(24 * days_ago);
    
    // Convert to time_t
    std::time_t past_time = std::chrono::system_clock::to_time_t(past);
    
    // Format as YYYY-MM-DD
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&past_time), "%Y-%m-%d");
    
    return ss.str();
}

// Validate timeframe according to Alpaca API specs
bool is_valid_timeframe(const std::string& timeframe) {
    // Check for minute timeframes: [1-59]Min or [1-59]T
    if (timeframe.size() >= 2 && timeframe.size() <= 5) {
        if (timeframe.substr(timeframe.size() - 3) == "Min" || 
            timeframe.substr(timeframe.size() - 1) == "T") {
            int minutes;
            try {
                minutes = std::stoi(timeframe.substr(0, timeframe.size() - (timeframe.back() == 'T' ? 1 : 3)));
                if (minutes >= 1 && minutes <= 59) {
                    return true;
                }
            } catch (...) {
                return false;
            }
        }
    }
    
    // Check for hour timeframes: [1-23]Hour or [1-23]H
    if (timeframe.size() >= 2 && timeframe.size() <= 6) {
        if (timeframe.substr(timeframe.size() - 4) == "Hour" || 
            timeframe.substr(timeframe.size() - 1) == "H") {
            int hours;
            try {
                hours = std::stoi(timeframe.substr(0, timeframe.size() - (timeframe.back() == 'H' ? 1 : 4)));
                if (hours >= 1 && hours <= 23) {
                    return true;
                }
            } catch (...) {
                return false;
            }
        }
    }
    
    // Check for day, week, month timeframes
    if (timeframe == "1Day" || timeframe == "1D" || 
        timeframe == "1Week" || timeframe == "1W") {
        return true;
    }
    
    // Check for month timeframes: [1,2,3,4,6,12]Month or [1,2,3,4,6,12]M
    if (timeframe.size() >= 2 && timeframe.size() <= 7) {
        if (timeframe.substr(timeframe.size() - 5) == "Month" || 
            timeframe.substr(timeframe.size() - 1) == "M") {
            int months;
            try {
                months = std::stoi(timeframe.substr(0, timeframe.size() - (timeframe.back() == 'M' ? 1 : 5)));
                if (months == 1 || months == 2 || months == 3 || 
                    months == 4 || months == 6 || months == 12) {
                    return true;
                }
            } catch (...) {
                return false;
            }
        }
    }
    
    return false;
}

// Read symbols from a file, one per line
std::vector<std::string> read_symbols_from_file(const std::string& filename) {
    std::vector<std::string> symbols;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return symbols;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Remove whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        if (!line.empty()) {
            symbols.push_back(line);
        }
    }
    
    return symbols;
}

// URL encode a string using libcurl
std::string url_encode(const std::string& value) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return value;
    }
    
    char* encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.length()));
    if (!encoded) {
        curl_easy_cleanup(curl);
        return value;
    }
    
    std::string result(encoded);
    curl_free(encoded);
    curl_easy_cleanup(curl);
    
    return result;
}

// Callback function to handle response data
size_t write_callback(char* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append(ptr, size * nmemb);
    return size * nmemb;
}

// Print usage information
void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [options]" << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  -n days       Number of days to fetch data for (default: 1)" << std::endl;
    std::cerr << "  -t timeframe  Timeframe (e.g., 1Min, 5Min, 1Hour, 1Day, 1Week, 1Month)" << std::endl;
    std::cerr << "  -s symbols    Comma-separated list of stock symbols" << std::endl;
    std::cerr << "  -f filename   File containing stock symbols (one per line)" << std::endl;
    std::cerr << "Note: Either -s or -f must be provided." << std::endl;
}

// Make an API request to Alpaca and handle pagination if needed
bool fetch_bar_data(const std::string& url_base, const std::string& api_key_id, 
                   const std::string& api_secret_key, std::string* combined_output) {
    bool success = true;
    std::string current_url = url_base;
    std::string next_page_token;
    bool first_page = true;
    
    do {
        // Initialize curl for this request
        CURL* curl = curl_easy_init();
        if (!curl) {
            std::cerr << "Error: Failed to initialize curl." << std::endl;
            return false;
        }
        
        // Set URL
        curl_easy_setopt(curl, CURLOPT_URL, current_url.c_str());
        
        // Set headers
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "accept: application/json");
        
        std::string api_key_header = "APCA-API-KEY-ID: " + api_key_id;
        std::string api_secret_header = "APCA-API-SECRET-KEY: " + api_secret_key;
        
        headers = curl_slist_append(headers, api_key_header.c_str());
        headers = curl_slist_append(headers, api_secret_header.c_str());
        
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        // Response data for this page
        std::string response_data;
        
        // Set callback function
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
        
        // Show pagination info
        if (!first_page) {
            std::cout << "Fetching next page with token: " << next_page_token << std::endl;
        }
        
        // Perform request
        CURLcode res = curl_easy_perform(curl);
        
        // Check for errors
        if (res != CURLE_OK) {
            std::cerr << "Error: curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return false;
        }
        
        // Get HTTP response code
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        
        if (http_code != 200) {
            std::cerr << "Error: HTTP status code " << http_code << std::endl;
            std::cerr << "Response: " << response_data << std::endl;
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return false;
        }
        
        // Parse the response for pagination token
        next_page_token = extract_json_string(response_data, "next_page_token");
        
        // Merge the content (for first page, keep header; for subsequent pages, extract only the data array)
        if (first_page) {
            *combined_output = response_data;
            first_page = false;
        } else {
            // Basic JSON array merging for bars data
            // Find position of "bars" in the current output
            size_t bars_pos = combined_output->find("\"bars\":{");
            if (bars_pos != std::string::npos) {
                // Find position of the same in the new response
                size_t new_bars_pos = response_data.find("\"bars\":{");
                if (new_bars_pos != std::string::npos) {
                    size_t symbol_start = new_bars_pos + 8; // skip "bars":
                    
                    // Find each symbol section in the new response
                    while (symbol_start < response_data.length()) {
                        // Find next symbol in format "SYMBOL":[
                        size_t quote_pos = response_data.find("\"", symbol_start);
                        if (quote_pos == std::string::npos) break;
                        
                        size_t end_quote = response_data.find("\"", quote_pos + 1);
                        if (end_quote == std::string::npos) break;
                        
                        std::string symbol = response_data.substr(quote_pos + 1, end_quote - quote_pos - 1);
                        
                        // Find array start
                        size_t array_start = response_data.find("[", end_quote);
                        if (array_start == std::string::npos) break;
                        
                        // Find array end
                        size_t array_end = array_start + 1;
                        int bracket_count = 1;
                        while (array_end < response_data.length() && bracket_count > 0) {
                            if (response_data[array_end] == '[') bracket_count++;
                            else if (response_data[array_end] == ']') bracket_count--;
                            array_end++;
                        }
                        
                        if (bracket_count != 0) break;
                        
                        // Extract the array content
                        std::string array_content = response_data.substr(array_start + 1, array_end - array_start - 2);
                        
                        // Find the same symbol in the combined output
                        std::string symbol_search = "\"" + symbol + "\":";
                        size_t combined_symbol_pos = combined_output->find(symbol_search, bars_pos);
                        
                        if (combined_symbol_pos != std::string::npos) {
                            size_t combined_array_start = combined_output->find("[", combined_symbol_pos);
                            if (combined_array_start != std::string::npos) {
                                // Find array end in combined output
                                size_t combined_array_end = combined_array_start + 1;
                                int combined_bracket_count = 1;
                                while (combined_array_end < combined_output->length() && combined_bracket_count > 0) {
                                    if ((*combined_output)[combined_array_end] == '[') combined_bracket_count++;
                                    else if ((*combined_output)[combined_array_end] == ']') combined_bracket_count--;
                                    combined_array_end++;
                                }
                                
                                if (combined_bracket_count == 0) {
                                    // Insert the new array content before the closing bracket
                                    if (!array_content.empty()) {
                                        combined_output->insert(combined_array_end - 1, "," + array_content);
                                    }
                                }
                            }
                        }
                        
                        // Move to next symbol
                        symbol_start = array_end;
                    }
                }
            }
        }
        
        // Clean up
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        // Update URL for next page if token exists
        if (!next_page_token.empty()) {
            // If URL already has parameters, add page_token as another parameter
            if (current_url.find('?') != std::string::npos) {
                current_url = url_base + "&page_token=" + url_encode(next_page_token);
            } else {
                current_url = url_base + "?page_token=" + url_encode(next_page_token);
            }
        }
        
    } while (!next_page_token.empty());
    
    return success;
}

// Function to get trading days from Alpaca calendar API
std::vector<std::string> get_trading_days(int requested_days, const std::string& api_key_id, const std::string& api_secret_key) {
    std::vector<std::string> trading_days;

    // Calculate a date range that's likely to include enough trading days
    // We'll multiply by 1.5 to account for weekends and holidays
    int calendar_range = std::ceil(requested_days * 1.5);

    // Get current date and calculate start date
    auto now = std::chrono::system_clock::now();
    auto start_date = now - std::chrono::hours(24 * calendar_range);

    // Format dates as YYYY-MM-DD
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::time_t start_time = std::chrono::system_clock::to_time_t(start_date);

    std::ostringstream end_ss, start_ss;
    end_ss << std::put_time(std::localtime(&now_time), "%Y-%m-%d");
    start_ss << std::put_time(std::localtime(&start_time), "%Y-%m-%d");

    std::string end_date_str = end_ss.str();
    std::string start_date_str = start_ss.str();

    // Construct URL for calendar API
    std::string url = "https://paper-api.alpaca.markets/v2/calendar?start=" +
                       url_encode(start_date_str) + "&end=" + url_encode(end_date_str);

    // Initialize curl
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Error: Failed to initialize curl for calendar API." << std::endl;
        return trading_days;
    }

    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // Set headers
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "accept: application/json");

    std::string api_key_header = "APCA-API-KEY-ID: " + api_key_id;
    std::string api_secret_header = "APCA-API-SECRET-KEY: " + api_secret_key;

    headers = curl_slist_append(headers, api_key_header.c_str());
    headers = curl_slist_append(headers, api_secret_header.c_str());

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Response data
    std::string response_data;

    // Set callback function
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

    // Perform request
    CURLcode res = curl_easy_perform(curl);

    // Check for errors
    if (res != CURLE_OK) {
        std::cerr << "Error: curl_easy_perform() failed for calendar API: "
                  << curl_easy_strerror(res) << std::endl;
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return trading_days;
    }

    // Get HTTP response code
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code != 200) {
        std::cerr << "Error: HTTP status code " << http_code << " for calendar API" << std::endl;
        std::cerr << "Response: " << response_data << std::endl;
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return trading_days;
    }

    // Clean up
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // Parse the calendar response to extract trading days
    // The calendar API returns an array of objects with 'date' field
    // Find all occurrences of "date":"YYYY-MM-DD"
    std::string date_prefix = "\"date\":\"";
    size_t pos = 0;

    while ((pos = response_data.find(date_prefix, pos)) != std::string::npos) {
        pos += date_prefix.length();
        size_t end_pos = response_data.find("\"", pos);

        if (end_pos != std::string::npos) {
            std::string date = response_data.substr(pos, end_pos - pos);
            trading_days.push_back(date);
            pos = end_pos;
        }
    }

    // Sort all trading days in descending order (newest first)
    std::sort(trading_days.begin(), trading_days.end(), std::greater<std::string>());

    // Limit to the requested number of trading days
    if (trading_days.size() > static_cast<size_t>(requested_days)) {
        trading_days.resize(requested_days);
    }

    // Sort the dates in ascending order for API request
    std::sort(trading_days.begin(), trading_days.end());

    return trading_days;
}

int main(int argc, char* argv[]) {
    // Default values
    int days = 1;
    std::string timeframe = "1Min";
    std::string symbols_str;
    std::string file_path;

    // Parse command-line arguments
    int opt;
    while ((opt = getopt(argc, argv, "n:t:s:f:h")) != -1) {
        switch (opt) {
            case 'n':
                try {
                    days = std::stoi(optarg);
                    if (days <= 0) {
                        std::cerr << "Error: Number of days must be positive." << std::endl;
                        return 1;
                    }
                } catch (...) {
                    std::cerr << "Error: Invalid number of days: " << optarg << std::endl;
                    return 1;
                }
                break;
            case 't':
                timeframe = optarg;
                if (!is_valid_timeframe(timeframe)) {
                    std::cerr << "Error: Invalid timeframe: " << timeframe << std::endl;
                    return 1;
                }
                break;
            case 's':
                symbols_str = optarg;
                break;
            case 'f':
                file_path = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Collect symbols
    std::vector<std::string> symbols;
    if (!file_path.empty()) {
        symbols = read_symbols_from_file(file_path);
    } else if (!symbols_str.empty()) {
        std::string symbol;
        std::istringstream stream(symbols_str);
        while (std::getline(stream, symbol, ',')) {
            // Remove whitespace
            symbol.erase(0, symbol.find_first_not_of(" \t\r\n"));
            symbol.erase(symbol.find_last_not_of(" \t\r\n") + 1);

            if (!symbol.empty()) {
                symbols.push_back(symbol);
            }
        }
    } else {
        std::cerr << "Error: No symbols provided. Use -s or -f option." << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    if (symbols.empty()) {
        std::cerr << "Error: No valid symbols found." << std::endl;
        return 1;
    }

    // Join symbols for URL
    std::string symbols_param;
    for (size_t i = 0; i < symbols.size(); ++i) {
        symbols_param += symbols[i];
        if (i < symbols.size() - 1) {
            symbols_param += ",";
        }
    }

    // Get API keys from environment
    const char* api_key_id = std::getenv("APCA_API_KEY_ID");
    const char* api_secret_key = std::getenv("APCA_API_SECRET_KEY");

    if (!api_key_id || !api_secret_key) {
        std::cerr << "Error: APCA_API_KEY_ID and APCA_API_SECRET_KEY must be set in environment." << std::endl;
        return 1;
    }

    // Get trading days from Alpaca API
    std::cout << "Fetching trading calendar for " << days << " trading days..." << std::endl;
    std::vector<std::string> trading_days = get_trading_days(days, api_key_id, api_secret_key);

    std::string start_date, end_date;

    if (trading_days.empty()) {
        std::cerr << "Error: Failed to retrieve trading days from calendar API." << std::endl;
        return 1;
    }

    std::cout << "Successfully retrieved " << trading_days.size() << " trading days" << std::endl;

    if (trading_days.size() < static_cast<size_t>(days)) {
        std::cout << "Warning: Only found " << trading_days.size() << " trading days (requested " << days << ")" << std::endl;
    }

    // Use the first trading day as the start date
    start_date = trading_days.front();

    // Use next day after the last trading day as the end date
    // This ensures we get all data for the last trading day
    std::string last_trading_day = trading_days.back();

    // Parse the date to add one day
    struct std::tm tm = {};
    std::istringstream ss(last_trading_day);
    ss >> std::get_time(&tm, "%Y-%m-%d");

    // Convert to time_t, add 24 hours, and convert back to string
    std::time_t time = std::mktime(&tm);
    time += 86400; // Add 24 hours (86400 seconds)
    std::tm* next_day = std::localtime(&time);

    std::ostringstream end_ss;
    end_ss << std::put_time(next_day, "%Y-%m-%d");
    end_date = end_ss.str();

    std::cout << "Using trading days from " << start_date << " to " << last_trading_day
              << " (API date range: " << start_date << " to " << end_date << ")" << std::endl;

    // Build URL with URL-encoded parameters
    std::string url = "https://data.alpaca.markets/v2/stocks/bars?symbols=" + url_encode(symbols_param) +
                      "&timeframe=" + url_encode(timeframe) +
                      "&start=" + url_encode(start_date) +
                      "&end=" + url_encode(end_date) +
                      "&limit=10000&adjustment=split&feed=sip&sort=asc";

    // Combined response data from all pages
    std::string combined_response;

    // Make request with pagination handling
    bool success = fetch_bar_data(url, api_key_id, api_secret_key, &combined_response);

    if (!success) {
        std::cerr << "Error: Failed to fetch data." << std::endl;
        return 1;
    }

    // Print combined response
    std::cout << combined_response << std::endl;

    return 0;
}
