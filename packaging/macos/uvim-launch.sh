#!/bin/bash
# Opens uVim (/usr/local/bin/uvim) in its own terminal window, used by
# uVim.app. Alacritty is used when installed, otherwise Terminal. Override:
#   defaults write io.github.mattilaa.uvim.app Terminal -string alacritty
#   defaults write io.github.mattilaa.uvim.app Terminal -string terminal
#   defaults delete io.github.mattilaa.uvim.app Terminal   # back to auto
# uVim runs in the user's login shell (so PATH from the shell profile
# applies) and the window stays at a shell prompt after quitting uVim.
# Arguments are files to open. UVIM_LAUNCH_DRY_RUN=1 prints instead of
# opening a window.
set -euo pipefail

uvim=/usr/local/bin/uvim
[[ -x "$uvim" ]] || uvim="$(command -v uvim || echo uvim)"

user_shell="$(dscl . -read "/Users/$(id -un)" UserShell 2>/dev/null |
              awk '{ print $2 }')"
[[ -n "$user_shell" && -x "$user_shell" ]] || user_shell=/bin/zsh

find_alacritty() {
  local app
  for app in /Applications/Alacritty.app "$HOME/Applications/Alacritty.app"; do
    [[ -d "$app" ]] && { echo "$app"; return; }
  done
  mdfind "kMDItemCFBundleIdentifier == 'org.alacritty'" 2>/dev/null | head -n 1
}

choice="$(defaults read io.github.mattilaa.uvim.app Terminal 2>/dev/null || true)"
alacritty=""
if [[ "$choice" != "terminal" ]]; then
  alacritty="$(find_alacritty)"
fi

# The command file runs uVim with the given files, then the login shell.
command_file="$(mktemp "${TMPDIR:-/tmp}/uvim-XXXXXX")"
mv "$command_file" "$command_file.command"
command_file="$command_file.command"
{
  printf '#!/bin/bash\n'
  printf 'rm -f %q\n' "$command_file"
  printf 'cd ~\n'
  printf '%q -lc %q' "$user_shell" "$(printf '%q ' "$uvim" "$@")"
  printf '\nexec %q -l\n' "$user_shell"
} > "$command_file"
chmod 0700 "$command_file"

if [[ -n "$alacritty" ]]; then
  cmd=(open -na "$alacritty" --args -e /bin/bash "$command_file")
else
  cmd=(open -a Terminal "$command_file")
fi

if [[ "${UVIM_LAUNCH_DRY_RUN:-}" == 1 ]]; then
  printf 'launch:'; printf ' %q' "${cmd[@]}"; printf '\n'
  cat "$command_file"
  rm -f "$command_file"
  exit 0
fi
"${cmd[@]}"
