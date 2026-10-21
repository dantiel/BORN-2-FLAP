# frozen_string_literal: true

module Born2Flap
  module Rulesets
    # Capture festival — "flying festival hunt" (Neon Abyss collection mode).
    #
    # The narrow ruleset contract from docs/game-modes.md and ruby-as-redstone.md:
    # C++ detects fly-through nets and supplies raw festival telemetry; Ruby owns
    # the rules, the scoring and the view-model. Headless-testable under system
    # Ruby before it ever sees the engine.
    class CaptureRules
      DEFAULT_TARGET = 12
      DEFAULT_DURATION = 120.0   # seconds
      COMBO_WINDOW = 5.0         # seconds — captures close in time chain a multiplier

      # Mutable per-run state. `setup` returns a fresh session; the other methods
      # mutate and return it so calls can be chained or folded.
      Session = Struct.new(
        :target, :duration, :elapsed, :captured, :score, :combo, :combo_timer,
        :best, :finished, keyword_init: true
      )

      def setup(session = nil, target: DEFAULT_TARGET, duration: DEFAULT_DURATION)
        session ||= Session.new
        session.target = target
        session.duration = duration
        session.elapsed = 0.0
        session.captured = 0
        session.score = 0
        session.combo = 0
        session.combo_timer = 0.0
        session.best = 0
        session.finished = false
        session
      end

      # Per-tick advance. `telemetry` carries the C++ festival feed:
      #   { delta: seconds, speed: m/s, active: jellyfish_count }
      # C++ owns the timer; Ruby mirrors it so the ruleset stays headless-testable.
      def tick(session, telemetry = {})
        dt = telemetry.fetch(:delta, 0.0)
        session.elapsed += dt
        session.combo_timer = [session.combo_timer - dt, 0.0].max
        session.combo = 0 if session.combo_timer <= 0.0
        session.finished = true if session.elapsed >= session.duration
        session
      end

      # A capture event from C++: { type: :capture, speed: m/s }.
      # Scoring: count + speed bonus + combo streak (captures close in time/distance
      # chain a multiplier).
      def on_event(session, event = {})
        return session unless event[:type] == :capture

        session.captured += 1
        session.combo += 1
        session.combo_timer = COMBO_WINDOW
        speed_bonus = [event.fetch(:speed, 0.0) * 0.2, 0].max.floor
        session.score += 1 + speed_bonus + (session.combo - 1)
        session.best = [session.best, session.score].max
        session.finished = true if session.captured >= session.target
        session
      end

      def score(session)
        session.score
      end

      def finished?(session)
        session.finished ||
          session.captured >= session.target ||
          session.elapsed >= session.duration
      end
    end
  end
end
