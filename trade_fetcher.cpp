#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <curl/curl.h>
#include <ctime>
#include <chrono>
#include <getopt.h>
#include <sstream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <future>
#include <queue>
#include <condition_variable>
#include <algorithm> // For std::sort
#include <cmath>     // For std::ceil

// Forward declarations
std::string url_encode(const std::string& value);
size_t write_callback(char* ptr, size_t size, size_t nmemb, std::string* data);

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

// Function to get trading days from Alpaca calendar API
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

// Get date string for N days ago (calendar days, not trading days)
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
    std::cerr << "  -n days     Number of trading days to fetch data for (default: 1)" << std::endl;
    std::cerr << "  -l limit    Maximum number of trades per request (default: 10000, max: 10000)" << std::endl;
    std::cerr << "  -f feed     Feed source: sip, iex, boats, otc (default: sip)" << std::endl;
    std::cerr << "  -a asof     As-of date for stock symbol (format: YYYY-MM-DD, default: current day)" << std::endl;
    std::cerr << "  -c currency Currency for prices (default: USD)" << std::endl;
    std::cerr << "  -s symbols  Comma-separated list of stock symbols" << std::endl;
    std::cerr << "  -i file     File containing stock symbols (one per line)" << std::endl;
    std::cerr << "  -d          Sort in descending order (default: ascending)" << std::endl;
    std::cerr << "  -t          Use trading days (weekdays excluding holidays) instead of calendar days" << std::endl;
    std::cerr << "Note: Either -s or -i must be provided." << std::endl;
}

// Structure to hold page information
struct PageData {
    int page_number;
    std::string response_data;
    std::string next_page_token;
};

// Function to fetch a single page
PageData fetch_page(const std::string& url, const std::string& api_key_id, 
                   const std::string& api_secret_key, int page_number) {
    PageData result;
    result.page_number = page_number;
    
    // Initialize curl for this request
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Error: Failed to initialize curl for page " << page_number << std::endl;
        return result;
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
    
    // Response data for this page
    std::string response_data;
    
    // Set callback function
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    // Check for errors
    if (res != CURLE_OK) {
        std::cerr << "Error: curl_easy_perform() failed for page " << page_number 
                  << ": " << curl_easy_strerror(res) << std::endl;
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return result;
    }
    
    // Get HTTP response code
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (http_code != 200) {
        std::cerr << "Error: HTTP status code " << http_code << " for page " << page_number << std::endl;
        std::cerr << "Response: " << response_data << std::endl;
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return result;
    }
    
    // Parse the response for pagination token
    result.response_data = response_data;
    result.next_page_token = extract_json_string(response_data, "next_page_token");
    
    // Clean up
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return result;
}

// Extract all trade objects from a JSON response for a given symbol
std::vector<std::string> extract_trades(const std::string& json, const std::string& symbol) {
    std::vector<std::string> trades;
    
    // Find the trades section
    std::string trades_search = "\"trades\":{";
    size_t trades_pos = json.find(trades_search);
    if (trades_pos == std::string::npos) {
        return trades;
    }
    
    // Find the symbol section
    std::string symbol_search = "\"" + symbol + "\":";
    size_t symbol_pos = json.find(symbol_search, trades_pos);
    if (symbol_pos == std::string::npos) {
        return trades;
    }
    
    // Find the array start
    size_t array_start = json.find("[", symbol_pos);
    if (array_start == std::string::npos) {
        return trades;
    }
    
    // Find the array end
    size_t array_end = array_start + 1;
    int bracket_count = 1;
    while (array_end < json.length() && bracket_count > 0) {
        if (json[array_end] == '[') bracket_count++;
        else if (json[array_end] == ']') bracket_count--;
        array_end++;
    }
    
    if (bracket_count != 0) {
        return trades;
    }
    
    // Extract the array content
    std::string array_content = json.substr(array_start + 1, array_end - array_start - 2);
    
    // Parse individual trade objects
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
        trades.push_back(trade_obj);
        
        // Move to next position
        pos = obj_end;
        
        // Skip comma if present
        if (pos < array_content.length() && array_content[pos] == ',') {
            pos++;
        }
    }
    
    return trades;
}

