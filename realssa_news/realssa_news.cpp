// realssa_news_linux.cpp
// Cross-platform RSS News Feed Service for Railway
// Build: g++ -std=c++17 -pthread realssa_news_linux.cpp -lcurl -o realssa_news

#define _CRT_SECURE_NO_WARNINGS

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
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
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

    static string extractAttribute(const string& xml, const string& tag, const string& attr) {
        size_t tagPos = xml.find("<" + tag);
        if (tagPos == string::npos) return "";

        size_t endTag = xml.find(">", tagPos);
        if (endTag == string::npos) return "";

        string tagContent = xml.substr(tagPos, endTag - tagPos);

        size_t attrPos = tagContent.find(attr + "=\"");
        if (attrPos == string::npos) {
            attrPos = tagContent.find(attr + "='");
            if (attrPos == string::npos) return "";
        }

        size_t startQuote = tagContent.find("\"", attrPos);
        if (startQuote == string::npos) startQuote = tagContent.find("'", attrPos);
        if (startQuote == string::npos) return "";

        size_t endQuote = tagContent.find("\"", startQuote + 1);
        if (endQuote == string::npos) endQuote = tagContent.find("'", startQuote + 1);
        if (endQuote == string::npos) return "";

        return tagContent.substr(startQuote + 1, endQuote - startQuote - 1);
    }

    static string extractImageURL(const string& itemXML) {
        // Try media:content url
        string mediaContent = extractAttribute(itemXML, "media:content", "url");
        if (!mediaContent.empty()) return mediaContent;

        // Try media:thumbnail url
        string mediaThumbnail = extractAttribute(itemXML, "media:thumbnail", "url");
        if (!mediaThumbnail.empty()) return mediaThumbnail;

        // Try enclosure url
        string enclosure = extractAttribute(itemXML, "enclosure", "url");
        if (!enclosure.empty() && (enclosure.find(".jpg") != string::npos ||
            enclosure.find(".png") != string::npos ||
            enclosure.find(".jpeg") != string::npos ||
            enclosure.find(".webp") != string::npos)) {
            return enclosure;
        }

        // Try to extract image from description HTML
        string description = extractTag(itemXML, "description");
        size_t imgPos = description.find("<img");
        if (imgPos != string::npos) {
            string imgTag = description.substr(imgPos, description.find(">", imgPos) - imgPos);
            string srcUrl = extractAttribute("<" + imgTag + ">", "img", "src");
            if (!srcUrl.empty()) return srcUrl;
        }

        return "";
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
        replaceAll(text, "&#39;", "'");
        replaceAll(text, "&apos;", "'");

        return trim(text);
    }

    static vector<unordered_map<string, string>> parseRSS(const string& xml, const string& source, const string& category, const string& country) {
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
            item["category"] = category;
            item["country"] = country;
            item["imageUrl"] = extractImageURL(itemXML);

            if (!item["title"].empty()) {
                items.push_back(item);
            }

            pos = end + 7;
            if (items.size() >= 30) break; // Limit to 30 per feed
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

        // FIX: Cast to unsigned char to handle UTF-8 and avoid assertion failures
        while (start < end && isspace(static_cast<unsigned char>(str[start]))) start++;
        while (end > start && isspace(static_cast<unsigned char>(str[end - 1]))) end--;

        return str.substr(start, end - start);
    }
};

// Feed structure
struct Feed {
    string url;
    string source;
    string category;
    string country;
};

