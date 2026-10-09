// Reads an identity out of whatever the Canada Identity Panel returns:
// JSON, an HTML page, or plain "Label: value" text.
// Plain C with no Windows dependencies, so it can be tested on any OS.

#ifndef PANEL_H
#define PANEL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#define PANEL_VALUE 256

enum {
    K_NONE = -1,
    K_EMAIL, K_FIRST, K_LAST, K_STREET, K_POSTAL, K_PROVINCE, K_CITY, K_PHONE,
    K_DOB, K_MONTH, K_DAY, K_YEAR, K_GENDER, K_FULLNAME,
    K_COUNT
};

typedef struct {
    wchar_t v[K_COUNT][PANEL_VALUE];
} PanelData;

static const char *PANEL_ALIASES[K_COUNT] = {
    /* email    */ "|email|emailaddress|mail|",
    /* first    */ "|firstname|first|givenname|fname|",
    /* last     */ "|lastname|last|surname|familyname|lname|",
    /* street   */ "|street|streetaddress|address|address1|addressline1|addr|",
    /* postal   */ "|postal|postalcode|postcode|zip|zipcode|",
    /* province */ "|province|state|region|provincestate|prov|",
    /* city     */ "|city|municipality|town|locality|",
    /* phone    */ "|phone|mobile|mobilephone|phonenumber|cell|cellphone|tel|telephone|mobilephonenumber|",
    /* dob      */ "|dob|dateofbirth|birthdate|birthday|",
    /* month    */ "|birthmonth|dobmonth|month|",
    /* day      */ "|dobday|day|",
    /* year     */ "|birthyear|dobyear|year|",
    /* gender   */ "|gender|sex|",
    /* fullname */ "|name|fullname|",
};

static const wchar_t *PANEL_MONTHS[12] = {
    L"January", L"February", L"March", L"April", L"May", L"June",
    L"July", L"August", L"September", L"October", L"November", L"December",
};

static const wchar_t *PANEL_PROVINCES[][2] = {
    {L"AB", L"Alberta"}, {L"BC", L"British Columbia"}, {L"MB", L"Manitoba"},
    {L"NB", L"New Brunswick"}, {L"NL", L"Newfoundland and Labrador"}, {L"NS", L"Nova Scotia"},
    {L"NT", L"Northwest Territories"}, {L"NU", L"Nunavut"}, {L"ON", L"Ontario"},
    {L"PE", L"Prince Edward Island"}, {L"QC", L"Quebec"}, {L"SK", L"Saskatchewan"}, {L"YT", L"Yukon"},
};

// Lowercase letters and digits only: "First Name:" -> "firstname".
static void Panel_Norm(const wchar_t *in, char *out, size_t n) {
    size_t j = 0;
    for (; *in && j + 1 < n; in++) {
        wchar_t c = towlower(*in);
        if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) out[j++] = (char)c;
    }
    out[j] = 0;
}

// Exact key name from the panel -> field.
static int Panel_KeyFor(const wchar_t *rawKey) {
    char k[64], pat[70];
    Panel_Norm(rawKey, k, sizeof k);
    if (!*k) return K_NONE;
    snprintf(pat, sizeof pat, "|%s|", k);
    for (int i = 0; i < K_COUNT; i++)
        if (strstr(PANEL_ALIASES[i], pat)) return i;
    return K_NONE;
}

// Label in identity.txt ("birth month", "mobile phone #") -> field.
static int Panel_LabelKey(const wchar_t *label) {
    char k[64];
    Panel_Norm(label, k, sizeof k);
    if (strstr(k, "mail")) return K_EMAIL;
    if (strstr(k, "first") || strstr(k, "given")) return K_FIRST;
    if (strstr(k, "last") || strstr(k, "surname")) return K_LAST;
    if (strstr(k, "street") || strstr(k, "address")) return K_STREET;
    if (strstr(k, "postal") || strstr(k, "zip")) return K_POSTAL;
    if (strstr(k, "province") || strstr(k, "state")) return K_PROVINCE;
    if (strstr(k, "municipal") || strstr(k, "city") || strstr(k, "town")) return K_CITY;
    if (strstr(k, "phone") || strstr(k, "mobile") || strstr(k, "cell")) return K_PHONE;
    if (strstr(k, "month")) return K_MONTH;
    if (strstr(k, "year")) return K_YEAR;
    if (strstr(k, "day") && strcmp(k, "birthday")) return K_DAY;
    if (strstr(k, "gender") || strstr(k, "sex")) return K_GENDER;
    if (strstr(k, "dob") || strstr(k, "birth")) return K_DOB;
    return K_NONE;
}

