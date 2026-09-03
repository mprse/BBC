#include "bbc/app/application.hpp"
#include "bbc/app/password_reader.hpp"

#include <iostream>
#include <string_view>
#include <vector>

int main(const int argument_count, char* argument_values[]) {
    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argument_count > 0 ? argument_count - 1 : 0));

    for (int index = 1; index < argument_count; ++index) {
        arguments.emplace_back(argument_values[index]);
    }

    const bbc::app::PasswordReader password_reader = [](const std::string_view prompt,
                                                        std::string& password) {
        return bbc::app::read_password_from_terminal(prompt, password, std::cerr);
    };

    return bbc::app::run(arguments, std::cout, std::cerr, password_reader);
}