// Extract timestamp from a trade object
std::string extract_timestamp(const std::string& trade_obj) {
    // Simple extraction of the "t" field
    size_t t_pos = trade_obj.find("\"t\":");
    if (t_pos == std::string::npos) {
        return "";
    }
    
    size_t quote_start = trade_obj.find("\"", t_pos + 4);
    if (quote_start == std::string::npos) {
        return "";
    }
    
    size_t quote_end = trade_obj.find("\"", quote_start + 1);
    if (quote_end == std::string::npos) {
        return "";
    }
    
    return trade_obj.substr(quote_start + 1, quote_end - quote_start - 1);
}

// Thread-safe worker pool for parallel page fetching
// Thread-safe worker pool for parallel page fetching
class PageFetcherPool {
private:
    std::vector<std::future<void>> workers;
    std::mutex queue_mutex;
    std::condition_variable cv;
    std::queue<std::string> url_queue;
    std::map<int, PageData> results;
    std::mutex results_mutex;
    std::string api_key_id;
    std::string api_secret_key;
    bool stop_flag;
    int next_page_number;
    int active_workers; // Track active worker count
    std::mutex active_workers_mutex;
    std::condition_variable all_done_cv;

public:
    PageFetcherPool(const std::string& key_id, const std::string& secret_key, int num_threads)
        : api_key_id(key_id), api_secret_key(secret_key), stop_flag(false), next_page_number(0), active_workers(0) {

        // Initialize worker threads
        for (int i = 0; i < num_threads; i++) {
            workers.push_back(std::async(std::launch::async, [this]() {
                worker_function();
            }));
        }
    }

    ~PageFetcherPool() {
        // Signal all threads to stop and wait for them
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            stop_flag = true;
        }
        cv.notify_all();

        for (auto& worker : workers) {
            if (worker.valid()) {
                worker.wait();
            }
        }
    }

    void add_url(const std::string& url) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            url_queue.push(url);
        }
        cv.notify_one();
    }

    bool is_queue_empty() {
        std::lock_guard<std::mutex> lock(queue_mutex);
        return url_queue.empty();
    }

    std::map<int, PageData> get_results() {
        std::lock_guard<std::mutex> lock(results_mutex);
        return results;
    }

    void wait_for_completion() {
        // Wait until the queue is empty AND no workers are active
        std::unique_lock<std::mutex> active_lock(active_workers_mutex);
        all_done_cv.wait(active_lock, [this]() {
            std::lock_guard<std::mutex> queue_lock(queue_mutex);
            return url_queue.empty() && active_workers == 0;
        });

        std::cout << "All pages processed. Total pages fetched: " << results.size() << std::endl;
    }