static void Panel_Trim(wchar_t *s) {
    wchar_t *a = s;
    while (*a && iswspace(*a)) a++;
    if (a != s) memmove(s, a, (wcslen(a) + 1) * sizeof(wchar_t));
    size_t n = wcslen(s);
    while (n && iswspace(s[n - 1])) s[--n] = 0;
}

static void Panel_Add(PanelData *d, const wchar_t *key, const wchar_t *value) {
    int k = Panel_KeyFor(key);
    if (k == K_NONE || d->v[k][0]) return;
    wcsncpy(d->v[k], value, PANEL_VALUE - 1);
    d->v[k][PANEL_VALUE - 1] = 0;
    Panel_Trim(d->v[k]);
}

// ---------- JSON ----------

// Reads a JSON string starting at the opening quote; returns pointer past the closing quote.
static const wchar_t *Panel_JsonString(const wchar_t *p, wchar_t *out, size_t n) {
    size_t j = 0;
    for (p++; *p && *p != L'"'; p++) {
        wchar_t c = *p;
        if (c == L'\\' && p[1]) {
            c = *++p;
            if (c == L'n') c = L'\n';
            else if (c == L't') c = L'\t';
            else if (c == L'u') {
                wchar_t hex[5] = {0};
                for (int i = 0; i < 4 && p[1]; i++) hex[i] = *++p;
                c = (wchar_t)wcstol(hex, NULL, 16);
            }
        }
        if (j + 1 < n) out[j++] = c;
    }
    out[j] = 0;
    return *p ? p + 1 : p;
}

static void Panel_ParseJson(PanelData *d, const wchar_t *p) {
    wchar_t key[128], val[PANEL_VALUE];
    while (*p) {
        if (*p != L'"') { p++; continue; }
        p = Panel_JsonString(p, key, 128);
        while (iswspace(*p)) p++;
        if (*p != L':') continue;
        p++;
        while (iswspace(*p)) p++;
        if (*p == L'"') {
            p = Panel_JsonString(p, val, PANEL_VALUE);
            Panel_Add(d, key, val);
        } else if (*p == L'-' || iswdigit(*p)) {
            size_t j = 0;
            while ((*p == L'-' || *p == L'.' || iswdigit(*p)) && j + 1 < PANEL_VALUE) val[j++] = *p++;
            val[j] = 0;
            Panel_Add(d, key, val);
        }
    }
}

// ---------- HTML / text ----------

static int Panel_StartsWithI(const wchar_t *s, const wchar_t *prefix) {
    for (; *prefix; s++, prefix++)
        if (towlower(*s) != towlower(*prefix)) return 0;
    return 1;
}

// Gets attr="value" out of a single tag.
static int Panel_Attr(const wchar_t *tag, const wchar_t *tagEnd, const wchar_t *name, wchar_t *out, size_t n) {
    size_t len = wcslen(name);
    for (const wchar_t *p = tag; p < tagEnd; p++) {
        if (!iswspace(p[-1]) || !Panel_StartsWithI(p, name)) continue;
        const wchar_t *q = p + len;
        while (iswspace(*q)) q++;
        if (*q != L'=') continue;
        q++;
        while (iswspace(*q)) q++;
        wchar_t quote = (*q == L'"' || *q == L'\'') ? *q++ : 0;
        size_t j = 0;
        while (q < tagEnd && *q && (quote ? *q != quote : !iswspace(*q) && *q != L'>') && j + 1 < n) out[j++] = *q++;
        out[j] = 0;
        return 1;
    }
    return 0;
}

