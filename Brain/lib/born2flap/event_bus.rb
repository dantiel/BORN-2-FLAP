# frozen_string_literal: true

module Born2Flap
  class EventBus
    def initialize
      @subscriptions = Hash.new { |hash, key| hash[key] = [] }
      @queue = []
    end

    def subscribe(event_name, &handler)
      raise ArgumentError, "handler required" unless handler

      @subscriptions[event_name.to_sym] << handler
      handler
    end

    def publish(event_name, payload = {})
      @queue << [event_name.to_sym, payload.freeze]
    end

    def drain(limit: 256)
      processed = 0
      while processed < limit && (event = @queue.shift)
        name, payload = event
        @subscriptions[name].dup.each { |handler| handler.call(payload) }
        processed += 1
      end
      processed
    end

    def pending?
      !@queue.empty?
    end
  end
end
