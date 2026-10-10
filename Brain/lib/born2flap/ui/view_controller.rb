# frozen_string_literal: true

module Born2Flap
  module UI
    # ViewController — the Ruby-side counterpart of a UI controller that owns a
    # directory of views. Each *view* is a `.umghaml` template (semantics only)
    # plus an optional `.umgss` theme (style tokens, SASS-like). One controller
    # may expose several named views ("endpoints") from the same directory.
    #
    #   class FlightHud < ViewController
    #     view :cockpit, template: "cockpit.umghaml", theme: "cockpit.umgss"
    #     view :minimal, template: "minimal.umghaml"
    #
    #     # Ruby sugar: resolve locals (defaults/derived values) for :cockpit.
    #     def cockpit_locals(locals = {})
    #       { altitude: locals[:altitude] || 0 }
    #     end
    #   end
    #
    #   FlightHud.render(:cockpit, altitude: 120)   # → neutral Node tree
    #
    # The directory is `views/<underscored class name>/` by default, overridable
    # via `view_dir`. `render` interpolates the template with the locals, parses
    # it with the UMGHAML grammar, and merges the theme tokens as defaults.
    class ViewController
      class << self
        # Declare (or read) this controller's view directory.
        def view_dir(dir = nil)
          @view_dir = dir.to_s if dir
          @view_dir ||= default_view_dir
        end

        # Register a named view/endpoint.
        #   view :cockpit                       # → cockpit.umghaml (+ cockpit.umgss)
        #   view :cockpit, template: "hud.umghaml", theme: "hud.umgss"
        #   view(:inline) { "%Banner{ text: \"hi\" }" }   # block supplies template
        def view(name, template: nil, theme: nil, &block)
          @views ||= {}
          @views[name.to_sym] = {
            template: template || default_template(name),
            theme: theme,
            block: block
          }
        end
        alias endpoint view

        def views
          @views ||= {}
        end

        # Instantiate + render in one shot.
        def render(name, locals = {})
          new.render(name, locals)
        end

        def default_view_dir
          to_s.split("::").last.gsub(/([a-z0-9])([A-Z])/, '\1_\2').downcase
        end

        def default_template(name)
          "#{name}.umghaml"
        end
      end

      # Resolve the locals for `name` (calls `#{name}_locals` when defined),
      # then produce the neutral tree: interpolate → parse → apply theme.
      def render(name, locals = {})
        lang = (locals[:lang] || I18n::DEFAULT).to_s
        locals = locals.merge(lang: lang)
        locals = send(:"#{name}_locals", locals) if respond_to?(:"#{name}_locals")
        locals = locals.merge(lang: lang) # re-assert: a locals hook may rebuild the hash

        source = load_template(name)
        source = Template.evaluate(source, locals) if source.is_a?(String)
        tree = HamlParser.parse(source).children.first

        theme = load_theme(name)
        theme ? theme.apply(tree) : tree
      end

      # Absolute path of this controller's directory on disk.
      def dir
        File.join(UI.view_root, self.class.view_dir)
      end

      private

      def load_template(name)
        spec = self.class.views.fetch(name.to_sym) do
          raise ArgumentError, "unknown view :#{name} in #{self.class}"
        end
        return spec[:block].call if spec[:block]

        path = File.join(dir, spec[:template])
        raise Errno::ENOENT, "missing template #{path}" unless File.file?(path)

        File.read(path)
      end

      def load_theme(name)
        spec = self.class.views[name.to_sym]
        return nil unless spec && spec[:theme]

        path = File.join(dir, spec[:theme])
        return nil unless File.file?(path)

        Umgss.parse_file(path)
      end
    end
  end
end