// Turns HTML into one text line per element, pulling <input value> pairs out as it goes.
static wchar_t *Panel_HtmlToText(PanelData *d, const wchar_t *html) {
    size_t len = wcslen(html);
    wchar_t *out = malloc((len + 1) * sizeof(wchar_t));
    size_t j = 0;
    wchar_t name[128], val[PANEL_VALUE];
    for (const wchar_t *p = html; *p;) {
        if (*p == L'<') {
            const wchar_t *end = wcschr(p, L'>');
            if (!end) break;
            if (Panel_StartsWithI(p, L"<script") || Panel_StartsWithI(p, L"<style")) {
                const wchar_t *close = Panel_StartsWithI(p, L"<script") ? L"</script" : L"</style";
                const wchar_t *q = end;
                while (*q && !Panel_StartsWithI(q, close)) q++;
                end = wcschr(q, L'>');
                if (!end) break;
            } else if (Panel_StartsWithI(p, L"<input") || Panel_StartsWithI(p, L"<textarea")) {
                if (Panel_Attr(p, end, L"value", val, PANEL_VALUE) &&
                    (Panel_Attr(p, end, L"name", name, 128) || Panel_Attr(p, end, L"id", name, 128) ||
                     Panel_Attr(p, end, L"placeholder", name, 128) || Panel_Attr(p, end, L"aria-label", name, 128)))
                    Panel_Add(d, name, val);
            }
            out[j++] = L'\n';
            p = end + 1;
        } else if (*p == L'&') {
            static const struct { const wchar_t *ent; wchar_t c; } ents[] = {
                {L"&amp;", L'&'}, {L"&nbsp;", L' '}, {L"&lt;", L'<'}, {L"&gt;", L'>'},
                {L"&quot;", L'"'}, {L"&#39;", L'\''}, {L"&#039;", L'\''}, {L"&apos;", L'\''},
            };
            size_t i;
            for (i = 0; i < sizeof ents / sizeof ents[0]; i++)
                if (Panel_StartsWithI(p, ents[i].ent)) break;
            if (i < sizeof ents / sizeof ents[0]) { out[j++] = ents[i].c; p += wcslen(ents[i].ent); }
            else out[j++] = *p++;
        } else {
            out[j++] = *p++;
        }
    }
    out[j] = 0;
    return out;
}

static void Panel_ParseLines(PanelData *d, wchar_t *text) {
    // Split into trimmed, non-empty lines.
    size_t cap = 256, count = 0;
    wchar_t **lines = malloc(cap * sizeof(wchar_t *));
    for (wchar_t *line = text, *next; line; line = next) {
        next = wcschr(line, L'\n');
        if (next) *next++ = 0;
        Panel_Trim(line);
        if (!*line) continue;
        if (count == cap) lines = realloc(lines, (cap *= 2) * sizeof(wchar_t *));
        lines[count++] = line;
    }

    for (size_t i = 0; i < count; i++) {
        wchar_t *line = lines[i];
        wchar_t *sep = wcspbrk(line, L":=");
        if (sep && sep - line >= 2 && sep - line <= 40) {
            wchar_t key[64];
            size_t kl = (size_t)(sep - line) < 63 ? (size_t)(sep - line) : 63;
            wcsncpy(key, line, kl);
            key[kl] = 0;
            wchar_t *val = sep + 1;
            while (iswspace(*val)) val++;
            if (*val) { Panel_Add(d, key, val); continue; }
            // "Email:" with the value on the next line.
            if (Panel_KeyFor(key) != K_NONE && i + 1 < count) { Panel_Add(d, key, lines[++i]); }
            continue;
        }
        // "Email" then the value on the next line.
        if (Panel_KeyFor(line) != K_NONE && i + 1 < count && Panel_KeyFor(lines[i + 1]) == K_NONE) {
            Panel_Add(d, line, lines[i + 1]);
            i++;
        }
    }
    free(lines);
}

// ---------- derived fields ----------

static int Panel_MonthFromWord(const wchar_t *w) {
    for (int i = 0; i < 12; i++)
        if (wcslen(w) >= 3 && Panel_StartsWithI(PANEL_MONTHS[i], (wchar_t[]){w[0], w[1], w[2], 0})) return i + 1;
    return 0;
}

// Fills month/day/year from a single date of birth value when they weren't given separately.
static void Panel_SplitDob(PanelData *d) {
    if (!d->v[K_DOB][0] || (d->v[K_MONTH][0] && d->v[K_DAY][0] && d->v[K_YEAR][0])) return;
    int nums[3], nn = 0, month = 0;
    const wchar_t *p = d->v[K_DOB];
    while (*p && nn < 3) {
        if (iswdigit(*p)) { nums[nn++] = (int)wcstol(p, (wchar_t **)&p, 10); continue; }
        if (iswalpha(*p)) {
            wchar_t word[16];
            size_t j = 0;
            while (iswalpha(*p)) { if (j < 15) word[j++] = *p; p++; }
            word[j] = 0;
            if (!month) month = Panel_MonthFromWord(word);
            continue;
        }
        p++;
    }
    int y = 0, m = 0, dd = 0;
    if (month && nn >= 2) {
        m = month;
        if (nums[0] > 31) { y = nums[0]; dd = nums[1]; } else { dd = nums[0]; y = nums[1]; }
    } else if (nn == 3) {
        if (nums[0] > 31) { y = nums[0]; m = nums[1]; dd = nums[2]; }
        else if (nums[0] > 12) { dd = nums[0]; m = nums[1]; y = nums[2]; }
        else { m = nums[0]; dd = nums[1]; y = nums[2]; }
    }
    if (!y || !m || !dd) return;
    if (!d->v[K_MONTH][0]) swprintf(d->v[K_MONTH], PANEL_VALUE, L"%d", m);
    if (!d->v[K_DAY][0]) swprintf(d->v[K_DAY], PANEL_VALUE, L"%d", dd);
    if (!d->v[K_YEAR][0]) swprintf(d->v[K_YEAR], PANEL_VALUE, L"%d", y);
}

