// realssa_news_linux.cpp
// Cross-platform RSS News Feed Service for Railway
// Build: g++ -std=c++17 -pthread realssa_news_linux.cpp -lcurl -o realssa_news

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <curl/curl.h>

// Single-header HTTP server library (embedded)
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"

using namespace std;

// Simple JSON builder
class JSON {
public:
    static string array(const vector<string>& items) {
        string result = "[";
        for (size_t i = 0; i < items.size(); i++) {
            result += items[i];
            if (i < items.size() - 1) result += ",";
        }
        result += "]";
        return result;
    }

    static string object(const unordered_map<string, string>& obj) {
        string result = "{";
        size_t i = 0;
        for (const auto& [key, val] : obj) {
            result += "\"" + key + "\":\"" + escape(val) + "\"";
            if (++i < obj.size()) result += ",";
        }
        result += "}";
        return result;
    }

private:
    static string escape(const string& str) {
        string result;
        for (char c : str) {
            switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c;
            }
        }
        return result;
    }
};

// HTTP fetcher using libcurl
class HTTPFetcher {
public:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* data) {
        data->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    static string fetch(const string& url) {
        CURL* curl = curl_easy_init();
        if (!curl) return "";

        string response;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        return (res == CURLE_OK) ? response : "";
    }
};

// Simple XML parser
class XMLParser {
public:
    static string extractTag(const string& xml, const string& tag) {
        string startTag = "<" + tag + ">";
        string endTag = "</" + tag + ">";

        size_t start = xml.find(startTag);
        if (start == string::npos) return "";

        start += startTag.length();
        size_t end = xml.find(endTag, start);
        if (end == string::npos) return "";

        return xml.substr(start, end - start);
    }

    static string cleanText(string text) {
        // Remove CDATA
        size_t cdataStart = text.find("<![CDATA[");
        if (cdataStart != string::npos) {
            size_t cdataEnd = text.find("]]>", cdataStart);
            if (cdataEnd != string::npos) {
                text = text.substr(cdataStart + 9, cdataEnd - cdataStart - 9);
            }
        }

        // Remove HTML tags
        size_t pos = 0;
        while ((pos = text.find('<')) != string::npos) {
            size_t end = text.find('>', pos);
            if (end == string::npos) break;
            text.erase(pos, end - pos + 1);
        }

        // Replace entities
        replaceAll(text, "&amp;", "&");
        replaceAll(text, "&lt;", "<");
        replaceAll(text, "&gt;", ">");
        replaceAll(text, "&quot;", "\"");

        return trim(text);
    }

    static vector<unordered_map<string, string>> parseRSS(const string& xml, const string& source) {
        vector<unordered_map<string, string>> items;

        size_t pos = 0;
        while ((pos = xml.find("<item>", pos)) != string::npos) {
            size_t end = xml.find("</item>", pos);
            if (end == string::npos) break;

            string itemXML = xml.substr(pos, end - pos);

            unordered_map<string, string> item;
            item["title"] = cleanText(extractTag(itemXML, "title"));
            item["link"] = cleanText(extractTag(itemXML, "link"));
            item["description"] = cleanText(extractTag(itemXML, "description"));
            item["pubDate"] = cleanText(extractTag(itemXML, "pubDate"));
            item["source"] = source;

            if (!item["title"].empty()) {
                items.push_back(item);
            }

            pos = end + 7;
            if (items.size() >= 5) break; // Limit to 5 per feed (38 feeds total)
        }

        return items;
    }

private:
    static void replaceAll(string& str, const string& from, const string& to) {
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != string::npos) {
            str.replace(pos, from.length(), to);
            pos += to.length();
        }
    }

    static string trim(const string& str) {
        size_t start = 0;
        size_t end = str.length();

        while (start < end && isspace(str[start])) start++;
        while (end > start && isspace(str[end - 1])) end--;

        return str.substr(start, end - start);
    }
};

// RSS Aggregator
class RSSAggregator {
public:
    RSSAggregator() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~RSSAggregator() {
        curl_global_cleanup();
    }

    void startBackgroundRefresh() {
        refreshThread = thread([this]() {
            while (true) {
                refresh();
                this_thread::sleep_for(chrono::hours(1));
            }
            });
        refreshThread.detach();
    }

    void refresh() {
        cout << "🔄 Fetching RSS feeds..." << endl;
        auto start = chrono::steady_clock::now();

        vector<thread> threads;
        vector<vector<unordered_map<string, string>>> results(feeds.size());

        for (size_t i = 0; i < feeds.size(); i++) {
            threads.emplace_back([this, i, &results]() {
                string xml = HTTPFetcher::fetch(feeds[i].second);
                if (!xml.empty()) {
                    results[i] = XMLParser::parseRSS(xml, feeds[i].first);
                }
                });
        }

        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }

        // Combine results
        lock_guard<mutex> lock(dataMutex);
        cachedItems.clear();
        for (const auto& result : results) {
            cachedItems.insert(cachedItems.end(), result.begin(), result.end());
        }

