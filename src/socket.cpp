#include <cassert>
#include <iostream>
#include "socket.hpp"

std::ostream& operator<<(std::ostream& os, Event::Type const& type)
{
    switch (type) {
        case Event::FAILURE: os << "FAILURE"; break;
        case Event::RECV:    os << "RECV";    break;
        case Event::SEND:    os << "SEND";    break;
    }
    os << " (" << (int) type << ")";
    return os;
}

std::ostream& operator<<(std::ostream& os, Event const& event)
{
    return os << "Event { type=" << event.type << ", data=" << event.data << " }";
}
