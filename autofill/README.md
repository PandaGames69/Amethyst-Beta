# Identity Autofill

A userscript that adds a floating **Fill** button (bottom-right of every page). Clicking it fills sign-up forms: email, first/last name, street address, postal code, province, municipality/city, mobile phone, date of birth (month/day/year) and gender.

## Install

1. Install [Tampermonkey](https://www.tampermonkey.net/) (or Violentmonkey).
2. Create a new script, paste in `identity-autofill.user.js`, and save.

## Buttons

- **Fill**: pulls an identity from the Canada Identity Panel (`http://localhost:8085` by default) and fills the form. If the panel can't be reached, it uses the saved profile.
- **✎**: edit the saved profile. Paste JSON or `Label: value` lines copied from the panel.
- **⚙**: change the panel URL, or clear it to always use the saved profile.

The panel response can be JSON (`{"firstName": "...", "postalCode": "...", ...}`), an HTML page, or plain `Label: value` text. Common key names (`first_name`, `postal`, `zip`, `mobile`, `dob`, ...) are recognised.

## Windows app (no browser extension needed)

`windows/IdentityAutofill.exe` is a small always-on-top window with a **Fill** button.

1. Run `IdentityAutofill.exe`. It creates `identity.txt` next to itself the first time.
2. Click the first box of the form (the email box).
3. Press **Fill** (or **F9**). It types each value from `identity.txt` in order, pressing Tab between them. Press **Esc** to stop.

Press **Edit** to change the details in Notepad. Lines are typed top to bottom, so they must match the form's Tab order. You can use `{tab}`, `{space}`, `{enter}`, `{skip}` and `{wait}` in a value; for example `gender = {tab}{space}` picks Female instead of Male.

Rebuild from source with MinGW:

    x86_64-w64-mingw32-gcc -O2 -municode -mwindows -s windows/autofill.c -o windows/IdentityAutofill.exe -luser32 -lgdi32 -lshell32
