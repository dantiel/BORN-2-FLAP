# frozen_string_literal: true
#
# mruby compatibility shim — adapts the Born2Flap Brain (written against MRI)
# to mruby's smaller standard library. Loaded BEFORE the Brain (see
# headless.rb / the C embed harness). Every entry here is a faithful
# reimplementation of the MRI feature the Brain relies on but mruby lacks.
#
# Gaps covered:
#   1. `require "json"` / `require "set"`  — both are compiled INTO mruby
#      (mruby-json / mruby-set), there is no json.rb/set.rb to load.
#   2. `require_relative`                  — mruby-require does not provide it.
#   3. `Struct.new(..., keyword_init: true)` — mruby-struct is positional-only.
#   4. `File.read(path, encoding: "UTF-8")` — mruby-io rejects the kwarg.
#   5. `File.mtime`                        — mruby-io has no stat/mtime.
#   6. `Dir[glob]`                         — mruby-io has no glob.

# --- 1) module_function (mruby's is a no-op) -------------------------------
# The Brain exposes module-level helpers via `module_function` (Dashboard.build,
# I18n.t, ...). mruby's Module#module_function is a stub, so emulate the
# semantics with `extend self` (makes every instance method a module method).
class Module
  def module_function(*_names)
    extend(self)
    self
  end
end

# --- 2) require / require_relative -----------------------------------------
module Kernel
  BUILTIN_FEATURES = %w[json set time].freeze

  unless method_defined?(:__b2f_require)
    alias_method :__b2f_require, :require
  end

  def require(feature)
    return false if BUILTIN_FEATURES.include?(feature.to_s)
    __b2f_require(feature)
  end

  def require_relative(path)
    # caller.first is "file:line" or "file:line:in method"; keep the file part.
    base = __caller_file__(1)
    require File.expand_path(path, File.dirname(base))
  end

  # __dir__ is a keyword in MRI but absent from mruby. Reimplement via caller.
  def __dir__
    File.dirname(__caller_file__(1))
  end

  private

  def __caller_file__(depth = 1)
    loc = caller[depth].to_s
    i = loc.rindex(":in ")
    loc = loc[0, i] if i
    i = loc.rindex(":")
    loc = loc[0, i] if i
    loc.empty? ? File.expand_path(__FILE__) : loc
  end
end

# --- 2) Struct keyword_init ------------------------------------------------
class Struct
  class << self
    # (re)loading compat.rb re-runs this block; guard the alias so a second
    # load does not re-alias `new` onto the shim (same recursion hazard as the
    # File.read shim below).
    unless method_defined?(:__b2f_struct_new)
      alias_method :__b2f_struct_new, :new
    end

    def new(*members, keyword_init: false, &blk)
      members = members.flatten
      names = members.map(&:to_sym)

      klass = Class.new do
        define_singleton_method(:members) { names }

        define_method(:initialize) do |*args, **kwargs|
          if keyword_init || (!args.empty? ? false : !kwargs.empty?)
            names.each { |n| instance_variable_set(:"@#{n}", kwargs[n]) }
          else
            names.each_with_index { |n, i| instance_variable_set(:"@#{n}", args[i]) }
          end
        end

        names.each do |n|
          define_method(n) { instance_variable_get(:"@#{n}") }
          define_method(:"#{n}=") { |v| instance_variable_set(:"@#{n}", v) }
        end

        define_method(:[])  { |k| instance_variable_get(:"@#{k.to_sym}") }
        define_method(:[]=) { |k, v| instance_variable_set(:"@#{k.to_sym}", v) }
        define_method(:to_h) { names.each_with_object({}) { |n, h| h[n] = instance_variable_get(:"@#{n}") } }
        define_method(:deconstruct_keys) { |_keys| to_h }
        define_method(:==) do |other|
          other.is_a?(self.class) && names.all? { |n| instance_variable_get(:"@#{n}") == other[n] }
        end
        define_method(:inspect) do
          body = names.map { |n| "#{n}=#{instance_variable_get(:"@#{n}").inspect}" }.join(", ")
          "#<struct #{body}>"
        end
      end

      klass.class_eval(&blk) if blk
      klass
    end
  end
end

# --- 3) File.read(path, encoding: ...) / File.mtime ------------------------
class File
  class << self
    # Idempotency guard: a double-load (the C host loads compat.rb AND
    # embed_host.rb re-loads it) would otherwise alias __b2f_read onto the
    # shim itself and make File.read recurse until SystemStackError.
    unless method_defined?(:__b2f_read)
      alias_method :__b2f_read, :read
    end

    def read(*args, **opts)
      __b2f_read(*args) # drop keyword opts (encoding:) — mruby-io reads UTF-8 natively
    end

    # mruby-io has no File.stat/File.mtime. Templates ship read-only in the
    # app bundle, so a constant mtime is faithful (the view-controller cache
    # simply never re-reads). A real embed would expose stat() via C.
    def mtime(_path)
      Time.at(0)
    end
  end
end

# --- 4) Dir[...] glob -------------------------------------------------------
class Dir
  class << self
    def [](pattern)
      idx = pattern.index("/*")
      return (File.exist?(pattern) ? [pattern] : []) unless idx

      base = pattern[0, idx]
      suffix = pattern[(idx + 2)..-1].to_s
      return [] unless File.directory?(base)

      match = suffix
      match = match[1, match.length - 1] if match.start_with?("*")
      match = match[0, match.length - 1] if match.end_with?("*")
      Dir.entries(base)
         .reject { |e| e == "." || e == ".." }
         .select { |e| e.end_with?(match) }
         .map { |e| File.join(base, e) }
    end
  end
end