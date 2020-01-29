#pragma once

#include "core.hpp"
#include "utility.hpp"
#include "containers.hpp"

#define MAX_EVENTS 20

enum receiver_t
    {
     WORLD,
     GUI,
     PARTICLES,
     AUDIO, // TODO: In the future
     INVALID_RECEIVER
    };

enum event_type_t 
    { 
     REQUEST_TO_JOIN_SERVER,
     ENTER_SERVER_WORLD,
     OPEN_MENU,
     EXIT_MENU,
     OPEN_CONSOLE,
     EXIT_CONSOLE,
     REQUEST_USERNAME,
     ENTERED_USERNAME,
     INVALID_EVENT_TYPE
    };

struct event_data_request_to_join_server_t
{
    const char *ip_address;
};

struct event_t
{
    event_type_t type;

    // Likely to be allocated on free list allocator
    void *data;
};

struct event_receiver_list_t
{
    uint32_t listening_receivers = 0;

    event_t events[receiver_t::INVALID_RECEIVER] = {};
    stack_dynamic_container_t<receiver_t, receiver_t::INVALID_RECEIVER> event_indices;
};

struct event_dispatcher_t
{
    event_receiver_list_t event_map[event_type_t::INVALID_EVENT_TYPE] = {};

    void subscribe(event_type_t type, receiver_t receiver);
    void dispatch_event(event_type_t type, void *data);
};
