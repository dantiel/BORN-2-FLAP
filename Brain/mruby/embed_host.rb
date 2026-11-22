# frozen_string_literal: true
#
# embed_host.rb — the in-process Ruby Brain host, driven tick-by-tick by the
# C++ mruby embed (Born2FlapMrubyHost). Mirrors bin/umghaml_brain's loop but
# replaces the process/file boundary with:
#   - a C-injected frame sink (B2FHost.frames_write)
#   - a per-tick entry point (Born2Flap::Embed.tick) fed telemetry JSON by C++.
#
# There is no infinite loop and no sleep: C++ owns the game loop and calls
# Embed.tick at its UI rate. Each tick parses one telemetry snapshot, advances
# the Brain, and (when the screen changed) renders the dashboard and flushes
# the NDJSON op frame into the C++ sink.
#
# B2FHost::BRAIN_ROOT is set by the C host before this file is loaded; it is
# the absolute path of the Brain/ directory (contains mruby/ and lib/).

root = B2FHost::BRAIN_ROOT
# compat.rb is already loaded by the C host (Born2FlapMrubyHost::Initialize)
# BEFORE this file. Do NOT re-load it here: its alias_method shims are not
# re-entrant and a double-load makes File.read recurse until SystemStackError.

$:.unshift File.join(root, "lib")
require "born2flap"
require "born2flap/ui/session"

module Born2Flap
  module Embed
    # mruby-json returns string-keyed hashes; the Brain expects symbol keys
    # (MRI's JSON.parse(text, symbolize_names: true)). Convert keys recursively,
    # including inside arrays (ui_events / worlds / poi ...).
    def self.__deep_symbolize(obj)
      if obj.is_a?(Hash)
        h = {}
        obj.each { |k, v| h[k.to_sym] = __deep_symbolize(v) }
        h
      elsif obj.is_a?(Array)
        obj.map { |v| __deep_symbolize(v) }
      else
        obj
      end
    end

    # In-memory frame sink: the transport's io. Writes go straight to the C++
    # frame buffer (B2FHost.frames_write) instead of a file/stdout.
    def self.sink
      return @sink if @sink
      @sink = Object.new
      def @sink.write(str); B2FHost.frames_write(str); end
      def @sink.flush; end
      def @sink.close; end
      def @sink.closed?; false; end
      @sink
    end

    @brain = Born2Flap::Brain.new
    @session = Born2Flap::UI::Session.new(@brain)
    @bus = @brain.bus
    @root = Born2Flap::UI::Root.new(bus: @bus)
    @transport = Born2Flap::UI::Transport.new(bus: @bus, io: sink)
    @current_tree = nil
    @last_visual = nil

    # One Brain tick. `telemetry_json` is the C++ telemetry snapshot as a JSON
    # string (the same shape Born2FlapUIBridge produced); nil means "no data".
    # Returns true on success; never raises (errors are logged and swallowed so
    # a Brain bug cannot take the native process down).
    def self.tick(telemetry_json)
      raw = telemetry_json ? __deep_symbolize(JSON.parse(telemetry_json)) : nil
      telemetry = @session.update(raw) if raw

      if raw || @current_tree.nil?
        @brain.tick(telemetry)
        screen = @brain.view_model[:screen]
        visual = (telemetry || {}).reject { |key, _| key == :ui_events }
        visual_key = [screen, visual]
        if visual_key != @last_visual
          @current_tree = Born2Flap::UI::Dashboard.build(visual, screen: screen)
          @root.render(@current_tree)
          @last_visual = visual_key
        end
      end
      @transport.flush
      true
    rescue => e
      B2FHost.log("brain tick error: #{e.inspect}")
      false
    end
  end
end