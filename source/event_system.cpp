#include "event_system.hpp"

void event_dispatcher_t::set_callback(listener_t listener, listener_callback_t callback, void *object)
{
    callbacks[listener] = callback;
    listener_objects[listener] = object;
}

void event_dispatcher_t::subscribe(event_type_t type, listener_t listener)
{
    subscriptions[type].listeners[subscriptions[type].count++] = listener;
}

void event_dispatcher_t::submit_event(event_type_t type, void *data)
{
    pending_events[pending_event_count++] = event_t{ type, data };
}

void event_dispatcher_t::dispatch_events()
{
    for (uint32_t i = 0; i < pending_event_count; ++i)
    {
        event_t *current_event = &pending_events[i];
        listener_subscriptions_t *subscription = &subscriptions[current_event->type];

        for (uint32_t event_listener = 0; event_listener < subscription->count; ++event_listener)
        {
            listener_callback_t callback = callbacks[subscription->listeners[event_listener]];
            void *object = listener_objects[event_listener];

            (*callback)(object, current_event);
        }
    }

    pending_event_count = 0;
}