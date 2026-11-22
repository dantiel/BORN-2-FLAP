# Build config: host (macOS) mruby for the in-process Ruby Brain PoC.
#
# This is the *host* build used to prove the 3173-line Born2Flap Brain runs
# under mruby (vs. MRI). A separate build_config_ios.rb cross-compiles the same
# source set to a static arm64-apple-ios archive (mirroring the Haskell
# libborn2flap_math-ios.a pattern in Born2Flap.Build.cs).
#
# Run from the mruby tree:
#   cd ThirdParty/mruby && MRUBY_CONFIG=../build_config_host.rb rake

MRuby::Build.new do |conf|
  toolchain :clang

  # default gembox pulls: core + stdlib + stdlib-ext + stdlib-io + math + metaprog
  #   stdlib        -> mruby-set, mruby-enumerator, mruby-fiber, String/Array/Hash/
  #                    Symbol/Object/Range/Proc/Numeric ext, ...
  #   stdlib-ext    -> mruby-struct, mruby-time, mruby-random, mruby-pack, sprintf
  #   stdlib-io     -> mruby-io, mruby-socket, mruby-print, mruby-errno, mruby-dir
  #   math          -> mruby-math, rational, complex, bigint
  #   metaprog      -> mruby-metaprog, mruby-method, mruby-eval, mruby-binding
  conf.gembox 'default'

  # External gems (cloned into ThirdParty/, local paths so the build is
  # reproducible without the mgem network fetch).
  #
  # ORDER MATTERS: mruby-require's mrbgem.rake `reject!`s every gem added AFTER
  # it (it "bundles" them as separate .so files). So any gem we want compiled
  # STATICALLY into libmruby.a must be added BEFORE mruby-require.
  conf.gem File.expand_path('../mruby-json', __FILE__)
  conf.gem File.expand_path('../mruby-onig-regexp', __FILE__)
  conf.gem File.expand_path('../mruby-require', __FILE__)

  conf.cc.flags << '-O2'

  # Avoid create_mrbc_build (Marshal-based) which builds each gem as a .so and
  # breaks on complex gems (mruby-onig-regexp). We only need the in-process
  # compiler library (mruby-compiler), not the standalone mrbc gem .so builds.
  conf.disable_presym

  # Debug info + backtraces for the compatibility iteration (host only).
  conf.enable_debug
end