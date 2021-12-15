#include <cstdlib>

#include <iostream>

#include <gpiod.hpp>

static int to_int(const char * str)
{
    char * endptr;
    auto const ret = strtol(str, &endptr, 0);
    return ret;
}

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: " << argv[0] << " <channel>" << std::endl;
        return 1;
    }

    auto const channel = to_int(argv[1]);
    if (channel == 0)
    {
        std::cerr << "invalid channel" << std::endl;
        return -1;
    }

    gpiod::chip const chip("gpiochip0");
    if (!chip)
    {
        std::cerr << "failed to open gpiochip0" << std::endl;
        return 1;
    }

    gpiod::line_request config;
    config.consumer.assign("example");
    config.request_type = gpiod::line_request::DIRECTION_OUTPUT;
    auto const line = chip.get_line(channel);
    line.request(config);
    line.set_value(1);
    return 0;
}