private:
    void worker_function() {
        while (true) {
            std::string url;
            int page_number;

            // Get a URL from the queue or wait
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                cv.wait(lock, [this] { return stop_flag || !url_queue.empty(); });

                if (stop_flag && url_queue.empty()) {
                    return;
                }

                if (!url_queue.empty()) {
                    url = url_queue.front();
                    url_queue.pop();
                    page_number = next_page_number++;

                    // Increment active worker count
                    {
                        std::lock_guard<std::mutex> active_lock(active_workers_mutex);
                        active_workers++;
                    }
                } else {
                    continue;
                }
            }

            // Log
            std::cout << "Fetching page " << page_number << " from URL: " << url << std::endl;

            // Fetch page
            PageData page_data = fetch_page(url, api_key_id, api_secret_key, page_number);

            // Store result
            {
                std::lock_guard<std::mutex> lock(results_mutex);
                results[page_number] = page_data;
            }

            // Check if we have a next page token
            if (!page_data.next_page_token.empty()) {
                std::string next_url;

                // Check if the original URL already has a page_token parameter
                if (url.find("page_token=") != std::string::npos) {
                    // Replace existing page_token parameter
                    size_t token_start = url.find("page_token=");
                    size_t token_end = url.find('&', token_start);
                    if (token_end == std::string::npos) {
                        token_end = url.length();
                    }
                    next_url = url.substr(0, token_start) + "page_token=" +
                               url_encode(page_data.next_page_token) +
                               (token_end < url.length() ? url.substr(token_end) : "");
                }
                else if (url.find('?') != std::string::npos) {
                    // URL has other parameters, add page_token as another parameter
                    next_url = url + "&page_token=" + url_encode(page_data.next_page_token);
                }
                else {
                    // URL has no parameters, add page_token as the first parameter
                    next_url = url + "?page_token=" + url_encode(page_data.next_page_token);
                }

                std::cout << "Adding next page URL with token: " << page_data.next_page_token << std::endl;

                // Add the next page URL to the queue
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    url_queue.push(next_url);
                }
                cv.notify_one();
            }

            // Decrement active worker count
            {
                std::lock_guard<std::mutex> active_lock(active_workers_mutex);
                active_workers--;

                // If no active workers and queue is empty, notify wait_for_completion
                if (active_workers == 0) {
                    std::lock_guard<std::mutex> queue_lock(queue_mutex);
                    if (url_queue.empty()) {
                        all_done_cv.notify_all();
                    }
                }
            }
        }
    }
};

