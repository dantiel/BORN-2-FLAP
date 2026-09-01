# frozen_string_literal: true

module Born2Flap
  class Router
    RouteError = Class.new(StandardError)

    attr_reader :current, :history

    def initialize(routes:, initial:)
      @routes = routes.map(&:to_sym).freeze
      @history = []
      navigate(initial, remember: false)
    end

    def navigate(route, remember: true)
      target = route.to_sym
      raise RouteError, "unknown route: #{target}" unless @routes.include?(target)

      @history << @current if remember && @current
      @current = target
    end

    def back
      @current = @history.pop if @history.any?
      @current
    end
  end
end