static void Panel_SplitName(PanelData *d) {
    if (d->v[K_FIRST][0] || d->v[K_LAST][0] || !d->v[K_FULLNAME][0]) return;
    wchar_t *space = wcschr(d->v[K_FULLNAME], L' ');
    if (!space) { wcscpy(d->v[K_FIRST], d->v[K_FULLNAME]); return; }
    wcsncpy(d->v[K_FIRST], d->v[K_FULLNAME], (size_t)(space - d->v[K_FULLNAME]));
    d->v[K_FIRST][space - d->v[K_FULLNAME]] = 0;
    wcscpy(d->v[K_LAST], space + 1);
    Panel_Trim(d->v[K_LAST]);
}

// Parses a panel response. Returns how many address/contact fields were found.
static int Panel_Parse(PanelData *d, const wchar_t *text) {
    memset(d, 0, sizeof *d);
    const wchar_t *p = text;
    while (iswspace(*p)) p++;
    if (*p == L'{' || *p == L'[') {
        Panel_ParseJson(d, p);
    } else {
        wchar_t *plain = Panel_HtmlToText(d, p);
        Panel_ParseLines(d, plain);
        free(plain);
    }
    Panel_SplitName(d);
    Panel_SplitDob(d);
    int found = 0;
    for (int k = K_EMAIL; k <= K_PHONE; k++) if (d->v[k][0]) found++;
    return found;
}

// The text to type for one field, in the form's expected format. Empty if the panel didn't have it.
static void Panel_Format(const PanelData *d, int key, wchar_t *out, size_t n) {
    out[0] = 0;
    if (key < 0 || key >= K_COUNT || !d->v[key][0]) return;
    const wchar_t *v = d->v[key];
    switch (key) {
    case K_MONTH: {
        int m = iswdigit(v[0]) ? (int)wcstol(v, NULL, 10) : Panel_MonthFromWord(v);
        if (m < 1 || m > 12) break;
        // "MAR", like the form's month box.
        for (int i = 0; i < 3; i++) out[i] = towupper(PANEL_MONTHS[m - 1][i]);
        out[3] = 0;
        return;
    }
    case K_DAY:
        swprintf(out, n, L"%02d", (int)wcstol(v, NULL, 10));
        return;
    case K_PHONE: {
        size_t j = 0;
        for (; *v && j + 1 < n; v++) if (iswdigit(*v)) out[j++] = *v;
        out[j] = 0;
        if (j == 11 && out[0] == L'1') memmove(out, out + 1, 11 * sizeof(wchar_t));
        return;
    }
    case K_POSTAL: {
        size_t j = 0;
        for (; *v && j + 2 < n; v++) if (!iswspace(*v)) out[j++] = towupper(*v);
        out[j] = 0;
        if (j == 6) { memmove(out + 4, out + 3, 4 * sizeof(wchar_t)); out[3] = L' '; }
        return;
    }
    case K_PROVINCE:
        for (size_t i = 0; i < sizeof PANEL_PROVINCES / sizeof PANEL_PROVINCES[0]; i++)
            if (wcslen(v) == 2 && (wchar_t)towupper(v[0]) == PANEL_PROVINCES[i][0][0] && (wchar_t)towupper(v[1]) == PANEL_PROVINCES[i][0][1]) {
                wcsncpy(out, PANEL_PROVINCES[i][1], n - 1);
                out[n - 1] = 0;
                return;
            }
        break;
    case K_GENDER:
        // Tab lands on Male; Female is one more Tab along.
        wcsncpy(out, towlower(v[0]) == L'f' ? L"{tab}{space}" : L"{space}", n - 1);
        out[n - 1] = 0;
        return;
    }
    wcsncpy(out, v, n - 1);
    out[n - 1] = 0;
}

#endif