// Make API requests to Alpaca in parallel and combine results in order
bool fetch_trade_data(const std::string& url_base, const std::string& api_key_id,
                     const std::string& api_secret_key, std::string* combined_output) {
    // Initialize libcurl globally
    curl_global_init(CURL_GLOBAL_ALL);

    // Determine number of threads based on hardware
    int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4; // Default if detection fails

    std::cout << "Starting data fetching with " << num_threads << " worker threads" << std::endl;

    // Create worker pool
    PageFetcherPool pool(api_key_id, api_secret_key, num_threads);

    // Add initial URL to the queue
    pool.add_url(url_base);

    // Wait for all pages to be fetched
    std::cout << "Waiting for all pages to be fetched..." << std::endl;
    pool.wait_for_completion();

    // Get results in page order
    std::map<int, PageData> page_results = pool.get_results();

    // Clean up global libcurl resources
    curl_global_cleanup();

    if (page_results.empty()) {
        std::cerr << "Error: No data received from API." << std::endl;
        return false;
    }

    std::cout << "Successfully fetched " << page_results.size() << " pages of data" << std::endl;

    // Extract and merge data from all pages
    std::map<std::string, std::vector<std::string>> symbol_trades;

    // Process each page in order
    int page_count = 0;
    for (const auto& page_pair : page_results) {
        const PageData& page = page_pair.second;

        // Log progress for large datasets
        if (++page_count % 10 == 0 || page_count == 1 || page_count == static_cast<int>(page_results.size())) {
            std::cout << "Processing page " << page_count << " of " << page_results.size() << std::endl;
        }

        // Extract trades for all symbols in this page
        size_t trades_pos = page.response_data.find("\"trades\":{");
        if (trades_pos != std::string::npos) {
            size_t symbol_start = trades_pos + 9; // skip "trades":

            // Find each symbol section in the response
            while (symbol_start < page.response_data.length()) {
                // Find next symbol in format "SYMBOL":[
                size_t quote_pos = page.response_data.find("\"", symbol_start);
                if (quote_pos == std::string::npos) break;

                size_t end_quote = page.response_data.find("\"", quote_pos + 1);
                if (end_quote == std::string::npos) break;

                std::string symbol = page.response_data.substr(quote_pos + 1, end_quote - quote_pos - 1);

                // Extract trades for this symbol from this page
                std::vector<std::string> new_trades = extract_trades(page.response_data, symbol);

                // Add to our collection
                symbol_trades[symbol].insert(
                    symbol_trades[symbol].end(),
                    new_trades.begin(),
                    new_trades.end()
                );

                // Find array start
                size_t array_start = page.response_data.find("[", end_quote);
                if (array_start == std::string::npos) break;

                // Find array end
                size_t array_end = array_start + 1;
                int bracket_count = 1;
                while (array_end < page.response_data.length() && bracket_count > 0) {
                    if (page.response_data[array_end] == '[') bracket_count++;
                    else if (page.response_data[array_end] == ']') bracket_count--;
                    array_end++;
                }

                if (bracket_count != 0) break;

                // Move to next symbol
                symbol_start = array_end;
            }
        }
    }

    // Log the total number of trades collected
    size_t total_trades = 0;
    for (const auto& symbol_pair : symbol_trades) {
        total_trades += symbol_pair.second.size();
        std::cout << "Symbol " << symbol_pair.first << ": " << symbol_pair.second.size() << " trades" << std::endl;
    }
    std::cout << "Collected a total of " << total_trades << " trades across "
              << symbol_trades.size() << " symbols from " << page_results.size() << " pages" << std::endl;

    // Sort trades by timestamp for each symbol
    for (auto& symbol_pair : symbol_trades) {
        std::cout << "Sorting " << symbol_pair.second.size() << " trades for " << symbol_pair.first << std::endl;
        std::sort(symbol_pair.second.begin(), symbol_pair.second.end(),
                 [](const std::string& a, const std::string& b) {
                     return extract_timestamp(a) < extract_timestamp(b);
                 });
    }

    // Get metadata from the first and last pages
    std::string next_page_token;
    std::string symbol_type;

    // Use the first page for metadata extraction as it should contain symbol_type
    if (!page_results.empty()) {
        const PageData& first_page = page_results.begin()->second;
        symbol_type = extract_json_string(first_page.response_data, "symbol_type");

        // Get the next_page_token from the last page if there are more results
        const PageData& last_page = page_results.rbegin()->second;
        next_page_token = last_page.next_page_token;

        if (!next_page_token.empty()) {
            std::cout << "WARNING: There are more pages available with next_page_token: "
                      << next_page_token << std::endl;
            std::cout << "To continue fetching from this point, add page_token="
                      << next_page_token << " to your URL" << std::endl;
        } else {
            std::cout << "All available data has been fetched (no next_page_token in the last response)" << std::endl;
        }
    }

    std::cout << "Building final JSON output..." << std::endl;

    // Build a new JSON structure
    std::stringstream new_output;
    new_output << "{";

    // Add the trades object with properly sorted data
    new_output << "\"trades\":{";
    bool first_symbol = true;

    for (const auto& symbol_pair : symbol_trades) {
        if (!first_symbol) {
            new_output << ",";
        }
        first_symbol = false;

        new_output << "\"" << symbol_pair.first << "\":[";

        bool first_trade = true;
        for (const auto& trade : symbol_pair.second) {
            if (!first_trade) {
                new_output << ",";
            }
            first_trade = false;

            new_output << trade;
        }

        new_output << "]";
    }

    new_output << "}";

    // Add metadata if present
    if (!symbol_type.empty()) {
        new_output << ",\"symbol_type\":\"" << symbol_type << "\"";
    }

    if (!next_page_token.empty()) {
        new_output << ",\"next_page_token\":\"" << next_page_token << "\"";
    }

    new_output << "}";

    // Replace the combined output with the newly constructed JSON
    *combined_output = new_output.str();

    std::cout << "JSON output successfully created with " << total_trades << " total trades" << std::endl;

    // Save the JSON output to individual files for each symbol
    for (const auto& symbol_pair : symbol_trades) {
        std::string filename = symbol_pair.first + ".json";
        std::ofstream output_file(filename);

        if (!output_file.is_open()) {
            std::cerr << "Error: Could not open file " << filename << " for writing." << std::endl;
            continue;
        }

        // Create a JSON output for just this symbol
        std::stringstream symbol_output;
        symbol_output << "{\"trades\":{\"" << symbol_pair.first << "\":[";

        bool first_trade = true;
        for (const auto& trade : symbol_pair.second) {
            if (!first_trade) {
                symbol_output << ",";
            }
            first_trade = false;

            symbol_output << trade;
        }

        symbol_output << "]}";

        // Add metadata if present
        if (!symbol_type.empty()) {
            symbol_output << ",\"symbol_type\":\"" << symbol_type << "\"";
        }

        symbol_output << "}";

        // Write to file
        output_file << symbol_output.str();
        output_file.close();

        std::cout << "Saved " << symbol_pair.second.size() << " trades for "
                  << symbol_pair.first << " to " << filename << std::endl;
    }

    return true;
}