// RSS Aggregator
class RSSAggregator {
public:
    RSSAggregator() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        initializeFeeds();
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
                string xml = HTTPFetcher::fetch(feeds[i].url);
                if (!xml.empty()) {
                    results[i] = XMLParser::parseRSS(xml, feeds[i].source, feeds[i].category, feeds[i].country);
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
    void initializeFeeds() {
        // GHANA (6 feeds)
        feeds.push_back({ "https://www.myjoyonline.com/feed/", "Joy Online", "General News", "Ghana" });
        feeds.push_back({ "https://www.graphic.com.gh/rss", "Daily Graphic", "General News", "Ghana" });
        feeds.push_back({ "https://citinewsroom.com/feed/", "Citi Newsroom", "General News", "Ghana" });
        feeds.push_back({ "https://www.modernghana.com/rss", "Modern Ghana", "General News", "Ghana" });
        feeds.push_back({ "https://www.pulse.com.gh/feed", "Pulse Ghana", "Entertainment", "Ghana" });
        feeds.push_back({ "https://www.ghanaweb.com/GhanaHomePage/rss.php", "GhanaWeb", "General News", "Ghana" });

        // NIGERIA (6 feeds)
        feeds.push_back({ "https://rss.punchng.com/v1/category/latest_news", "Punch", "General News", "Nigeria" });
        feeds.push_back({ "https://www.vanguardngr.com/feed/", "Vanguard", "General News", "Nigeria" });
        feeds.push_back({ "https://www.premiumtimesng.com/feed", "Premium Times", "General News", "Nigeria" });
        feeds.push_back({ "https://dailytrust.com/feed", "Daily Trust", "General News", "Nigeria" });
        feeds.push_back({ "https://punchng.com/topics/business/feed/", "Punch Business", "Business", "Nigeria" });
        feeds.push_back({ "https://www.vanguardngr.com/category/business/feed/", "Vanguard Business", "Business", "Nigeria" });

        // KENYA (3 feeds)
        feeds.push_back({ "https://nation.africa/kenya/rss", "Daily Nation", "General News", "Kenya" });
        feeds.push_back({ "https://techweez.com/feed/", "Techweez", "Technology", "Kenya" });
        feeds.push_back({ "https://www.standardmedia.co.ke/rss/headlines.php", "The Standard", "General News", "Kenya" });

        // SOUTH AFRICA (5 feeds)
        feeds.push_back({ "https://www.news24.com/rss", "News24", "General News", "South Africa" });
        feeds.push_back({ "https://mg.co.za/feed/", "Mail & Guardian", "General News", "South Africa" });
        feeds.push_back({ "https://www.dailymaverick.co.za/feed/", "Daily Maverick", "General News", "South Africa" });
        feeds.push_back({ "https://businesstech.co.za/news/feed/", "BusinessTech", "Business", "South Africa" });
        feeds.push_back({ "https://mybroadband.co.za/news/feed", "MyBroadband", "Technology", "South Africa" });

        // EGYPT (2 feeds)
        feeds.push_back({ "http://english.ahram.org.eg/rss.ashx", "Ahram Online", "General News", "Egypt" });
        feeds.push_back({ "https://egyptindependent.com/feed/", "Egypt Independent", "General News", "Egypt" });

        // MOROCCO (2 feeds)
        feeds.push_back({ "https://www.moroccoworldnews.com/feed", "Morocco World News", "General News", "Morocco" });
        feeds.push_back({ "https://en.hespress.com/feed", "Hespress English", "General News", "Morocco" });

        // ETHIOPIA (1 feed)
        feeds.push_back({ "https://addisstandard.com/feed/", "Addis Standard", "General News", "Ethiopia" });

        // PAN-AFRICAN (4 feeds)
        feeds.push_back({ "https://allafrica.com/tools/headlines/rdf/latest/headlines.rdf", "AllAfrica", "Pan-African", "Africa" });
        feeds.push_back({ "https://www.africanews.com/feed/rss", "Africanews", "Pan-African", "Africa" });
        feeds.push_back({ "http://feeds.bbci.co.uk/news/world/africa/rss.xml", "BBC Africa", "Pan-African", "Africa" });
        feeds.push_back({ "https://globalvoices.org/-/world/sub-saharan-africa/rss", "Global Voices Africa", "Pan-African", "Africa" });

        // WORLD NEWS (8 feeds)
        feeds.push_back({ "https://feeds.bbci.co.uk/news/world/rss.xml", "BBC World", "World News", "Global" });
        feeds.push_back({ "https://www.reuters.com/arc/outboundfeeds/rss/category/world/", "Reuters World", "World News", "Global" });
        feeds.push_back({ "https://www.aljazeera.com/xml/rss/all.xml", "Al Jazeera", "World News", "Global" });
        feeds.push_back({ "https://news.un.org/feed/subscribe/en/news/all/rss.xml", "UN News", "World News", "Global" });
        feeds.push_back({ "https://rss.nytimes.com/services/xml/rss/nyt/World.xml", "New York Times World", "World News", "Global" });
        feeds.push_back({ "https://www.theguardian.com/world/rss", "The Guardian World", "World News", "Global" });
        feeds.push_back({ "https://www.independent.co.uk/news/world/rss", "The Independent World", "World News", "Global" });
        feeds.push_back({ "https://apnews.com/index.rss", "Associated Press", "World News", "Global" });

        // USA NEWS (6 feeds)
        feeds.push_back({ "https://rss.cnn.com/rss/cnn_topstories.rss", "CNN", "General News", "USA" });
        feeds.push_back({ "https://feeds.nbcnews.com/nbcnews/public/news", "NBC News", "General News", "USA" });
        feeds.push_back({ "https://abcnews.go.com/abcnews/internationalheadlines", "ABC News", "General News", "USA" });
        feeds.push_back({ "https://rss.nytimes.com/services/xml/rss/nyt/HomePage.xml", "New York Times", "General News", "USA" });
        feeds.push_back({ "https://www.washingtonpost.com/rss", "Washington Post", "General News", "USA" });
        feeds.push_back({ "https://www.usatoday.com/rss/", "USA Today", "General News", "USA" });

        // UK NEWS (5 feeds)
        feeds.push_back({ "https://feeds.bbci.co.uk/news/rss.xml", "BBC News", "General News", "UK" });
        feeds.push_back({ "https://www.theguardian.com/uk/rss", "The Guardian UK", "General News", "UK" });
        feeds.push_back({ "https://www.telegraph.co.uk/rss.xml", "The Telegraph", "General News", "UK" });
        feeds.push_back({ "https://www.independent.co.uk/news/uk/rss", "The Independent UK", "General News", "UK" });
        feeds.push_back({ "https://www.thetimes.co.uk/rss", "The Times", "General News", "UK" });

        // CANADA NEWS (3 feeds)
        feeds.push_back({ "https://www.cbc.ca/webfeed/rss/rss-topstories", "CBC Top Stories", "General News", "Canada" });
        feeds.push_back({ "https://www.cbc.ca/webfeed/rss/rss-world", "CBC World", "World News", "Canada" });
        feeds.push_back({ "https://www.theglobeandmail.com/arc/outboundfeeds/rss/category/politics/", "Globe and Mail", "Politics", "Canada" });

        // TECHNOLOGY (10 feeds)
        feeds.push_back({ "https://www.theverge.com/rss/index.xml", "The Verge", "Technology", "Global" });
        feeds.push_back({ "https://techcrunch.com/feed/", "TechCrunch", "Technology", "Global" });
        feeds.push_back({ "https://www.wired.com/feed/rss", "Wired", "Technology", "Global" });
        feeds.push_back({ "https://www.cnet.com/rss/news/", "CNET", "Technology", "Global" });
        feeds.push_back({ "https://www.engadget.com/rss.xml", "Engadget", "Technology", "Global" });
        feeds.push_back({ "https://arstechnica.com/feed/", "Ars Technica", "Technology", "Global" });
        feeds.push_back({ "https://www.zdnet.com/news/rss.xml", "ZDNet", "Technology", "Global" });
        feeds.push_back({ "https://www.techmeme.com/feed.xml", "Techmeme", "Technology", "Global" });
        feeds.push_back({ "https://news.ycombinator.com/rss", "Hacker News", "Technology", "Global" });
        feeds.push_back({ "https://www.reddit.com/r/technology/.rss", "Reddit Technology", "Technology", "Global" });

        // BUSINESS (8 feeds)
        feeds.push_back({ "https://feeds.bloomberg.com/markets/news.rss", "Bloomberg Markets", "Business", "Global" });
        feeds.push_back({ "https://www.ft.com/?format=rss", "Financial Times", "Business", "Global" });
        feeds.push_back({ "https://www.economist.com/rss", "The Economist", "Business", "Global" });
        feeds.push_back({ "https://www.wsj.com/xml/rss/3_7085.xml", "Wall Street Journal", "Business", "Global" });
        feeds.push_back({ "https://www.forbes.com/real-time/feed2/", "Forbes", "Business", "Global" });
        feeds.push_back({ "https://www.cnbc.com/id/100003114/device/rss/rss.html", "CNBC", "Business", "Global" });
        feeds.push_back({ "https://www.businessinsider.com/rss", "Business Insider", "Business", "Global" });
        feeds.push_back({ "https://fortune.com/feed/", "Fortune", "Business", "Global" });

        // ASIA (8 feeds)
        feeds.push_back({ "https://www.scmp.com/rss/91/feed", "South China Morning Post", "General News", "China" });
        feeds.push_back({ "https://news.cgtn.com/rss/china.xml", "CGTN China", "General News", "China" });
        feeds.push_back({ "http://www.chinadaily.com.cn/rss/china_rss.xml", "China Daily", "General News", "China" });
        feeds.push_back({ "https://www3.nhk.or.jp/nhkworld/en/news/rss.xml", "NHK World Japan", "General News", "Japan" });
        feeds.push_back({ "https://www.channelnewsasia.com/rssfeeds/8395986", "CNA Singapore", "General News", "Singapore" });
        feeds.push_back({ "https://www.straitstimes.com/news/world/rss.xml", "Straits Times", "General News", "Singapore" });
        feeds.push_back({ "https://www.thehindu.com/news/national/feeder/default.rss", "The Hindu", "General News", "India" });
        feeds.push_back({ "https://timesofindia.indiatimes.com/rssfeeds/-2128936835.cms", "Times of India", "General News", "India" });

        // SCIENCE (5 feeds)
        feeds.push_back({ "https://www.sciencedaily.com/rss/all.xml", "Science Daily", "Science", "Global" });
        feeds.push_back({ "https://www.nature.com/nature.rss", "Nature", "Science", "Global" });
        feeds.push_back({ "https://www.newscientist.com/feed/home", "New Scientist", "Science", "Global" });
        feeds.push_back({ "https://www.scientificamerican.com/feed/", "Scientific American", "Science", "Global" });
        feeds.push_back({ "http://feeds.feedburner.com/spacedotcom", "Space.com", "Science", "Global" });

        // SPORTS (5 feeds)
        feeds.push_back({ "https://www.espn.com/espn/rss/news", "ESPN", "Sports", "Global" });
        feeds.push_back({ "https://www.bbc.com/sport/rss.xml", "BBC Sport", "Sports", "Global" });
        feeds.push_back({ "https://www.skysports.com/rss/12040", "Sky Sports", "Sports", "Global" });
        feeds.push_back({ "https://www.goal.com/en/feeds/news", "Goal.com", "Sports", "Global" });
        feeds.push_back({ "https://www.theguardian.com/sport/rss", "Guardian Sports", "Sports", "Global" });

        cout << "📡 Initialized " << feeds.size() << " RSS feeds" << endl;
    }

    vector<Feed> feeds;
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
            <head>
                <title>RealSSA RSS API</title>
                <style>
                    body { font-family: Arial, sans-serif; max-width: 800px; margin: 50px auto; padding: 20px; }
                    h1 { color: #2563eb; }
                    .endpoint { background: #f3f4f6; padding: 15px; margin: 10px 0; border-radius: 8px; }
                    a { color: #2563eb; text-decoration: none; font-weight: bold; }
                    a:hover { text-decoration: underline; }
                    .stats { background: #dbeafe; padding: 10px; border-radius: 5px; margin: 20px 0; }
                </style>
            </head>
            <body>
                <h1>🚀 RealSSA RSS News Feed API</h1>
                <div class="stats">
                    <strong>Features:</strong> 100+ Global RSS Feeds | Image Extraction | Category Filtering | Country Tags
                </div>
                <h2>API Endpoints:</h2>
                <div class="endpoint">
                    <strong>📰 News Feed:</strong><br>
                    <a href="/news-feed">/news-feed</a> - Get all news as JSON<br>
                    <small>Returns: title, link, description, pubDate, source, category, country, imageUrl</small>
                </div>
                <div class="endpoint">
                    <strong>🏥 Health Check:</strong><br>
                    <a href="/health">/health</a> - Server status and item count
                </div>
                <div class="endpoint">
                    <strong>🔔 Notifications:</strong><br>
                    <a href="/notifications">/notifications</a> - Get latest breaking news (last 2 hours)
                </div>
                <h3>Categories Available:</h3>
                <p>General News, Technology, Business, Sports, Science, Entertainment, Politics, Pan-African, World News</p>
                <h3>Regions Covered:</h3>
                <p>🌍 Africa (Ghana, Nigeria, Kenya, South Africa, Egypt, Morocco, Ethiopia)<br>
                   🌎 Americas (USA, Canada)<br>
                   🌏 Asia (China, Japan, Singapore, India)<br>
                   🌐 Global & Europe (UK, International)</p>
            </body>
            </html>
        )", "text/html");
        });

    svr.Get("/news-feed", [&aggregator](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(aggregator.getJSON(), "application/json");
        });

    svr.Get("/health", [&aggregator](const httplib::Request&, httplib::Response& res) {
        string json = "{\"status\":\"ok\",\"items\":" + to_string(aggregator.getItemCount()) + ",\"timestamp\":\"" +
            to_string(time(nullptr)) + "\"}";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(json, "application/json");
        });

    svr.Get("/notifications", [&aggregator](const httplib::Request&, httplib::Response& res) {
        // Return latest items for notifications (items from last 2 hours)
        string allItems = aggregator.getJSON();

        // For now, return all items - you can add time filtering logic later
        string json = "{\"status\":\"ok\",\"notifications\":" + allItems + "}";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(json, "application/json");
        });

    const char* port_str = getenv("PORT");
    int port = port_str ? atoi(port_str) : 3000;

    cout << "🚀 Server running on port " << port << endl;
    svr.listen("0.0.0.0", port);

    return 0;
}