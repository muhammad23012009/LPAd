#include <iostream>
#include <cstdint>
#include <cstring>
#include <curl/curl.h>

struct http_trans_response_data {
    uint8_t *data;
    size_t size;
};

static struct libcurl_interface {
    CURLcode (*_curl_global_init)(long flags);
    CURL *(*_curl_easy_init)(void);
    CURLcode (*_curl_easy_setopt)(CURL *curl, CURLoption option, ...);
    CURLcode (*_curl_easy_perform)(CURL *curl);
    CURLcode (*_curl_easy_getinfo)(CURL *curl, CURLINFO info, ...);
    const char *(*_curl_easy_strerror)(CURLcode);
    void (*_curl_easy_cleanup)(CURL *curl);

    struct curl_slist *(*_curl_slist_append)(struct curl_slist *list, const char *data);
    void (*_curl_slist_free_all)(struct curl_slist *list);

    char *(*_curl_version)(void);
} libcurl;

static size_t http_trans_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct http_trans_response_data *mem = (struct http_trans_response_data *)userp;

    mem->data = (uint8_t *)realloc(mem->data, mem->size + realsize + 1);
    if (mem->data == NULL) {
        /* out of memory! */
        std::cerr << "not enough memory (realloc returned NULL)" << std::endl;
        return 0;
    }

    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

int http_interface_transmit(struct euicc_ctx *ctx, const char *url, uint32_t *rcode, uint8_t **rx,
                                   uint32_t *rx_len, const uint8_t *tx, uint32_t tx_len, const char **h) {
    int fret = 0;
    CURL *curl;
    CURLcode res;
    struct http_trans_response_data responseData = {0};
    struct curl_slist *headers = NULL, *nheaders = NULL;
    long response_code;

    std::cout << "HTTP Transmit to URL: " << url << std::endl;

    (*rx) = NULL;
    (*rcode) = 0;

    curl = libcurl._curl_easy_init();
    if (!curl) {
        goto err;
    }

    libcurl._curl_easy_setopt(curl, CURLOPT_URL, url);
    libcurl._curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_trans_write_callback);
    libcurl._curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&responseData);
    libcurl._curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    libcurl._curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    for (int i = 0; h[i] != NULL; i++) {
        nheaders = libcurl._curl_slist_append(headers, h[i]);
        if (nheaders == NULL) {
            goto err;
        }
        headers = nheaders;
    }
    libcurl._curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (tx != NULL) {
        libcurl._curl_easy_setopt(curl, CURLOPT_POSTFIELDS, tx);
        libcurl._curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)tx_len);
    }

    res = libcurl._curl_easy_perform(curl);

    if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << libcurl._curl_easy_strerror(res) << std::endl;
        goto err;
    }

    libcurl._curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    printf("HTTP Response Code: %ld\n", response_code);

    *rcode = response_code;
    *rx = responseData.data;
    *rx_len = responseData.size;

    fret = 0;
    goto exit;

err:
    fret = -1;
    std::free(responseData.data);
exit:
    libcurl._curl_easy_cleanup(curl);
    libcurl._curl_slist_free_all(headers);
    return fret;
}

int _init_libcurl(void) {
    libcurl._curl_global_init = curl_global_init;
    libcurl._curl_easy_init = curl_easy_init;
    libcurl._curl_easy_setopt = curl_easy_setopt;
    libcurl._curl_easy_perform = curl_easy_perform;
    libcurl._curl_easy_getinfo = curl_easy_getinfo;
    libcurl._curl_easy_strerror = curl_easy_strerror;
    libcurl._curl_easy_cleanup = curl_easy_cleanup;
    libcurl._curl_slist_append = curl_slist_append;
    libcurl._curl_slist_free_all = curl_slist_free_all;
    libcurl._curl_version = curl_version;

    if (libcurl._curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return -1;
    }

    return 0;
}