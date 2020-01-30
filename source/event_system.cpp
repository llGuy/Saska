#include "event_system.hpp"

void event_dispatcher_t::subscribe(event_type_t type, receiver_t receiver)
{
    event_receiver_list_t *list = &event_map[type];

    uint32_t receiver_index = list->listening_receivers++;

    list->event_indices.add();
}

void event_dispatcher_t::dispatch_event(event_type_t type, void *data)
{
    
}
