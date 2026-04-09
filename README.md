# Moon-Apps

External apps for [Moon Firmware](https://github.com/KaraZajac/Moon-Firmware).

Forked from [Momentum-Apps](https://github.com/Next-Flip/Momentum-Apps) with modifications for Moon Firmware's XIP flash execution architecture.

---

### What's Different from Momentum-Apps?

Moon Firmware runs all user-facing applications as external FAPs loaded via XIP (execute-in-place) from flash. Apps that use SubGhz or NFC device APIs bundle those libraries directly as `fap_private_libs`, making them fully self-contained — no firmware-exported symbols required.

**Apps modified for Moon:**
- **radio_scanner** — bundled `lib/subghz` + device API table
- **tanks** — bundled `lib/subghz` + device API table
- **subghz_playlist** — bundled `lib/subghz` + device API table
- **rolling_flaws** — bundled `lib/subghz` + device API table
- **esubghz_chat** — bundled `lib/subghz` + device API table

### Adding SubGhz Support to an App

To make an external app self-contained with SubGhz device access:

1. Create a library symlink:
   ```
   mkdir -p your_app/lib
   ln -s ../../../../lib/subghz your_app/lib/subghz
   ```

2. Copy the SubGhz API table from the main app:
   ```
   mkdir -p your_app/api
   cp applications/main/subghz/api/subghz_app_api_table.cpp your_app/api/
   cp applications/main/subghz/api/subghz_app_api_table_i.h your_app/api/
   cp applications/main/subghz/api/subghz_app_api_interface.h your_app/api/
   ```

3. Update `application.fam`:
   ```python
   App(
       ...
       fap_private_libs=[Lib(name="subghz")],
       ...
   )
   ```

---

### Base

Forked from [Momentum-Apps](https://github.com/Next-Flip/Momentum-Apps). All original Momentum app modifications, asset pack support, and community apps are preserved.
