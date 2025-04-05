#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

// Helper function to build header with environment variable
struct curl_slist* add_env_header(struct curl_slist* headers, const char* header_name, const char* env_var_name) {
    const char* env_value = getenv(env_var_name);
    if (!env_value) {
        fprintf(stderr, "Environment variable %s not set\n", env_var_name);
        return headers;
    }
    
    char header[1024];
    snprintf(header, sizeof(header), "%s: %s", header_name, env_value);
    return curl_slist_append(headers, header);
}

int main(void) {
    CURL *hnd;
    CURLcode ret;
    struct curl_slist *headers = NULL;
    
    // Get base URL from environment or use default
    const char* base_url = getenv("APCA_API_BASE_URL");
    if (!base_url) {
        base_url = "https://paper-api.alpaca.markets";
        fprintf(stderr, "Warning: APCA_API_BASE_URL not set, using default: %s\n", base_url);
    }
    
    // Construct full URL
    char url[1024];
    snprintf(url, sizeof(url), "%s/v2/account", base_url);
    
    // Initialize curl
    hnd = curl_easy_init();
    if (!hnd) {
        fprintf(stderr, "Failed to initialize curl\n");
        return 1;
    }
    
    // Set up request
    curl_easy_setopt(hnd, CURLOPT_CUSTOMREQUEST, "GET");
    curl_easy_setopt(hnd, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(hnd, CURLOPT_URL, url);
    
    // Add headers
    headers = curl_slist_append(headers, "accept: application/json");
    
    // Add API key headers from environment variables
    headers = add_env_header(headers, "APCA-API-KEY-ID", "APCA_API_KEY_ID");
    headers = add_env_header(headers, "APCA-API-SECRET-KEY", "APCA_API_SECRET_KEY");
    
    curl_easy_setopt(hnd, CURLOPT_HTTPHEADER, headers);
    
    // Perform request
    // printf("Making request to: %s\n", url);
    ret = curl_easy_perform(hnd);
    
    // Check for errors
    if (ret != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(ret));
    }
    
    // Clean up
    curl_slist_free_all(headers);
    curl_easy_cleanup(hnd);
    
    return (ret == CURLE_OK) ? 0 : 1;
}
