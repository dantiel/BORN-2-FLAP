# frozen_string_literal: true
#
# headless.rb — PoC harness: run the Born2Flap Ruby Brain IN-PROCESS under
# mruby and emit the NDJSON op-frame it would hand to the C++ UMG host.
#
#   ThirdParty/mruby/build/host/bin/mruby Brain/mruby/headless.rb
#
# This is the mirror of bin/umghaml_brain's loop, but with the process/file
# boundary replaced by an in-memory sink — exactly what the iOS embed will do
# (the transport writes to an injected IO; here that IO is a String accumulator).

here = File.dirname(File.expand_path(__FILE__))
load File.join(here, "compat.rb")

$:.unshift File.expand_path("../lib", here)
require "born2flap"
require "born2flap/ui/session"

# --- in-memory frame sink (stands in for the C++ ring buffer / renderer) ----
sink = Object.new
def sink.write(str); @buf = (@buf || "") << str; end
def sink.flush; end
def sink.close; end
def sink.closed?; false; end
def sink.buf; @buf.to_s; end

brain = Born2Flap::Brain.new
session = Born2Flap::UI::Session.new(brain)
bus = brain.bus
root = Born2Flap::UI::Root.new(bus: bus)
transport = Born2Flap::UI::Transport.new(bus: bus, io: sink)

# A single telemetry snapshot mirroring what Born2FlapUIBridge::WriteTelemetry
# emits while on the frontend menu (no loading screen).
raw = session.update({ frontend: true, menu: true, loading: false })
telemetry = raw
brain.tick(telemetry)
screen = brain.view_model[:screen]
visual = (telemetry || {}).reject { |key, _| key == :ui_events }
tree = Born2Flap::UI::Dashboard.build(visual, screen: screen)
root.render(tree)

bytes = transport.flush
frame = sink.buf

$stderr.puts "screen=#{screen} bytes=#{bytes} frame_len=#{frame.bytesize}"
puts frame