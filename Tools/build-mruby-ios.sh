#!/usr/bin/env bash
# Cross-compile the in-process Ruby Brain runtime (mruby) to an iOS static
# library. Mirrors Tools/build-math-ios.sh (Haskell -> libborn2flap_math-ios.a).
#
# Produces ThirdParty/mruby/build-ios/ios-arm64/lib/libmruby.a — a self-contained
# arm64-apple-ios archive bundling mruby core + stdlib + the three external gems
# the Brain needs (mruby-json, mruby-onig-regexp, mruby-require). The regexp
# engine is the cross-compiled oniguruma from Tools/build-onig-ios.sh.
#
# LINK REQUIREMENT: when this archive is linked into the iOS app, also link
# ThirdParty/oniguruma/dist/lib/libonig.a and add its include path (the gem
# compiles mruby_onig_regexp.c against oniguruma.h). Born2Flap.Build.cs adds
# both, mirroring the Haskell libiconv pattern.
#
# The build is deliberately isolated into build-ios/ (via MRUBY_BUILD_DIR) so it
# never clobbers the host PoC build in build/host/.
#
# Usage:
#   Tools/build-mruby-ios.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# 1. arm64 oniguruma (idempotent — no-op if already built).
"$REPO_ROOT/Tools/build-onig-ios.sh"

# 2. mruby cross-compile.
cd "$REPO_ROOT/ThirdParty/mruby"

# Clean environment: the TextMate shell sets RUBYOPT=-rbundler/setup and a
# TextMate GEM_HOME which silently break mruby's rake (bundler hijacks it).
# Use the rvm ruby 3.1.7 rake directly with a pristine env (but keep rvm's
# own bin on PATH so the ruby_executable_hooks wrapper resolves, and /usr/sbin
# so sysctl resolves).
export PATH="/Users/d/.rvm/rubies/ruby-3.1.7/bin:/usr/local/bin:/usr/bin:/usr/sbin:/bin"
unset RUBYOPT RUBYLIB BUNDLE_GEMFILE BUNDLE_PATH 2>/dev/null || true
export GEM_HOME="/Users/d/.rvm/gems/ruby-3.1.7"
export GEM_PATH="/Users/d/.rvm/gems/ruby-3.1.7:/Users/d/.rvm/gems/ruby-3.1.7@global"

export MRUBY_CONFIG="$REPO_ROOT/ThirdParty/build_config_ios.rb"
export MRUBY_BUILD_DIR="$REPO_ROOT/ThirdParty/mruby/build-ios"
export B2F_MIN_IOS="${B2F_MIN_IOS:-15.0}"

RAKE="/Users/d/.rvm/rubies/ruby-3.1.7/bin/rake"

echo "== mruby cross-compile -> arm64-apple-ios =="
echo "   MRUBY_CONFIG: $MRUBY_CONFIG"
echo "   MRUBY_BUILD_DIR: $MRUBY_BUILD_DIR"

# Clean slate: a stale gem_init.c silently drops gems (only registers the
# first), which is how the host build lost mruby_onig_regexp before.
rm -rf "$MRUBY_BUILD_DIR"

"$RAKE" -j"$(sysctl -n hw.ncpu)"

echo "== Done =="
ls -la "$MRUBY_BUILD_DIR/ios-arm64/lib/libmruby.a"
xcrun --sdk iphoneos lipo -info "$MRUBY_BUILD_DIR/ios-arm64/lib/libmruby.a"