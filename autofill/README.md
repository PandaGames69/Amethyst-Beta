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
