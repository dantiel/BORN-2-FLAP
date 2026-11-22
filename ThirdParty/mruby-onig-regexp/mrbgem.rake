MRuby::Gem::Specification.new('mruby-onig-regexp') do |spec|
  spec.license = 'MIT'
  spec.authors = 'mattn'
  spec.add_dependency 'mruby-string-ext', core: 'mruby-string-ext'

  # NOTE: the upstream gem's fallback `def spec.bundle_onigmo` defines a
  # SINGLETON method on the spec, which breaks mruby's create_mrbc_build
  # (it Marshals the build config -> "singleton can't be dumped"). We link
  # against the system oniguruma/onigmo via pkg-config instead, so the
  # autotools bundle fallback is unnecessary here. Find either engine:

  # B2F_ONIG_DIR: cross-compiled oniguruma prefix (Tools/build-onig-ios.sh).
  # Takes precedence so the iOS cross-build links the arm64 archive, not the
  # macOS host libonig. Set only inside the cross build block.
  if (dir = ENV['B2F_ONIG_DIR']) && !dir.empty?
    spec.cc.include_paths << File.join(dir, 'include')
    spec.linker.library_paths << File.join(dir, 'lib')
    spec.cc.defines += ['HAVE_ONIGURUMA_H']
    spec.linker.libraries << 'onig'
  elsif spec.respond_to?(:search_package) && spec.search_package('onigmo')
    spec.cc.defines += ['HAVE_ONIGMO_H']
    spec.linker.libraries << 'onigmo'
  elsif spec.respond_to?(:search_package) && spec.search_package('oniguruma')
    spec.cc.defines += ['HAVE_ONIGURUMA_H']
    spec.linker.libraries << 'onig'
  elsif build.cc.respond_to?(:search_header_path) && build.cc.search_header_path('onigmo.h')
    spec.cc.defines += ['HAVE_ONIGMO_H']
    spec.linker.libraries << 'onigmo'
  elsif build.cc.respond_to?(:search_header_path) && build.cc.search_header_path('oniguruma.h')
    spec.cc.defines += ['HAVE_ONIGURUMA_H']
    spec.linker.libraries << 'onig'
  else
    fail 'mruby-onig-regexp: onigmo/oniguruma not found (install oniguruma or onigmo)'
  end
end
