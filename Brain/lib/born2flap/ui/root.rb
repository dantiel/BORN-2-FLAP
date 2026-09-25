# frozen_string_literal: true

module Born2Flap
  module UI
    # The reactive render root — the Ruby counterpart to ReactDOM's
    # createRoot(...).render(...). It holds the current widget tree and, on
    # `render`, reconciles the new tree against the previous one, then publishes
    # the minimal patch on the EventBus (channel :ui_patch). Renderers (UMG,
    # Web, RN) subscribe and apply only what changed — live propagation without
    # Redux, using the Brain's own event plumbing.
    class Root
      attr_reader :tree

      def initialize(bus: nil, reconciler: Reconciler.new)
        @bus = bus
        @reconciler = reconciler
        @tree = nil
      end

      # Returns the patch (nil if nothing changed) and, when a bus is wired,
      # publishes it as `:ui_patch`. The previous tree is kept for the next diff.
      def render(tree)
        previous = @tree
        @tree = tree
        patch = @reconciler.diff(previous, tree)
        @bus.publish(:ui_patch, patch: patch, tree: tree) if @bus && !patch.empty?
        patch.empty? ? nil : patch
      end
    end
  end
end
