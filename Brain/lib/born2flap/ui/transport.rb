# frozen_string_literal: true

module Born2Flap
  module UI
    # The process-boundary transport — the writer half of the JSON-Wire bridge.
    #
    # The Brain runs standalone (no mruby). It publishes host ops on the
    # EventBus: `:ui_patch` (discrete tree commits from Root.render) and
    # `:ui_effect` (continuous material params from EffectDriver.drive). This
    # transport collects both and writes them as newline-delimited JSON (NDJSON)
    # frames — ONE frame per tick, each frame a `Wire.dump_ops` array — to any
    # IO: stdout (pipe), a file (shared path), or a socket. The C++ half
    # (Unreal/Born2Flap/Source/Born2Flap/UI/Born2FlapTransport.h) reads one
    # frame per line.
    #
    #   Brain tick:  state → root.render          → :ui_patch
    #                telemetry → driver.drive     → :ui_effect
    #                transport.flush              → one NDJSON frame → C++/UMG
    #
    # Framing: each line is a complete, self-contained op array, so the reader
    # never needs frame boundaries beyond `\n` and a malformed/partial line can
    # be dropped without desynchronizing the stream.
    #
    # PATH-ALIGNMENT CONTRACT: the rendered tree and the effect bindings must be
    # the SAME object (the top widget, i.e. `HamlParser.parse(src).children.first`,
    # NOT the document root), so `:ui_patch` and `:ui_effect` paths share the
    # root-is-`[]` convention the C++ host expects.
    class Transport
      def initialize(bus:, io: $stdout)
        @bus = bus
        @io = io
        @buffer = []
        @bus.subscribe(:ui_patch) { |payload| @buffer.concat(HostConfig.translate(payload[:patch])) }
        @bus.subscribe(:ui_effect) { |payload| @buffer.concat(payload[:ops]) }
      end

      # Drain the event bus (running the collectors) and emit a single NDJSON
      # frame containing every op accumulated this tick. Returns bytes written
      # (0 when nothing changed). Call once per Brain tick.
      def flush
        @bus.drain
        return 0 if @buffer.empty?

        frame = Wire.dump_ops(@buffer) + "\n"
        @io.write(frame)
        @io.flush
        @buffer.clear
        frame.bytesize
      end

      # Close the underlying IO (idempotent). A closed transport is inert.
      def close
        @io.close unless @io.closed?
      rescue IOError
        nil
      end
    end
  end
end
