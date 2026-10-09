// ==UserScript==
// @name         Identity Autofill
// @namespace    amethyst
// @version      1.0.0
// @description  Floating "Fill" button that fills sign-up forms with an identity from the Canada Identity Panel (localhost:8085) or a saved profile.
// @match        *://*/*
// @grant        GM_xmlhttpRequest
// @grant        GM_getValue
// @grant        GM_setValue
// @connect      localhost
// @connect      127.0.0.1
// @run-at       document-idle
// @noframes
// ==/UserScript==

(function () {
  'use strict';

  // Where the Canada Identity Panel runs. Set to '' to only use the saved profile.
  const PANEL_URL = 'http://localhost:8085';

  // Used when the panel can't be reached and nothing has been saved yet.
  const DEFAULT_PROFILE = {
    email: 'pandahax7221@gmail.com',
    firstName: 'Bob',
    lastName: 'Martin',
    street: '123 Barney Avenue',
    postal: 'M2K 9E4',
    province: 'Ontario',
    city: 'Toronto',
    phone: '(437) 921-8267',
    dob: '1980-03-04',
    gender: 'Male',
  };

  const MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July',
    'August', 'September', 'October', 'November', 'December'];

  const PROVINCES = {
    AB: 'Alberta', BC: 'British Columbia', MB: 'Manitoba', NB: 'New Brunswick',
    NL: 'Newfoundland and Labrador', NS: 'Nova Scotia', NT: 'Northwest Territories',
    NU: 'Nunavut', ON: 'Ontario', PE: 'Prince Edward Island', QC: 'Quebec',
    SK: 'Saskatchewan', YT: 'Yukon',
  };

  // Field key -> pattern matched against a form field's label/name/placeholder.
  const FIELD_PATTERNS = {
    email: /e-?mail/,
    firstName: /first.?name|given.?name|fname|prenom/,
    lastName: /last.?name|surname|family.?name|lname/,
    street: /street|address.?(line)?.?1|^address$|addr1|street.?address/,
    postal: /postal|zip|post.?code/,
    province: /province|state|region/,
    city: /municipality|city|town|locality/,
    phone: /phone|mobile|cell|tel/,
    dobMonth: /birth.?month|dob.?month|^month$|^mm$/,
    dobDay: /birth.?day|dob.?day|^day$|^dd$/,
    dobYear: /birth.?year|dob.?year|^year$|^yyyy$/,
    dob: /birth.?date|date.?of.?birth|^dob$|birthday/,
  };

  // Aliases for keys that the panel might use (compared after normalising).
  const KEY_ALIASES = {
    email: ['email', 'emailaddress', 'mail'],
    firstName: ['firstname', 'first', 'givenname', 'fname'],
    lastName: ['lastname', 'last', 'surname', 'familyname', 'lname'],
    street: ['street', 'streetaddress', 'address', 'address1', 'addressline1'],
    postal: ['postal', 'postalcode', 'postcode', 'zip', 'zipcode'],
    province: ['province', 'state', 'region', 'provincestate'],
    city: ['city', 'municipality', 'town', 'locality'],
    phone: ['phone', 'mobile', 'mobilephone', 'phonenumber', 'cell', 'tel', 'telephone'],
    dob: ['dob', 'dateofbirth', 'birthdate', 'birthday'],
    dobMonth: ['birthmonth', 'dobmonth', 'month'],
    dobDay: ['birthday_day', 'dobday', 'day'],
    dobYear: ['birthyear', 'dobyear', 'year'],
    gender: ['gender', 'sex'],
  };

  // ---------- storage ----------

  const store = {
    get(key, fallback) {
      try {
        if (typeof GM_getValue === 'function') return GM_getValue(key, fallback);
        const v = localStorage.getItem('__idfill_' + key);
        return v == null ? fallback : JSON.parse(v);
      } catch (e) { return fallback; }
    },
    set(key, value) {
      try {
        if (typeof GM_setValue === 'function') return GM_setValue(key, value);
        localStorage.setItem('__idfill_' + key, JSON.stringify(value));
      } catch (e) { /* ignore */ }
    },
  };

  // ---------- identity loading ----------

  const norm = (s) => String(s || '').toLowerCase().replace(/[^a-z0-9]/g, '');

  function canonicalKey(rawKey) {
    const k = norm(rawKey);
    for (const [key, aliases] of Object.entries(KEY_ALIASES)) {
      if (aliases.includes(k)) return key;
    }
    return null;
  }

  function flatten(obj, out = {}) {
    for (const [k, v] of Object.entries(obj || {})) {
      if (v && typeof v === 'object' && !Array.isArray(v)) flatten(v, out);
      else if (v != null && !(k in out)) out[k] = v;
    }
    return out;
  }

  function fromPairs(pairs) {
    const profile = {};
    for (const [k, v] of pairs) {
      const key = canonicalKey(k);
      const value = String(v).trim();
      if (key && value && !(key in profile)) profile[key] = value;
    }
    if (!profile.firstName && !profile.lastName) {
      const full = pairs.find(([k]) => ['name', 'fullname'].includes(norm(k)));
      if (full) {
        const parts = String(full[1]).trim().split(/\s+/);
        profile.firstName = parts.shift();
        profile.lastName = parts.join(' ');
      }
    }
    return profile;
  }

  // Accepts JSON, an HTML page, or plain "Label: value" text.
  function parseIdentity(text) {
    text = String(text || '').trim();
    if (!text) return {};

    try {
      let data = JSON.parse(text);
      if (Array.isArray(data)) data = data[0];
      return fromPairs(Object.entries(flatten(data)));
    } catch (e) { /* not JSON */ }

    const pairs = [];
    if (/<[a-z][\s\S]*>/i.test(text)) {
      const doc = new DOMParser().parseFromString(text, 'text/html');
      doc.querySelectorAll('script,style').forEach((n) => n.remove());
      doc.querySelectorAll('input,textarea,select').forEach((el) => {
        const label = (el.id && doc.querySelector(`label[for="${CSS.escape(el.id)}"]`)) || el.closest('label');
        const name = (label && label.textContent) || el.getAttribute('name') || el.id || el.getAttribute('placeholder');
        const value = el.value || el.getAttribute('value') || '';
        if (name && value) pairs.push([name, value]);
      });
      // Leaf-text sequence: "First Name" followed by "Bob", or "First Name: Bob".
      const tokens = [];
      doc.body && doc.body.querySelectorAll('*').forEach((el) => {
        if (el.children.length === 0) {
          const t = el.textContent.trim();
          if (t) tokens.push(t);
        }
      });
      text = tokens.join('\n');
    }

    const lines = text.split(/\r?\n/).map((l) => l.trim()).filter(Boolean);
    for (let i = 0; i < lines.length; i++) {
      const m = lines[i].match(/^([^:=]{2,40})\s*[:=]\s*(.+)$/);
      if (m) {
        pairs.push([m[1], m[2]]);
      } else if (canonicalKey(lines[i].replace(/[:#]/g, '')) && lines[i + 1] && !canonicalKey(lines[i + 1])) {
        pairs.push([lines[i], lines[i + 1]]);
        i++;
      }
    }
    return fromPairs(pairs);
  }

  function request(url) {
    return new Promise((resolve, reject) => {
      if (typeof GM_xmlhttpRequest === 'function') {
        GM_xmlhttpRequest({
          method: 'GET', url, timeout: 4000,
          headers: { Accept: 'application/json, text/html;q=0.9, */*;q=0.8' },
          onload: (r) => (r.status >= 200 && r.status < 300 ? resolve(r.responseText) : reject(new Error('HTTP ' + r.status))),
          onerror: () => reject(new Error('network error')),
          ontimeout: () => reject(new Error('timeout')),
        });
      } else {
        fetch(url, { headers: { Accept: 'application/json' } })
          .then((r) => (r.ok ? r.text() : Promise.reject(new Error('HTTP ' + r.status))))
          .then(resolve, reject);
      }
    });
  }

  async function loadFromPanel() {
    const url = store.get('panelUrl', PANEL_URL);
    if (!url) throw new Error('no panel URL set');
    const base = url.replace(/\/+$/, '');
    let lastErr;
    for (const path of ['', '/api/identity', '/identity', '/api/generate', '/generate']) {
      try {
        const profile = parseIdentity(await request(base + path));
        if (Object.keys(profile).length >= 3) return profile;
      } catch (e) { lastErr = e; }
    }
    throw lastErr || new Error('no identity found at ' + url);
  }

  function getSavedProfile() {
    return Object.assign({}, DEFAULT_PROFILE, store.get('profile', {}));
  }

  // ---------- value helpers ----------

  function splitDob(profile) {
    let { dobMonth: m, dobDay: d, dobYear: y } = profile;
    if (profile.dob && !(m && d && y)) {
      const s = String(profile.dob).trim();
      let match;
      if ((match = s.match(/^(\d{4})[-/.](\d{1,2})[-/.](\d{1,2})/))) [, y, m, d] = match;
      else if ((match = s.match(/^(\d{1,2})[-/.](\d{1,2})[-/.](\d{4})/))) [, m, d, y] = match;
      else if ((match = s.match(/^([A-Za-z]+)\.?\s+(\d{1,2}),?\s+(\d{4})/))) [, m, d, y] = match;
      else if ((match = s.match(/^(\d{1,2})\s+([A-Za-z]+)\.?\s+(\d{4})/))) [, d, m, y] = match;
    }
    if (m && isNaN(m)) {
      const idx = MONTHS.findIndex((name) => name.toLowerCase().startsWith(String(m).toLowerCase().slice(0, 3)));
      m = idx >= 0 ? idx + 1 : m;
    }
    return { month: Number(m) || null, day: Number(d) || null, year: Number(y) || null };
  }

  const pad2 = (n) => String(n).padStart(2, '0');

  function monthVariants(n) {
    const name = MONTHS[n - 1];
    return [name, name.slice(0, 3), String(n), pad2(n)];
  }

  function provinceVariants(p) {
    const s = String(p || '').trim();
    const code = Object.keys(PROVINCES).find((c) => c === s.toUpperCase() || norm(PROVINCES[c]) === norm(s));
    return code ? [PROVINCES[code], code, PROVINCES[code].replace('Quebec', 'Québec')] : [s];
  }

  // ---------- DOM helpers ----------

  const isVisible = (el) => {
    const r = el.getBoundingClientRect();
    const cs = getComputedStyle(el);
    return r.width > 0 && r.height > 0 && cs.visibility !== 'hidden' && cs.display !== 'none';
  };

  function fieldDescriptor(el) {
    const parts = [];
    if (el.id) {
      const lbl = document.querySelector(`label[for="${CSS.escape(el.id)}"]`);
      if (lbl) parts.push(lbl.textContent);
    }
    const wrap = el.closest('label');
    if (wrap) parts.push(wrap.textContent);
    const lb = el.getAttribute('aria-labelledby');
    if (lb) lb.split(/\s+/).forEach((id) => { const n = document.getElementById(id); if (n) parts.push(n.textContent); });
    parts.push(el.getAttribute('aria-label'), el.getAttribute('placeholder'));
    // Outlined/floating labels (MUI, etc.) live in a sibling inside the same wrapper.
    if (!parts.some((p) => p && p.trim())) {
      let p = el.parentElement;
      for (let i = 0; i < 3 && p; i++, p = p.parentElement) {
        if (p.querySelectorAll('input,select,textarea').length > 1) break;
        const legend = p.querySelector('legend, label, [class*="label" i]');
        if (legend && legend.textContent.trim()) { parts.push(legend.textContent); break; }
      }
    }
    parts.push(el.getAttribute('name'), el.id, el.getAttribute('autocomplete'), el.getAttribute('data-testid'));
    return parts.filter(Boolean).join(' | ').toLowerCase().trim();
  }

  function matchField(el) {
    const auto = (el.getAttribute('autocomplete') || '').toLowerCase();
    const autoMap = {
      email: 'email', 'given-name': 'firstName', 'family-name': 'lastName',
      'street-address': 'street', 'address-line1': 'street', 'postal-code': 'postal',
      'address-level1': 'province', 'address-level2': 'city', tel: 'phone', 'tel-national': 'phone',
      'bday-month': 'dobMonth', 'bday-day': 'dobDay', 'bday-year': 'dobYear', bday: 'dob', sex: 'gender',
    };
    if (autoMap[auto]) return autoMap[auto];
    if (el.type === 'email') return 'email';
    if (el.type === 'tel') return 'phone';

    const desc = fieldDescriptor(el);
    if (!desc) return null;
    for (const piece of desc.split(' | ')) {
      const p = piece.trim();
      for (const key of ['dobMonth', 'dobDay', 'dobYear']) if (FIELD_PATTERNS[key].test(p)) return key;
    }
    for (const [key, re] of Object.entries(FIELD_PATTERNS)) {
      if (key.startsWith('dob') && key !== 'dob') continue;
      if (re.test(desc)) return key;
    }
    return null;
  }

  function setNativeValue(el, value) {
    const proto = el instanceof HTMLSelectElement ? HTMLSelectElement.prototype
      : el instanceof HTMLTextAreaElement ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
    const setter = Object.getOwnPropertyDescriptor(proto, 'value').set;
    el.focus();
    setter.call(el, value);
    el.dispatchEvent(new Event('input', { bubbles: true }));
    el.dispatchEvent(new Event('change', { bubbles: true }));
    el.dispatchEvent(new KeyboardEvent('keyup', { bubbles: true }));
    el.blur();
    el.dispatchEvent(new Event('focusout', { bubbles: true }));
  }

  function selectOption(select, variants) {
    const wanted = variants.map(norm).filter(Boolean);
    const opts = Array.from(select.options);
    const hit = opts.find((o) => wanted.includes(norm(o.textContent)) || wanted.includes(norm(o.value)))
      || opts.find((o) => wanted.some((w) => w.length > 2 && norm(o.textContent).startsWith(w)));
    if (!hit) return false;
    setNativeValue(select, hit.value);
    return true;
  }

  // Custom (non-<select>) dropdowns: open the field, then click the matching option.
  async function pickFromCustomList(el, variants) {
    const wanted = variants.map(norm);
    await new Promise((r) => setTimeout(r, 250));
    const opts = document.querySelectorAll('[role="option"], li[class*="option" i], div[class*="option" i]');
    for (const o of opts) {
      if (isVisible(o) && wanted.includes(norm(o.textContent))) {
        o.dispatchEvent(new MouseEvent('mousedown', { bubbles: true }));
        o.click();
        return true;
      }
    }
    return false;
  }

  async function fillValue(el, variants) {
    if (el instanceof HTMLSelectElement) return selectOption(el, variants);
    setNativeValue(el, variants[0]);
    if (el.getAttribute('role') === 'combobox' || el.getAttribute('aria-autocomplete') || el.getAttribute('list')) {
      el.focus();
      await pickFromCustomList(el, variants);
    }
    return true;
  }

  function clickChoice(text) {
    const want = norm(text);
    if (!want) return false;
    const radios = Array.from(document.querySelectorAll('input[type=radio]'));
    for (const r of radios) {
      if (norm(r.value) === want || norm(fieldDescriptor(r)).replace(/gender|sex/g, '') === want) {
        r.click();
        return true;
      }
    }
    const clickables = document.querySelectorAll('button, [role="button"], [role="radio"], label, a, div[class*="gender" i] > *');
    for (const c of clickables) {
      if (isVisible(c) && norm(c.textContent) === want) {
        c.click();
        return true;
      }
    }
    return false;
  }

  // Finds the Month/Day/Year trio under a "Date of Birth" heading when the fields have no labels.
  function findDobTrio() {
    const heading = Array.from(document.querySelectorAll('label, span, div, p, h1, h2, h3, h4, h5, h6, legend'))
      .find((n) => n.children.length === 0 && /date\s*of\s*birth|birth\s*date|birthday|\bdob\b/i.test(n.textContent));
    if (!heading) return null;
    let p = heading.parentElement;
    for (let i = 0; i < 5 && p; i++, p = p.parentElement) {
      const fields = Array.from(p.querySelectorAll('input:not([type=hidden]), select')).filter(isVisible);
      if (fields.length >= 3) return fields.slice(0, 3);
    }
    return null;
  }

  function guessDobPart(el) {
    if (!(el instanceof HTMLSelectElement)) return null;
    const texts = Array.from(el.options).map((o) => o.textContent.trim());
    if (texts.some((t) => /^(jan|feb|mar)/i.test(t))) return 'dobMonth';
    if (texts.some((t) => /^(19|20)\d\d$/.test(t))) return 'dobYear';
    if (texts.some((t) => t === '31')) return 'dobDay';
    return null;
  }

  // ---------- main fill ----------

  async function fill(profile) {
    const { month, day, year } = splitDob(profile);
    const phoneDigits = String(profile.phone || '').replace(/\D/g, '').replace(/^1(?=\d{10}$)/, '');
    const values = {
      email: [profile.email],
      firstName: [profile.firstName],
      lastName: [profile.lastName],
      street: [profile.street],
      postal: [String(profile.postal || '').toUpperCase()],
      province: provinceVariants(profile.province),
      city: [profile.city],
      phone: [profile.phone, phoneDigits],
      dobMonth: month ? monthVariants(month) : [],
      dobDay: day ? [pad2(day), String(day)] : [],
      dobYear: year ? [String(year)] : [],
      dob: month && day && year ? [`${year}-${pad2(month)}-${pad2(day)}`, `${pad2(month)}/${pad2(day)}/${year}`] : [],
    };

    const fields = Array.from(document.querySelectorAll('input, select, textarea')).filter((el) =>
      isVisible(el) && !el.disabled && !el.readOnly &&
      !['hidden', 'submit', 'button', 'checkbox', 'radio', 'file', 'image', 'reset', 'password'].includes(el.type));

    const assigned = new Map();
    for (const el of fields) {
      const key = matchField(el) || guessDobPart(el);
      if (key) assigned.set(el, key);
    }
    const trio = findDobTrio();
    if (trio) ['dobMonth', 'dobDay', 'dobYear'].forEach((k, i) => {
      if (!assigned.has(trio[i]) || assigned.get(trio[i]) === 'dob') assigned.set(trio[i], k);
    });

    let filled = 0;
    for (const [el, key] of assigned) {
      let v = (values[key] || []).filter((x) => x != null && x !== '');
      if (!v.length) continue;
      if (key === 'dob' && el.type === 'date') v = [v[0]];
      else if (key === 'dob') v = [v[1]];
      if (key === 'phone' && !(el instanceof HTMLSelectElement)) {
        const max = Number(el.getAttribute('maxlength')) || 0;
        // Masked phone inputs usually want raw digits.
        if ((max && max < v[0].length) || /mask|\(|_/.test(el.getAttribute('placeholder') || '')) v = [phoneDigits];
      }
      if (key === 'dobMonth' && !(el instanceof HTMLSelectElement)) {
        const hint = (el.getAttribute('placeholder') || '') + (el.getAttribute('maxlength') || '') + el.type;
        // Numeric-looking inputs get "03", text inputs get "MAR".
        v = /mm|2|number/i.test(hint) ? [pad2(month)] : [MONTHS[month - 1].slice(0, 3).toUpperCase()].concat(monthVariants(month));
      }
      if (await fillValue(el, v)) filled++;
    }

    if (profile.gender && clickChoice(profile.gender)) filled++;
    return filled;
  }

  // ---------- UI ----------

  function toast(msg) {
    const t = document.createElement('div');
    t.textContent = msg;
    Object.assign(t.style, {
      position: 'fixed', right: '20px', bottom: '80px', zIndex: 2147483647, padding: '8px 12px',
      background: '#222', color: '#fff', font: '13px/1.4 system-ui, sans-serif', borderRadius: '6px',
      boxShadow: '0 2px 10px rgba(0,0,0,.3)', maxWidth: '320px',
    });
    document.body.appendChild(t);
    setTimeout(() => t.remove(), 3500);
  }

  function editProfile() {
    const current = getSavedProfile();
    const text = prompt(
      'Paste an identity (JSON, or "Label: value" lines copied from the panel).\n' +
      'Leave empty to reset to the default profile.',
      JSON.stringify(current));
    if (text === null) return;
    if (!text.trim()) { store.set('profile', {}); toast('Profile reset to default'); return; }
    const parsed = parseIdentity(text);
    if (Object.keys(parsed).length === 0) { toast('Could not read any fields from that text'); return; }
    store.set('profile', parsed);
    toast('Saved ' + Object.keys(parsed).length + ' fields');
  }

  function setPanelUrl() {
    const url = prompt('Canada Identity Panel URL (empty = only use saved profile):', store.get('panelUrl', PANEL_URL));
    if (url !== null) store.set('panelUrl', url.trim());
  }

  async function onFill(btn) {
    btn.disabled = true;
    btn.textContent = '…';
    let profile;
    let source = 'saved profile';
    try {
      profile = Object.assign(getSavedProfile(), await loadFromPanel());
      source = 'panel';
    } catch (e) {
      profile = getSavedProfile();
    }
    try {
      const n = await fill(profile);
      toast(n ? `Filled ${n} field${n === 1 ? '' : 's'} from ${source}` : 'No matching fields found on this page');
    } catch (e) {
      toast('Fill failed: ' + e.message);
    }
    btn.disabled = false;
    btn.textContent = 'Fill';
  }

  function mountButton() {
    if (!document.body || document.getElementById('__idfill_root')) return;
    const root = document.createElement('div');
    root.id = '__idfill_root';
    Object.assign(root.style, {
      position: 'fixed', right: '20px', bottom: '20px', zIndex: 2147483647, display: 'flex', gap: '6px',
    });

    const mk = (label, title, style) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.textContent = label;
      b.title = title;
      Object.assign(b.style, {
        border: 'none', borderRadius: '8px', cursor: 'pointer', color: '#fff',
        font: '600 15px system-ui, sans-serif', boxShadow: '0 2px 10px rgba(0,0,0,.25)',
      }, style);
      return b;
    };

    const fillBtn = mk('Fill', 'Autofill this form', { background: '#76d600', padding: '12px 22px' });
    const editBtn = mk('✎', 'Edit saved profile', { background: '#555', padding: '12px 12px' });
    const urlBtn = mk('⚙', 'Set identity panel URL', { background: '#555', padding: '12px 12px' });

    fillBtn.addEventListener('click', () => onFill(fillBtn));
    editBtn.addEventListener('click', editProfile);
    urlBtn.addEventListener('click', setPanelUrl);

    root.append(editBtn, urlBtn, fillBtn);
    document.body.appendChild(root);
  }

  mountButton();
  // Single-page apps can wipe the body; put the button back if that happens.
  new MutationObserver(mountButton).observe(document.documentElement, { childList: true, subtree: false });
  setInterval(mountButton, 2000);
})();
