#include <unistd.h>

#include <iostream>

#include <gpiod.hpp>

void ds18b20_init(gpiod::line & line)
{
    gpiod::line_request config;
    config.consumer.assign("example");

    line.release();
    config.request_type = gpiod::line_request::DIRECTION_OUTPUT;
    line.request(config);

    usleep(3);
    line.set_value(1);
    usleep(3);
    line.set_value(0);
    usleep(550);
    line.set_value(1);

    line.release();
    config.request_type = gpiod::line_request::DIRECTION_INPUT;
    line.request(config);
    if (line.event_wait(std::chrono::microseconds(60)))
    {
        auto const event = line.event_read();
    }

    line.release();
    config.request_type = gpiod::line_request::DIRECTION_OUTPUT;
    line.request(config);
    usleep(3);
    line.set_value(1);
}

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

    auto line = chip.get_line(channel);
    ds18b20_init(line);
    sleep(500);
}
