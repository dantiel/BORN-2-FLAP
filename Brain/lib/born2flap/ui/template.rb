# frozen_string_literal: true

module Born2Flap
  module UI
    # Template — Ruby interpolation sugar for `.umghaml` views.
    #
    # A `.umghaml` template is the *semantic structure* of a screen; dynamic
    # values are written as `#{...}` Ruby expressions. This module evaluates
    # each expression against a locals hash, exposing each key as a method on
    # the evaluation context (a la Rails views / ERB, but without a template
    # engine — it's just Ruby sugar).
    #
    #   Template.interpolate("%Stat{ value: #{altitude} }", altitude: 42)
    #   # => "%Stat{ value: 42 }"
    #
    # The context is a tiny method-missing facade: unknown keys read as `nil`
    # rather than raising, so templates may use `#{speed || 0}` for defaults.
    module Template
      module_function

      # Evaluate `#{...}` expressions against `locals`. Returns the source
      # unchanged when there is nothing to interpolate (fast path). Each
      # expression is evaluated independently, so no string-literal escaping is
      # needed and injected values (multi-line fragments, arrays) stay intact.
      def interpolate(source, locals = {})
        return source unless source.include?('#{')

        context = Context.new(locals)
        source.gsub(INTERPOLATION) { context.instance_eval(Regexp.last_match(1)) }
      end

      INTERPOLATION = /#\{(.*?)\}/m

      # The evaluation context: keys become methods; unknown keys are `nil`.
      class Context
        def initialize(locals)
          @locals = locals
          locals.each_key do |key|
            define_singleton_method(key) { @locals[key] }
          end
        end

        def [](key)
          @locals[key]
        end

        def fetch(key, default = nil)
          @locals.fetch(key, default)
        end

        def locals
          @locals
        end

        def method_missing(name, *args, &block)
          return @locals[name] if @locals.key?(name)
          return nil if args.empty? && block.nil?

          super
        end

        def respond_to_missing?(name, include_private = false)
          @locals.key?(name) || super
        end
      end
    end
  end
end