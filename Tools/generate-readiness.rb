#!/usr/bin/env ruby
# frozen_string_literal: true

# BORN-2-FLAP Release-Readiness-Generator (Philosopher's Stone).
# Stdlib-only: json, optparse, fileutils. No external dependencies.
#
#   ruby Tools/generate-readiness.rb            # generate docs/release-readiness.md + THIRD-PARTY-NOTICES.md
#   ruby Tools/generate-readiness.rb --check    # validate + evidence + drift detection (CI gate)
#   ruby Tools/generate-readiness.rb --json PATH
#
# Exit codes: 0 = ok, 1 = schema error or check red, 2 = usage error.
#
# Gate-Semantik: G1 (Rechtliche Freigabe) bremst nur bei Blockern, die den
# Versand betreffen (scope ⊆ {Steam, Epic, Alle}). Web-only (GitHub) Blocker
# werden als Warnung gemeldet und blockieren das Store-Release nicht.

require 'json'
require 'optparse'

module Readiness
  module Schema
    STATUSES   = %w[🟢 🟡 🔴 ⛔ ➖].freeze
    CATEGORIES = %w[Lizenz Recht Stabilität Build/Distribution Store-Infra Store-Präsentation Plattform].freeze
    GATE_IDS   = %w[G1 G2 G3 G4 G5].freeze
    SCOPES     = %w[Steam Epic GitHub Alle].freeze
    ID_RE      = /\A(LIC|STA|WIN|STM|EGS|DEC|REV)-\d{2}\z/

    class SchemaError < StandardError; end

    def self.validate!(catalog)
      raise SchemaError, 'root must be an object' unless catalog.is_a?(Hash)
      raise SchemaError, 'schema must be 1' unless catalog['schema'] == 1

      meta = catalog['meta'] or raise SchemaError, 'meta missing'
      %w[title product version].each do |k|
        raise SchemaError, "meta.#{k} missing" unless meta[k].is_a?(String) && !meta[k].empty?
      end

      legend = catalog['legend'] or raise SchemaError, 'legend missing'
      STATUSES.each { |s| raise SchemaError, "legend missing #{s}" unless legend[s] }

      gates = catalog['gates'] or raise SchemaError, 'gates missing'
      unless gates.is_a?(Array) && gates.map { |g| g['id'] } == GATE_IDS
        raise SchemaError, "gates must be #{GATE_IDS.join(', ')} in order"
      end

      records = catalog['records'] or raise SchemaError, 'records missing'
      raise SchemaError, 'records must be an array' unless records.is_a?(Array)
      seen = {}
      records.each_with_index do |r, i|
        loc = "records[#{i}]"
        raise SchemaError, "#{loc} must be object" unless r.is_a?(Hash)
        id = r['id'] or raise SchemaError, "#{loc}.id missing"
        raise SchemaError, "#{loc}.id #{id.inspect} must match #{ID_RE.inspect}" unless id =~ ID_RE
        raise SchemaError, "duplicate id #{id}" if seen[id]
        seen[id] = true
        raise SchemaError, "#{loc}.req missing" unless r['req'].is_a?(String) && !r['req'].empty?
        raise SchemaError, "#{loc}.cat invalid: #{r['cat']}" unless CATEGORIES.include?(r['cat'])
        scopes = r['scope'] or raise SchemaError, "#{loc}.scope missing"
        raise SchemaError, "#{loc}.scope must be array" unless scopes.is_a?(Array)
        invalid = scopes - SCOPES
        raise SchemaError, "#{loc}.scope invalid: #{invalid.inspect}" unless invalid.empty? && !scopes.empty?
        raise SchemaError, "#{loc}.status invalid: #{r['status']}" unless STATUSES.include?(r['status'])
        raise SchemaError, "#{loc}.gate invalid: #{r['gate']}" unless GATE_IDS.include?(r['gate'])
        if r['status'] == '🟢'
          ev = r['evidence']
          raise SchemaError, "#{loc}: 🟢 requires non-empty evidence[]" unless ev.is_a?(Array) && !ev.empty?
        end
      end

      comps = catalog['components'] or raise SchemaError, 'components missing'
      raise SchemaError, 'components must be an array' unless comps.is_a?(Array)
      comps.each_with_index do |c, i|
        loc = "components[#{i}]"
        raise SchemaError, "#{loc} must be object" unless c.is_a?(Hash)
        %w[name version spdx origin bundled_in].each do |k|
          raise SchemaError, "#{loc}.#{k} missing" unless c[k].is_a?(String) && !c[k].empty?
        end
        if c['attribution'] == true
          raise SchemaError, "#{loc}: attribution=true requires notes" unless c['notes'].is_a?(String) && !c['notes'].empty?
        end
      end

      catalog
    end
  end

  class Catalog
    def self.load(path)
      Schema.validate!(JSON.parse(File.read(path)))
    end

    def self.records_in_gate(catalog, gate)
      catalog['records'].select { |r| r['gate'] == gate }.sort_by { |r| r['id'] }
    end

    def self.gate_summary(catalog)
      summary = {}
      catalog['gates'].each do |g|
        recs = records_in_gate(catalog, g['id'])
        summary[g['id']] = {
          total: recs.size,
          ok: recs.count { |r| r['status'] == '🟢' },
          warn: recs.count { |r| r['status'] == '🟡' },
          missing: recs.count { |r| r['status'] == '🔴' },
          blocked: recs.count { |r| r['status'] == '⛔' }
        }
      end
      summary
    end
  end

  module Evidence
    Finding = Struct.new(:id, :status, :missing, :gate)

    def self.check(catalog, root: Dir.pwd)
      catalog['records'].map do |r|
        missing = Array(r['evidence']).reject { |p| File.exist?(File.join(root, p)) }
        Finding.new(r['id'], r['status'], missing, r['gate'])
      end
    end

    def self.gate_report(findings, catalog)
      shipping = %w[Steam Epic Alle]
      report = Hash[catalog['gates'].map { |g| [g['id'], :ok] }]
      by_id = Hash[catalog['records'].map { |r| [r['id'], r] }]
      findings.each do |f|
        gate = f.gate
        rec = by_id[f.id]
        if f.status == '⛔'
          blocks = !((rec['scope'] || []) & shipping).empty?
          report[gate] = :blocked if blocks
          report[gate] = :warn if !blocks && report[gate] == :ok
        elsif (!f.missing.empty? || f.status == '🔴') && report[gate] == :ok
          report[gate] = :incomplete
        end
      end
      report
    end
  end

  module Renderer
    HEADER = '<!-- GENERIERT aus docs/release-readiness.json -- nicht von Hand editieren. -->'

    def self.catalog(catalog)
      lines = [HEADER, '', "# #{catalog['meta']['title']}", '',
               "Produkt: **#{catalog['meta']['product']}** · Version: **#{catalog['meta']['version']}**",
               '', '## Legende', '']
      catalog['legend'].each { |k, v| lines << "- #{k} = #{v}" }
      lines << '' << '## Gates' << ''
      summary = Catalog.gate_summary(catalog)
      catalog['gates'].each do |g|
        s = summary[g['id']]
        lines << "- **#{g['id']} — #{g['name']}**: #{s[:ok]}🟢 / #{s[:warn]}🟡 / #{s[:missing]}🔴 / #{s[:blocked]}⛔ — #{g['description']}"
      end
      lines << '' << '## Katalog'
      Schema::GATE_IDS.each do |gate|
        g = catalog['gates'].find { |x| x['id'] == gate }
        lines << '' << "### #{gate} — #{g['name']}" << '' <<
          '| ID | Anforderung | Kategorie | Scope | Status | Beleg |' <<
          '|----|-------------|-----------|-------|--------|-------|'
        Catalog.records_in_gate(catalog, gate).each do |r|
          ev = Array(r['evidence']).join(', ')
          lines << "| #{r['id']} | #{r['req']} | #{r['cat']} | #{r['scope'].join(', ')} | #{r['status']} | #{ev} |"
        end
      end
      lines << ''
      lines.join("\n") + "\n"
    end

    def self.notices(catalog)
      lines = [HEADER, '', '# THIRD-PARTY-NOTICES', '',
               "Für **#{catalog['meta']['product']}** #{catalog['meta']['version']} — Drittanbieter-Komponenten und deren Lizenzen.", '']
      catalog['components'].each do |c|
        lines << "## #{c['name']}"
        lines << "- Version: #{c['version']}"
        lines << "- Lizenz: #{c['spdx']}"
        lines << "- Herkunft: #{c['origin']}"
        lines << "- Gebündelt in: #{c['bundled_in']}"
        lines << "- Attribution erforderlich: #{c['attribution'] ? 'ja' : 'nein'}"
        lines << "- Hinweise: #{c['notes']}" if c['notes'] && !c['notes'].empty?
        lines << ''
      end
      lines.join("\n")
    end
  end

  module CLI
    def self.parse(argv)
      options = { mode: :generate, json: 'docs/release-readiness.json' }
      parser = OptionParser.new do |o|
        o.banner = 'Usage: ruby Tools/generate-readiness.rb [--check] [--json PATH]'
        o.on('--check', 'Validate, verify evidence, detect drift (CI gate)') { options[:mode] = :check }
        o.on('--json PATH', 'Path to the JSON source') { |p| options[:json] = p }
        o.on('-h', '--help') { puts o; exit 0 }
      end
      begin
        parser.parse!(argv)
      rescue OptionParser::ParseError => e
        warn "Error: #{e.message}"
        warn parser
        exit 2
      end
      options
    end

    def self.run(argv)
      options = parse(argv)
      root = File.expand_path(File.join(__dir__, '..'))
      json_path = File.expand_path(options[:json], root)
      unless File.exist?(json_path)
        warn "Error: JSON source not found: #{json_path}"
        return 1
      end

      catalog = Catalog.load(json_path)

      if options[:mode] == :check
        findings = Evidence.check(catalog, root: root)
        report = Evidence.gate_report(findings, catalog)
        bad = findings.select { |f| !f.missing.empty? }
        unless bad.empty?
          warn 'Belegpflicht verletzt (fehlende Dateien):'
          bad.each { |f| warn "  #{f.id}: #{f.missing.join(', ')}" }
        end
        drift = []
        [['docs/release-readiness.md', Renderer.catalog(catalog)],
         ['THIRD-PARTY-NOTICES.md', Renderer.notices(catalog)]].each do |path, content|
          target = File.join(root, path)
          drift << path unless File.exist?(target) && File.read(target) == content
        end
        warn "Drift erkannt (neu generieren): #{drift.join(', ')}" unless drift.empty?

        if report['G1'] == :blocked || !bad.empty? || !drift.empty?
          warn "Check: ROT (G1=#{report['G1']})"
          return 1
        end
        puts "Check: OK (G1=#{report['G1']})"
        return 0
      end

      File.write(File.join(root, 'docs/release-readiness.md'), Renderer.catalog(catalog))
      File.write(File.join(root, 'THIRD-PARTY-NOTICES.md'), Renderer.notices(catalog))
      puts 'Generated docs/release-readiness.md + THIRD-PARTY-NOTICES.md'
      0
    rescue Schema::SchemaError => e
      warn "Schema-Fehler: #{e.message}"
      1
    rescue JSON::ParserError => e
      warn "JSON-Fehler: #{e.message}"
      1
    end
  end
end

exit Readiness::CLI.run(ARGV) if $PROGRAM_NAME == __FILE__
