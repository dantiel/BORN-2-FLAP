# Build config: iOS (arm64-apple-ios) static libmruby.a for the in-process
# Ruby Brain. Mirrors the Haskell libborn2flap_math-ios.a pattern in
# Born2Flap.Build.cs (dlopen is forbidden on iOS -> everything statically
# linked into the app binary).
#
# Two targets:
#   * 'host'       — macOS build, produces the mrbc bytecode compiler (runs on
#                    the Mac; the cross build uses it to compile the gems'
#                    mrblib .rb to bytecode during the build). Only mrbc is
#                    needed here, not the host libmruby.
#   * 'ios-arm64'  — the static libmruby.a (json + onig-regexp + require) that
#                    gets linked into the Unreal iOS app.
#
# Requires the cross-compiled oniguruma from Tools/build-onig-ios.sh (arm64
# libonig in ThirdParty/oniguruma/dist). The gem order is load-bearing:
# json + onig-regexp MUST precede mruby-require (its mrbgem.rake reject!s any
# gem added after it, bundling them as .so — impossible on iOS).
#
# Run from the mruby tree:
#   cd ThirdParty/mruby && MRUBY_CONFIG=../build_config_ios.rb rake
#
# Output:
#   ThirdParty/mruby/build/ios-arm64/lib/libmruby.a   (arm64-apple-ios)

# --- host: mrbc only ------------------------------------------------------
# Minimal host: only the mrbc bytecode compiler. Mirrors the host that
# CrossBuild auto-adds (toolchain + build_mrbc_exec + disable_libmruby), which
# is the smallest recipe that yields a working mrbc without pulling the
# mruby-bin-* executables (mruby-bin-config needs libmruby.flags.mak, which
# disable_libmruby omits -> build breaks under `gembox 'default'`).
MRuby::Build.new('host') do |conf|
  toolchain :clang
  conf.build_mrbc_exec
  conf.disable_libmruby
  conf.disable_presym
end

# --- cross: arm64-apple-ios static libmruby.a -----------------------------
MRuby::CrossBuild.new('ios-arm64') do |conf|
  toolchain :clang

  # iOS oniguruma (Tools/build-onig-ios.sh). Takes precedence in
  # mruby-onig-regexp's mrbgem.rake so the cross build links arm64 libonig,
  # not the macOS host libonig.
  ENV['B2F_ONIG_DIR'] = File.expand_path('../oniguruma/dist', __FILE__)

  sdk = `xcrun --sdk iphoneos --show-sdk-path`.strip
  min_ios = ENV['B2F_MIN_IOS'] || '15.0'
  ios_flags = ['-arch arm64', '-target arm64-apple-ios',
               "-isysroot #{sdk}", "-miphoneos-version-min=#{min_ios}"]

  [conf.cc, conf.objc, conf.asm].each do |cc|
    cc.command = 'xcrun -sdk iphoneos clang'
    cc.flags << ios_flags
  end
  conf.cxx.command = 'xcrun -sdk iphoneos clang++'
  conf.cxx.flags << ios_flags

  conf.linker.command = 'xcrun -sdk iphoneos clang'
  conf.linker.flags << ios_flags
  conf.archiver.command = 'xcrun -sdk iphoneos ar'

  # Same source set as the host build, minus the mruby-bin-* executables
  # (which would require a cross-link and can't run on-device anyway).
  conf.gembox 'stdlib'
  conf.gembox 'stdlib-ext'
  conf.gembox 'stdlib-io'
  conf.gembox 'math'
  conf.gembox 'metaprog'

  # ORDER MATTERS: json + onig-regexp before require (see header note).
  conf.gem File.expand_path('../mruby-json', __FILE__)
  conf.gem File.expand_path('../mruby-onig-regexp', __FILE__)
  conf.gem File.expand_path('../mruby-require', __FILE__)

  conf.cc.flags << '-O2'
  conf.disable_presym
end