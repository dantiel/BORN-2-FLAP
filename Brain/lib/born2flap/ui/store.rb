# frozen_string_literal: true

module Born2Flap
  module UI
    # Redux-style store: a single source of truth for the UI state.
    #
    #   store = UI::Store.new(UI::Reducer.combine(
    #     screen: screen_reducer,
    #     hud:    hud_reducer
    #   ))
    #   store.dispatch(type: "UI_NAVIGATE", to: :free_flight)
    #   store.state[:screen] # => { current: :free_flight }
    class Store
      Error = Class.new(StandardError)

      attr_reader :state

      def initialize(reducer, initial_state = {})
        @reducer = reducer
        @state = initial_state
        @listeners = []
        @dispatching = false
      end

      def dispatch(action)
        raise ArgumentError, "action must have a :type" unless action.respond_to?(:[]) && action[:type]
        raise Error, "reducers may not dispatch actions" if @dispatching

        begin
          @dispatching = true
          @state = @reducer.call(@state, action)
        ensure
          @dispatching = false
        end

        @listeners.dup.each { |listener| listener.call(@state, action) }
        action
      end

      def subscribe(&listener)
        raise ArgumentError, "listener required" unless listener

        @listeners << listener
        -> { @listeners.delete(listener) }
      end

      def replace_reducer(reducer)
        @reducer = reducer
      end
    end

    module Reducer
      module_function

      # Combines slice reducers into one root reducer. Returns the same state
      # object when nothing changed (identity check), so renderers can cheaply
      # skip no-op updates.
      def combine(reducers)
        ->(state = {}, action) do
          next_state = {}
          changed = false

          reducers.each do |key, reducer|
            previous = state[key]
            current = reducer.call(previous, action)
            next_state[key] = current
            changed = true unless current.equal?(previous)
          end

          changed ? next_state : state
        end
      end

      # Identity reducer for slices that never change.
      def identity
        ->(state, _action) { state }
      end
    end
  end
end