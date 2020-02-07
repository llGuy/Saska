#pragma once

#include "core.hpp"
#include "utility.hpp"
#include "containers.hpp"

#define MAX_EVENTS 20

enum listener_t
{
    WORLD,
    GUI,
    PARTICLES,
    AUDIO, // TODO: In the future
    // Depends on the mode of the program
    SERVER, CLIENT,
    INVALID_LISTENER
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
    CACHE_PLAYER_COMMAND,
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

typedef void(*listener_callback_t)(void *object, event_t *);

struct listener_subscriptions_t
{
    uint32_t count = 0;
    listener_t listeners[listener_t::INVALID_LISTENER] = {};
};

struct event_dispatcher_t
{

    void set_callback(listener_t listener, listener_callback_t callback, void *object);
    void subscribe(event_type_t type, listener_t listener);
    void submit_event(event_type_t type, void *data);
    void dispatch_events();

private:
    listener_callback_t callbacks[listener_t::INVALID_LISTENER] = {};
    void *listener_objects[listener_t::INVALID_LISTENER] = {};

    listener_subscriptions_t subscriptions[event_type_t::INVALID_EVENT_TYPE] = {};

    uint32_t pending_event_count = 0;
    event_t pending_events[MAX_EVENTS] = {};
};