int main(int argc, char* argv[]) {
    // Default values
    int days = 1;
    int limit = 10000;
    std::string feed = "sip";
    std::string asof = "";
    std::string currency = "USD";
    std::string symbols_str;
    std::string file_path;
    bool sort_desc = false;
    // Removed the use_trading_days flag - we'll always use trading days

    // Parse command-line arguments
    int opt;
    while ((opt = getopt(argc, argv, "n:l:f:a:c:s:i:dh")) != -1) { // Removed 't' from the option string
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
            case 'l':
                try {
                    limit = std::stoi(optarg);
                    if (limit <= 0 || limit > 10000) {
                        std::cerr << "Error: Limit must be between 1 and 10000." << std::endl;
                        return 1;
                    }
                } catch (...) {
                    std::cerr << "Error: Invalid limit: " << optarg << std::endl;
                    return 1;
                }
                break;
            case 'f':
                feed = optarg;
                if (feed != "sip" && feed != "iex" && feed != "boats" && feed != "otc") {
                    std::cerr << "Error: Invalid feed: " << feed << ". Must be sip, iex, boats, or otc." << std::endl;
                    return 1;
                }
                break;
            case 'a':
                asof = optarg;
                break;
            case 'c':
                currency = optarg;
                break;
            case 's':
                symbols_str = optarg;
                break;
            case 'i':
                file_path = optarg;
                break;
            case 'd':
                sort_desc = true;
                break;
            // Removed the 't' case since we're always using trading days
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
        std::cerr << "Error: No symbols provided. Use -s or -i option." << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    if (symbols.empty()) {
        std::cerr << "Error: No valid symbols found." << std::endl;
        return 1;
    }

    // Get API keys from environment
    const char* api_key_id = std::getenv("APCA_API_KEY_ID");
    const char* api_secret_key = std::getenv("APCA_API_SECRET_KEY");

    if (!api_key_id || !api_secret_key) {
        std::cerr << "Error: APCA_API_KEY_ID and APCA_API_SECRET_KEY must be set in environment." << std::endl;
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
    std::string url = "https://data.alpaca.markets/v2/stocks/trades?symbols=" + url_encode(symbols_param) +
                      "&start=" + url_encode(start_date) +
                      "&end=" + url_encode(end_date) +
                      "&limit=" + std::to_string(limit) +
                      "&feed=" + url_encode(feed) +
                      "&sort=" + (sort_desc ? "desc" : "asc");

    // Add optional parameters if specified
    if (!asof.empty()) {
        url += "&asof=" + url_encode(asof);
    }

    if (!currency.empty() && currency != "USD") {
        url += "&currency=" + url_encode(currency);
    }

    // Print request details (optional, for debugging)
    std::cout << "Fetching trade data for " << symbols.size() << " symbol(s) from "
              << start_date << " to " << end_date << " with limit " << limit << std::endl;
    std::cout << "Using " << std::thread::hardware_concurrency() << " threads for parallel requests" << std::endl;

    // Combined response data from all pages
    std::string combined_response;

    // Make request with pagination handling
    bool success = fetch_trade_data(url, api_key_id, api_secret_key, &combined_response);

    if (!success) {
        std::cerr << "Error: Failed to fetch trade data." << std::endl;
        return 1;
    }

    // Print combined response
    std::cout << combined_response << std::endl;

    return 0;
}