        auto duration = chrono::duration_cast<chrono::seconds>(
            chrono::steady_clock::now() - start
        ).count();

        cout << "✅ Fetched " << cachedItems.size() << " items in " << duration << "s" << endl;
    }

    string getJSON() {
        lock_guard<mutex> lock(dataMutex);

        vector<string> jsonItems;
        for (const auto& item : cachedItems) {
            jsonItems.push_back(JSON::object(item));
        }

        return JSON::array(jsonItems);
    }

    size_t getItemCount() {
        lock_guard<mutex> lock(dataMutex);
        return cachedItems.size();
    }

private:
    vector<pair<string, string>> feeds = {
        {"Ghana News", "https://www.myjoyonline.com/feed/"},
        {"Ghana News", "https://www.graphic.com.gh/rss"},
        {"Ghana News", "https://citinewsroom.com/feed/"},
        {"Ghana News", "https://www.modernghana.com/rss"},
        {"Ghana Entertainment", "https://www.pulse.com.gh/feed"},
        {"Ghana News", "https://www.ghanaweb.com/GhanaHomePage/rss.php"},
        {"Nigeria News", "https://rss.punchng.com/v1/category/latest_news"},
        {"Nigeria News", "https://www.vanguardngr.com/feed/"},
        {"Nigeria News", "https://www.premiumtimesng.com/feed"},
        {"Nigeria News", "https://dailytrust.com/feed"},
        {"Kenya News", "https://nation.africa/kenya/rss"},
        {"Kenya Tech", "https://techweez.com/feed/"},
        {"Pan-African", "https://allafrica.com/tools/headlines/rdf/latest/headlines.rdf"},
        {"Pan-African", "https://www.africanews.com/feed/rss"},
        {"Pan-African", "http://feeds.bbci.co.uk/news/world/africa/rss.xml"},
        {"World News", "https://feeds.bbci.co.uk/news/world/rss.xml"},
        {"World News", "https://www.reuters.com/arc/outboundfeeds/rss/category/world/"},
        {"World News", "https://www.aljazeera.com/xml/rss/all.xml"},
        {"World News", "https://news.un.org/feed/subscribe/en/news/all/rss.xml"},
        {"World News", "https://rss.nytimes.com/services/xml/rss/nyt/World.xml"},
        {"US News", "https://rss.cnn.com/rss/cnn_topstories.rss"},
        {"US News", "https://feeds.nbcnews.com/nbcnews/public/news"},
        {"US News", "https://abcnews.go.com/abcnews/internationalheadlines"},
        {"Canada News", "https://www.cbc.ca/webfeed/rss/rss-topstories"},
        {"Canada News", "https://www.cbc.ca/webfeed/rss/rss-world"},
        {"China News", "https://www.scmp.com/rss/91/feed"},
        {"China News", "https://news.cgtn.com/rss/china.xml"},
        {"China News", "http://www.chinadaily.com.cn/rss/china_rss.xml"},
        {"China News", "http://www.chinadaily.com.cn/rss/world_rss.xml"},
        {"Japan News", "https://www3.nhk.or.jp/nhkworld/en/news/rss.xml"},
        {"South Africa News", "https://www.news24.com/rss"},
        {"South Africa News", "https://mg.co.za/feed/"},
        {"South Africa News", "https://www.dailymaverick.co.za/feed/"},
        {"Egypt News", "http://english.ahram.org.eg/rss.ashx"},
        {"Egypt News", "https://egyptindependent.com/feed/"},
        {"Morocco News", "https://www.moroccoworldnews.com/feed"},
        {"Morocco News", "https://en.hespress.com/feed"},
        {"Global Voices", "https://globalvoices.org/-/world/sub-saharan-africa/rss"}
    };

    vector<unordered_map<string, string>> cachedItems;
    mutex dataMutex;
    thread refreshThread;
};

int main() {
    RSSAggregator aggregator;

    // Initial fetch
    aggregator.refresh();

    // Start background refresh
    aggregator.startBackgroundRefresh();

    // HTTP server
    httplib::Server svr;

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"(
            <!DOCTYPE html>
            <html>
            <head><title>RealSSA RSS API</title></head>
            <body>
                <h1>🚀 RealSSA RSS News Feed API</h1>
                <p>Endpoints:</p>
                <ul>
                    <li><a href="/news-feed">/news-feed</a> - Get news JSON</li>
                    <li><a href="/health">/health</a> - Health check</li>
                </ul>
            </body>
            </html>
        )", "text/html");
        });

    svr.Get("/news-feed", [&aggregator](const httplib::Request&, httplib::Response& res) {
        res.set_content(aggregator.getJSON(), "application/json");
        });

    svr.Get("/health", [&aggregator](const httplib::Request&, httplib::Response& res) {
        string json = "{\"status\":\"ok\",\"items\":" + to_string(aggregator.getItemCount()) + "}";
        res.set_content(json, "application/json");
        });

    const char* port_str = getenv("PORT");
    int port = port_str ? atoi(port_str) : 3000;

    cout << "🚀 Server running on port " << port << endl;
    svr.listen("0.0.0.0", port);

    return 0;
}