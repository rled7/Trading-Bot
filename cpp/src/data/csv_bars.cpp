#include "data/csv_bars.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

namespace af {

static char detect_delim(const std::string &line) {
    int commas = 0, tabs = 0, semis = 0;
    for (char c : line) {
        if (c == ',')  ++commas;
        if (c == '\t') ++tabs;
        if (c == ';')  ++semis;
    }
    if (tabs   >= commas && tabs   >= semis) return '\t';
    if (semis  >  commas)                    return ';';
    return ',';
}

static std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) { out.push_back(std::move(cur)); cur.clear(); }
        else if (c != '\r') cur.push_back(c);
    }
    out.push_back(std::move(cur));
    return out;
}

static std::string strip(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

static bool parse_double(const std::string &s, double &out) {
    if (s.empty()) return false;
    char *end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end != s.c_str() && *end == '\0';
}

/* Returns epoch seconds; -1 on failure. Datetimes treated as UTC. */
static int64_t parse_timestamp(const std::string &s_in) {
    std::string s = strip(s_in);
    if (s.empty()) return -1;
    /* Pure integer = epoch seconds */
    {
        char *end = nullptr;
        long long v = std::strtoll(s.c_str(), &end, 10);
        if (end != s.c_str() && *end == '\0') return (int64_t)v;
    }
    /* Datetime: tolerate '-' '.' '/' for date and 'T' or space for date/time gap */
    int y=0, mo=0, d=0, h=0, mi=0, se=0;
    std::string norm = s;
    for (char &c : norm) {
        if (c == '.' || c == '/') c = '-';
        if (c == 'T' || c == 't') c = ' ';
    }
    int got = std::sscanf(norm.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se);
    if (got < 5) return -1;
    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon  = mo - 1; tm.tm_mday = d;
    tm.tm_hour = h;        tm.tm_min  = mi;     tm.tm_sec  = se;
    /* timegm is a GNU/BSD extension — fine on the supported platforms here */
    time_t t = timegm(&tm);
    return (t == (time_t)-1) ? -1 : (int64_t)t;
}

CsvLoadResult load_csv_bars(const std::string &path) {
    CsvLoadResult r;
    std::ifstream f(path);
    if (!f) {
        r.error = "cannot open file: " + path;
        return r;
    }

    std::string line;
    char delim = 0;
    bool header_checked = false;

    while (std::getline(f, line)) {
        if (strip(line).empty()) { ++r.rows_skipped; continue; }
        if (delim == 0) delim = detect_delim(line);

        auto fields = split(line, delim);
        if (fields.size() < 6) { ++r.rows_skipped; continue; }

        /* Header detection: first field of first non-blank line not parseable
           as a timestamp -> skip it. */
        if (!header_checked) {
            header_checked = true;
            if (parse_timestamp(fields[0]) < 0) { ++r.rows_skipped; continue; }
        }

        AF_Bar b{};
        int64_t ts = parse_timestamp(fields[0]);
        double o, h, l, c, v;
        if (ts < 0
            || !parse_double(strip(fields[1]), o)
            || !parse_double(strip(fields[2]), h)
            || !parse_double(strip(fields[3]), l)
            || !parse_double(strip(fields[4]), c)
            || !parse_double(strip(fields[5]), v)) {
            ++r.rows_skipped;
            continue;
        }
        b.timestamp = ts;
        b.open = o; b.high = h; b.low = l; b.close = c; b.volume = v;
        if (fields.size() >= 7) {
            double sp = 0; parse_double(strip(fields[6]), sp);
            b.spread = sp;
        }
        af_bar_init(&b);
        r.bars.push_back(b);
        ++r.rows_parsed;
    }

    if (r.bars.empty() && r.error.empty()) r.error = "no rows parsed";
    return r;
}

} /* namespace af */
