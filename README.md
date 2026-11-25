# euilscrr-watcher

Lightweight native C++ Windows desktop scrobbler for [euilscrr](https://euilscrr.vercel.app).

- Reads Spotify Desktop playback via Windows SMTC (`Windows.Media.Control`) — works with **Spotify Free**, no Spotify Premium or Developer API keys needed.
- Auto-scrobbles at 50% duration or 4 minutes (30s fallback), handles pause/resume/seek/repeat-one, and skips Spotify Free ads.
- Zero runtime dependencies, portable single `.exe`, system tray support, optional `Start with Windows`.

## Usage

1. Download `euilscrr-watcher.exe` from **Releases**.
2. Generate your personal `Scrobble Token` in the **Watcher** tab on euilscrr.
3. Open `euilscrr-watcher.exe`, paste your token, and click **Save**.

## Build from source (Windows 10/11 + MSVC Build Tools)

```powershell
.\build.ps1
```

Override API endpoint at build time or runtime (for custom domains or local testing):

```powershell
.\build.ps1 -ApiUrl "https://your-domain.com/api/watcher"
# or runtime override:
$env:EUILSCRR_API_URL = "http://127.0.0.1:3000/api/watcher"
.\dist\euilscrr-watcher.exe
```
