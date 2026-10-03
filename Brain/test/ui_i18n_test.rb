# frozen_string_literal: true

require "minitest/autorun"
$LOAD_PATH.unshift File.expand_path("../lib", __dir__)
require "born2flap"

module Born2Flap
  class I18nTest < Minitest::Test
    def test_available_languages
      assert_includes I18n.available, "en"
      %w[ar de es fr hi it ja ko no pt ru zh].each do |lang|
        assert_includes I18n.available, lang
      end
    end

    def test_translates_and_falls_back_to_english
      assert_equal "ALTITUDE", I18n.t("hud.altitude")
      assert_equal "HÖHE", I18n.t("hud.altitude", lang: "de")
      assert_equal "高度", I18n.t("hud.altitude", lang: "ja")
      assert_equal "الارتفاع", I18n.t("hud.altitude", lang: "ar")
    end

    def test_unknown_key_surfaces_the_key_itself
      assert_equal "no.such.key", I18n.t("no.such.key", lang: "de")
    end

    def test_every_locale_covers_the_english_key_set
      en = I18n.load("en")
      (I18n.available - ["en"]).each do |lang|
        assert_equal en.keys.sort, I18n.load(lang).keys.sort, "key mismatch for #{lang}"
      end
    end

    def test_cockpit_renders_localized_labels
      hud = UI::Views::FlightHud.render(:cockpit, lang: "de")
      assert_equal "BORN 2 FLAP", hud.find(:banner)[:text]
      assert_equal "HÖHE", hud.find(:stat)[:label]

      hud_ja = UI::Views::FlightHud.render(:cockpit, lang: "ja")
      assert_equal "高度", hud_ja.find(:stat)[:label]
    end

    def test_tuning_editor_localizes_section_titles
      ed = UI::Views::TuningEditor.render(:editor, lang: "fr")
      titles = ed.each.to_a.select { |n| n.type == :section }.map { |n| n[:title] }
      assert_includes titles, "SILHOUETTE"
      assert_includes titles, "BATTERIE"
    end
  end
end
