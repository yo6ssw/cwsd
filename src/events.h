// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

//
// Created by benny on 14/05/24.
//

#ifndef CWSD_EVENTS_H
#define CWSD_EVENTS_H


#include <vector>

enum class event_type {
    connected,
    disconnected
};

struct event {
    int producer;
    event_type type;
    // bool handled; //?
};

struct event_listener {
    virtual void on_event(event *e) = 0;
};

class event_bus {
public:
    void publish(event *e) {
        for (auto& l : listeners) {
            l->on_event(e);
        }
    }

    void add_listener(event_listener *l) {
        listeners.emplace_back(l);
    }

private:
    std::vector<event_listener* > listeners;
};


#endif //CWSD_EVENTS_H
