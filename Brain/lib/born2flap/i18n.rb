# frozen_string_literal: true

require "json"

module Born2Flap
  # I18n — the game's locale dictionary. Semantic keys map to translated
  # strings; English is canonical and every other language falls back to it
  # for untranslated keys. This is the single authority the Ruby Brain, the
  # C++ renderer (via a generated header) and the website all share.
  #
  #   Born2Flap::I18n.t("hud.altitude", lang: "de")   # => "HÖHE"
  #   Born2Flap::I18n.t("hud.altitude")               # => "ALTITUDE"
  #   Born2Flap::I18n.available                       # => ["ar", "de", ...]
  module I18n
    DEFAULT = "en"
    LOCALES_DIR = File.expand_path("i18n/locales", __dir__)

    @cache = {}

    class << self
      # Locale codes present on disk, sorted (English first).
      def available
        langs = Dir[File.join(LOCALES_DIR, "*.json")].map { |f| File.basename(f, ".json") }
        (langs - [DEFAULT]).sort.unshift(DEFAULT)
      end

      # The flattened dictionary for a language (English merged underneath).
      def load(lang)
        lang = (lang || DEFAULT).to_s
        @cache[lang] ||= begin
          base = load_file(DEFAULT)
          lang == DEFAULT ? base : base.merge(load_file(lang))
        end
      end

      # Translate a semantic key. Falls back to English when the key is
      # missing, and to the key itself as a last resort.
      def t(key, lang: nil, locale: nil)
        lang = locale || lang || DEFAULT
        load(lang).fetch(key.to_s, load(DEFAULT)[key.to_s] || key.to_s)
      end

      private

      def load_file(lang)
        path = File.join(LOCALES_DIR, "#{lang}.json")
        return {} unless File.file?(path)

        JSON.parse(File.read(path, encoding: "UTF-8"))
      end
    end
  end
end
