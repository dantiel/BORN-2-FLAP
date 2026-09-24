# Windows remote control (same-room)

Goal: drive the Windows box from the Mac — the human through the GUI, the AI
through the CLI.

## Architecture

| Channel | Tool | Who uses it |
|---|---|---|
| GUI (UE editor, viewport, level editing) | Parsec | human |
| CLI (build / test / git on Windows) | OpenSSH Server | AI |
| See GUI results | screenshot of the Parsec window on the Mac | AI (`take_screenshot`) |

Parsec streams the GPU output to the human. The AI cannot click or type, so it
acts exclusively through SSH shell commands. The only way the AI "sees" the
Windows GUI is by screenshotting the Mac display while the Parsec client is
showing the Windows screen.

## Windows side (run once, admin PowerShell)

Enable the built-in OpenSSH Server and set PowerShell as the default shell:

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service -Name sshd -StartupType Automatic
New-ItemProperty -Path "HKLM:\SOFTWARE\OpenSSH" `
  -Name DefaultShell -Value "C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe" -Force
```

Install the Mac's public key (copy `~/.ssh/id_ed25519.pub` from the Mac first):

```powershell
$key = "<paste the public key line here>"
$dir = "$env:USERPROFILE\.ssh"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
Add-Content -Path "$dir\authorized_keys" -Value $key
# non-admin account: the file above is enough
# admin account: use administrators_authorized_keys instead
icacls "$dir\authorized_keys" /inheritance:r /grant "$env:USERNAME:F"
```

### PATH for build tools

SSH non-interactive sessions get a minimal PATH. Source the toolchain in the
user PowerShell profile (`$PROFILE`) so `ghc`, `cabal`, `cmake`, `ninja` resolve:

```powershell
$env:CABAL_DIR = "V:\Born2FlapTools\cabal"
$env:PATH = "V:\Born2FlapTools\ghcup\bin;V:\Born2FlapTools\cabal\bin;" + $env:PATH
```

## Mac side

Keypair lives at `~/.ssh/id_ed25519` / `id_ed25519.pub` (generated once).

Test the bridge:

```bash
ssh <user>@<windows-ip> "echo OK from Windows"
```

Then the AI drives the Windows box directly, e.g.:

```bash
ssh <user>@<windows-ip> "cd /d V:\Born2Flap && cabal test"
```

## Same-room advantages

- Wired gigabit LAN: ~1 ms latency, no internet dependency, no port-forwarding.
- Key install can be done in person.
- Human can babysit GUI-only prompts (UAC, UE rebuild dialogs) that SSH cannot.
- Human can skip Parsec for themselves and just use the Windows monitor; Parsec's
  remaining value is giving the AI a screenshot-able view of the screen.
